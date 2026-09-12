// Geekatplay TerraForge - the Gradient node and the shapes it draws.
//
// What has to stay true:
//   - a centred shape (circle, ellipse, square, diamond) is highest at its
//     centre and gone at its size, and its contours are its own shape -
//     round, stretched, square, diamond;
//   - a linear ramp rises along its direction from end to end, a reflected
//     one both ways from its line, an angular one once round the centre;
//   - past the end a ramp holds, repeats with a step, or mirrors without one;
//   - terraces are flat levels and no more of them than asked for;
//   - the node fills its map with finite values over the whole 0..1 range,
//     and the same settings give the same bits.
#include "gpx/gradient_shape.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
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
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void test_centred_shapes() {
  std::printf("centred shapes...\n");
  gradient::Params p;
  p.profile = gradient::PLinear;
  for (int type : {gradient::Circle, gradient::Ellipse, gradient::Square, gradient::Diamond}) {
    p.type = type;
    const std::string name = "type " + std::to_string(type);
    check(near(gradient::value(p, 0.5f, 0.5f), 1.f), name + " is 1 at its centre");
    check(gradient::value(p, 0.5f + 0.49f, 0.5f) < 0.05f || type == gradient::Ellipse,
          name + " has fallen to nothing at its size");
    // higher nearer the middle, along any line out of it
    float prev = 2.f;
    bool falls = true;
    for (int i = 0; i <= 10; ++i) {
      const float h = gradient::value(p, 0.5f + 0.045f * i, 0.5f + 0.02f * i);
      falls = falls && h <= prev + 1e-6f;
      prev = h;
    }
    check(falls, name + " falls away from its centre");
  }
  // contours: round for a circle, square for a square, diamond for a diamond
  p.type = gradient::Circle;
  check(near(gradient::value(p, 0.5f + 0.2f, 0.5f),
             gradient::value(p, 0.5f + 0.2f * 0.7071068f, 0.5f + 0.2f * 0.7071068f), 1e-3f),
        "a circle's contour is round");
  p.type = gradient::Square;
  check(near(gradient::value(p, 0.5f + 0.2f, 0.5f), gradient::value(p, 0.7f, 0.7f), 1e-3f),
        "a square's contour runs out to its corner");
  p.type = gradient::Diamond;
  check(near(gradient::value(p, 0.5f + 0.2f, 0.5f), gradient::value(p, 0.6f, 0.6f), 1e-3f),
        "a diamond's contour is straight between its tips");
  // corner rounding takes a square all the way to a circle
  p.type = gradient::Square;
  p.roundness = 1.f;
  gradient::Params c = p;
  c.type = gradient::Circle;
  check(near(gradient::value(p, 0.63f, 0.71f), gradient::value(c, 0.63f, 0.71f), 1e-4f),
        "a fully rounded square is a circle");
  // an ellipse is longer along its direction than across it
  p = gradient::Params{};
  p.type = gradient::Ellipse;
  p.profile = gradient::PLinear;
  check(gradient::value(p, 0.5f + 0.3f, 0.5f) > gradient::value(p, 0.5f, 0.5f + 0.3f),
        "an ellipse reaches further along its direction");
  p.angle_deg = 90.f;
  check(gradient::value(p, 0.5f, 0.5f + 0.3f) > gradient::value(p, 0.5f + 0.3f, 0.5f),
        "and turns with it");
}

static void test_ramps() {
  std::printf("ramps...\n");
  gradient::Params p;
  p.type = gradient::Linear;
  p.profile = gradient::PLinear;
  check(near(gradient::value(p, 0.f, 0.3f), 0.f), "a linear ramp starts at its far end");
  check(near(gradient::value(p, 1.f, 0.7f), 1.f), "and ends at the other");
  check(near(gradient::value(p, 0.25f, 0.1f), 0.25f), "rising evenly between");
  p.angle_deg = 90.f;
  check(gradient::value(p, 0.5f, 0.9f) > gradient::value(p, 0.5f, 0.1f),
        "and rises the way it is turned");
  p = gradient::Params{};
  p.type = gradient::Reflected;
  p.profile = gradient::PLinear;
  check(near(gradient::value(p, 0.3f, 0.5f), gradient::value(p, 0.7f, 0.5f)),
        "a reflected ramp is the same both sides of its line");
  check(near(gradient::value(p, 0.5f, 0.2f), 0.f), "and lowest on it");
  p.type = gradient::Angular;
  float lo = 1.f, hi = 0.f;
  for (int i = 0; i < 64; ++i) {
    const float a = i / 64.f * 6.2831853f;
    const float h = gradient::value(p, 0.5f + 0.3f * std::cos(a), 0.5f + 0.3f * std::sin(a));
    lo = std::min(lo, h);
    hi = std::max(hi, h);
  }
  check(lo < 0.05f && hi > 0.95f, "an angular sweep covers the whole range once round");
  check(near(gradient::value(p, 0.8f, 0.5f), gradient::value(p, 0.95f, 0.5f)),
        "and depends on the angle alone");
}

