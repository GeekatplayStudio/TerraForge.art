// Geekatplay TerraForge - the plant engine: curves, random parameters, and
// growing a species into a mesh (engine/gpx/plant.hpp).
//
// The promises, each asserted directly:
//   1. a curve evaluates, keeps its shape through text, and a curve set
//      picks the same alternative for the same plant every time;
//   2. a random parameter reads its spread the three ways the manual
//      defines, and stays inside the attribute's range;
//   3. a species of trunk, branches and leaves grows a mesh with positions,
//      normals, texture coordinates, wind weights, tints and materials;
//   4. the same seed grows the same bytes, twice, and another seed does not;
//   5. what the parameters say happens: more branches means more parts, a
//      cut shortens, winter drops the leaves, health browns the tint;
//   6. the wind moves the crown and leaves the foot where it stands, and
//      the shader's twin is there for the studio to splice in.
#include "gpx/plant.hpp"
#include "gpx/plant_curve.hpp"
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <cstdlib>
#include <vector>

using namespace gpx;

int test_plant_species_all(); // tests/cpp/test_plant_species.cpp

static int failures = 0;
static void check(bool ok, const std::string &what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
  }
}

// ---------------------------------------------------------------- curves
static void test_curves() {
  std::printf("curves...\n");
  const Curve c = Curve::constant(0.7f);
  check(std::fabs(c.eval(0.f) - 0.7f) < 1e-6f && std::fabs(c.eval(1.f) - 0.7f) < 1e-6f, "a constant curve is constant");
  const Curve line = Curve::line(1.f, 0.f);
  check(std::fabs(line.eval(0.5f) - 0.5f) < 1e-3f, "a line halves at the middle");
  check(line.eval(-2.f) == 1.f && line.eval(3.f) == 0.f, "outside its domain it holds its ends");
  Curve hump = Curve::through({0.f, 0.f, 0.5f, 1.f, 1.f, 0.f});
  check(hump.eval(0.5f) > 0.9f && hump.eval(0.f) < 0.1f, "a curve through points passes through them");
  bool monotone = true;
  for (float x = 0.f; x < 0.5f; x += 0.02f) monotone = monotone && hump.eval(x) <= hump.eval(x + 0.02f) + 1e-4f;
  check(monotone, "and does not overshoot between them");

  Curve back;
  check(curve_from_string(curve_to_string(hump), back), "a curve reads back from its text");
  check(back.keys.size() == hump.keys.size() && std::fabs(back.eval(0.37f) - hump.eval(0.37f)) < 1e-4f,
        "and evaluates the same");

  CurveSet set;
  set.curves = {Curve::constant(1.f), Curve::constant(2.f), Curve::constant(3.f)};
  set.weights = {1.f, 1.f, 1.f};
  const float a = set.eval(0.5f, 4242u), b = set.eval(0.5f, 4242u);
  check(a == b, "a curve set picks the same alternative for one seed");
  int seen[4] = {0, 0, 0, 0};
  for (uint32_t s = 1; s < 200; ++s) seen[(int)set.eval(0.5f, s)]++;
  check(seen[1] > 20 && seen[2] > 20 && seen[3] > 20, "and spreads its alternatives over the seeds");
}

// ------------------------------------------------------------ the schema
// Every parameter of every plant node is declared once. A key declared
// twice on one node is unreachable by the second declaration - AttrSet::find
// returns the first - so a parameter would silently do nothing.
static void test_schema() {
  std::printf("the parameter tables...\n");
  Graph g;
  int total = 0;
  for (int i = 0; i < (int)plant::Kind::COUNT; ++i) {
    const std::string type = plant::kind_type((plant::Kind)i);
    Node *n = g.add_node(type, 0, 0);
    check(n != nullptr, type + " is a node type");
    if (!n) continue;
    total += (int)n->attrs.items.size();
    std::vector<std::string> seen;
    for (const Attribute &a : n->attrs.items) {
      const bool dup = std::find(seen.begin(), seen.end(), a.key) != seen.end();
      check(!dup, type + " declares '" + a.key + "' twice");
      seen.push_back(a.key);
      check(!a.tooltip.empty(), type + "'s '" + a.key + "' has a tooltip");
      if (a.type == AttrType::Choice) check(!a.labels.empty(), type + "'s '" + a.key + "' has choices");
    }
    g.remove_node(n->id);
  }
  check(total > 500, "the tables hold the manual's parameters (" + std::to_string(total) + ")");
}

