// Geekatplay TerraForge - the distribution engine, without a window.
//
// What a population must keep true (docs/ECOSYSTEM.md, "Gates"):
//   - the same settings give the same instances, at any thread count;
//   - raising the density adds instances and moves none;
//   - a mask edit moves nothing outside the edited area;
//   - nothing stands where presence is zero, nothing outside a slope band;
//   - affinity gathers a layer around the layer below, repulsion opens a
//     void around it, a negative repulsion fills only that void;
//   - overlap avoidance leaves no pair closer than their footprints;
//   - species follow their weights; the layer and the Points nodes agree.
#include "gpx/imprint.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/parallel.hpp"
#include "gpx/points.hpp"
#include "gpx/scatter.hpp"
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

static bool same_cloud(const PointCloud &a, const PointCloud &b) {
  return a.x == b.x && a.y == b.y && a.id == b.id && a.species == b.species &&
         a.sx == b.sx && a.yaw == b.yaw && a.tilt == b.tilt && a.tint == b.tint;
}

// a ramp: height rises with x, so slope is uniform and known
static Heightmap ramp(int n, float rise) {
  Heightmap h(n, n);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) h.at(x, y) = rise * x / (n - 1);
  return h;
}

static void test_candidates() {
  std::printf("candidates...\n");
  scatter::CandidateParams p;
  p.spacing = 0.05f; // 20 x 20 cells
  p.per_cell = 4;
  PointCloud c;
  scatter::candidates(p, c);
  check(c.size() == 20 * 20 * 4, "lattice x per-cell candidates");
  check(c.has_attrs(), "candidates carry the channel set");
  std::set<uint64_t> ids(c.id.begin(), c.id.end());
  check(ids.size() == c.size(), "ids are unique");
  bool in_range = true;
  for (size_t i = 0; i < c.size(); ++i)
    in_range &= c.x[i] >= 0.f && c.x[i] < 1.f && c.y[i] >= 0.f && c.y[i] < 1.f;
  check(in_range, "candidates stay on the tile");
  PointCloud again;
  scatter::candidates(p, again);
  check(same_cloud(c, again), "candidates are bit-identical across runs");
  p.seed = 7;
  scatter::candidates(p, again);
  check(!same_cloud(c, again), "a different seed is a different lattice");
  // clumping pulls candidates together: the mean nearest-neighbour distance
  // drops, while the ids stay the same
  p.seed = 0;
  p.clump_amount = 0.9f;
  p.clump_size = 0.2f;
  PointCloud cl;
  scatter::candidates(p, cl);
  check(cl.id == c.id, "clumping keeps the ids");
  std::vector<int> nb0, nb1;
  scatter::neighbour_counts(c, 0.02f, nb0);
  scatter::neighbour_counts(cl, 0.02f, nb1);
  long s0 = 0, s1 = 0;
  for (int v : nb0) s0 += v;
  for (int v : nb1) s1 += v;
  check(s1 > s0 * 2, "clumped candidates have far more close neighbours");
}

static void test_filter_stability() {
  std::printf("filter and stability...\n");
  scatter::CandidateParams p;
  p.spacing = 0.02f;
  p.per_cell = 4;
  PointCloud base;
  scatter::candidates(p, base);
  scatter::Presence pr;
  PointCloud lo = base, hi = base;
  scatter::filter(lo, pr, 0.25f);
  scatter::filter(hi, pr, 0.5f);
  check(lo.size() > base.size() * 0.2f && lo.size() < base.size() * 0.3f, "rate 0.25 keeps about a quarter");
  check(hi.size() > base.size() * 0.45f && hi.size() < base.size() * 0.55f, "rate 0.5 keeps about half");
  std::set<uint64_t> in_hi(hi.id.begin(), hi.id.end());
  bool superset = true;
  for (uint64_t id : lo.id) superset &= in_hi.count(id) > 0;
  check(superset, "raising the density adds instances and moves none");

  // a mask that empties the left half
  Heightmap m(64, 64, 1.f);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 32; ++x) m.at(x, y) = 0.f;
  pr.mask = &m;
  PointCloud masked = base;
  scatter::filter(masked, pr, 0.5f);
  bool none_left = true;
  for (float x : masked.x) none_left &= x > 0.49f;
  check(none_left, "nothing stands where presence is zero");
  // and the right half is untouched by the edit
  std::set<uint64_t> in_masked(masked.id.begin(), masked.id.end());
  bool right_same = true;
  for (size_t i = 0; i < hi.size(); ++i)
    if (hi.x[i] > 0.52f) right_same &= in_masked.count(hi.id[i]) > 0;
  check(right_same, "a mask edit moves nothing outside the edited area");
  check(masked.size() < hi.size() * 0.6f && masked.size() > hi.size() * 0.4f, "half the tile, half the instances");

  // threshold: a faint presence places nothing
  Heightmap faint(8, 8, 0.1f);
  scatter::Presence pt;
  pt.mask = &faint;
  pt.threshold = 0.2f;
  PointCloud t = base;
  scatter::filter(t, pt, 1.f);
  check(t.size() == 0, "presence below the threshold places nothing");
}