static void test_wrap_and_terraces() {
  std::printf("wrap and terraces...\n");
  check(near(gradient::wrap_t(gradient::Hold, 1.7f), 1.f), "hold stays at the end");
  check(near(gradient::wrap_t(gradient::Repeat, 1.25f), 0.25f), "repeat starts again");
  check(near(gradient::wrap_t(gradient::Mirror, 1.25f), 0.75f), "mirror runs back");
  // mirror has no step anywhere; repeat has one per repeat
  float worst_mirror = 0.f, worst_repeat = 0.f;
  for (int i = 0; i < 400; ++i) {
    const float t0 = i / 100.f, t1 = (i + 1) / 100.f;
    worst_mirror = std::max(worst_mirror, std::fabs(gradient::wrap_t(gradient::Mirror, t1) -
                                                    gradient::wrap_t(gradient::Mirror, t0)));
    worst_repeat = std::max(worst_repeat, std::fabs(gradient::wrap_t(gradient::Repeat, t1) -
                                                    gradient::wrap_t(gradient::Repeat, t0)));
  }
  check(worst_mirror < 0.011f, "mirroring is continuous");
  check(worst_repeat > 0.9f, "repeating steps");
  gradient::Params p;
  p.type = gradient::Linear;
  p.profile = gradient::PLinear;
  p.steps = 5;
  std::set<int> levels;
  for (int i = 0; i <= 200; ++i) levels.insert(int(std::lround(gradient::value(p, i / 200.f, 0.5f) * 1000.f)));
  check((int)levels.size() <= 6, "five terraces make at most six flat levels");
  check((int)levels.size() >= 5, "and use them");
  // every profile runs from 0 to 1
  for (int k = 0; k <= gradient::Bell; ++k) {
    check(near(gradient::profile(k, 0.f), 0.f, 1e-4f) && near(gradient::profile(k, 1.f), 1.f, 1e-4f),
          "profile " + std::to_string(k) + " runs from 0 to 1");
  }
}

static void test_node() {
  std::printf("the node...\n");
  for (int type = 0; type < 8; ++type) {
    Graph g;
    g.resolution = 64;
    Node *n = g.add_node("Gradient");
    check(n != nullptr, "the Gradient node exists");
    if (!n) return;
    if (Attribute *t = n->attrs.find("type")) t->i = type;
    if (Attribute *w = n->attrs.find("warp")) w->f = 0.3f;
    g.evaluate();
    const Port *po = n->port("output", PortDir::Out);
    const Heightmap *h = po && po->hmap ? po->hmap.get() : nullptr;
    check(h && h->w == 64 && h->h == 64, "type " + std::to_string(type) + " fills a map");
    if (!h) continue;
    float mn = 1e9f, mx = -1e9f;
    bool finite = true;
    for (float v : h->v) {
      finite = finite && std::isfinite(v);
      mn = std::min(mn, v);
      mx = std::max(mx, v);
    }
    check(finite, "type " + std::to_string(type) + " is finite everywhere");
    check(near(mn, 0.f, 1e-4f) && near(mx, 1.f, 1e-4f),
          "type " + std::to_string(type) + " spans the terrain's range");
    // the same settings give the same bits
    Graph g2;
    g2.resolution = 64;
    Node *n2 = g2.add_node("Gradient");
    n2->attrs = n->attrs;
    g2.evaluate();
    const Port *po2 = n2->port("output", PortDir::Out);
    const Heightmap *h2 = po2 && po2->hmap ? po2->hmap.get() : nullptr;
    check(h2 && h2->v == h->v, "type " + std::to_string(type) + " is deterministic");
  }
}

int main() {
  test_centred_shapes();
  test_ramps();
  test_wrap_and_terraces();
  test_node();
  std::printf("%d checks, %s\n", checks, failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
