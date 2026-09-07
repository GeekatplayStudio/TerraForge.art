// Geekatplay TerraForge - the footprint an object leaves on the terrain:
// the convex hull of its base, placed as the viewport places it. A cube's
// four corners, turned by its heading; a base band that ignores the roof;
// the bounding box when there is no geometry; the line the node reads.
// Linked into undo_tests.
#include "imprint_footprint.hpp"
#include "scene.hpp"
#include "surface_features.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace studio;

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static SceneObject cube() {
  SceneObject o;
  o.type = SceneObject::Mesh;
  scene_primitive_verts("cube", o.verts);
  o.vert_count = (int)(o.verts.size() / 6);
  scene_object_bounds(o);
  o.scale = 0.1f;
  o.pos[0] = 0.5f; o.pos[1] = 0.2f; o.pos[2] = 0.5f;
  return o;
}

static bool has_point(const Footprint &f, float x, float z) {
  for (size_t i = 0; i + 1 < f.xz.size(); i += 2)
    if (near(f.xz[i], x, 2e-4f) && near(f.xz[i + 1], z, 2e-4f)) return true;
  return false;
}

static void test_hull() {
  std::printf("imprint: convex hull...\n");
  std::vector<float> pts = {0, 0, 1, 0, 1, 1, 0, 1, 0.5f, 0.5f, 0.2f, 0.7f};
  std::vector<float> h = convex_hull_xz(pts);
  CHECK(h.size() == 8, "a square with inner points hulls to four corners");
  // counter-clockwise: positive signed area
  double A = 0;
  for (size_t i = 0; i < 4; ++i) { size_t j = (i + 1) % 4; A += (double)h[i * 2] * h[j * 2 + 1] - (double)h[j * 2] * h[i * 2 + 1]; }
  CHECK(A > 0, "the hull is counter-clockwise");
  CHECK(convex_hull_xz({0, 0, 1, 1}).size() == 4, "two points stay two");
}

static void test_cube_footprint() {
  std::printf("imprint: a cube's footprint...\n");
  SceneObject o = cube();
  const float hs = 0.25f;
  Footprint f = imprint_footprint(o, hs);
  CHECK(f.valid() && f.xz.size() == 8, "a cube's base is four corners");
  const float half = 0.05f; // scale 0.1 over a unit cube
  CHECK(has_point(f, 0.5f - half, 0.5f - half) && has_point(f, 0.5f + half, 0.5f + half), "corners at pos +- half the size");
  CHECK(near(f.cx, 0.5f) && near(f.cz, 0.5f), "centroid at the object");
  CHECK(near(f.radius, std::sqrt(0.1f * 0.1f / 3.14159265f), 1e-4f), "radius of the equivalent disc");
  // base: the cube's lowest vertex, placed, in heightmap units
  const float world_base = o.pos[1] * hs + o.bmin[1] * o.scale;
  CHECK(near(f.base, world_base / hs), "the base height in heightmap units");
  // heading turns the corners
  o.yaw = 45.f;
  Footprint r = imprint_footprint(o, hs);
  const float d = half * std::sqrt(2.f);
  CHECK(has_point(r, 0.5f + d, 0.5f) || has_point(r, 0.5f, 0.5f + d), "a 45 degree heading puts a corner on the axis");
  CHECK(!has_point(r, 0.5f - half, 0.5f - half), "and the unturned corner is gone");
  // squeeze in X halves the footprint's width
  o.yaw = 0.f;
  o.scl[0] = 0.5f;
  Footprint s = imprint_footprint(o, hs);
  CHECK(has_point(s, 0.5f - half * 0.5f, 0.5f - half) && !has_point(s, 0.5f - half, 0.5f - half), "squeeze narrows the base");
}