static void test_presence_environment() {
  std::printf("presence by the ground...\n");
  Heightmap terrain = ramp(128, 0.5f); // tan(slope) = 0.5 -> 26.6 degrees
  scatter::CandidateParams p;
  p.spacing = 0.02f;
  PointCloud c;
  scatter::candidates(p, c);
  scatter::Presence pr;
  pr.terrain = &terrain;
  pr.use_slope = true;
  pr.slope_lo = 0.f; pr.slope_hi = 20.f; pr.slope_fuzz = 0.f;
  PointCloud steep = c;
  scatter::filter(steep, pr, 1.f);
  // the border texels read a half gradient (central difference, clamped),
  // exactly as the material layer does; the ramp proper is empty
  size_t inland = 0;
  for (float x : steep.x) inland += x > 1.f / 128 && x < 1.f - 1.f / 128;
  check(inland == 0, "a 26 degree ramp is outside a 0..20 band");
  pr.slope_hi = 30.f;
  PointCloud ok = c;
  scatter::filter(ok, pr, 1.f);
  check(ok.size() == c.size(), "and inside a 0..30 band");
  // altitude by terrain range: the upper half only
  scatter::Presence pa;
  pa.terrain = &terrain;
  pa.use_altitude = true;
  pa.alt_lo = 0.5f; pa.alt_hi = 1.f; pa.alt_fuzz = 0.f;
  PointCloud high = c;
  scatter::filter(high, pa, 1.f);
  bool upper = true;
  for (float x : high.x) upper &= x >= 0.49f;
  check(upper && high.size() > c.size() * 0.4f, "altitude band keeps the upper half");
  // slope influence thins steep ground
  scatter::Presence ps;
  ps.terrain = &terrain;
  ps.slope_influence = 1.f;
  float at = scatter::presence_at(ps, 0.5f, 0.5f);
  check(at > 0.65f && at < 0.75f, "slope influence 1 on a 26.6 degree slope leaves ~0.70");
  // decay near objects: zero at the object, full far away
  Heightmap dist(16, 16, 1.f);
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x)
      dist.at(x, y) = std::clamp((std::hypot(x - 7.5f, y - 7.5f) - 1.f) / 16.f, 0.f, 1.f);
  scatter::Presence pd;
  pd.distance = &dist;
  pd.decay_influence = 1.f;
  pd.decay_reach = 0.25f;
  check(scatter::presence_at(pd, 0.5f, 0.5f) < 0.05f, "decay: empty at the object");
  check(scatter::presence_at(pd, 0.02f, 0.02f) > 0.95f, "decay: untouched far from it");
  pd.decay_influence = 0.5f;
  check(std::fabs(scatter::presence_at(pd, 0.5f, 0.5f) - 0.5f) < 0.05f, "decay: half influence, half void");
}

