// Geekatplay TerraForge - the mesh module: diagnosis, repair, reduction and
// the file formats (engine/mesh_*.cpp), tested with no window and no GPU.
//
// Every case is a mesh built here with a known defect, so an assertion can
// name the exact number the analysis must produce. A repair tool that says
// "fixed" without being able to prove it is the thing this module exists to
// replace, and the same standard applies to its tests.
#include "gpx/mesh_engines.hpp"
#include "gpx/mesh_io.hpp"
#include "gpx/mesh_report.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace gpx;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
static void check_near(double got, double want, double tol, const char *what) {
  if (std::fabs(got - want) > tol) {
    std::printf("FAIL: %s (got %g, want %g)\n", what, got, want);
    ++failures;
  }
}

// A closed unit cube at the origin, wound outward: 8 vertices, 12 triangles,
// volume 1, area 6.
static TriMesh unit_cube() {
  TriMesh m;
  const float v[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                         {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  for (auto &p : v) {
    m.v.push_back(p[0]);
    m.v.push_back(p[1]);
    m.v.push_back(p[2]);
  }
  const uint32_t f[12][3] = {
      {0, 3, 2}, {0, 2, 1}, // z = 0, facing -z
      {4, 5, 6}, {4, 6, 7}, // z = 1
      {0, 1, 5}, {0, 5, 4}, // y = 0
      {2, 3, 7}, {2, 7, 6}, // y = 1
      {0, 4, 7}, {0, 7, 3}, // x = 0
      {1, 2, 6}, {1, 6, 5}, // x = 1
  };
  for (auto &t : f)
    for (int k = 0; k < 3; ++k) m.f.push_back(t[k]);
  return m;
}

// The same cube with every triangle its own three vertices, as an STL has it:
// 36 vertices, nothing shared, so no edge is joined to its neighbour.
static TriMesh unwelded_cube() {
  TriMesh src = unit_cube(), m;
  for (size_t i = 0; i < src.face_count(); ++i)
    for (int k = 0; k < 3; ++k) {
      const float *p = src.vert(src.f[i * 3 + k]);
      m.f.push_back((uint32_t)m.vert_count());
      m.v.insert(m.v.end(), p, p + 3);
    }
  return m;
}

static bool has_issue(const MeshReport &r, const char *id) {
  for (const MeshIssue &i : r.issues)
    if (i.id == id) return true;
  return false;
}
static size_t issue_count(const MeshReport &r, const char *id) {
  for (const MeshIssue &i : r.issues)
    if (i.id == id) return i.count;
  return 0;
}

static void test_healthy_cube() {
  MeshReport r;
  mesh_analyse(unit_cube(), r);
  check(r.stats.faces == 12, "a cube has 12 triangles");
  check(r.stats.edges == 18, "a closed cube has 18 unique edges");
  check(r.stats.watertight, "a closed cube is watertight");
  check(r.stats.winding_consistent, "a cube's winding is consistent");
  check(!r.stats.inverted, "an outward cube is not inside-out");
  check(r.stats.shells == 1, "a cube is one shell");
  check_near(r.stats.volume, 1.0, 1e-5, "unit cube volume");
  check_near(r.stats.area, 6.0, 1e-5, "unit cube surface area");
  check(r.stats.has_euler && r.stats.euler == 2, "Euler number of a sphere is 2");
  check(r.stats.has_genus && r.stats.genus == 0, "a cube has genus 0");
  check(r.issues.empty(), "a clean cube reports no issues");
  check(r.score == 100, "a clean cube scores 100");
  check(r.verdict == "Print ready", "a clean cube is print ready");
}

static void test_hole_is_found_and_closed() {
  TriMesh m = unit_cube();
  m.f.resize(m.f.size() - 6); // drop the two triangles of the x = 1 face
  MeshReport r;
  mesh_analyse(m, r);
  check(!r.stats.watertight, "a cube missing a face is not watertight");
  check(r.stats.holes == 1, "the missing face is one hole");
  check(r.stats.boundary_edges == 4, "a square hole has four open edges");
  check(has_issue(r, "holes"), "the hole is reported");
  check(r.score < 70, "a hole costs a critical amount of score");
  check(r.verdict == "Repair required", "an open mesh needs repair");

  MeshRepairOptions opt;
  MeshRepairResult res;
  mesh_repair(m, opt, res);
  check(res.changed, "repair reports that it changed something");
  check(res.after.stats.watertight, "repair closes the hole");
  check(res.after.stats.holes == 0, "no holes remain");
  check_near(res.after.stats.volume, 1.0, 1e-5,
             "the repaired cube encloses the same volume");
  bool mentions = false;
  for (const std::string &f : res.fixes)
    if (f.find("Holes") != std::string::npos) mentions = true;
  check(mentions, "the report says the hole was closed");
  bool watertight_line = false;
  for (const MeshChange &c : res.changes)
    if (c.label == "Watertight" && c.improved) watertight_line = true;
  check(watertight_line, "the before/after table records becoming watertight");
}

static void test_two_holes_counted_separately() {
  TriMesh m = unit_cube();
  // The two z faces, which touch nothing of each other: an open-ended tube.
  // (Two ADJACENT missing faces share an edge and leave one L-shaped loop,
  // which is one hole, and the count must not double it.)
  m.f.erase(m.f.begin(), m.f.begin() + 12);
  MeshReport r;
  mesh_analyse(m, r);
  check(r.stats.holes == 2, "two opposite missing faces are two holes");
  check(r.stats.boundary_edges == 8, "two square holes have eight open edges");

  TriMesh joined = unit_cube();
  joined.f.resize(joined.f.size() - 6);              // x = 1
  joined.f.erase(joined.f.begin(), joined.f.begin() + 6); // z = 0, adjacent
  MeshReport rj;
  mesh_analyse(joined, rj);
  check(rj.stats.holes == 1,
        "two adjacent missing faces are one hole, not two");
  check(rj.stats.boundary_edges == 6,
        "and the edge they shared is gone, not open");
}

static void test_flipped_face() {
  TriMesh m = unit_cube();
  std::swap(m.f[1], m.f[2]); // turn one triangle around
  MeshReport r;
  mesh_analyse(m, r);
  check(!r.stats.winding_consistent, "a flipped triangle breaks the winding");
  check(has_issue(r, "winding"), "inconsistent winding is reported");

  MeshRepairOptions opt;
  MeshRepairResult res;
  mesh_repair(m, opt, res);
  check(res.after.stats.winding_consistent, "repair unifies the winding");
  check_near(res.after.stats.volume, 1.0, 1e-5, "and the volume is right again");
}

static void test_inside_out() {
  TriMesh m = unit_cube();
  for (size_t i = 0; i < m.face_count(); ++i)
    std::swap(m.f[i * 3 + 1], m.f[i * 3 + 2]);
  MeshReport r;
  mesh_analyse(m, r);
  check(r.stats.inverted, "an all-flipped cube reads as inside-out");
  check(has_issue(r, "inverted"), "being inside-out is reported");

  MeshRepairOptions opt;
  MeshRepairResult res;
  mesh_repair(m, opt, res);
  check(!res.after.stats.inverted, "repair turns it the right way out");
  check(mesh_volume(m) > 0, "and the signed volume is positive again");
}

static void test_unwelded_cube_welds() {
  TriMesh m = unwelded_cube();
  MeshReport r;
  mesh_analyse(m, r);
  check(r.stats.vertices == 36, "an unwelded cube has 36 vertices");
  check(!r.stats.watertight,
        "unshared vertices leave every edge open, so it is not watertight");
  check(r.stats.duplicate_vertices == 28,
        "28 of the 36 vertices repeat one of the 8 corners");
  check(has_issue(r, "dupverts"), "duplicate vertices are reported");

  MeshRepairOptions opt;
  MeshRepairResult res;
  mesh_repair(m, opt, res);
  check(m.vert_count() == 8, "welding leaves the eight real corners");
  check(res.after.stats.watertight, "and the cube closes");
  check_near(res.after.stats.volume, 1.0, 1e-5, "with the right volume");
}

static void test_degenerate_and_duplicate_faces() {
  TriMesh m = unit_cube();
  m.f.insert(m.f.end(), {0u, 1u, 1u});          // repeated index: zero area
  m.f.insert(m.f.end(), {0u, 3u, 2u});          // an exact copy of face 0
  MeshReport r;
  mesh_analyse(m, r);
  check(r.stats.degenerate_faces == 1, "the collapsed triangle is found");
  check(r.stats.duplicate_faces == 1, "the copied triangle is found");
  check(has_issue(r, "degenerate") && has_issue(r, "dupfaces"),
        "both are reported");

  MeshRepairOptions opt;
  MeshRepairResult res;
  mesh_repair(m, opt, res);
  check(m.face_count() == 12, "repair leaves exactly the twelve real faces");
  check(res.after.stats.watertight, "and the cube is still closed");
}

static void test_nonmanifold_edge() {
  TriMesh m = unit_cube();
  // A fin: a triangle hanging off an edge that already has two faces.
  uint32_t apex = (uint32_t)m.vert_count();
  m.v.insert(m.v.end(), {0.5f, 0.5f, 2.0f});
  m.f.insert(m.f.end(), {0u, 1u, apex});
  MeshReport r;
  mesh_analyse(m, r);
  check(r.stats.nonmanifold_edges >= 1, "the fin creates a non-manifold edge");
  check(has_issue(r, "nonmanifold"), "it is reported as critical");
  check(!r.stats.watertight, "and the mesh is no longer a clean solid");
  check(issue_count(r, "nonmanifold") == r.stats.nonmanifold_edges,
        "the issue count matches the measured count");
}

static void test_separate_shells() {
  TriMesh m = unit_cube();
  TriMesh far = unit_cube();
  uint32_t base = (uint32_t)m.vert_count();
  for (size_t i = 0; i < far.vert_count(); ++i) {
    m.v.push_back(far.v[i * 3] + 10.f);
    m.v.push_back(far.v[i * 3 + 1]);
    m.v.push_back(far.v[i * 3 + 2]);
  }
  for (uint32_t idx : far.f) m.f.push_back(base + idx);
  MeshReport r;
  mesh_analyse(m, r);
  check(r.stats.shells == 2, "two cubes are two shells");
  check(has_issue(r, "shells"), "separate shells are reported");
  check_near(r.stats.volume, 2.0, 1e-5, "and both volumes are counted");

  // A stray piece is removable, but only when asked for.
  MeshRepairOptions opt;
  MeshRepairResult keep;
  TriMesh a = m;
  mesh_repair(a, opt, keep);
  check(keep.after.stats.shells == 2, "repair keeps both pieces by default");

  // A cube with one loose triangle floating beside it.
  TriMesh stray = unit_cube();
  uint32_t loose = (uint32_t)stray.vert_count();
  stray.v.insert(stray.v.end(), {5.f, 0.f, 0.f, 5.1f, 0.f, 0.f, 5.f, 0.1f, 0.f});
  stray.f.insert(stray.f.end(), {loose, loose + 1, loose + 2});
  opt.drop_small_shells = true;
  MeshRepairResult drop;
  mesh_repair(stray, opt, drop);
  check(drop.after.stats.shells == 1, "the loose triangle is removed");
  check(drop.after.stats.watertight, "and what is left is the closed cube");

  // Every shell being small must not empty the scene: a model made only of
  // small pieces is still the user's model.
  TriMesh all_small;
  for (int i = 0; i < 2; ++i) {
    uint32_t b0 = (uint32_t)all_small.vert_count();
    float x = i * 5.f;
    all_small.v.insert(all_small.v.end(),
                       {x, 0.f, 0.f, x + .1f, 0.f, 0.f, x, .1f, 0.f});
    all_small.f.insert(all_small.f.end(), {b0, b0 + 1, b0 + 2});
  }
  MeshRepairResult none;
  mesh_repair(all_small, opt, none);
  check(all_small.face_count() == 2,
        "when every piece is small, none of them are deleted");
}

static void test_location_payload() {
  TriMesh m = unit_cube();
  m.f.resize(m.f.size() - 6);
  MeshReport r;
  mesh_analyse(m, r);
  for (const MeshIssue &i : r.issues)
    if (i.id == "holes") {
      check(!i.points.empty(), "the hole carries points to look at");
      check(i.total >= 4, "and says how many there really were");
      check(i.extent > 0.f, "and how big the affected area is");
      check(i.center[0] > 0.9f,
            "the centre sits on the missing x = 1 face, not at the origin");
    }
}

static void test_solidity_warns() {
  MeshSolidity closed = mesh_solidity(unit_cube());
  check(closed.is_solid, "a closed cube is a solid");
  check(closed.warnings.empty(), "and warns about nothing");
  TriMesh open = unit_cube();
  open.f.resize(open.f.size() - 6);
  MeshSolidity leaky = mesh_solidity(open);
  check(!leaky.is_solid, "an open mesh is not a solid");
  check(leaky.warnings.size() == 1,
        "and says so before six hours of printing, not after");
}

static void test_reduce() {
  // A sphere, dense enough that halving it is a real decimation.
  TriMesh s;
  const int rings = 24, seg = 48;
  for (int i = 0; i <= rings; ++i) {
    double phi = 3.14159265358979 * i / rings;
    for (int j = 0; j < seg; ++j) {
      double th = 2 * 3.14159265358979 * j / seg;
      s.v.push_back((float)(std::sin(phi) * std::cos(th)));
      s.v.push_back((float)(std::cos(phi)));
      s.v.push_back((float)(std::sin(phi) * std::sin(th)));
    }
  }
  for (int i = 0; i < rings; ++i)
    for (int j = 0; j < seg; ++j) {
      uint32_t a = (uint32_t)(i * seg + j);
      uint32_t b = (uint32_t)(i * seg + (j + 1) % seg);
      uint32_t c = (uint32_t)((i + 1) * seg + j);
      uint32_t d = (uint32_t)((i + 1) * seg + (j + 1) % seg);
      s.f.insert(s.f.end(), {a, c, b});
      s.f.insert(s.f.end(), {b, c, d});
    }
  const size_t before = s.face_count();
  TriMesh copy = s;
  MeshReduceResult res;
  check(mesh_reduce(copy, before / 4, res), "reduction runs");
  check(copy.face_count() <= before / 4 + 8,
        "and reaches about the face count asked for");
  check(res.faces_before == before, "it reports what it started from");
  check(res.deviation_frac < 0.05,
        "a quarter of a sphere's triangles still sits within 5% of it");
  check(res.max_deviation > 0.0, "and the deviation is measured, not zero");
  MeshReduceResult none;
  check(!mesh_reduce(copy, copy.face_count() + 10, none),
        "asking for more faces than there are does nothing");
}

static void test_file_round_trip() {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "terraforge_mesh_test";
  fs::create_directories(dir);
  const TriMesh cube = unit_cube();
  const char *exts[] = {"stl", "obj", "ply", "off"};
  for (const char *ext : exts) {
    fs::path p = dir / (std::string("cube.") + ext);
    std::string err;
    check(mesh_save(p.string(), cube, err), (std::string("write ") + ext).c_str());
    TriMesh back;
    check(mesh_load(p.string(), back, err),
          (std::string("read back ") + ext).c_str());
    check(back.face_count() == 12,
          (std::string(ext) + " keeps all twelve triangles").c_str());
    MeshReport r;
    mesh_analyse(back, r);
    // STL stores three loose vertices per triangle by design, so it comes
    // back unwelded - that is the format, not a bug, and repair welds it.
    if (std::string(ext) == "stl") {
      MeshRepairOptions opt;
      MeshRepairResult res;
      mesh_repair(back, opt, res);
      mesh_analyse(back, r);
    }
    check(r.stats.watertight,
          (std::string(ext) + " round trips a closed solid").c_str());
    check_near(r.stats.volume, 1.0, 1e-4,
               (std::string(ext) + " round trips the volume").c_str());
  }
  // ASCII STL is the same mesh in a bigger file.
  fs::path a = dir / "cube_ascii.stl";
  std::string err;
  check(mesh_save(a.string(), cube, err, true), "write ascii STL");
  TriMesh back;
  check(mesh_load(a.string(), back, err), "read ascii STL");
  check(back.face_count() == 12, "ascii STL keeps all twelve triangles");
  // And an unreadable file fails with a reason rather than silence.
  TriMesh none;
  check(!mesh_load((dir / "cube.xyz").string(), none, err),
        "an unknown extension is refused");
  check(!err.empty(), "and says why");
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// Two cubes that run through each other: each is closed on its own, so
// nothing reads as a hole and our own stages have nothing to grip - yet the
// pair is not a printable solid. This is the case the optional engine earns
// its place on, and the test asserts the contract in BOTH builds: with the
// engine it becomes one solid, without it the repair says the stage could
// not run rather than implying everything was tried.
static void test_overlapping_shells() {
  TriMesh m = unit_cube();
  TriMesh other = unit_cube();
  uint32_t base = (uint32_t)m.vert_count();
  for (size_t i = 0; i < other.vert_count(); ++i) {
    m.v.push_back(other.v[i * 3] + 0.5f); // half a cube's overlap
    m.v.push_back(other.v[i * 3 + 1] + 0.5f);
    m.v.push_back(other.v[i * 3 + 2] + 0.5f);
  }
  for (uint32_t idx : other.f) m.f.push_back(base + idx);

  MeshReport before;
  mesh_analyse(m, before);
  check(before.stats.shells == 2, "the two cubes read as two shells");
  check(before.stats.watertight,
        "and each is closed, which is why our own stages cannot help");

  const bool have = mesh_engines().solidify;
  check(!mesh_engines_text().empty(), "the engine line is never empty");

  TriMesh work = m;
  std::string err;
  const bool ok = mesh_solidify(work, err);
  if (have) {
    check(ok, "with the engine, the pair resolves");
    if (ok) {
      MeshReport after;
      mesh_analyse(work, after);
      check(after.stats.shells == 1, "into a single shell");
      check(after.stats.watertight, "that is closed");
      // Union, not sum: two unit cubes overlapping by half in every axis
      // enclose 2 - 0.125 of a unit.
      check_near(after.stats.volume, 1.875, 1e-3,
                 "and encloses the union's volume, not both cubes added up");
    }
  } else {
    check(!ok, "without the engine, it refuses");
    check(err.find("not compiled in") != std::string::npos,
          "and says the engine is missing rather than failing silently");
    TriMesh again = m;
    MeshRepairOptions opt;
    MeshRepairResult res;
    mesh_repair(again, opt, res);
    check(!res.notes.empty(),
          "and a repair records that a stage could not run");
  }
}

int main() {
  test_overlapping_shells();
  test_healthy_cube();
  test_hole_is_found_and_closed();
  test_two_holes_counted_separately();
  test_flipped_face();
  test_inside_out();
  test_unwelded_cube_welds();
  test_degenerate_and_duplicate_faces();
  test_nonmanifold_edge();
  test_separate_shells();
  test_location_payload();
  test_solidity_warns();
  test_reduce();
  test_file_round_trip();
  if (failures) {
    std::printf("%d mesh check(s) failed\n", failures);
    return 1;
  }
  std::printf("mesh tests passed\n");
  return 0;
}