// ------------------------------------------------------- random parameters
static void test_random_range() {
  std::printf("random range...\n");
  using plant::random_range;
  check(random_range(10.f, 1.f, 0, 1.f, 0.f) == 11.f, "absolute: the value plus the spread");
  check(random_range(10.f, 1.f, 0, 0.f, 0.f) == 9.f, "absolute: and minus it");
  check(std::fabs(random_range(10.f, 0.2f, 1, 1.f, 0.f) - 12.f) < 1e-5f, "relative: a share of the value");
  check(std::fabs(random_range(10.f, 2.f, 2, 0.5f, 1.5f) - 13.f) < 1e-5f, "gaussian: the deviation times the draw");
  check(random_range(10.f, 0.f, 0, 0.9f, 2.f) == 10.f, "no spread, no variation");
}

// ------------------------------------------------------------- a species
// Trunk -> branches -> leaves, the graph a person builds first.
struct Species {
  Graph g;
  Node *root = nullptr, *trunk = nullptr, *branch = nullptr, *leaf = nullptr;
};
static void make_species(Species &s) {
  s.root = s.g.add_node("PlantSpecies", 900, 0);
  s.trunk = s.g.add_node("PlantSegment", 600, 0);
  s.branch = s.g.add_node("PlantSegment", 300, 0);
  s.leaf = s.g.add_node("PlantLeaf", 0, 0);
  if (!s.root || !s.trunk || !s.branch || !s.leaf) return;
  s.g.add_link(s.trunk->id, "plant", s.root->id, "trunk");
  s.g.add_link(s.branch->id, "plant", s.trunk->id, "child 1");
  s.g.add_link(s.leaf->id, "plant", s.branch->id, "child 1");
  if (Attribute *a = s.trunk->attrs.find("length")) a->f = 6.f;
  if (Attribute *a = s.trunk->attrs.find("radius")) a->f = 0.22f;
  if (Attribute *a = s.branch->attrs.find("length")) a->f = 2.f;
  if (Attribute *a = s.branch->attrs.find("count")) a->f = 12.f;
  if (Attribute *a = s.branch->attrs.find("start")) a->f = 0.35f;
  if (Attribute *a = s.leaf->attrs.find("count")) a->f = 8.f;
}

static bool build(Species &s, PlantBuildOptions o, PlantMesh &m, std::string &err) {
  return plant_build(s.g, *s.root, o, m, err);
}

static void test_grows() {
  std::printf("a species grows...\n");
  Species s;
  make_species(s);
  check(s.root && s.leaf, "the nodes exist");
  if (!s.root) return;
  PlantMesh m;
  std::string err;
  PlantBuildOptions o;
  o.seed = 7;
  check(build(s, o, m, err), "it builds: " + err);
  check(m.vertex_count() > 200, "with a mesh of real size");
  check(m.triangle_count() > 100, "and triangles");
  check(m.nrm.size() == m.pos.size(), "a normal per vertex");
  check(m.uv.size() / 2 == m.vertex_count(), "a texture coordinate per vertex");
  check(m.wind.size() / 4 == m.vertex_count(), "a wind weight per vertex");
  check(m.tint.size() / 4 == m.vertex_count(), "a tint per vertex");
  check(m.height_m > 1.f && m.height_m < 40.f, "it is a plausible height: " + std::to_string(m.height_m));
  check(m.parts.size() >= 2, "it has parts");
  check(!m.materials.empty(), "and materials");
  check(m.leaves > 0, "and leaves");
  bool finite = true;
  for (float v : m.pos) finite = finite && std::isfinite(v);
  check(finite, "every position is a number");
  bool w_ok = true;
  for (size_t i = 0; i + 3 < m.wind.size(); i += 4)
    w_ok = w_ok && m.wind[i + 3] >= 0.f && m.wind[i + 3] <= 1.f;
  check(w_ok, "the wind height is a fraction of the plant");
  bool idx_ok = true;
  for (uint32_t i : m.idx) idx_ok = idx_ok && i < m.vertex_count();
  check(idx_ok, "every triangle names a vertex that exists");
  // the trunk stands at the origin and grows up
  check(std::fabs(m.bmin[1]) < 0.5f, "its foot is on the ground");
}

