// Geekatplay TerraForge - the shape of the ground, and the water on it.
//
// What TerrainShape and Lake have to keep true:
//   - a shape is solid in the middle and gone outside, whichever outline;
//   - the three edge controls are three different things - extent says how
//     far in the blend reaches, gradient says what curve it takes on the way,
//     intensity says how far down it goes - and changing one must not do the
//     job of another;
//   - the rim wanders when told to and is exact when not, and never reaches
//     past the radius it was given;
//   - a lake's radius is in metres and follows the terrain's size;
//   - a lake settles into ground already below its level, and does not lie
//     across a hillside unless told to;
//   - the shore band marks the waterline and nothing else.
#include "gpx/node_graph.hpp"
#include "gpx/shapes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace gpx;

static int failures = 0, checks = 0;
static void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
  }
}

// The node under test, fed by a real source node.
//
// Not by hand-filling a buffer: evaluate() recomputes every source, so
// values written into a source's output before evaluating are simply thrown
// away and the test silently runs on something else. `src_type` is
// "Constant" for flat ground or "Shape" for a ramp (its type 0 is a slope
// plane, remapped to 0..1 by the standard output block).
static Node *chain(Graph &g, const char *type, const char *src_type, int res) {
  Node *src = g.add_node(src_type);
  Node *n = g.add_node(type);
  if (!src || !n) return nullptr;
  if (Attribute *t = src->attrs.find("type")) t->i = 0; // Shape: slope plane
  if (Attribute *v = src->attrs.find("value")) v->f = 1.f;
  g.add_link(src->id, "output", n->id, "input");
  g.resolution = res;
  return n;
}

// ------------------------------------------------------------ the shape maths
static void test_shape_field() {
  std::printf("shape field...\n");
  shape::Params p;
  p.kind = shape::Ellipse;
  p.cx = p.cy = 0.5f;
  p.rx = p.ry = 0.4f;
  p.extent = 0.3f;
  p.gradient = 1.f;
  p.intensity = 1.f;
  p.edge_amount = 0.f;

  check(std::fabs(shape::presence(p, 0.5f, 0.5f) - 1.f) < 1e-5f,
        "solid at the centre");
  check(shape::presence(p, 0.98f, 0.5f) < 1e-5f, "gone well outside the rim");
  check(std::fabs(shape::inside(p, 0.9f, 0.5f)) < 1e-5f,
        "inside() is zero exactly on the rim");

  // The rim is where it was asked to be, for every outline.
  for (int k = 0; k < shape::Mask; ++k) {
    p.kind = k;
    check(shape::presence(p, 0.5f, 0.5f) > 0.99f,
          "outline " + std::to_string(k) + " is solid at its centre");
    check(shape::presence(p, 0.5f + 0.45f, 0.5f) < 0.01f,
          "outline " + std::to_string(k) + " is gone outside its half-width");
  }
  p.kind = shape::Ellipse;

  // Extent and gradient are different questions. At the same distance in
  // from the rim, a wider extent must give a *lower* value (the fade started
  // further out), and a higher gradient must also give a lower one (the
  // curve starts falling from further in) - but they must not be the same
  // knob: with extent fixed, gradient still moves the answer.
  const float u = 0.5f + 0.4f * 0.85f; // 15 % of the radius inside the rim
  p.extent = 0.2f;
  p.gradient = 1.f;
  const float narrow = shape::presence(p, u, 0.5f);
  p.extent = 0.6f;
  const float wide = shape::presence(p, u, 0.5f);
  check(wide < narrow, "a wider blend extent reaches further in");
  p.gradient = 4.f;
  const float steep = shape::presence(p, u, 0.5f);
  p.gradient = 0.25f;
  const float shallow = shape::presence(p, u, 0.5f);
  check(steep < wide && wide < shallow,
        "gradient shapes the curve independently of the extent");

  // Intensity says how far down, and only that: at the rim it is exactly
  // 1 - intensity, whatever the other two are set to.
  for (float in : {0.f, 0.35f, 1.f}) {
    p.intensity = in;
    p.gradient = 1.f;
    p.extent = 0.4f;
    const float rim = shape::presence(p, 0.9f, 0.5f);
    check(std::fabs(rim - (1.f - in)) < 1e-5f,
          "intensity " + std::to_string(in) + " sets the level at the rim");
  }
  p.intensity = 1.f;

  // A wandering rim stays inside the radius it was given, which is what
  // Terragen's "Max radius" promises and what keeps a lake off its bank.
  p.edge_amount = 0.9f;
  p.edge_scale = 6.f;
  bool escaped = false;
  for (int i = 0; i <= 400; ++i) {
    const float a = i * 6.283185307f / 400.f;
    // just outside the stated radius, in every direction
    const float x = 0.5f + std::cos(a) * 0.4f * 1.02f;
    const float y = 0.5f + std::sin(a) * 0.4f * 1.02f;
    if (shape::presence(p, x, y) > 0.f) escaped = true;
  }
  check(!escaped, "a wandering rim never reaches past the radius given");

  // and it is deterministic
  p.seed = 7;
  const float a1 = shape::presence(p, 0.62f, 0.44f);
  const float a2 = shape::presence(p, 0.62f, 0.44f);
  check(a1 == a2, "the shape is a pure function of the point");
}

