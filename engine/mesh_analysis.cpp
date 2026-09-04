// Geekatplay TerraForge - mesh diagnosis: every print-relevant property of a
// mesh, turned into a list of concrete issues and a readiness score.
// Ported from Meshwright's engine (MIT, Geekatplay Studio).
#include "gpx/mesh_report.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace gpx {

namespace {

// Sample a set of points down to something a viewport can draw, keeping the
// count and the bounds of the real set so the report never lies about size.
void set_location(MeshIssue &issue, const std::vector<float> &pts) {
  size_t total = pts.size() / 3;
  issue.total = total;
  if (!total) return;
  float lo[3] = {pts[0], pts[1], pts[2]}, hi[3] = {pts[0], pts[1], pts[2]};
  for (size_t i = 0; i < total; ++i)
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], pts[i * 3 + k]);
      hi[k] = std::max(hi[k], pts[i * 3 + k]);
    }
  for (int k = 0; k < 3; ++k) issue.center[k] = (lo[k] + hi[k]) * 0.5f;
  issue.extent = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) +
                           (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                           (hi[2] - lo[2]) * (hi[2] - lo[2]));
  if (total <= MESH_MAX_LOCATIONS) {
    issue.points = pts;
    return;
  }
  issue.points.reserve(MESH_MAX_LOCATIONS * 3);
  for (size_t i = 0; i < MESH_MAX_LOCATIONS; ++i) {
    size_t src = (size_t)((double)i * (total - 1) / (MESH_MAX_LOCATIONS - 1));
    issue.points.insert(issue.points.end(), pts.begin() + src * 3,
                        pts.begin() + src * 3 + 3);
  }
}

void add_issue(MeshReport &r, const char *id, int severity, std::string title,
               std::string detail, size_t count,
               const std::vector<float> *pts = nullptr) {
  MeshIssue is;
  is.id = id;
  is.severity = severity;
  is.title = std::move(title);
  is.detail = std::move(detail);
  is.count = count;
  if (pts) set_location(is, *pts);
  r.issues.push_back(std::move(is));
}

std::string plural(size_t n, const char *one, const char *many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// Smallest angle of a triangle, in degrees. A sliver is a triangle so thin
// that a slicer's own arithmetic starts to disagree with itself about it.
float min_angle_deg(const TriMesh &m, const uint32_t *fc) {
  const float *p[3] = {m.vert(fc[0]), m.vert(fc[1]), m.vert(fc[2])};
  float best = 180.f;
  for (int i = 0; i < 3; ++i) {
    const float *a = p[i], *b = p[(i + 1) % 3], *c = p[(i + 2) % 3];
    double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    double lu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    double lv = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (lu <= 0 || lv <= 0) return 0.f;
    double d = (u[0] * v[0] + u[1] * v[1] + u[2] * v[2]) / (lu * lv);
    d = std::clamp(d, -1.0, 1.0);
    best = std::min(best, (float)(std::acos(d) * 57.29577951308232));
  }
  return best;
}

} // namespace