static void test_interaction() {
  std::printf("affinity, repulsion, overlap...\n");
  // the layer below: four trees
  PointCloud trees;
  trees.add(0.25f, 0.25f, 1.f);
  trees.add(0.75f, 0.25f, 1.f);
  trees.add(0.25f, 0.75f, 1.f);
  trees.add(0.75f, 0.75f, 1.f);
  trees.ensure_attrs();
  scatter::CandidateParams p;
  p.spacing = 0.01f;
  p.per_cell = 1;
  PointCloud base;
  scatter::candidates(p, base);
  auto mean_dist = [&](const PointCloud &c) {
    std::vector<float> d;
    scatter::nearest_distance(c, trees, 1.f, d);
    double s = 0;
    for (float v : d) s += v;
    return d.empty() ? 0.f : (float)(s / d.size());
  };
  const float d0 = mean_dist(base);
  scatter::Interaction it;
  it.below = &trees;
  it.affinity = 1.f;
  it.affinity_radius = 0.15f;
  PointCloud near = base;
  scatter::interact(near, it);
  check(near.size() > 0 && near.size() < base.size() / 2, "affinity 1 thins the population away from the trees");
  check(mean_dist(near) < d0 * 0.6f, "and what remains gathers around them");
  bool within = true;
  for (size_t i = 0; i < near.size(); ++i) {
    std::vector<float> d;
    PointCloud one; one.add(near.x[i], near.y[i], 1.f);
    scatter::nearest_distance(one, trees, 1.f, d);
    within &= d[0] < 0.15f;
  }
  check(within, "affinity 1: nothing beyond the radius");
  it.affinity = -1.f;
  PointCloud far = base;
  scatter::interact(far, it);
  // -1: the population thins towards the trees; what stands right at a
  // trunk is a rare survivor of a near-zero chance
  auto near_trunks = [&](const PointCloud &c) {
    std::vector<float> d;
    scatter::nearest_distance(c, trees, 1.f, d);
    size_t k = 0;
    for (float v : d) k += v < 0.03f;
    return k;
  };
  check(mean_dist(far) > d0 * 1.02f && near_trunks(far) * 5 < near_trunks(base),
        "affinity -1 keeps the population away");

  it.affinity = 0.f;
  it.repulsion = 1.f;
  it.repulsion_radius = 0.1f;
  PointCloud voids = base;
  scatter::interact(voids, it);
  std::vector<float> dv;
  scatter::nearest_distance(voids, trees, 1.f, dv);
  float dmin = 1.f;
  for (float v : dv) dmin = std::min(dmin, v);
  check(dmin >= 0.1f - 1e-4f, "repulsion 1 opens a void of the full radius");
  it.repulsion = -1.f;
  PointCloud only = base;
  scatter::interact(only, it);
  std::vector<float> dn;
  scatter::nearest_distance(only, trees, 1.f, dn);
  float dmax = 0.f;
  for (float v : dn) dmax = std::max(dmax, v);
  check(only.size() > 0 && dmax <= 0.1f + 1e-4f, "repulsion -1 keeps only the void");
  // both: near the trees but not under them
  it.affinity = 1.f; it.affinity_radius = 0.2f;
  it.repulsion = 1.f; it.repulsion_radius = 0.05f;
  PointCloud ring = base;
  scatter::interact(ring, it);
  std::vector<float> dr;
  scatter::nearest_distance(ring, trees, 1.f, dr);
  bool ringed = ring.size() > 0;
  for (float v : dr) ringed &= v >= 0.05f - 1e-4f && v < 0.2f;
  check(ringed, "affinity with repulsion: a ring around each tree");

  // overlap: no pair closer than the two radii
  scatter::Interaction ov;
  ov.avoid_overlap = true;
  PointCloud dense = base;
  for (size_t i = 0; i < dense.size(); ++i) dense.radius[i] = 0.012f + 0.006f * scatter::unit(dense.id[i], 50);
  scatter::interact(dense, ov);
  check(dense.size() < base.size() && dense.size() > base.size() / 20, "overlap avoidance thins a dense lattice");
  bool clear = true;
  for (size_t a = 0; a < dense.size() && clear; ++a)
    for (size_t b = a + 1; b < dense.size(); ++b) {
      float dx = dense.x[a] - dense.x[b], dy = dense.y[a] - dense.y[b];
      float lim = dense.radius[a] + dense.radius[b];
      if (dx * dx + dy * dy < lim * lim - 1e-7f) { clear = false; break; }
    }
  check(clear, "no two instances closer than their footprints");
  // the same set in another order resolves the same way
  PointCloud shuffled = base;
  for (size_t i = 0; i < shuffled.size(); ++i) shuffled.radius[i] = 0.012f + 0.006f * scatter::unit(shuffled.id[i], 50);
  std::vector<uint8_t> keep(shuffled.size(), 1);
  // reverse by rebuilding
  PointCloud rev;
  for (size_t i = shuffled.size(); i-- > 0;) rev.add(shuffled.x[i], shuffled.y[i], shuffled.v[i]);
  rev.ensure_attrs();
  for (size_t i = 0; i < rev.size(); ++i) {
    rev.id[i] = shuffled.id[rev.size() - 1 - i];
    rev.radius[i] = shuffled.radius[rev.size() - 1 - i];
  }
  scatter::interact(rev, ov);
  std::set<uint64_t> a(dense.id.begin(), dense.id.end()), b(rev.id.begin(), rev.id.end());
  check(a == b, "overlap resolution does not depend on arrival order");
}