static void test_determinism() {
  std::printf("determinism...\n");
  Species s;
  make_species(s);
  if (!s.root) return;
  if (Attribute *a = s.branch->attrs.find("length")) {
    a->spread = 0.4f;
    a->spread_mode = 1;
  }
  PlantBuildOptions o;
  o.seed = 11;
  PlantMesh a, b, c;
  std::string err;
  build(s, o, a, err);
  build(s, o, b, err);
  check(a.pos == b.pos && a.idx == b.idx && a.uv == b.uv, "the same seed grows the same bytes");
  o.seed = 12;
  build(s, o, c, err);
  check(a.pos != c.pos, "another seed grows another individual");
  check(std::fabs(a.height_m - c.height_m) < a.height_m * 0.5f, "but the same kind of plant");
}

static void test_parameters() {
  std::printf("parameters do what they say...\n");
  Species s;
  make_species(s);
  if (!s.root) return;
  PlantBuildOptions o;
  o.seed = 3;
  PlantMesh few, many;
  std::string err;
  if (Attribute *a = s.branch->attrs.find("count")) a->f = 4.f;
  build(s, o, few, err);
  if (Attribute *a = s.branch->attrs.find("count")) a->f = 16.f;
  build(s, o, many, err);
  check(many.primitives > few.primitives, "more branches means more parts");
  check(many.leaves > few.leaves, "and more leaves");

  // a cut branch is a shorter branch
  PlantMesh whole, cut;
  build(s, o, whole, err);
  if (Attribute *a = s.branch->attrs.find("cut_probability")) a->f = 1.f;
  if (Attribute *a = s.branch->attrs.find("cut_length")) a->f = 0.3f;
  build(s, o, cut, err);
  const float wide = whole.bmax[0] - whole.bmin[0], narrow = cut.bmax[0] - cut.bmin[0];
  check(narrow < wide, "cutting every branch pulls the crown in");
  check(cut.cut > 0, "and says how many were cut");
  if (Attribute *a = s.branch->attrs.find("cut_probability")) a->f = 0.f;

  // winter: the leaves are gone, and the plant is still there
  PlantMesh summer, winter;
  o.season = 0.5f;
  build(s, o, summer, err);
  if (Attribute *a = s.leaf->attrs.find("presence_season")) {
    Curve c;
    curve_from_string("d:0,1,0,1|0;0,0,0,0;0.2,1,0,0;0.8,1,0,0;1,0,0,0", c);
    a->curves.curves = {c};
    a->curves.weights = {1.f};
  }
  o.season = 0.98f;
  build(s, o, winter, err);
  check(winter.leaves < summer.leaves, "winter drops the leaves");
  check(winter.vertex_count() > 100, "and leaves the wood standing");
}