static void test_band_and_fallbacks() {
  std::printf("imprint: base band and fallbacks...\n");
  // a pyramid-ish shape: a wide base and a narrow top - the base band must
  // ignore the top so the footprint is the base
  SceneObject o;
  o.type = SceneObject::Mesh;
  auto tri = [&](float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz) {
    const float v[9] = {ax, ay, az, bx, by, bz, cx, cy, cz};
    for (int k = 0; k < 3; ++k) {
      o.verts.insert(o.verts.end(), v + k * 3, v + k * 3 + 3);
      o.verts.insert(o.verts.end(), {0.f, 1.f, 0.f});
    }
  };
  tri(-1, 0, -1, 1, 0, -1, 1, 0, 1);
  tri(-1, 0, -1, 1, 0, 1, -1, 0, 1);
  tri(-0.1f, 1, -0.1f, 0.1f, 1, -0.1f, 0, 2, 0); // a spike on top
  o.vert_count = (int)(o.verts.size() / 6);
  scene_object_bounds(o);
  o.scale = 0.05f;
  o.pos[0] = 0.3f; o.pos[1] = 0.f; o.pos[2] = 0.3f;
  Footprint f = imprint_footprint(o, 0.25f);
  CHECK(f.valid() && f.xz.size() == 8, "the base, not the spike, is the footprint");
  CHECK(has_point(f, 0.3f - 0.05f, 0.3f - 0.05f), "at the base's corners");
  // no geometry: the bounding box
  SceneObject box;
  box.type = SceneObject::Mesh;
  box.scale = 0.2f;
  box.pos[0] = 0.5f; box.pos[2] = 0.5f;
  Footprint b = imprint_footprint(box, 0.25f);
  CHECK(b.valid() && b.xz.size() == 8 && has_point(b, 0.4f, 0.4f), "no vertices: the bounding box's corners");
  // the line the node reads
  std::string line = imprint_footprint_line(b, 0.01f, 0.002f, 0.f, 1e9f, 1e9f);
  float base, sink, margin, blend; int n;
  CHECK(std::sscanf(line.c_str(), "%f %f %f %f %d", &base, &sink, &margin, &blend, &n) == 5 && n == 4 && near(sink, 0.01f) && near(margin, 0.002f), "the line carries base, sink, margin, blend and the point count");
}

// The features that ride on the placed ground: the material's relief is
// added, the natural ground is the result, the imprint is measured against
// that natural ground (a boulder half-buried in the rocks stays put), and
// nothing is applied twice.
// An object may be placed at a height and stay there.
//
// It could not before: the mould's target was clamp(h, base, base + sink),
// so lifting an object raised a mound to meet it and lowering it dug a pit
// to follow it. The object was always on the surface, because the surface
// always came along - which made "put it two metres up" impossible to
// express, and an arch, a bridge deck or a half-buried ruin impossible to
// place.
//
// The line's trailing lift and dig cap how far the ground may travel. Absent
// they are unlimited, which is exactly the old behaviour, and the first case
// here is what proves that.
static void test_ground_may_be_told_to_stay_put() {
  std::printf("imprint: lift and dig limits...\n");
  // flat ground at 0.5, a square base at 0.8 - the object floats 0.3 above
  const char *hull = "4 0.4 0.4 0.6 0.4 0.6 0.6 0.4 0.6";
  auto run = [&](const std::string &line) {
    gpx::Heightmap g(64, 64);
    for (float &v : g.v) v = 0.5f;
    gpx::ImprintParams prm;
    prm.retain = 0.f;
    gpx::imprint_apply(g, line, prm, nullptr);
    return g;
  };

  // no limits given: the ground rises to meet it, as it always did
  {
    gpx::Heightmap g = run(std::string("0.8 0 0 0.05 ") + hull + "\n");
    CHECK(near(g.at(32, 32), 0.8f),
          "with no limit the ground still rises to a floating base");
  }
  // lift 0: the object hangs there and the ground keeps its own height
  {
    gpx::Heightmap g = run(std::string("0.8 0 0 0.05 ") + hull + " 0 0\n");
    CHECK(near(g.at(32, 32), 0.5f),
          "lift 0 leaves the ground alone under a floating object, got " +
              std::to_string(g.at(32, 32)));
  }
  // lift 0.1: it comes a tenth of the way and stops
  {
    gpx::Heightmap g = run(std::string("0.8 0 0 0.05 ") + hull + " 0.1 0\n");
    CHECK(near(g.at(32, 32), 0.6f),
          "lift 0.1 raises the ground by exactly that, got " +
              std::to_string(g.at(32, 32)));
  }

  // and the other direction: a base *below* the ground
  {
    // base 0.2 under ground at 0.5 - the object is buried by 0.3
    gpx::Heightmap g = run(std::string("0.2 0 0 0.05 ") + hull + "\n");
    CHECK(near(g.at(32, 32), 0.2f),
          "with no limit the ground still digs out to a sunken base");
  }
  {
    gpx::Heightmap g = run(std::string("0.2 0 0 0.05 ") + hull + " 0 0\n");
    CHECK(near(g.at(32, 32), 0.5f),
          "dig 0 leaves the ground closed over a buried object, got " +
              std::to_string(g.at(32, 32)));
  }
  {
    gpx::Heightmap g = run(std::string("0.2 0 0 0.05 ") + hull + " 0 0.1\n");
    CHECK(near(g.at(32, 32), 0.4f),
          "dig 0.1 hollows by exactly that, got " +
              std::to_string(g.at(32, 32)));
  }

  // the two are independent: a mound may be forbidden while digging is not
  {
    gpx::Heightmap g = run(std::string("0.8 0 0 0.05 ") + hull + " 0 9\n");
    CHECK(near(g.at(32, 32), 0.5f), "lift 0 with dig free still does not lift");
    gpx::Heightmap d = run(std::string("0.2 0 0 0.05 ") + hull + " 0 9\n");
    CHECK(near(d.at(32, 32), 0.2f), "and digging still digs");
  }

  // sink is unchanged: a dead band in which nothing moves at all
  {
    // base 0.45, sink 0.1: ground at 0.5 is inside [0.45, 0.55], left alone
    gpx::Heightmap g = run(std::string("0.45 0.1 0 0.05 ") + hull + " 0 0\n");
    CHECK(near(g.at(32, 32), 0.5f),
          "inside the sink band the ground is untouched");
  }
}

