// Geekatplay TerraForge — FakeStones layers: generations of stone that gather
// around the generation before them.
//
// The golden for surface_geology.gpxt already pins that one layer produces
// exactly what it always did. What is left to prove is that the new controls
// do the thing they claim: more layers put down more stone, clustering keeps
// the later ones near the earlier ones instead of scattered over open ground,
// and sharpness changes the shape of a stone rather than just its size.
#include "gpx/node_graph.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
void check_fail(const std::string &msg, int line) {
  std::printf("  [FAIL] %s (line %d)\n", msg.c_str(), line);
  ++g_failures;
}
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) check_fail(msg, __LINE__);                                    \
  } while (0)

struct Field {
  std::vector<float> v;
  int w = 0, h = 0;
  bool covered(int x, int y) const { return v[(size_t)y * w + x] > 1e-7f; }
};

// The stones alone (the node's displacement output), for one set of settings.
Field run(const std::vector<std::pair<std::string, float>> &floats,
          const std::vector<std::pair<std::string, int>> &ints = {}) {
  gpx::Graph g;
  g.resolution = 128;
  gpx::Node *flat = g.add_node("Constant");
  gpx::Node *st = g.add_node("FakeStones");
  g.add_link(flat->id, "output", st->id, "input");
  // Stones only appear inside the slope band, and a Constant is perfectly
  // flat, so the band has to include zero slope or nothing is ever placed.
  if (gpx::Attribute *sb = st->attrs.find("slope_band")) {
    sb->v2[0] = 0.f;
    sb->v2[1] = 1.f;
  }
  // Patchiness off: it is large-scale noise that would swamp the differences
  // being measured here.
  if (gpx::Attribute *a = st->attrs.find("vary_density")) a->f = 0.f;
  for (const auto &f : floats)
    if (gpx::Attribute *a = st->attrs.find(f.first)) a->f = f.second;
  for (const auto &i : ints)
    if (gpx::Attribute *a = st->attrs.find(i.first)) a->i = i.second;
  g.evaluate();
  Field out;
  gpx::Port *p = st->port("displacement", gpx::PortDir::Out);
  if (p && p->hmap) {
    out.v = p->hmap->v;
    out.w = p->hmap->w;
    out.h = p->hmap->h;
  }
  return out;
}

double area(const Field &f) {
  double n = 0;
  for (float v : f.v)
    if (v > 1e-7f) ++n;
  return n;
}
double total(const Field &f) {
  double s = 0;
  for (float v : f.v) s += v;
  return s;
}
float peak(const Field &f) {
  float m = 0;
  for (float v : f.v) m = std::max(m, v);
  return m;
}

// How much of `child`'s new ground sits within `reach` texels of `parent`'s.
// The measure clustering is supposed to move.
double share_near_parent(const Field &parent, const Field &child, int reach) {
  double near = 0, fresh = 0;
  for (int y = 0; y < child.h; ++y)
    for (int x = 0; x < child.w; ++x) {
      if (!child.covered(x, y) || parent.covered(x, y)) continue; // new ground
      ++fresh;
      bool close = false;
      for (int dy = -reach; dy <= reach && !close; ++dy)
        for (int dx = -reach; dx <= reach && !close; ++dx) {
          int px = x + dx, py = y + dy;
          if (px < 0 || py < 0 || px >= parent.w || py >= parent.h) continue;
          if (parent.covered(px, py)) close = true;
        }
      if (close) ++near;
    }
  return fresh > 0 ? near / fresh : 0.0;
}

// ---------------------------------------------------------------------------

// The controls must be inert at one layer, or every project that has a
// FakeStones in it changes the moment this node gains a default.
void test_one_layer_ignores_the_layer_controls() {
  std::printf("fake stones: one layer ignores the layer controls...\n");
  Field a = run({}, {{"layers", 1}});
  Field b = run({{"layer_scale", 0.9f},
                 {"layer_density", 3.f},
                 {"cluster", 0.f},
                 {"layer_sharpness", 1.f}},
                {{"layers", 1}});
  CHECK(!a.v.empty(), "the node produced something to compare");
  CHECK(a.v == b.v, "the layer controls do nothing while there is one layer");
}