// ------------------------------------------------------------- the flower
// The bloom, and the turn that nearly did not happen: a part standing
// straight up asked to droop has no axis to turn about (the cross product of
// a direction with its own opposite is zero), and every builder used to skip
// the turn. Gravitropism and the dry droop then did nothing on exactly the
// parts that stand upright. Both are asserted here.
static void test_flower() {
  std::printf("the flower...\n");
  Graph g;
  Node *root = g.add_node("PlantSpecies", 600, 0);
  Node *stem = g.add_node("PlantSegment", 300, 0);
  Node *fl = g.add_node("PlantFlower", 0, 0);
  if (!root || !stem || !fl) {
    check(false, "a flower on a stem");
    return;
  }
  g.add_link(stem->id, "plant", root->id, "trunk");
  g.add_link(fl->id, "plant", stem->id, "child 1");
  if (Attribute *a = stem->attrs.find("length")) a->f = 0.4f;
  if (Attribute *a = stem->attrs.find("radius")) a->f = 0.004f;
  if (Attribute *a = fl->attrs.find("count")) a->f = 1.f;
  if (Attribute *a = fl->attrs.find("positioning")) a->i = 0; // on the tip

  PlantBuildOptions o;
  o.seed = 4;
  PlantMesh m;
  std::string err;
  check(plant_build(g, *root, o, m, err), "it grows: " + err);
  check(m.vertex_count() > 100, "the bloom has a mesh (" + std::to_string(m.vertex_count()) + " vertices)");
  size_t petals = 0;
  for (const PlantPart &p : m.parts)
    if (p.kind == PlantPartKind::Petal) ++petals;
  check(petals >= 1, "and a petal part");
  bool uv_ok = true, finite = true;
  for (const PlantPart &p : m.parts) {
    if (p.kind != PlantPartKind::Petal) continue;
    for (uint32_t k = p.first_index; k < p.first_index + p.index_count && k < m.idx.size(); ++k) {
      const size_t i = m.idx[k];
      if (m.uv.size() < (i + 1) * 2) continue;
      uv_ok = uv_ok && m.uv[i * 2] >= -0.01f && m.uv[i * 2] <= 1.01f && m.uv[i * 2 + 1] >= -0.01f &&
              m.uv[i * 2 + 1] <= 1.01f;
    }
  }
  for (float v : m.pos) finite = finite && std::isfinite(v);
  check(uv_ok, "the bloom's texture coordinates are inside its picture");
  check(finite, "every position is a number");

  // the flutter weight runs from the foot of the bloom to its rim
  float fmin = 2.f, fmax = -1.f;
  for (const PlantPart &p : m.parts) {
    if (p.kind != PlantPartKind::Petal) continue;
    for (uint32_t k = p.first_index; k < p.first_index + p.index_count && k < m.idx.size(); ++k) {
      const size_t i = m.idx[k];
      if (m.wind.size() < (i + 1) * 4) continue;
      fmin = std::min(fmin, m.wind[i * 4 + 2]);
      fmax = std::max(fmax, m.wind[i * 4 + 2]);
    }
  }
  check(fmin < 0.2f && fmax > 0.8f, "the bloom flutters from its foot to its rim");

  // the same seed grows the same bloom; another seed grows another one
  PlantMesh again;
  check(plant_build(g, *root, o, again, err) && again.pos == m.pos, "the same seed grows the same bloom");

  // the regression: a flower held upright and told to fall must fall
  PlantMesh upright, drooped;
  if (Attribute *a = fl->attrs.find("orient_vertical")) a->f = 1.f; // stand it straight up
  plant_build(g, *root, o, upright, err);
  if (Attribute *a = fl->attrs.find("gravitropism")) a->f = 1.f;
  plant_build(g, *root, o, drooped, err);
  check(drooped.pos != upright.pos, "an upright flower told to droop does droop");
  // and it droops downward: the bloom's middle sits lower than it did. (Its
  // topmost point may rise a little - a disc tipped over lifts one edge - so
  // the bounding box is the wrong thing to ask.)
  auto bloom_centre_y = [](const PlantMesh &mm) {
    double sum = 0.0;
    size_t n = 0;
    for (const PlantPart &p : mm.parts) {
      if (p.kind != PlantPartKind::Petal) continue;
      for (uint32_t k = p.first_index; k < p.first_index + p.index_count && k < mm.idx.size(); ++k) {
        sum += mm.pos[(size_t)mm.idx[k] * 3 + 1];
        ++n;
      }
    }
    return n ? (float)(sum / (double)n) : 0.f;
  };
  const float up_y = bloom_centre_y(upright), down_y = bloom_centre_y(drooped);
  check(down_y < up_y - 1e-4f, "and the bloom hangs lower than it stood (" + std::to_string(down_y) +
                                   " below " + std::to_string(up_y) + ")");
  if (Attribute *a = fl->attrs.find("gravitropism")) a->f = 0.f;
  if (Attribute *a = fl->attrs.find("orient_vertical")) a->f = 0.f;
}