void mesh_analyse(const TriMesh &m, MeshReport &r, float unit_mm) {
  r = MeshReport();
  MeshStats &s = r.stats;
  s.vertices = m.vert_count();
  s.faces = m.face_count();
  if (!s.faces) {
    add_issue(r, "empty", MESH_CRITICAL, "No geometry",
              "The mesh contains no triangles.", 0);
    r.score = 0;
    r.verdict = "Nothing to print";
    return;
  }

  const EdgeTable et = mesh_edges(m);
  s.edges = et.size();
  std::vector<float> boundary_pts, nm_pts;
  bool winding_ok = true;
  for (size_t i = 0; i < et.size(); ++i) {
    if (et.uses[i] == 1) {
      ++s.boundary_edges;
      for (uint32_t vi : {et.a[i], et.b[i]})
        boundary_pts.insert(boundary_pts.end(), m.vert(vi), m.vert(vi) + 3);
    } else if (et.uses[i] > 2) {
      ++s.nonmanifold_edges;
      for (uint32_t vi : {et.a[i], et.b[i]})
        nm_pts.insert(nm_pts.end(), m.vert(vi), m.vert(vi) + 3);
    }
    if (!et.consistent[i]) winding_ok = false;
  }
  s.winding_consistent = winding_ok;
  s.watertight = s.boundary_edges == 0 && s.nonmanifold_edges == 0;
  s.manifold = s.watertight && winding_ok;
  s.holes = s.boundary_edges ? mesh_boundary_loops(m).size() : 0;

  // Face quality, in one pass.
  std::vector<float> degen_pts, sliver_pts, dup_face_pts;
  std::unordered_map<uint64_t, uint32_t> face_seen;
  face_seen.reserve(s.faces);
  for (size_t i = 0; i < s.faces; ++i) {
    const uint32_t *fc = m.face(i);
    float cx = (m.vert(fc[0])[0] + m.vert(fc[1])[0] + m.vert(fc[2])[0]) / 3.f;
    float cy = (m.vert(fc[0])[1] + m.vert(fc[1])[1] + m.vert(fc[2])[1]) / 3.f;
    float cz = (m.vert(fc[0])[2] + m.vert(fc[1])[2] + m.vert(fc[2])[2]) / 3.f;
    float ang = min_angle_deg(m, fc);
    bool repeated = fc[0] == fc[1] || fc[1] == fc[2] || fc[0] == fc[2];
    if (repeated || ang <= 0.f) {
      ++s.degenerate_faces;
      degen_pts.insert(degen_pts.end(), {cx, cy, cz});
    } else if (ang < 1.f) {
      ++s.sliver_faces;
      sliver_pts.insert(sliver_pts.end(), {cx, cy, cz});
    }
    uint32_t sorted[3] = {fc[0], fc[1], fc[2]};
    std::sort(sorted, sorted + 3);
    uint64_t key = ((uint64_t)sorted[0] * 73856093ull) ^
                   ((uint64_t)sorted[1] * 19349663ull) ^
                   ((uint64_t)sorted[2] * 83492791ull);
    auto it = face_seen.find(key);
    if (it == face_seen.end())
      face_seen.emplace(key, 1u);
    else {
      ++s.duplicate_faces;
      dup_face_pts.insert(dup_face_pts.end(), {cx, cy, cz});
    }
  }

  // Duplicate and unused vertices.
  std::vector<float> dup_vert_pts, unref_pts;
  {
    std::unordered_map<uint64_t, uint32_t> at;
    at.reserve(s.vertices);
    const double q = 1e6; // compare to six decimals, as Meshwright does
    for (size_t i = 0; i < s.vertices; ++i) {
      const float *p = m.vert(i);
      uint64_t k = 1469598103934665603ull;
      for (int c = 0; c < 3; ++c) {
        int64_t iv = (int64_t)std::llround((double)p[c] * q);
        k ^= (uint64_t)iv;
        k *= 1099511628211ull;
      }
      if (!at.emplace(k, 1u).second) {
        ++s.duplicate_vertices;
        dup_vert_pts.insert(dup_vert_pts.end(), p, p + 3);
      }
    }
    std::vector<uint8_t> used(s.vertices, 0);
    for (uint32_t idx : m.f)
      if (idx < used.size()) used[idx] = 1;
    for (size_t i = 0; i < s.vertices; ++i)
      if (!used[i]) {
        ++s.unreferenced_vertices;
        unref_pts.insert(unref_pts.end(), m.vert(i), m.vert(i) + 3);
      }
  }

  std::vector<int> label;
  s.shells = (size_t)mesh_components(m, label);
  double vol = mesh_volume(m);
  s.inverted = s.watertight && vol < 0.0;
  s.volume = s.watertight ? std::fabs(vol) : 0.0;
  s.area = mesh_area(m);
  mesh_bounds(m, s.lo, s.hi);
  for (int k = 0; k < 3; ++k) s.dims[k] = s.hi[k] - s.lo[k];
  s.max_dim = std::max({s.dims[0], s.dims[1], s.dims[2]});
  if (s.watertight) {
    s.has_euler = true;
    s.euler = (int)s.vertices - (int)s.edges + (int)s.faces;
    if (s.shells == 1) {
      s.has_genus = true;
      s.genus = (2 - s.euler) / 2;
    }
  }

  // ------------------------------------------------------------ the issues
  if (s.holes || s.boundary_edges)
    add_issue(r, "holes", MESH_CRITICAL,
              plural(s.holes, "open hole", "open holes"),
              std::to_string(s.boundary_edges) +
                  " boundary edges are not shared by two faces. A slicer "
                  "cannot tell inside from outside.",
              s.holes, &boundary_pts);
  if (s.nonmanifold_edges)
    add_issue(r, "nonmanifold", MESH_CRITICAL, "Non-manifold edges",
              std::to_string(s.nonmanifold_edges) +
                  " edges are shared by three or more faces. These make the "
                  "solid ambiguous.",
              s.nonmanifold_edges, &nm_pts);
  if (!s.winding_consistent)
    add_issue(r, "winding", MESH_WARNING, "Inconsistent face winding",
              "Neighbouring triangles point in opposite directions; normals "
              "flip across the surface.",
              0);
  if (s.inverted)
    add_issue(r, "inverted", MESH_WARNING, "Inside-out mesh",
              "Every normal points inward, so the model reads as a cavity.", 0);
  if (s.degenerate_faces)
    add_issue(r, "degenerate", MESH_WARNING, "Degenerate triangles",
              std::to_string(s.degenerate_faces) +
                  " triangles have zero area (collapsed or collinear points).",
              s.degenerate_faces, &degen_pts);
  if (s.duplicate_faces)
    add_issue(r, "dupfaces", MESH_WARNING, "Duplicate faces",
              std::to_string(s.duplicate_faces) +
                  " triangles are exact copies of another triangle.",
              s.duplicate_faces, &dup_face_pts);
  if (s.duplicate_vertices)
    add_issue(r, "dupverts", MESH_INFO, "Duplicate vertices",
              std::to_string(s.duplicate_vertices) +
                  " vertices share a position with another; their edges may "
                  "not actually be joined.",
              s.duplicate_vertices, &dup_vert_pts);
  if (s.unreferenced_vertices)
    add_issue(r, "unref", MESH_INFO, "Unused vertices",
              std::to_string(s.unreferenced_vertices) +
                  " vertices are not used by any face.",
              s.unreferenced_vertices, &unref_pts);
  if (s.sliver_faces)
    add_issue(r, "slivers", MESH_INFO, "Sliver triangles",
              std::to_string(s.sliver_faces) +
                  " triangles are thinner than one degree, which can produce "
                  "slicing artefacts.",
              s.sliver_faces, &sliver_pts);
  if (s.shells > 1)
    add_issue(r, "shells", MESH_INFO,
              plural(s.shells, "separate shell", "separate shells"),
              "The model is several disconnected pieces. Check that this is "
              "intended.",
              s.shells);
  {
    float mm = s.max_dim * unit_mm;
    if (mm > 0.f && (mm < 1.f || mm > 1000.f))
      add_issue(r, "scale", MESH_INFO, "Unusual scale",
                "The largest dimension is " + std::to_string((int)mm) +
                    " mm. The file may use different units.",
                0);
  }

  int score = 100;
  for (const MeshIssue &is : r.issues)
    score -= (is.severity == MESH_CRITICAL) ? 35
             : (is.severity == MESH_WARNING) ? 12
                                             : 3;
  score = std::clamp(score, 0, 100);
  // A closed, correctly wound solid is printable whatever cosmetic notes it
  // collected, so it never scores below 80.
  if (s.watertight && s.winding_consistent && !s.inverted)
    score = std::max(score, 80);
  r.score = score;

  bool critical = false, warning = false;
  for (const MeshIssue &is : r.issues) {
    critical |= is.severity == MESH_CRITICAL;
    warning |= is.severity == MESH_WARNING;
  }
  r.verdict = r.issues.empty() ? "Print ready"
              : critical       ? "Repair required"
              : warning        ? "Repair recommended"
                               : "Printable, minor cleanup available";
}