static void test_transform() {
  std::printf("transform...\n");
  scatter::CandidateParams p;
  p.spacing = 0.01f;
  PointCloud c;
  scatter::candidates(p, c);
  scatter::Transform t;
  t.species_count = 3;
  t.weights[0] = 1.f; t.weights[1] = 2.f; t.weights[2] = 1.f;
  t.variation = 1.f;
  t.keep_proportions = 1.f;
  t.scale = 2.f;
  t.direction = 0.3f;
  t.radius = 0.004f;
  scatter::transform(c, t);
  size_t cnt[3] = {0, 0, 0};
  for (uint16_t s : c.species) if (s < 3) ++cnt[s];
  const float n = (float)c.size();
  check(std::fabs(cnt[1] / n - 0.5f) < 0.03f && std::fabs(cnt[0] / n - 0.25f) < 0.03f, "species follow their weights");
  bool range = true, prop = true, tilt = true;
  for (size_t i = 0; i < c.size(); ++i) {
    range &= c.sx[i] >= 1.f - 1e-4f && c.sx[i] <= 4.f + 1e-4f; // half to twice of 2
    prop &= std::fabs(c.sx[i] - c.sy[i]) < 1e-5f && std::fabs(c.sx[i] - c.sz[i]) < 1e-5f;
    tilt &= std::fabs(c.tilt[i] - 0.3f) < 1e-6f;
  }
  check(range, "variation 1 spans half to twice");
  check(prop, "keep proportions 1 scales the axes together");
  check(tilt, "direction from surface reaches every instance");
  t.keep_proportions = 0.f;
  PointCloud d = c;
  scatter::transform(d, t);
  bool differ = false;
  for (size_t i = 0; i < d.size() && !differ; ++i) differ = std::fabs(d.sx[i] - d.sy[i]) > 1e-3f;
  check(differ, "keep proportions 0 lets the axes differ");
  // a driver picks the species by interval: value 0 -> species 0, 1 -> 2
  Heightmap drv(16, 16, 0.f);
  for (int y = 0; y < 16; ++y)
    for (int x = 8; x < 16; ++x) drv.at(x, y) = 1.f;
  t.driver = &drv;
  PointCloud e = c;
  scatter::transform(e, t);
  bool left0 = true, right2 = true;
  for (size_t i = 0; i < e.size(); ++i) {
    if (e.x[i] < 0.4f) left0 &= e.species[i] == 0;
    if (e.x[i] > 0.6f) right2 &= e.species[i] == 2;
  }
  check(left0 && right2, "a driver cuts the species by interval");
  // shrink at low density: a lone instance is smaller than one in a crowd
  PointCloud lone;
  lone.add(0.1f, 0.1f, 1.f);
  for (int i = 0; i < 30; ++i) lone.add(0.8f + 0.005f * (i % 6), 0.8f + 0.005f * (i / 6), 1.f);
  lone.ensure_attrs();
  scatter::Transform s;
  s.shrink = 0.5f;
  s.shrink_radius = 0.05f;
  scatter::transform(lone, s);
  check(lone.sx[0] < lone.sx[5] * 0.7f, "shrink at low density: the lone one is smaller");
}

static void test_layer_node() {
  std::printf("EcosystemLayer node...\n");
  Graph g;
  g.resolution = 64;
  Node *eco = g.add_node("EcosystemLayer", 0, 0);
  check(eco != nullptr, "EcosystemLayer registers");
  if (!eco) return;
  eco->attrs.find("size_m")->f = 1000.f;   // 100 ha
  eco->attrs.find("density")->f = 50.f;    // 5000 instances
  eco->attrs.find("spacing_m")->f = 5.f;
  eco->attrs.find("slope_influence")->f = 0.f;
  eco->attrs.find("avoid_overlap")->b = false;
  g.mark_all_dirty();
  g.evaluate();
  const Port *pp = eco->port("points", PortDir::Out);
  check(pp && pp->pts, "the layer emits points");
  if (!pp || !pp->pts) return;
  const size_t n0 = pp->pts->size();
  check(n0 > 4500 && n0 < 5500, "50 per hectare over 100 ha is ~5000 (" + std::to_string(n0) + ")");
  check(pp->pts->has_attrs(), "with the full channel set");
  PointCloud first = *pp->pts;
  // evaluate again: identical
  g.mark_all_dirty();
  g.evaluate();
  check(same_cloud(first, *pp->pts), "the layer is bit-identical across evaluations");
  // at another thread count: identical
  set_worker_count(3);
  g.mark_all_dirty();
  g.evaluate();
  check(same_cloud(first, *pp->pts), "and across thread counts");
  set_worker_count(0);
  // density up: superset
  eco->attrs.find("density")->f = 80.f;
  g.mark_dirty(eco->id);
  g.evaluate();
  std::set<uint64_t> more(pp->pts->id.begin(), pp->pts->id.end());
  bool sup = true;
  for (uint64_t id : first.id) sup &= more.count(id) > 0;
  check(sup && pp->pts->size() > first.size(), "more density on the layer adds, never moves");
  // the density map exists and is the presence rule
  const Port *dp = eco->port("density", PortDir::Out);
  check(dp && dp->hmap && dp->hmap->w == 256, "the layer publishes its density map");
  // objects: a footprint through TerrainImprint's 'objects' output keeps
  // the population clear of the house
  Node *src = g.add_node("Constant", 0, 0);
  Node *imp = g.add_node("TerrainImprint", 0, 0);
  check(src && imp, "imprint chain");
  if (src && imp) {
    g.add_link(src->id, "output", imp->id, "input");
    imp->attrs.find("footprints")->s = "0.5 0 0 0.05 4 0.4 0.4 0.6 0.4 0.6 0.6 0.4 0.6";
    g.add_link(imp->id, "objects", eco->id, "objects");
    eco->attrs.find("decay_influence")->f = 1.f;
    eco->attrs.find("decay_reach")->f = 0.1f;
    g.mark_all_dirty();
    g.evaluate();
    const Port *op = imp->port("objects", PortDir::Out);
    check(op && op->hmap && op->hmap->sample(0.5f, 0.5f) == 0.f && op->hmap->sample(0.05f, 0.05f) > 0.4f,
          "TerrainImprint's objects map is 0 inside the footprint and grows away from it");
    bool clear = true;
    for (size_t i = 0; i < pp->pts->size(); ++i)
      clear &= !(pp->pts->x[i] > 0.41f && pp->pts->x[i] < 0.59f && pp->pts->y[i] > 0.41f && pp->pts->y[i] < 0.59f);
    check(clear && pp->pts->size() > 1000, "decay near objects keeps the population out of the house");
  }
}