static void test_surface_features() {
  std::printf("imprint: surface features after placement...\n");
  gpx::Heightmap ground(64, 64);
  for (float &v : ground.v) v = 0.5f;
  auto disp = std::make_shared<gpx::Heightmap>(64, 64);
  for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) disp->at(x, y) = x < 32 ? 0.1f : 0.f; // rocks on the left half
  SurfaceFeatures f;
  f.displacement = disp;
  f.footprints = "0.7 0 0 0.05 4 0.4 0.4 0.6 0.4 0.6 0.6 0.4 0.6\n"; // a square base at 0.7
  f.imprint.retain = 0.f;
  gpx::Heightmap natural;
  gpx::Heightmap g = ground;
  surface_features_apply(g, f, &natural);
  CHECK(near(natural.at(10, 10), 0.6f) && near(natural.at(50, 10), 0.5f), "natural = placed ground plus the material's relief");
  CHECK(near(g.at(32, 32), 0.7f) && near(g.at(26, 26), 0.7f), "the imprint moulds the natural ground to the base, corners included");
  CHECK(near(g.at(10, 10), 0.6f) && near(g.at(50, 10), 0.5f), "far away the relief stays and nothing else moved");
  // the sink allowance is measured against the natural ground (rocks included)
  f.footprints = "0.55 0.1 0 0.05 4 0.2 0.2 0.3 0.2 0.3 0.3 0.2 0.3\n"; // base 0.55 on the rocky left half (natural 0.6)
  g = ground;
  surface_features_apply(g, f, &natural);
  CHECK(near(g.at(16, 16), 0.6f), "within the allowance the rocky ground is left as it is");
  f.footprints.clear();
  f.displacement.reset();
  g = ground;
  surface_features_apply(g, f, &natural);
  CHECK(g.v == ground.v && natural.v == ground.v, "no features: the ground passes through untouched");
}

int test_imprint_run() {
  test_hull();
  test_cube_footprint();
  test_band_and_fallbacks();
  test_ground_may_be_told_to_stay_put();
  test_surface_features();
  return g_fail;
}