std::vector<MeshChange> mesh_compare(const MeshReport &b,
                                     const MeshReport &a) {
  std::vector<MeshChange> out;
  auto num = [&](const char *label, size_t bv, size_t av) {
    if (bv == av) return;
    MeshChange c;
    c.label = label;
    c.before = (double)bv;
    c.after = (double)av;
    c.improved = av < bv;
    out.push_back(c);
  };
  auto flag = [&](const char *label, bool bv, bool av, bool good) {
    if (bv == av) return;
    MeshChange c;
    c.label = label;
    c.before = bv ? 1 : 0;
    c.after = av ? 1 : 0;
    c.improved = (av == good);
    c.boolean = true;
    out.push_back(c);
  };
  num("Open holes", b.stats.holes, a.stats.holes);
  num("Boundary edges", b.stats.boundary_edges, a.stats.boundary_edges);
  num("Non-manifold edges", b.stats.nonmanifold_edges,
      a.stats.nonmanifold_edges);
  num("Degenerate faces", b.stats.degenerate_faces, a.stats.degenerate_faces);
  num("Duplicate faces", b.stats.duplicate_faces, a.stats.duplicate_faces);
  num("Duplicate vertices", b.stats.duplicate_vertices,
      a.stats.duplicate_vertices);
  num("Unused vertices", b.stats.unreferenced_vertices,
      a.stats.unreferenced_vertices);
  num("Sliver faces", b.stats.sliver_faces, a.stats.sliver_faces);
  num("Separate shells", b.stats.shells, a.stats.shells);
  flag("Watertight", b.stats.watertight, a.stats.watertight, true);
  flag("Consistent winding", b.stats.winding_consistent,
       a.stats.winding_consistent, true);
  flag("Inside-out", b.stats.inverted, a.stats.inverted, false);
  if (b.score != a.score) {
    MeshChange c;
    c.label = "Readiness score";
    c.before = b.score;
    c.after = a.score;
    c.improved = a.score > b.score;
    out.push_back(c);
  }
  return out;
}

