// Geekatplay TerraForge - the tree archetypes (plant_archetypes.hpp).
//
// A broadleaf tree is a trunk that flares at the foot, branches that
// leave it at the description's angle and shorten toward the crown's
// shape, twigs on the branches and leaves on the twigs; past two levels
// of branching the branch grows into itself through a Repeat with the
// twig as its tail, so a five-level oak is five nodes, not fifteen. A
// conifer keeps its leader and sends branches out in whorls that shorten
// toward the top; its needles are blades on the twigs. A palm is a ringed
// column with a crown of fronds spread by a Hydra at its tip, each frond a
// segment with two symmetrical blades. A weeping tree is a broadleaf whose
// branches and twigs sink; a dead tree is one with no leaves, low health
// and cut branches; a bonsai a small, twisted trunk with dense pads.
#include "plant/plant_archetypes.hpp"

namespace gpx {
namespace plant {
namespace arch {

namespace {

// How branch length changes with where the branch leaves the trunk (x = 0
// at the foot, 1 at the top), by the description's crown word.
Curve crown_curve(const std::string &crown) {
  if (crown == "conical") return Curve::line(1.f, 0.12f);
  if (crown == "columnar") return Curve::constant(0.45f);
  if (crown == "spreading") return Curve::through({0.f, 1.f, 0.6f, 0.8f, 1.f, 0.4f});
  if (crown == "vase") return Curve::through({0.f, 0.3f, 0.7f, 0.9f, 1.f, 1.f});
  if (crown == "umbrella") return Curve::through({0.f, 0.2f, 0.7f, 1.f, 1.f, 0.8f});
  if (crown == "irregular") return Curve::through({0.f, 0.4f, 0.3f, 1.f, 0.6f, 0.5f, 1.f, 0.7f});
  if (crown == "weeping") return Curve::through({0.f, 0.6f, 0.5f, 1.f, 1.f, 0.5f});
  if (crown == "shrubby") return Curve::constant(0.8f);
  return Curve::through({0.f, 0.5f, 0.5f, 1.f, 1.f, 0.3f}); // round
}

Node *make_trunk(Builder &b, float length_m, float radius_m) {
  Node *t = make_segment(b, 1, "Trunk", length_m, radius_m, true);
  if (!t) return nullptr;
  b.set_curve(*t, "radius_profile", Curve::through({0.f, 1.f, 0.3f, 0.85f, 0.8f, 0.5f, 1.f, 0.08f}));
  b.set_i(*t, "flare_number", 4);
  b.set(*t, "flare_height", length_m * 0.12f);
  b.set(*t, "flare_swell", 0.5f);
  b.set(*t, "flare_depth", radius_m * 1.5f);
  b.set_choice(*t, "rdisp_source", "Bark ridges");
  b.set(*t, "rdisp_amount", 0.05f);
  b.set(*t, "perturb_strength", (1.f - b.d.straightness) * 0.7f);
  b.set(*t, "perturb_frequency", 1.5f);
  b.set(*t, "tropism", 0.3f); // a trunk rights itself
  b.set_choice(*t, "positioning", "Bottom");
  if (Node *bark = make_bark(b, 2)) b.material(*bark, *t);
  return t;
}

// The description's branch: angle from the trunk, count from density,
// tropism from droop, arrangement, and the crown's hierarchy curve.
Node *make_branch(Builder &b, int level, const std::string &label, float length_m, float inherit,
                  float count, float droop) {
  Node *br = make_segment(b, level, label, length_m, inherit, false);
  if (!br) return nullptr;
  b.set(*br, "count", count, b.spread(), 1);
  b.set(*br, "start", 0.35f);
  b.set(*br, "angle", attach_angle(b.d.branch_angle_deg), b.angle_spread(), 0);
  b.set(*br, "tropism", 0.25f - droop * 0.9f, 0.1f, 0);
  b.set(*br, "perturb_strength", (1.f - b.d.straightness) * 0.5f);
  b.set(*br, "inherit_scale", 0.62f);
  b.set(*br, "shrink_radius", 0.08f);
  b.set_hier(*br, "length", crown_curve(b.d.crown));
  arrange(b, *br);
  return br;
}

// Trunk, branches and twigs for a broadleaf shape with the given droop;
// returns the twig (the part the foliage goes on), its level and the root.
Node *broadleaf_frame(Builder &b, float droop, int &twig_level, uint64_t &root_id) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *trunk = make_trunk(b, H * 0.55f, H * b.d.trunk_ratio);
  if (!root || !trunk) return nullptr;
  root_id = root->id;
  b.trunk(*trunk, *root);
  int slot = 1;
  if (b.d.forks > 0) {
    Node *fork = make_segment(b, 2, "Fork", H * 0.45f, 0.75f, false);
    if (fork) {
      b.set(*fork, "count", (float)b.d.forks);
      b.set_choice(*fork, "count_mode", "Fixed value");
      b.set(*fork, "start", 0.08f);
      b.set(*fork, "end", 0.25f);
      b.set(*fork, "angle", attach_angle(25.f), 6.f, 0);
      b.set(*fork, "tropism", 0.5f);
      b.set(*fork, "inherit_scale", 0.85f);
      b.child(*fork, *trunk, slot++);
    }
  }
  const float count = 6.f + b.d.density * 16.f;
  const float blen = H * 0.38f;
  Node *twig = nullptr;
  if (b.d.levels >= 3) {
    Node *rep = b.add(Kind::Repeat, 2, "Branching");
    Node *br = make_branch(b, 3, "Branch", blen, pipe_ratio(count), count, droop);
    twig = make_branch(b, 3, "Twig", blen * 0.35f, pipe_ratio(count * 0.6f), count * 0.6f, droop);
    if (!rep || !br || !twig) return nullptr;
    b.set(*rep, "iterations", (float)(b.d.levels - 1));
    b.set(*rep, "start", 0.35f);
    b.child(*rep, *trunk, slot++);
    b.child(*br, *rep, 1);
    b.tail(*twig, *rep);
    twig_level = 4;
  } else if (b.d.levels == 2) {
    Node *br = make_branch(b, 2, "Branch", blen, pipe_ratio(count), count, droop);
    twig = make_branch(b, 3, "Twig", blen * 0.35f, 0.45f, count * 0.6f, droop);
    if (!br || !twig) return nullptr;
    b.child(*br, *trunk, slot++);
    b.child(*twig, *br, 1);
    twig_level = 4;
  } else {
    twig = make_branch(b, 2, "Branch", blen, pipe_ratio(count), count, droop);
    if (!twig) return nullptr;
    b.child(*twig, *trunk, slot++);
    twig_level = 3;
  }
  b.set(*twig, "start", 0.15f);
  b.set_b(*twig, "cap_enable", false);
  return twig;
}

} // namespace

// ---------------------------------------------------------------- builders

uint64_t build_broadleaf_tree(Builder &b, std::string &err) {
  int lvl = 0;
  uint64_t root = 0;
  Node *twig = broadleaf_frame(b, b.d.droop, lvl, root);
  if (!twig) { err = "could not add the plant nodes"; return 0; }
  foliage(b, lvl, *twig, 1, 10.f + b.d.leaf_density * 30.f);
  return root;
}

uint64_t build_weeping_tree(Builder &b, std::string &err) {
  int lvl = 0;
  uint64_t root = 0;
  Node *twig = broadleaf_frame(b, 1.f, lvl, root);
  if (!twig) { err = "could not add the plant nodes"; return 0; }
  b.set(*twig, "length", b.d.height_m * 0.4f, b.spread(), 1); // long hanging twigs
  b.set(*twig, "tropism", -1.4f, 0.2f, 0);
  b.set(*twig, "count", 12.f + b.d.density * 20.f, b.spread(), 1);
  b.set(*twig, "angle", attach_angle(70.f), 10.f, 0);
  b.set(*twig, "wind_flexibility", 2.5f);
  foliage(b, lvl, *twig, 1, 20.f + b.d.leaf_density * 40.f);
  return root;
}

uint64_t build_dead_tree(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *trunk = make_trunk(b, H * 0.6f, H * b.d.trunk_ratio);
  if (!root || !trunk) { err = "could not add the plant nodes"; return 0; }
  b.set(*root, "health", 0.1f);
  b.set(*trunk, "perturb_strength", 0.5f);
  b.set(*trunk, "cut_probability", 0.3f);
  b.set(*trunk, "cut_length", H * 0.45f);
  b.trunk(*trunk, *root);
  const float count = 4.f + b.d.density * 10.f;
  Node *br = make_branch(b, 2, "Branch", H * 0.4f, pipe_ratio(count), count, b.d.droop * 0.5f);
  Node *tw = make_branch(b, 3, "Twig", H * 0.15f, pipe_ratio(count * 0.5f), count * 0.5f, b.d.droop * 0.5f);
  if (!br || !tw) { err = "could not add the plant nodes"; return 0; }
  for (Node *n : {br, tw}) {
    b.set(*n, "cut_probability", 0.4f);
    b.set(*n, "cut_length", H * 0.12f);
    b.set(*n, "cut_radius_reduction", 0.7f);
    b.set(*n, "perturb_strength", 0.8f);
    b.set(*n, "pruning", 0.3f);
  }
  b.child(*br, *trunk, 1);
  b.child(*tw, *br, 1);
  return root->id;
}

uint64_t build_bonsai(Builder &b, std::string &err) {
  const float H = b.d.height_m < 2.f ? b.d.height_m : 0.5f;
  Node *root = make_root(b);
  Node *trunk = make_trunk(b, H * 0.6f, H * b.d.trunk_ratio * 3.f);
  if (!root || !trunk) { err = "could not add the plant nodes"; return 0; }
  b.trunk(*trunk, *root);
  b.set_choice(*trunk, "axis_mode", "S curve");
  b.set(*trunk, "axis_bend", 45.f, 15.f, 0);
  b.set(*trunk, "perturb_strength", 1.2f);
  b.set(*trunk, "perturb_frequency", 3.f);
  b.set(*trunk, "flare_height", H * 0.2f);
  b.set(*trunk, "flare_swell", 0.8f);
  Node *br = make_branch(b, 2, "Branch", H * 0.5f, pipe_ratio(5.f + b.d.density * 4.f), 5.f + b.d.density * 4.f, 0.3f);
  Node *pad = make_branch(b, 3, "Pad twig", H * 0.15f, pipe_ratio(6.f), 6.f, 0.f);
  if (!br || !pad) { err = "could not add the plant nodes"; return 0; }
  b.set(*br, "angle", attach_angle(70.f), 12.f, 0);
  b.set(*br, "perturb_strength", 0.9f);
  b.set(*br, "start", 0.3f);
  b.set(*pad, "start", 0.5f);
  b.set(*pad, "tropism", 0.6f); // pads flatten upward
  b.set_b(*pad, "cap_enable", false);
  b.child(*br, *trunk, 1);
  b.child(*pad, *br, 1);
  Node *leaf = make_leaf(b, 4, *pad, 1, 25.f + b.d.leaf_density * 20.f);
  if (leaf) {
    b.set(*leaf, "length", 0.015f, b.spread(), 1);
    b.set(*leaf, "width", 0.008f, b.spread(), 1);
  }
  return root->id;
}

uint64_t build_conifer(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *trunk = make_trunk(b, H * 0.92f, H * b.d.trunk_ratio);
  if (!root || !trunk) { err = "could not add the plant nodes"; return 0; }
  b.trunk(*trunk, *root);
  b.set_curve(*trunk, "radius_profile", Curve::through({0.f, 1.f, 0.5f, 0.6f, 1.f, 0.02f}));
  b.set(*trunk, "flare_height", H * 0.06f);
  b.set(*trunk, "perturb_strength", (1.f - b.d.straightness) * 0.3f);
  // whorls of branches, shortening to a point
  const float whorl_n = 6.f + b.d.density * 12.f;
  Node *br = make_segment(b, 2, "Whorl branch", H * 0.28f, pipe_ratio(whorl_n), false);
  if (!br) { err = "could not add the plant nodes"; return 0; }
  b.set(*br, "count", whorl_n, b.spread(), 1);
  b.set(*br, "start", 0.2f);
  b.set(*br, "end", 0.97f);
  b.set(*br, "per_whorl", 5.f, 1.f, 0);
  b.set_choice(*br, "arrangement", "Spiral");
  b.set(*br, "coil", 40.f);
  b.set(*br, "angle", attach_angle(b.d.branch_angle_deg > 60.f ? b.d.branch_angle_deg : 75.f), 6.f, 0);
  b.set(*br, "tropism", 0.35f - b.d.droop * 0.8f, 0.1f, 0);
  b.set_hier(*br, "length", Curve::through({0.f, 1.f, 0.6f, 0.55f, 1.f, 0.05f}));
  b.set(*br, "inherit_scale", 0.6f);
  b.child(*br, *trunk, 1);
  const float twig_n = 16.f + b.d.density * 24.f;
  Node *tw = make_segment(b, 3, "Twig", H * 0.08f, pipe_ratio(twig_n), false);
  if (!tw) { err = "could not add the plant nodes"; return 0; }
  b.set(*tw, "count", twig_n, b.spread(), 1);
  b.set(*tw, "start", 0.2f);
  b.set(*tw, "angle", attach_angle(55.f), 8.f, 0);
  b.set(*tw, "tropism", 0.2f, 0.1f, 0);
  b.set_choice(*tw, "arrangement", "Opposite");
  b.set(*tw, "coil", 90.f);
  b.set_b(*tw, "cap_enable", false);
  b.set_hier(*tw, "length", Curve::line(1.f, 0.4f));
  // needles: two symmetrical blades along the twig, wearing the leaf picture
  // A needle spray, not a ribbon: three cards spread most of the way round
  // so the branchlet is clothed from any angle, each half as wide as the
  // needles are long (the width is a half-width, and Symmetrical draws both
  // sides, so a 5 cm needle gives a 16 cm spray). Two narrow cards left the
  // wood showing through and the tree read as dead.
  b.set_i(*tw, "blade_number", 3);
  b.set_choice(*tw, "blade_style", "Symmetrical");
  b.set(*tw, "blade_width", b.d.leaf_size_cm * 0.016f, b.spread(), 1);
  b.set(*tw, "blade_start", 0.05f);
  b.set(*tw, "blade_spread", 240.f);
  b.set(*tw, "blade_section_height", 0.1f);
  b.set_curve(*tw, "blade_profile", Curve::through({0.f, 0.6f, 0.5f, 1.f, 1.f, 0.4f}));
  season_leaf(b, *tw);
  b.child(*tw, *br, 1);
  // the card stands for a whole needle shoot, so it wears a spray of them
  if (Node *m = make_leaf_material(b, 4, b.d.leaf_shape == "scale" ? "scale" : "needle spray"))
    b.material(*m, *tw, "blade material");
  // Cones on the branches, not on every needle shoot. Two per twig meant a
  // cone for every handful of needles - a pine carries them in ones and twos
  // near the ends of its limbs, and there are hundreds of twigs.
  if (b.d.fruits) make_fruit(b, 4, *br, 2, 1.f + b.d.density); // cones as balls
  return root->id;
}

uint64_t build_palm(Builder &b, std::string &err) {
  const float H = b.d.height_m;
  Node *root = make_root(b);
  Node *trunk = make_segment(b, 1, "Trunk", H * 0.8f, H * b.d.trunk_ratio, true);
  if (!root || !trunk) { err = "could not add the plant nodes"; return 0; }
  b.trunk(*trunk, *root);
  b.set_curve(*trunk, "radius_profile", Curve::through({0.f, 1.1f, 0.15f, 0.9f, 1.f, 0.8f}));
  b.set_choice(*trunk, "axis_mode", "Curve up");
  b.set(*trunk, "axis_bend", (1.f - b.d.straightness) * 25.f, 10.f, 0);
  b.set_choice(*trunk, "rdisp_source", "Bark ridges");
  b.set(*trunk, "rdisp_amount", 0.08f);
  b.set(*trunk, "rdisp_scale", 0.3f);
  b.set(*trunk, "tropism", 0.2f);
  b.set_choice(*trunk, "positioning", "Bottom");
  if (Node *bark = make_bark(b, 2)) {
    b.set_choice(*bark, "bark_kind", "Ringed");
    b.material(*bark, *trunk);
  }
  Node *crown = b.add(Kind::Hydra, 2, "Crown");
  if (!crown) { err = "could not add the plant nodes"; return 0; }
  b.set(*crown, "child_count", 10.f + b.d.density * 14.f, b.spread(), 1);
  b.set(*crown, "radius", H * b.d.trunk_ratio * 0.6f);
  b.set(*crown, "angle1", 55.f, 12.f, 0);
  b.set_choice(*crown, "positioning", "Tip");
  b.child(*crown, *trunk, 1);
  Node *frond = make_segment(b, 3, "Frond", H * 0.42f, H * 0.006f, true);
  if (!frond) { err = "could not add the plant nodes"; return 0; }
  b.set_choice(*frond, "axis_mode", "Curve down");
  b.set(*frond, "axis_bend", 35.f + b.d.droop * 40.f, 10.f, 0);
  b.set(*frond, "tropism", -0.6f - b.d.droop * 0.6f, 0.15f, 0);
  b.set_b(*frond, "cap_enable", false);
  b.set_i(*frond, "blade_number", 2);
  b.set_choice(*frond, "blade_style", "Symmetrical");
  b.set(*frond, "blade_width", H * 0.03f, b.spread(), 1);
  b.set(*frond, "blade_start", 0.15f);
  b.set(*frond, "blade_spread", 160.f);
  b.set(*frond, "blade_section_height", 0.35f);
  b.set_curve(*frond, "blade_profile", Curve::through({0.f, 0.2f, 0.35f, 1.f, 0.8f, 0.7f, 1.f, 0.f}));
  b.set(*frond, "wind_flexibility", 2.f);
  b.set(*frond, "wind_blade_influence", 1.5f);
  season_leaf(b, *frond);
  b.child(*frond, *crown, 1);
  if (Node *m = make_leaf_material(b, 4, "frond")) b.material(*m, *frond, "blade material");
  if (b.d.fruits) {
    Node *fr = make_fruit(b, 3, *trunk, 2, 8.f + b.d.density * 12.f);
    if (fr) {
      b.set(*fr, "start", 0.9f);
      b.set(*fr, "end", 0.98f);
    }
  }
  return root->id;
}

} // namespace arch
} // namespace plant
} // namespace gpx