static void test_stack_and_nodes_agree() {
  std::printf("a stack of layers; the Points nodes...\n");
  Graph g;
  g.resolution = 64;
  Node *trees = g.add_node("EcosystemLayer", 0, 0);
  Node *grass = g.add_node("EcosystemLayer", 0, 0);
  if (!trees || !grass) { check(false, "layers"); return; }
  for (Node *n : {trees, grass}) {
    n->attrs.find("size_m")->f = 1000.f;
    n->attrs.find("slope_influence")->f = 0.f;
    n->attrs.find("avoid_overlap")->b = false;
  }
  trees->attrs.find("density")->f = 2.f;      // 200 trees
  trees->attrs.find("spacing_m")->f = 20.f;
  trees->attrs.find("footprint_m")->f = 10.f;
  grass->attrs.find("density")->f = 300.f;    // 30000 candidates' worth
  grass->attrs.find("spacing_m")->f = 4.f;
  grass->attrs.find("seed")->seed = 9;
  g.add_link(trees->id, "points", grass->id, "below");
  grass->attrs.find("repulsion")->f = 1.f;
  grass->attrs.find("repulsion_radius_m")->f = 20.f; // 0.02 tile, minus the 0.01 canopy
  g.mark_all_dirty();
  g.evaluate();
  const PointCloud &T = *trees->port("points", PortDir::Out)->pts;
  const PointCloud &G = *grass->port("points", PortDir::Out)->pts;
  check(T.size() > 150 && T.size() < 250, "the tree layer places ~200");
  std::vector<float> d;
  scatter::nearest_distance(G, T, 1.f, d);
  float dmin = 1.f;
  for (float v : d) dmin = std::min(dmin, v);
  check(G.size() > 10000 && dmin >= 0.02f - 1e-4f, "grass keeps out from under the canopy (" + std::to_string(dmin) + ")");

  // the same population from the pieces
  Node *area = g.add_node("ScatterArea", 0, 0);
  Node *xf = g.add_node("PointsTransform", 0, 0);
  Node *inter = g.add_node("PointsInteract", 0, 0);
  if (!area || !xf || !inter) { check(false, "points nodes register"); return; }
  for (const Attribute &at : grass->attrs.items) {
    for (Node *n : {area, xf, inter})
      if (Attribute *mine = n->attrs.find(at.key)) *mine = at;
  }
  g.add_link(area->id, "points", xf->id, "points");
  g.add_link(xf->id, "points", inter->id, "points");
  g.add_link(trees->id, "points", inter->id, "below");
  g.mark_all_dirty();
  g.evaluate();
  const PointCloud &P = *inter->port("points", PortDir::Out)->pts;
  check(same_cloud(P, G), "ScatterArea > PointsTransform > PointsInteract equals the layer");
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0); // a crash must not eat the log
  test_candidates();
  test_filter_stability();
  test_presence_environment();
  test_interaction();
  test_transform();
  test_layer_node();
  test_stack_and_nodes_agree();
  std::printf("%d checks, %s\n", checks, failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