MeshSolidity mesh_solidity(const TriMesh &m, float unit_mm) {
  MeshSolidity out;
  if (m.empty()) {
    out.warnings.push_back("The mesh has no triangles.");
    return out;
  }
  const EdgeTable et = mesh_edges(m);
  for (size_t i = 0; i < et.size(); ++i)
    if (et.uses[i] == 1) ++out.boundary_edges;
  out.watertight = out.boundary_edges == 0;
  double vol = std::fabs(mesh_volume(m));
  double area = mesh_area(m);
  out.volume = out.watertight ? vol : 0.0;
  out.is_solid = out.watertight && vol > 1e-12;
  float lo[3], hi[3];
  mesh_bounds(m, lo, hi);
  float max_dim =
      std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) * unit_mm;
  if (out.watertight && area > 0.0) {
    // A closed shell of wall thickness t has volume about area/2 * t, so 2V/A
    // is a fair estimate of the average wall.
    out.avg_wall = 2.0 * vol / area * unit_mm;
    out.has_wall = true;
  }
  if (!out.watertight)
    out.warnings.push_back(
        "This mesh is not a closed solid - " +
        std::to_string(out.boundary_edges) +
        " edges are open. A slicer treats an open surface as a thin shell: "
        "one perimeter thick, with no infill. Repair it before exporting.");
  else if (vol <= 1e-12)
    out.warnings.push_back(
        "This mesh encloses no volume - the surfaces sit on top of each "
        "other. A slicer will produce a shell with nothing inside it.");
  else if (out.has_wall && out.avg_wall < 1.2 && max_dim >= 20.f)
    out.warnings.push_back(
        "The model is hollow, with walls averaging " +
        std::to_string(out.avg_wall).substr(0, 4) +
        " mm - thinner than most nozzles can fill, so the slicer will print "
        "walls and no infill. That is correct if it really is a hollow shell.");
  return out;
}

} // namespace gpx