// ----------------------------------------------- the plant holds together
// A part must not come away from the part it grows on. The wind moves every
// vertex from four baked numbers alone, so two vertices in the same place
// that disagree about any of them are pulled apart - and they did: a leaf
// drew its own wind phase, and its flutter had a floor under it that shook
// its hook, the very point nailed to the twig. The crown came apart and
// jittered.
//
// The test pairs each leaf's hook - the vertex whose flutter is least, the
// one it hangs by - with the wood vertex touching it, then blows a full gale
// and checks the pair is still together. A small species is used on purpose:
// in a mature crown the wood nearest a leaf is often a neighbour's twig, and
// two different branches are supposed to move apart.
// --------------------------------------------- flexibility from thickness
// A branch bends like a beam clamped at one end: its stiffness is EI with
// I ~ r^4 and the wind's push on it goes with its diameter, so how far it
// swings relative to its own length goes as the slenderness (L/r) cubed.
// A thin branch must therefore move far more than a thick one of the same
// length, and neither may move at the point it is fixed.
static void test_flexibility() {
  std::printf("flexibility follows thickness...\n");
  auto tip_swing = [](float radius) {
    Graph g;
    Node *root = g.add_node("PlantSpecies", 600, 0);
    Node *trunk = g.add_node("PlantSegment", 300, 0);
    if (!root || !trunk) return 0.f;
    g.add_link(trunk->id, "plant", root->id, "trunk");
    if (Attribute *a = trunk->attrs.find("length")) a->f = 8.f;
    if (Attribute *a = trunk->attrs.find("radius")) a->f = radius;
    if (Attribute *a = trunk->attrs.find("radius_mode")) a->i = 1; // user defined
    PlantMesh m;
    PlantBuildOptions o;
    o.seed = 3;
    std::string err;
    if (!plant_build(g, *root, o, m, err) || m.vertex_count() == 0) return 0.f;
    PlantWind w;
    w.strength = 1.f;
    w.dir[0] = 1.f;
    w.dir[1] = 0.f;
    size_t top = 0;
    for (size_t i = 1; i < m.vertex_count(); ++i)
      if (m.pos[i * 3 + 1] > m.pos[top * 3 + 1]) top = i;
    // How far the tip travels, not where it happens to be: the sway is a sum
    // of sines and one instant says nothing - a stiff stem leaning steadily
    // can be further from where it started than a whippy one caught swinging
    // back through the middle.
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < 40; ++k) {
      std::vector<float> moved;
      plant_wind_apply(m, w, (float)k * 0.17f, moved);
      const float along = moved[top * 3] - m.pos[top * 3]; // the wind blows along +x
      lo = std::min(lo, along);
      hi = std::max(hi, along);
    }
    return hi - lo;
  };

  const float thick = tip_swing(0.5f);   // slenderness 16: a stout trunk
  const float thin = tip_swing(0.06f);   // slenderness 133: a wand
  check(thin > thick * 4.f, "a thin stem swings far more than a thick one of the same length (" +
                                std::to_string(thin) + " against " + std::to_string(thick) + ")");
  check(thick >= 0.f && thick < 0.6f, "and a stout one barely moves (" + std::to_string(thick) + " m of travel)");

  // the shape along it: a cantilever leaves its clamped end with no slope,
  // so the wood at the very foot does not move at all however hard it blows
  Graph g;
  Node *root = g.add_node("PlantSpecies", 600, 0);
  Node *trunk = g.add_node("PlantSegment", 300, 0);
  if (!root || !trunk) return;
  g.add_link(trunk->id, "plant", root->id, "trunk");
  if (Attribute *a = trunk->attrs.find("length")) a->f = 8.f;
  if (Attribute *a = trunk->attrs.find("radius")) a->f = 0.06f;
  PlantMesh m;
  PlantBuildOptions o;
  o.seed = 3;
  std::string err;
  if (!plant_build(g, *root, o, m, err)) return;
  PlantWind w;
  w.strength = 1.f;
  w.dir[0] = 1.f;
  std::vector<float> moved;
  plant_wind_apply(m, w, 1.1f, moved);
  float foot = 0.f;
  const float low = m.bmin[1] + (m.bmax[1] - m.bmin[1]) * 0.02f;
  for (size_t i = 0; i < m.vertex_count(); ++i) {
    if (m.pos[i * 3 + 1] > low) continue;
    float d = 0.f;
    for (int c = 0; c < 3; ++c) {
      const float e = moved[i * 3 + c] - m.pos[i * 3 + c];
      d += e * e;
    }
    foot = std::max(foot, std::sqrt(d));
  }
  check(foot < m.height_m * 0.002f,
        "the foot stays where it stands in a gale (" + std::to_string(foot) + " m)");
}