// ------------------------------------------------------------- TerrainShape
static void test_terrain_shape_node() {
  std::printf("TerrainShape node...\n");
  Graph g;
  Node *n = chain(g, "TerrainShape", "Constant", 128);
  check(n != nullptr, "TerrainShape exists");
  if (!n) return;
  n->attrs.find("shape")->i = shape::Ellipse;
  n->attrs.find("edge_amount")->f = 0.f;
  n->attrs.find("base")->i = 1; // zero
  g.mark_all_dirty();
  g.evaluate();
  const Heightmap &out = *n->port("output", PortDir::Out)->hmap;
  const Heightmap &msk = *n->port("mask", PortDir::Out)->hmap;
  check(out.at(64, 64) > 0.99f, "flat ground survives in the middle");
  check(out.at(2, 2) < 0.01f, "the corner is taken to the base level");
  check(msk.at(64, 64) > 0.99f && msk.at(2, 2) < 0.01f,
        "the mask output is the shape's presence");

  // "From mask" without a mask must say so rather than silently doing
  // something else.
  n->attrs.find("shape")->i = shape::Mask;
  g.mark_all_dirty();
  g.evaluate();
  check(!n->error.empty(), "From mask without a mask input reports an error");

  // Keeping the relief outside leaves the ground out there varying rather
  // than flat, which is the difference between an island and a cut-out.
  Graph g2;
  Node *n2 = chain(g2, "TerrainShape", "Shape", 128);
  if (!n2) return;
  n2->attrs.find("shape")->i = shape::Ellipse;
  n2->attrs.find("edge_amount")->f = 0.f;
  n2->attrs.find("base")->i = 1;
  n2->attrs.find("keep_relief")->b = true;
  g2.mark_all_dirty();
  g2.evaluate();
  const Heightmap &o2 = *n2->port("output", PortDir::Out)->hmap;
  check(std::fabs(o2.at(4, 64) - o2.at(120, 64)) > 0.1f,
        "with relief kept, the ground outside still varies");
}