void test_more_layers_put_down_more_stone() {
  std::printf("fake stones: more layers, more stone...\n");
  Field one = run({}, {{"layers", 1}});
  Field two = run({}, {{"layers", 2}});
  Field three = run({}, {{"layers", 3}});
  CHECK(area(one) > 0, "one layer covers ground");
  CHECK(area(two) > area(one),
        "two layers cover more than one (" + std::to_string(area(one)) + " -> " +
            std::to_string(area(two)) + ")");
  CHECK(area(three) > area(two), "three cover more than two");
  CHECK(total(two) > total(one), "and there is more stone by volume");
}

// The point of the feature: the second generation gathers around the first
// rather than scattering over open ground.
void test_clustering_keeps_the_small_stones_near_the_big_ones() {
  std::printf("fake stones: clustering...\n");
  Field parent = run({}, {{"layers", 1}});
  Field loose = run({{"cluster", 0.f}}, {{"layers", 2}});
  Field tight = run({{"cluster", 1.f}}, {{"layers", 2}});

  const double near_loose = share_near_parent(parent, loose, 2);
  const double near_tight = share_near_parent(parent, tight, 2);
  CHECK(near_tight > near_loose + 0.05,
        "clustered stones sit nearer the big ones (" +
            std::to_string(near_loose) + " -> " + std::to_string(near_tight) +
            ")");
  // ...and being confined, they cover less new ground than a free scatter.
  CHECK(area(tight) < area(loose),
        "and cover less open ground than a free scatter (" +
            std::to_string(area(tight)) + " vs " + std::to_string(area(loose)) +
            ")");
  CHECK(area(tight) > area(parent),
        "while still adding stone the first layer did not have");
}

// Sharpness has to change a stone's shape, not merely its size: a shard is
// thinner than a boulder of the same footprint and the same height.
void test_sharpness_makes_a_thinner_stone() {
  std::printf("fake stones: sharpness...\n");
  Field round = run({{"sharpness", 0.f}}, {{"layers", 1}});
  Field shard = run({{"sharpness", 1.f}}, {{"layers", 1}});
  CHECK(std::fabs(peak(round) - peak(shard)) < peak(round) * 0.25f,
        "a shard still reaches about the same height");
  const double fill_round = total(round) / std::max(area(round), 1.0);
  const double fill_shard = total(shard) / std::max(area(shard), 1.0);
  CHECK(fill_shard < fill_round,
        "but holds less rock under the same footprint (" +
            std::to_string(fill_round) + " -> " + std::to_string(fill_shard) +
            ")");
}

// The small generations are the broken ones, so raising this must sharpen the
// later layers without touching the first.
void test_layer_sharpness_only_reaches_the_later_layers() {
  std::printf("fake stones: sharper per layer...\n");
  Field blunt = run({{"layer_sharpness", 0.f}}, {{"layers", 2}});
  Field sharp = run({{"layer_sharpness", 1.f}}, {{"layers", 2}});
  CHECK(blunt.v != sharp.v, "it changes a two-layer field");
  Field one_blunt = run({{"layer_sharpness", 0.f}}, {{"layers", 1}});
  Field one_sharp = run({{"layer_sharpness", 1.f}}, {{"layers", 1}});
  CHECK(one_blunt.v == one_sharp.v, "and leaves a one-layer field alone");
}

// Every layer must be finite and non-negative: a stone that dips below the
// ground it sits on is a hole, and the displacement output feeds a material.
void test_layers_stay_finite_and_positive() {
  std::printf("fake stones: finite and positive...\n");
  for (int L = 1; L <= 4; ++L) {
    Field f = run({{"sharpness", 0.7f}, {"cluster", 0.9f}}, {{"layers", L}});
    bool ok = !f.v.empty();
    for (float v : f.v) ok = ok && std::isfinite(v) && v >= -1e-6f;
    CHECK(ok, "layer count " + std::to_string(L) + " is finite and never digs in");
  }
}

} // namespace

int test_fake_stones_suite() {
  g_failures = 0;
  test_one_layer_ignores_the_layer_controls();
  test_more_layers_put_down_more_stone();
  test_clustering_keeps_the_small_stones_near_the_big_ones();
  test_sharpness_makes_a_thinner_stone();
  test_layer_sharpness_only_reaches_the_later_layers();
  test_layers_stay_finite_and_positive();
  return g_failures;
}