static void test_holds_together() {
  std::printf("the plant holds together in the wind...\n");
  Species sp;
  make_species(sp);
  if (!sp.root) return;
  PlantMesh m;
  PlantBuildOptions o;
  o.seed = 21;
  std::string err;
  if (!build(sp, o, m, err)) {
    check(false, "it grows: " + err);
    return;
  }

  // every leaf's hook, and the wood vertex it touches
  struct Joint { size_t leaf, wood; float rest; };
  std::vector<Joint> joints;
  std::vector<size_t> wood;
  for (const PlantPart &p : m.parts) {
    if (p.kind != PlantPartKind::Body && p.kind != PlantPartKind::Cap) continue;
    for (uint32_t k = p.first_index; k < p.first_index + p.index_count && k < m.idx.size(); ++k)
      wood.push_back(m.idx[k]);
  }
  check(!wood.empty(), "the plant has wood");
  for (const PlantPart &p : m.parts) {
    if (p.kind != PlantPartKind::Leaf) continue;
    size_t hook = SIZE_MAX;
    float least = 1e9f;
    for (uint32_t k = p.first_index; k < p.first_index + p.index_count && k < m.idx.size(); ++k) {
      const size_t v = m.idx[k];
      if (m.wind.size() > v * 4 + 2 && m.wind[v * 4 + 2] < least) {
        least = m.wind[v * 4 + 2];
        hook = v;
      }
    }
    if (hook == SIZE_MAX) continue;
    check(least < 1e-6f, "a leaf's hook does not flutter (it is nailed to the wood)");
    size_t near = SIZE_MAX;
    float best = 1e18f;
    for (size_t wv : wood) {
      float q = 0.f;
      for (int c = 0; c < 3; ++c) {
        const float e = m.pos[hook * 3 + c] - m.pos[wv * 3 + c];
        q += e * e;
      }
      if (q < best) {
        best = q;
        near = wv;
      }
    }
    if (near != SIZE_MAX) joints.push_back({hook, near, std::sqrt(best)});
  }
  check(joints.size() > 10, "there are leaves to check (" + std::to_string(joints.size()) + ")");
  if (joints.empty()) return;

  // the four numbers must agree where the two meet
  // Averaged, not worst-case: the wood nearest a hook is a ring vertex a
  // little to one side, and a leaf at a branch's foot may well be nearer the
  // trunk's skin than its own branch. A phase drawn per part - the bug this
  // guards - averages a quarter of a cycle apart, nowhere near this.
  double sum_phase = 0, sum_bend = 0;
  int touching = 0;
  for (const Joint &j : joints) {
    if (j.rest > 0.02f) continue; // not a joint, just a near neighbour
    ++touching;
    sum_phase += std::fabs(m.wind[j.leaf * 4] - m.wind[j.wood * 4]);
    sum_bend += std::fabs(m.wind[j.leaf * 4 + 1] - m.wind[j.wood * 4 + 1]);
  }
  check(touching > 5, "leaves are touching their wood at rest (" + std::to_string(touching) + ")");
  const double n = (double)std::max(touching, 1);
  check(sum_phase / n < 0.05, "a leaf and its wood are in the same part of the gust (" +
                                  std::to_string(sum_phase / n) + " apart on average)");
  check(sum_bend / n < 0.25, "and bend by nearly the same amount (" + std::to_string(sum_bend / n) + ")");

  // now blow, hard, and see whether they stay together
  PlantWind w;
  w.strength = 1.f;
  w.dir[0] = 1.f;
  w.dir[1] = 0.f;
  double worst = 0.0;
  int parted = 0, checked = 0;
  for (float t : {0.3f, 1.7f, 4.2f, 8.8f, 15.5f}) {
    std::vector<float> moved;
    plant_wind_apply(m, w, t, moved);
    for (const Joint &j : joints) {
      if (j.rest > 0.02f) continue;
      float a = 0.f;
      for (int c = 0; c < 3; ++c) {
        const float e = moved[j.leaf * 3 + c] - moved[j.wood * 3 + c];
        a += e * e;
      }
      const float apart = std::fabs(std::sqrt(a) - j.rest);
      worst = std::max(worst, (double)apart);
      ++checked;
      if (apart > m.height_m * 0.01f) ++parted;
    }
  }
  check(checked > 0, "the wind was applied");
  {
    // how far does a leaf's tip swing, measured against the leaf's own size?
    // Flutter is a leaf rippling; it must not stretch the leaf out of shape.
    std::vector<float> moved;
    plant_wind_apply(m, w, 1.7f, moved);
    double worst_ratio = 0.0;
    for (const PlantPart &p : m.parts) {
      if (p.kind != PlantPartKind::Leaf) continue;
      float rest_span = 0.f, blown_span = 0.f;
      float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
      float lo2[3] = {1e9f, 1e9f, 1e9f}, hi2[3] = {-1e9f, -1e9f, -1e9f};
      for (uint32_t k = p.first_index; k < p.first_index + p.index_count && k < m.idx.size(); ++k) {
        const size_t v = m.idx[k];
        for (int c = 0; c < 3; ++c) {
          lo[c] = std::min(lo[c], m.pos[v * 3 + c]);
          hi[c] = std::max(hi[c], m.pos[v * 3 + c]);
          lo2[c] = std::min(lo2[c], moved[v * 3 + c]);
          hi2[c] = std::max(hi2[c], moved[v * 3 + c]);
        }
      }
      for (int c = 0; c < 3; ++c) {
        rest_span = std::max(rest_span, hi[c] - lo[c]);
        blown_span = std::max(blown_span, hi2[c] - lo2[c]);
      }
      if (rest_span > 1e-5f) worst_ratio = std::max(worst_ratio, (double)(blown_span / rest_span));
    }
    // Flutter is a leaf rippling, so it is baked as an amplitude in metres -
    // a third of the leaf's own length - and the wind turns that into its own
    // units. Baked as a bare 0..1 weight, the wind scaled it by the whole
    // PLANT's height instead, and every leaf on a 22 m oak was smeared over
    // 22 cm, twice its own length: the crown drew as green streaks.
    check(worst_ratio < 1.6, "the wind ripples a leaf, it does not stretch it (" +
                                 std::to_string(worst_ratio) + "x its own size)");
  }
  check(parted == 0, "no leaf comes away from its twig in a gale (" + std::to_string(parted) + " of " +
                         std::to_string(checked) + " did, worst " + std::to_string(worst) + " m)");
  check(worst < m.height_m * 0.01f,
        "the worst joint holds to within a hundredth of the plant's height (" + std::to_string(worst) + " m)");
}

