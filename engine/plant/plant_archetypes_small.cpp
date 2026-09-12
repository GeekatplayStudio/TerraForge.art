// Geekatplay TerraForge - the small-plant archetypes (plant_archetypes.hpp).
//
// What grows below the trees, each from its own habit. A shrub is a Hydra
// of stems from the ground with leaves and, when the description says so,
// flowers and berries. A fern is a rosette of fronds, each a segment with
// pinnate blades; a grass tuft a rosette of flat blades; a reed the same
// but tall and stiff. A flowering plant is stems with leaves and a flower
// head at each tip, present in its season. A columnar cactus is a thick
// star-section segment with arms; a paddle cactus a Repeat of flattened
// segments; a succulent rosette a Hydra of thick curled boards. A vine is
// one long segment that sinks with gravity and wanders; bamboo a Repeat of
// ringed internodes with leaf clusters at the nodes; a mushroom a stem
// with a Flower for a cap; ground cover a Hydra of leaves on the soil.
#include "plant/plant_archetypes.hpp"

namespace gpx {
namespace plant {
namespace arch {

namespace {

// A Hydra at the foot: `n` children fanned from a circle of `radius_m`,
// leaning `lean_deg` from the vertical. The base part of a clump.
Node *make_clump(Builder &b, uint64_t &root_id, float n, float radius_m, float lean_deg,
                 const std::string &label) {
  Node *root = make_root(b);
  Node *h = b.add(Kind::Hydra, 1, label);
  if (!root || !h) return nullptr;
  root_id = root->id;
  b.trunk(*h, *root);
  b.set(*h, "child_count", n, b.spread(), 1);
  b.set(*h, "radius", radius_m, b.spread(), 1);
  b.set(*h, "angle1", 90.f - lean_deg, b.angle_spread() + 5.f, 0);
  b.set(*h, "angle3", 0.f, 180.f, 0);
  return h;
}

// A green, scaly skin for cacti and succulents.
Node *make_skin(Builder &b, int level) {
  Node *m = b.add(Kind::Material, level, "Skin");
  if (!m) return nullptr;
  b.set_s(*m, "name", "Skin");
  b.set_choice(*m, "source", "Bark");
  b.set_choice(*m, "bark_kind", "Scaly");
  b.set_color(*m, "color", rgb(b.d.leaf_color));
  RGB crack{b.d.leaf_color[0] * 0.6f, b.d.leaf_color[1] * 0.6f, b.d.leaf_color[2] * 0.6f};
  b.set_color(*m, "crack_color", crack);
  b.set(*m, "roughness", 0.5f);
  b.set(*m, "bark_scale", 0.5f);
  return m;
}

const char *NO_NODES = "could not add the plant nodes";

} // namespace

uint64_t build_shrub(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *clump = make_clump(b, root, 5.f + b.d.density * 10.f, H * 0.08f, 25.f, "Clump");
  if (!clump) { err = NO_NODES; return 0; }
  Node *stem = make_segment(b, 2, "Stem", H * 0.8f, H * b.d.trunk_ratio * 0.6f, true);
  if (!stem) { err = NO_NODES; return 0; }
  b.set(*stem, "tropism", 0.3f - b.d.droop * 0.7f, 0.1f, 0);
  b.set(*stem, "perturb_strength", 0.3f);
  b.set_choice(*stem, "rdisp_source", "Noise");
  b.set(*stem, "rdisp_amount", 0.03f);
  b.child(*stem, *clump, 1);
  if (Node *bark = make_bark(b, 3)) b.material(*bark, *stem);
  Node *tw = make_segment(b, 3, "Twig", H * 0.3f, 0.5f, false);
  if (!tw) { err = NO_NODES; return 0; }
  b.set(*tw, "count", 6.f + b.d.density * 10.f, b.spread(), 1);
  b.set(*tw, "start", 0.2f);
  b.set(*tw, "angle", attach_angle(b.d.branch_angle_deg), b.angle_spread(), 0);
  b.set(*tw, "tropism", 0.2f - b.d.droop * 0.6f, 0.1f, 0);
  b.set_b(*tw, "cap_enable", false);
  arrange(b, *tw);
  b.child(*tw, *stem, 1);
  foliage(b, 4, *tw, 1, 8.f + b.d.leaf_density * 16.f);
  return root;
}

uint64_t build_fern(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *ros = make_clump(b, root, 6.f + b.d.density * 10.f, 0.02f, 35.f + b.d.droop * 25.f, "Rosette");
  if (!ros) { err = NO_NODES; return 0; }
  Node *frond = make_segment(b, 2, "Frond", H * 1.1f, H * 0.006f, true);
  if (!frond) { err = NO_NODES; return 0; }
  b.set_choice(*frond, "axis_mode", "Curve down");
  b.set(*frond, "axis_bend", 30.f + b.d.droop * 30.f, 10.f, 0);
  b.set(*frond, "tropism", -0.3f - b.d.droop * 0.4f, 0.1f, 0);
  b.set_b(*frond, "cap_enable", false);
  b.set_i(*frond, "blade_number", 2);
  b.set_choice(*frond, "blade_style", "Symmetrical");
  b.set(*frond, "blade_width", H * 0.12f, b.spread(), 1);
  b.set(*frond, "blade_start", 0.2f);
  b.set(*frond, "blade_spread", 170.f);
  b.set(*frond, "blade_section_height", 0.15f);
  b.set_curve(*frond, "blade_profile", Curve::through({0.f, 0.3f, 0.4f, 1.f, 0.85f, 0.6f, 1.f, 0.f}));
  season_leaf(b, *frond);
  b.child(*frond, *ros, 1);
  if (Node *m = make_leaf_material(b, 3, "pinnate")) b.material(*m, *frond, "blade material");
  return root;
}

uint64_t build_grass_tuft(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *tuft = make_clump(b, root, 12.f + b.d.density * 30.f, H * 0.04f, 20.f + b.d.droop * 20.f, "Tuft");
  if (!tuft) { err = NO_NODES; return 0; }
  Node *blade = make_segment(b, 2, "Blade", H, H * 0.004f, true);
  if (!blade) { err = NO_NODES; return 0; }
  b.set_choice(*blade, "skin", "None");
  b.set_choice(*blade, "axis_mode", "Curve down");
  b.set(*blade, "axis_bend", 20.f + b.d.droop * 50.f, 15.f, 0);
  b.set(*blade, "tropism", -0.2f - b.d.droop * 0.5f, 0.15f, 0);
  b.set_b(*blade, "cap_enable", false);
  b.set_i(*blade, "blade_number", 1);
  b.set_choice(*blade, "blade_style", "Simple flat");
  b.set(*blade, "blade_width", b.d.leaf_size_cm * 0.0015f + 0.004f, b.spread(), 1);
  b.set(*blade, "blade_section_height", 0.3f);
  b.set_curve(*blade, "blade_profile", Curve::through({0.f, 1.f, 0.6f, 0.8f, 1.f, 0.f}));
  b.set(*blade, "wind_flexibility", 2.5f);
  b.set(*blade, "wind_blade_influence", 1.5f);
  season_leaf(b, *blade);
  b.child(*blade, *tuft, 1);
  if (Node *m = make_leaf_material(b, 3, "blade")) b.material(*m, *blade, "blade material");
  if (b.d.flowers) {
    Node *head = make_flower(b, 3, *blade, 1, 1.f);
    if (head) b.set_choice(*head, "profile_mode", "Cylinder"); // a seed head
  }
  return root;
}

uint64_t build_flowering_plant(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *clump = make_clump(b, root, 3.f + b.d.density * 6.f, H * 0.05f, 12.f, "Clump");
  if (!clump) { err = NO_NODES; return 0; }
  Node *stem = make_segment(b, 2, "Stem", H, H * 0.008f + 0.002f, true);
  if (!stem) { err = NO_NODES; return 0; }
  b.set(*stem, "tropism", 0.4f - b.d.droop * 0.8f, 0.1f, 0);
  b.set(*stem, "perturb_strength", 0.2f);
  b.set_b(*stem, "cap_enable", false);
  b.set_curve(*stem, "radius_profile", Curve::line(1.f, 0.5f));
  b.child(*stem, *clump, 1);
  if (Node *m = b.add(Kind::Material, 3, "Stem")) {
    b.set_s(*m, "name", "Stem");
    b.set_color(*m, "color", RGB{b.d.leaf_color[0] * 0.9f, b.d.leaf_color[1] * 0.9f, b.d.leaf_color[2] * 0.8f});
    b.material(*m, *stem);
  }
  Node *leaf = make_leaf(b, 3, *stem, 1, 4.f + b.d.leaf_density * 8.f);
  if (leaf) b.set(*leaf, "angle", attach_angle(60.f), 10.f, 0);
  const PlantDescription &d = b.d;
  Node *head = make_flower(b, 3, *stem, 2, 1.f);
  if (head && !d.flowers) b.set(*head, "presence", 1.f); // a flowering plant flowers
  if (d.fruits) make_fruit(b, 3, *stem, 3, 1.f);
  return root;
}

uint64_t build_cactus_columnar(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *body = make_segment(b, 1, "Column", H, H * (b.d.trunk_ratio > 0.06f ? b.d.trunk_ratio : 0.08f), true);
  if (!root || !body) { err = NO_NODES; return 0; }
  b.trunk(*body, *root);
  b.set_choice(*body, "section", "Star");
  b.set(*body, "section_squash", 0.85f);
  b.set_i(*body, "radial_symmetry", 4);
  b.set_curve(*body, "radius_profile", Curve::through({0.f, 0.85f, 0.4f, 1.f, 0.9f, 0.8f, 1.f, 0.3f}));
  b.set_choice(*body, "rdisp_source", "Noise");
  b.set(*body, "rdisp_amount", 0.02f);
  b.set(*body, "tropism", 0.5f);
  b.set_choice(*body, "positioning", "Bottom");
  if (Node *skin = make_skin(b, 2)) b.material(*skin, *body);
  Node *arm = make_segment(b, 2, "Arm", H * 0.45f, 0.7f, false);
  if (!arm) { err = NO_NODES; return 0; }
  b.set(*arm, "count", 1.f + b.d.density * 4.f, 0.5f, 1);
  b.set(*arm, "start", 0.3f);
  b.set(*arm, "end", 0.7f);
  b.set(*arm, "angle", attach_angle(80.f), 8.f, 0);
  b.set_choice(*arm, "axis_mode", "Curve up");
  b.set(*arm, "axis_bend", 80.f, 10.f, 0);
  b.set(*arm, "tropism", 1.2f);
  b.set_choice(*arm, "section", "Star");
  b.set_i(*arm, "radial_symmetry", 4);
  b.set_choice(*arm, "arrangement", "Spiral");
  b.set(*arm, "coil", 137.5f);
  b.child(*arm, *body, 1);
  if (b.d.flowers) {
    Node *f = make_flower(b, 3, *arm, 1, 1.f);
    if (f) b.set(*f, "angle", 90.f);
  }
  return root->id;
}

uint64_t build_cactus_paddle(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *rep = b.add(Kind::Repeat, 1, "Pads");
  Node *pad = make_segment(b, 2, "Pad", H * 0.3f, H * 0.12f, true);
  if (!root || !rep || !pad) { err = NO_NODES; return 0; }
  b.trunk(*rep, *root);
  b.set_choice(*rep, "positioning", "Bottom");
  b.set(*rep, "iterations", 2.f + b.d.levels, 1.f, 0);
  b.set_choice(*pad, "section", "Ellipse");
  b.set(*pad, "section_squash", 0.2f);
  b.set(*pad, "section_twist", 0.25f, 0.15f, 0); // each pad turns from the last
  b.set_curve(*pad, "radius_profile", Curve::through({0.f, 0.4f, 0.5f, 1.f, 1.f, 0.35f}));
  b.set(*pad, "count", 1.f + b.d.density * 2.f, 0.5f, 1);
  b.set_choice(*pad, "positioning", "Tip");
  b.set(*pad, "angle", 60.f, 20.f, 0);
  b.set(*pad, "tropism", 0.6f);
  b.set(*pad, "inherit_scale", 0.9f);
  b.set_choice(*pad, "rdisp_source", "Knots");
  b.set(*pad, "rdisp_amount", 0.03f);
  b.child(*pad, *rep, 1);
  if (Node *skin = make_skin(b, 3)) {
    b.set_choice(*skin, "bark_kind", "Smooth");
    b.material(*skin, *pad);
  }
  if (b.d.flowers || b.d.fruits) {
    Node *tip = make_segment(b, 2, "Crown pad", H * 0.25f, H * 0.1f, true);
    if (tip) {
      b.set_choice(*tip, "section", "Ellipse");
      b.set(*tip, "section_squash", 0.2f);
      b.set_choice(*tip, "positioning", "Tip");
      b.tail(*tip, *rep);
      foliage(b, 3, *tip, 1, 0.f);
    }
  }
  return root->id;
}

uint64_t build_succulent_rosette(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *ros = make_clump(b, root, 14.f + b.d.density * 20.f, H * 0.05f, 45.f, "Rosette");
  if (!ros) { err = NO_NODES; return 0; }
  b.set(*ros, "angle1", 50.f, 25.f, 0); // inner leaves stand, outer ones lie
  Node *leaf = b.add(Kind::Warpboard, 2, "Fleshy leaf");
  if (!leaf) { err = NO_NODES; return 0; }
  b.set(*leaf, "length", H, b.spread(), 1);
  b.set(*leaf, "width", H * 0.35f, b.spread(), 1);
  b.set(*leaf, "curl", 0.6f, 0.2f, 0);
  b.set(*leaf, "flexibility", -0.3f - b.d.droop * 0.4f, 0.15f, 0);
  b.set(*leaf, "randomness", 0.3f);
  b.set_i(*leaf, "min_subdiv", 4);
  season_leaf(b, *leaf);
  b.child(*leaf, *ros, 1);
  if (Node *skin = make_skin(b, 3)) {
    b.set_choice(*skin, "bark_kind", "Smooth");
    b.set_b(*skin, "two_sided", true);
    b.material(*skin, *leaf);
  }
  if (b.d.flowers) {
    Node *stalk = make_segment(b, 2, "Flower stalk", H * 2.5f, H * 0.03f, true);
    if (stalk) {
      b.set(*stalk, "tropism", 0.8f);
      b.set_b(*stalk, "cap_enable", false);
      b.child(*stalk, *ros, 2);
      make_flower(b, 3, *stalk, 1, 1.f);
    }
  }
  return root;
}

uint64_t build_vine(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *stem = make_segment(b, 1, "Vine", H * 1.6f, H * b.d.trunk_ratio * 0.5f + 0.005f, true);
  if (!root || !stem) { err = NO_NODES; return 0; }
  b.trunk(*stem, *root);
  b.set(*stem, "tropism", -1.2f + (1.f - b.d.droop) * 0.6f, 0.2f, 0);
  b.set(*stem, "perturb_strength", 1.f);
  b.set(*stem, "perturb_frequency", 3.f);
  b.set_choice(*stem, "axis_mode", "Spiral");
  b.set(*stem, "axis_bend", 60.f, 30.f, 0);
  b.set_b(*stem, "cap_enable", false);
  b.set_curve(*stem, "radius_profile", Curve::line(1.f, 0.3f));
  b.set_choice(*stem, "positioning", "Bottom");
  if (Node *bark = make_bark(b, 2)) {
    b.set_choice(*bark, "bark_kind", "Fibrous");
    b.material(*bark, *stem);
  }
  Node *runner = make_segment(b, 2, "Runner", H * 0.5f, 0.5f, false);
  if (!runner) { err = NO_NODES; return 0; }
  b.set(*runner, "count", 4.f + b.d.density * 8.f, b.spread(), 1);
  b.set(*runner, "start", 0.1f);
  b.set(*runner, "angle", attach_angle(60.f), 15.f, 0);
  b.set(*runner, "tropism", -1.f, 0.2f, 0);
  b.set(*runner, "perturb_strength", 0.8f);
  b.set_b(*runner, "cap_enable", false);
  arrange(b, *runner);
  b.child(*runner, *stem, 1);
  foliage(b, 3, *runner, 1, 10.f + b.d.leaf_density * 20.f);
  return root->id;
}

uint64_t build_bamboo(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  const float inter = H / 14.f;
  Node *root = make_root(b);
  Node *rep = b.add(Kind::Repeat, 1, "Culm");
  Node *node = make_segment(b, 2, "Internode", inter, H * b.d.trunk_ratio * 0.3f + 0.01f, true);
  if (!root || !rep || !node) { err = NO_NODES; return 0; }
  b.trunk(*rep, *root);
  b.set_choice(*rep, "positioning", "Bottom");
  b.set(*rep, "iterations", 12.f, 3.f, 0);
  b.set_choice(*node, "positioning", "Tip");
  b.set(*node, "angle", 90.f);
  b.set(*node, "tropism", 0.5f - b.d.droop * 0.8f);
  b.set_curve(*node, "radius_profile", Curve::through({0.f, 1.15f, 0.06f, 1.f, 0.95f, 0.98f, 1.f, 1.1f}));
  b.set(*node, "inherit_scale", 0.96f);
  b.set_b(*node, "cap_enable", true);
  b.set_choice(*node, "cap_mode", "Only top");
  b.set(*node, "wind_flexibility", 1.8f);
  b.child(*node, *rep, 1);
  if (Node *m = b.add(Kind::Material, 3, "Culm")) {
    b.set_s(*m, "name", "Culm");
    b.set_choice(*m, "source", "Bark");
    b.set_choice(*m, "bark_kind", "Smooth");
    b.set_color(*m, "color", RGB{b.d.bark_color[0], b.d.bark_color[1], b.d.bark_color[2]});
    b.set(*m, "roughness", 0.4f);
    b.material(*m, *node);
  }
  Node *tw = make_segment(b, 3, "Leaf twig", inter * 1.2f, 0.2f, false);
  if (!tw) { err = NO_NODES; return 0; }
  b.set(*tw, "count", 1.f + b.d.density, 0.5f, 1);
  b.set(*tw, "start", 0.92f);
  b.set(*tw, "end", 0.98f);
  b.set(*tw, "angle", attach_angle(40.f), 10.f, 0);
  b.set(*tw, "tropism", -0.5f, 0.2f, 0);
  b.set_b(*tw, "cap_enable", false);
  b.set_hier(*tw, "presence", Curve::through({0.f, 0.f, 0.4f, 0.2f, 1.f, 1.f})); // leaves near the top
  b.child(*tw, *node, 1);
  Node *leaf = make_leaf(b, 4, *tw, 1, 5.f + b.d.leaf_density * 6.f);
  if (leaf) {
    b.set(*leaf, "angle", attach_angle(45.f), 10.f, 0);
    b.set_choice(*leaf, "arrangement", "Alternate");
  }
  return root->id;
}

uint64_t build_reed(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *tuft = make_clump(b, root, 8.f + b.d.density * 20.f, H * 0.03f, 6.f, "Stand");
  if (!tuft) { err = NO_NODES; return 0; }
  Node *stem = make_segment(b, 2, "Reed", H, H * 0.004f + 0.002f, true);
  if (!stem) { err = NO_NODES; return 0; }
  b.set(*stem, "tropism", 0.6f - b.d.droop * 0.5f, 0.1f, 0);
  b.set_choice(*stem, "axis_mode", "Curve down");
  b.set(*stem, "axis_bend", 5.f + b.d.droop * 25.f, 8.f, 0);
  b.set_b(*stem, "cap_enable", false);
  b.set_curve(*stem, "radius_profile", Curve::line(1.f, 0.4f));
  b.set_i(*stem, "blade_number", 2);
  b.set_choice(*stem, "blade_style", "Symmetrical");
  b.set(*stem, "blade_width", 0.01f + b.d.leaf_size_cm * 0.0005f, b.spread(), 1);
  b.set(*stem, "blade_start", 0.05f);
  b.set(*stem, "blade_end", 0.85f);
  b.set(*stem, "blade_spread", 120.f);
  b.set_curve(*stem, "blade_profile", Curve::through({0.f, 0.8f, 0.5f, 1.f, 1.f, 0.f}));
  b.set(*stem, "wind_flexibility", 2.f);
  season_leaf(b, *stem);
  b.child(*stem, *tuft, 1);
  if (Node *m = make_leaf_material(b, 3, "linear")) b.material(*m, *stem, "blade material");
  Node *head = make_flower(b, 3, *stem, 1, 1.f);
  if (head) {
    b.set_choice(*head, "profile_mode", "Cylinder"); // a seed head or a cattail
    b.set(*head, "length", H * 0.12f, b.spread(), 1);
    b.set(*head, "radius", H * 0.008f + 0.004f, b.spread(), 1);
    if (!b.d.flowers) season_window(b, *head, b.d.flower_season);
  }
  return root;
}

uint64_t build_mushroom(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *stem = make_segment(b, 1, "Stem", H * 0.8f, H * 0.12f, true);
  if (!root || !stem) { err = NO_NODES; return 0; }
  b.trunk(*stem, *root);
  b.set_curve(*stem, "radius_profile", Curve::through({0.f, 1.2f, 0.3f, 0.9f, 1.f, 0.8f}));
  b.set(*stem, "tropism", 0.3f);
  b.set(*stem, "perturb_strength", 0.2f);
  b.set_b(*stem, "cap_enable", false);
  b.set_choice(*stem, "positioning", "Bottom");
  if (Node *m = b.add(Kind::Material, 2, "Stem")) {
    b.set_s(*m, "name", "Stem");
    b.set_color(*m, "color", RGB{0.9f, 0.86f, 0.75f});
    b.set(*m, "roughness", 0.8f);
    b.material(*m, *stem);
  }
  Node *capn = b.add(Kind::Flower, 2, "Cap");
  if (!capn) { err = NO_NODES; return 0; }
  b.set(*capn, "radius", H * 0.5f, b.spread(), 1);
  b.set(*capn, "length", H * 0.25f, b.spread(), 1);
  b.set_choice(*capn, "profile_mode", "Cup");
  b.set(*capn, "plug_radius", H * 0.1f);
  b.set_b(*capn, "invert_faces", true); // the cup opens downward
  b.set_choice(*capn, "positioning", "Tip");
  b.set(*capn, "angle", 90.f);
  b.set(*capn, "rot_x", 180.f);
  b.child(*capn, *stem, 1);
  if (Node *m = b.add(Kind::Material, 3, "Cap")) {
    b.set_s(*m, "name", "Cap");
    b.set_color(*m, "color", rgb(b.d.flower_color));
    b.set_b(*m, "two_sided", true);
    b.set(*m, "roughness", 0.6f);
    b.material(*m, *capn);
  }
  return root->id;
}

uint64_t build_ground_cover(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  uint64_t root = 0;
  Node *mat = make_clump(b, root, 20.f + b.d.density * 40.f, H * 2.f, 70.f, "Mat");
  if (!mat) { err = NO_NODES; return 0; }
  Node *stalk = make_segment(b, 2, "Stalk", H, 0.003f, true);
  if (!stalk) { err = NO_NODES; return 0; }
  b.set(*stalk, "tropism", 0.6f);
  b.set_b(*stalk, "cap_enable", false);
  b.child(*stalk, *mat, 1);
  Node *leaf = make_leaf(b, 3, *stalk, 1, 2.f + b.d.leaf_density * 3.f);
  if (leaf) {
    b.set_choice(*leaf, "positioning", "Tip");
    b.set(*leaf, "angle", 20.f, 20.f, 0);
  }
  if (b.d.flowers) make_flower(b, 3, *stalk, 2, 1.f);
  return root;
}

} // namespace arch
} // namespace plant
} // namespace gpx