// --------------------------------------------------------------------- Lake
static void test_lake_node() {
  std::printf("Lake node...\n");
  Graph g;
  Node *n = chain(g, "Lake", "Shape", 128);
  check(n != nullptr, "Lake exists");
  if (!n) return;
  n->attrs.find("size_m")->f = 1000.f;
  n->attrs.find("radius_m")->f = 250.f; // a quarter of the tile
  n->attrs.find("edge_amount")->f = 0.f;
  n->attrs.find("level")->f = 0.5f;
  n->attrs.find("carve")->f = 0.4f;
  n->attrs.find("conform")->b = true;
  g.mark_all_dirty();
  g.evaluate();
  const Heightmap &depth = *n->port("depth", PortDir::Out)->hmap;
  const Heightmap &msk = *n->port("mask", PortDir::Out)->hmap;
  const Heightmap &shore = *n->port("shore", PortDir::Out)->hmap;
  const Heightmap &water = *n->port("water", PortDir::Out)->hmap;

  check(depth.at(64, 64) > 0.f, "there is water in the middle of the lake");
  check(depth.at(4, 64) == 0.f && depth.at(124, 64) == 0.f,
        "there is no water outside the radius");
  check(msk.at(64, 64) > 0.9f, "the mask marks the lake");

  // The radius is in metres, so doubling the terrain's size halves the
  // lake's share of the tile.
  int wide_before = 0, wide_after = 0;
  for (int x = 0; x < 128; ++x) wide_before += depth.at(x, 64) > 0.f;
  n->attrs.find("size_m")->f = 2000.f;
  g.mark_all_dirty();
  g.evaluate();
  for (int x = 0; x < 128; ++x) wide_after += depth.at(x, 64) > 0.f;
  check(wide_after < wide_before,
        "the radius is in metres: a bigger terrain makes the lake a smaller "
        "share of it");
  n->attrs.find("size_m")->f = 1000.f;

  // Conforming: on a ramp, the uphill half is above the water level, so a
  // lake that follows the ground must not appear there. Turned off, it is a
  // flat disc that ignores the ground - which is Terragen's Lake object.
  n->attrs.find("carve")->f = 0.f;
  n->attrs.find("bank")->f = 0.f;
  n->attrs.find("level")->f = 0.35f;
  n->attrs.find("conform")->b = true;
  g.mark_all_dirty();
  g.evaluate();
  // The mask is the honest discriminator, not the depth: a flat disc has a
  // water surface over ground that pokes through it, and zero depth there,
  // so a depth test would call both modes the same.
  int uphill_wet = 0;
  for (int x = 74; x < 90; ++x) uphill_wet += msk.at(x, 64) > 0.f;
  check(uphill_wet == 0, "a conforming lake does not climb the hillside");
  n->attrs.find("conform")->b = false;
  g.mark_all_dirty();
  g.evaluate();
  int disc_wet = 0;
  for (int x = 74; x < 90; ++x) disc_wet += msk.at(x, 64) > 0.f;
  check(disc_wet > 0, "a flat disc ignores the ground, as Terragen's does");

  // The shore band marks the waterline: it is water, and it is shallower
  // than the middle.
  n->attrs.find("conform")->b = true;
  n->attrs.find("level")->f = 0.5f;
  n->attrs.find("carve")->f = 0.4f;
  g.mark_all_dirty();
  g.evaluate();
  check(shore.at(64, 64) < 0.2f, "the middle of the lake is not shore");
  // The band sits within `shore` of the rim, wherever that lands - scan the
  // whole row rather than guessing which pixels it covers.
  bool band = false;
  for (int x = 0; x < 128; ++x)
    if (shore.at(x, 64) > 0.5f && depth.at(x, 64) > 0.f) band = true;
  check(band, "there is a shore band inside the waterline");
  check(water.at(64, 64) > water.at(4, 64) - 1.f,
        "the water surface output is defined everywhere");

  // Carving must actually lower the bed under the lake.
  const float bed_carved = n->port("output", PortDir::Out)->hmap->at(64, 64);
  n->attrs.find("carve")->f = 0.f;
  g.mark_all_dirty();
  g.evaluate();
  const float bed_flat = n->port("output", PortDir::Out)->hmap->at(64, 64);
  check(bed_carved < bed_flat, "carving lowers the lake bed");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  test_shape_field();
  test_terrain_shape_node();
  test_lake_node();
  std::printf("%d checks, %s\n", checks, failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