static void test_wind() {
  std::printf("wind...\n");
  PlantWind w;
  w.strength = 0.8f;
  w.breeze = 1.f;
  float foot[3] = {0.f, 0.f, 0.f}, top[3] = {0.f, 10.f, 0.f}, out[3] = {0, 0, 0};
  const float still[4] = {0.3f, 0.f, 0.f, 0.f};   // at the foot: no bend, no height
  const float moving[4] = {0.3f, 2.f, 0.f, 1.f};  // at the tip
  plant_wind_vertex(w, 1.7f, foot, still, 10.f, out);
  check(out[0] == foot[0] && out[1] == foot[1] && out[2] == foot[2], "the foot does not move");
  plant_wind_vertex(w, 1.7f, top, moving, 10.f, out);
  check(std::fabs(out[0] - top[0]) > 1e-3f, "the crown does");
  const std::string glsl = plant_wind_glsl();
  check(glsl.find("vec3 plant_wind(") != std::string::npos, "the shader twin is there");
  check(glsl.find("0.6 * sin(t * breeze_speed * 1.3") != std::string::npos,
        "with the same arithmetic as the CPU");
}




int main() {
  test_curves();
  test_schema();
  test_random_range();
  test_grows();
  test_determinism();
  test_parameters();
  test_flower();
  test_holds_together();
  test_flexibility();
  test_wind();
  failures += test_plant_species_all();
  if (failures) {
    std::printf("%d plant check(s) failed\n", failures);
    return 1;
  }
  std::printf("plant tests passed\n");
  return 0;
}
