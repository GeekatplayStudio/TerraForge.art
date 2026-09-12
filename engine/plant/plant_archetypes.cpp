// Geekatplay TerraForge - a description becomes a species graph.
//
// plant_species_from_description picks the archetype's builder and hands
// it a Builder (plant_archetypes.hpp): nodes are added by kind in columns,
// attributes are set by schema key and refused when the key is unknown,
// links go from a part to the part it grows on. The material makers and
// the seasonal look live here because every archetype shares them: a bark
// made from rules with the description's kind and colour, a leaf picture
// with its shape and colour, a tint gradient over the year that lightens
// in spring and turns in autumn, and a presence curve that drops the
// leaves in winter unless the plant is evergreen.
//
// Nothing here is copied from a catalogue: the archetypes are written from
// botany - how a broadleaf tree forks, how a conifer whorls, how a palm
// crowns - and the description says how much of each.
#include "plant/plant_archetypes.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace gpx {

namespace plant {
namespace arch {

Node *Builder::add(Kind k, int level, const std::string &label) {
  if ((int)rows.size() <= level) rows.resize((size_t)level + 1, 0);
  const int row = rows[(size_t)level]++;
  Node *n = g.add_node(kind_type(k), ox - COL * (float)level, oy + ROW * (float)row);
  if (n && !label.empty() && n->attrs.find("name")) n->attrs.find("name")->s = label;
  return n;
}

static Attribute *want(Builder &b, Node &n, const char *key, AttrType t, AttrType alt = AttrType::Random) {
  Attribute *a = n.attrs.find(key);
  if (!a || (a->type != t && a->type != alt)) {
    b.missing += n.type + "." + key + "\n";
    return nullptr;
  }
  return a;
}

bool Builder::set(Node &n, const char *key, float value, float spread_v, int mode) {
  Attribute *a = want(*this, n, key, AttrType::Random, AttrType::Float);
  if (!a) return false;
  a->f = value < a->fmin ? a->fmin : value > a->fmax ? a->fmax : value;
  if (a->type == AttrType::Random) {
    a->spread = spread_v;
    a->spread_mode = mode;
  }
  return true;
}
bool Builder::set_i(Node &n, const char *key, int value) {
  Attribute *a = want(*this, n, key, AttrType::Int, AttrType::Int);
  if (!a) return false;
  a->i = value < a->imin ? a->imin : value > a->imax ? a->imax : value;
  return true;
}
bool Builder::set_b(Node &n, const char *key, bool value) {
  Attribute *a = want(*this, n, key, AttrType::Bool, AttrType::Bool);
  if (!a) return false;
  a->b = value;
  return true;
}
bool Builder::set_s(Node &n, const char *key, const std::string &value) {
  Attribute *a = want(*this, n, key, AttrType::Text, AttrType::Filename);
  if (!a) return false;
  a->s = value;
  return true;
}
bool Builder::set_choice(Node &n, const char *key, const char *label) {
  Attribute *a = want(*this, n, key, AttrType::Choice, AttrType::Choice);
  if (!a) return false;
  for (size_t i = 0; i < a->labels.size(); ++i)
    if (a->labels[i] == label) {
      a->i = (int)i;
      return true;
    }
  missing += n.type + "." + key + " label " + label + "\n";
  return false;
}
bool Builder::set_color(Node &n, const char *key, RGB c) {
  Attribute *a = want(*this, n, key, AttrType::Color, AttrType::Color);
  if (!a) return false;
  a->col[0] = c.r; a->col[1] = c.g; a->col[2] = c.b; a->col[3] = 1.f;
  return true;
}
bool Builder::set_curve(Node &n, const char *key, const Curve &c) {
  Attribute *a = want(*this, n, key, AttrType::Curve, AttrType::Curve);
  if (!a) return false;
  a->curves.curves.assign(1, c);
  a->curves.weights.clear();
  return true;
}
bool Builder::set_hier(Node &n, const char *key, const Curve &c) {
  Attribute *a = want(*this, n, key, AttrType::Random);
  if (!a) return false;
  a->curve_hier.curves.assign(1, c);
  a->curve_hier.weights.clear();
  return true;
}
bool Builder::set_along(Node &n, const char *key, const Curve &c) {
  Attribute *a = want(*this, n, key, AttrType::Random);
  if (!a) return false;
  a->curve_along.curves.assign(1, c);
  a->curve_along.weights.clear();
  return true;
}
bool Builder::set_gradient(Node &n, const char *key, const std::vector<GradientStop> &stops) {
  Attribute *a = want(*this, n, key, AttrType::Gradient, AttrType::Gradient);
  if (!a) return false;
  a->stops = stops;
  return true;
}
bool Builder::child(Node &c, Node &parent, int slot) {
  return g.add_link(c.id, "plant", parent.id, "child " + std::to_string(slot));
}
bool Builder::material(Node &m, Node &part, const char *port) {
  return g.add_link(m.id, "plant", part.id, port);
}
bool Builder::trunk(Node &part, Node &root) { return g.add_link(part.id, "plant", root.id, "trunk"); }
bool Builder::tail(Node &t, Node &repeat) { return g.add_link(t.id, "plant", repeat.id, "tail"); }

// ------------------------------------------------------------------- root

Node *make_root(Builder &b) {
  Node *r = b.add(Kind::Species, 0, b.d.name);
  if (!r) return nullptr;
  b.set_s(*r, "name", b.d.name);
  b.set(*r, "age", b.d.age_years);
  b.set(*r, "max_age", b.d.max_age_years);
  b.set(*r, "health", b.d.health);
  b.set(*r, "season", b.d.season);
  b.set(*r, "gravity", 1.f);
  b.set(*r, "wind_strength", 0.3f);
  // A description's height is the height the person asked for, and the
  // archetype builds to it. But the engine also scales a plant by how mature
  // it is, so a named plant - whose age comes from the table, not from the
  // height - would come out short: a 22 m oak aged 30 of its 600 years grew
  // 6 m. The root's own scale cancels that, so the plant is its stated height
  // at its stated age, and moving the age slider from there grows or shrinks
  // it as it should. (plant_eval.cpp multiplies the two.)
  const float by_age = plant_size_at(plant_maturity(b.d.age_years, b.d.max_age_years));
  b.set(*r, "scale", by_age > 1e-3f ? 1.f / by_age : 1.f);
  if (Attribute *s = r->attrs.find("seed")) s->seed = 1;
  return r;
}

// -------------------------------------------------------------- materials

static const char *cap(const std::string &s, const std::vector<const char *> &labels) {
  for (const char *l : labels) {
    std::string low = l;
    for (char &c : low) c = (char)std::tolower((unsigned char)c);
    if (low == s) return l;
  }
  return labels.empty() ? "" : labels[0];
}

Node *make_bark(Builder &b, int level) {
  Node *m = b.add(Kind::Material, level, "Bark");
  if (!m) return nullptr;
  b.set_s(*m, "name", "Bark");
  b.set_choice(*m, "source", "Bark");
  b.set_choice(*m, "bark_kind",
               cap(b.d.bark, {"Fissured", "Plated", "Smooth", "Birch", "Ringed", "Peeling", "Scaly", "Fibrous"}));
  b.set_color(*m, "color", rgb(b.d.bark_color));
  RGB crack{b.d.bark_color[0] * 0.35f, b.d.bark_color[1] * 0.33f, b.d.bark_color[2] * 0.35f};
  b.set_color(*m, "crack_color", crack);
  b.set(*m, "roughness", 0.9f);
  b.set(*m, "bark_scale", b.d.height_m > 15.f ? 1.5f : 1.f);
  return m;
}

// The height a person asked for is the height they get.
//
// An archetype builds from botany - a trunk that is this share of the tree,
// branches at that angle, a crown of about so many levels - and what those
// rules add up to is only roughly the height in the description. A gnarled
// oak wanders as it climbs and comes out two thirds the height of a straight
// one grown from the same numbers. Rather than tune every archetype against
// every modifier, the plant is grown here and the root's scale corrected by
// what it actually made.
//
// It takes a few passes because height does not follow scale in step: a part
// whose children are counted per metre carries more of them when it is
// longer, and those reach further again, so twice the scale is nearer five
// times the height. The step is damped by that power and the loop stops as
// soon as it is within a few percent - usually two or three passes. The
// measuring builds are meshed as coarsely as the engine allows, because only
// the skeleton's reach is being read, not its surface.
void fit_to_height(Graph &g, uint64_t root_id, float wanted_m) {
  Node *root = g.find_node(root_id);
  if (!root || wanted_m <= 0.f) return;
  Attribute *sc = root->attrs.find("scale");
  if (!sc || sc->type != AttrType::Float) return;
  PlantBuildOptions o;
  o.seed = root->attrs.get_seed("seed");
  if (o.seed == 0) o.seed = 1;
  o.detail = -3.f;       // the coarsest meshing: the skeleton is what is read
  o.wind_weights = false;
  o.tints = false;
  const float start = sc->f;
  for (int pass = 0; pass < 6; ++pass) {
    PlantMesh m;
    std::string err;
    if (!plant_build(g, *root, o, m, err) || m.height_m <= 1e-4f) break;
    const float ratio = wanted_m / m.height_m;
    if (ratio > 0.97f && ratio < 1.03f) break;
    // A correction, not a rebuild of the species: a factor far from one means
    // the archetype and the description disagree about what the plant is, and
    // stretching it eight times would make a caricature rather than a fix.
    if (sc->f / start > 4.f || sc->f / start < 0.25f) break;
    const float step = std::pow(ratio, 0.42f); // height ~ scale^2.4
    const float next = std::clamp(sc->f * step, sc->fmin, sc->fmax);
    if (std::fabs(next - sc->f) < 1e-4f * std::max(1.f, sc->f)) break;
    sc->f = next;
  }
  plant_mesh_forget(root_id);
  g.mark_dirty(root_id);
}

// Every piece of wood wears the species' bark, not just the trunk.
//
// An archetype hangs one bark material on the part it is thinking about -
// usually the trunk - and the parts it grows afterwards are left to the
// builders' fallback wood: a flat brown with no picture at all. On a tree
// that is most of what you see, so a bark carefully made from the
// description ended up on the one part the crown hides. This pass runs when
// the archetype is done: any wood part with nothing on its first material
// slot is linked to the bark the species already has.
void dress_bare_wood(Builder &b, uint64_t root_id) {
  const Node *root = b.g.find_node(root_id);
  if (!root) return;
  const std::vector<const Node *> parts = plant_subtree(b.g, *root);
  // the species' bark: the first material node called "Bark"
  Node *bark = nullptr;
  for (const Node *n : parts)
    if (n->type == "PlantMaterial" && n->attrs.get_s("name") == "Bark") {
      bark = b.g.find_node(n->id);
      break;
    }
  if (!bark) return;
  for (const Node *n : parts) {
    // wood only: a leaf, a petal or a fruit has a material of its own kind
    if (n->type != "PlantSegment" && n->type != "PlantWarpboard") continue;
    bool taken = false;
    for (const Link &l : b.g.links)
      if (l.to_node == n->id && l.to_port == "material") {
        taken = true;
        break;
      }
    if (!taken) b.g.add_link(bark->id, "plant", n->id, "material");
  }
}

Node *make_leaf_material(Builder &b, int level, const char *shape) {
  Node *m = b.add(Kind::Material, level, "Leaf");
  if (!m) return nullptr;
  const std::string sh = shape ? shape : b.d.leaf_shape;
  b.set_s(*m, "name", "Leaf");
  b.set_choice(*m, "source", "Leaf");
  b.set_choice(*m, "leaf_shape",
               cap(sh, {"Ovate", "Lanceolate", "Lobed", "Palmate", "Needle", "Scale", "Pinnate", "Heart",
                        "Linear", "Round", "Elliptic", "Frond", "Blade"}));
  b.set_color(*m, "color", rgb(b.d.leaf_color));
  RGB vein{b.d.leaf_color[0] * 1.3f + 0.1f, b.d.leaf_color[1] * 1.15f + 0.1f, b.d.leaf_color[2] * 1.2f + 0.1f};
  b.set_color(*m, "vein_color", vein);
  b.set_b(*m, "two_sided", true);
  b.set(*m, "translucency", 0.4f);
  b.set(*m, "roughness", 0.6f);
  b.set(*m, "leaf_serration", sh == "lobed" || sh == "elliptic" ? 0.3f : 0.f);
  b.set(*m, "leaf_aspect", sh == "needle" || sh == "linear" || sh == "blade" ? 0.08f
                          : sh == "lanceolate" ? 0.3f : sh == "round" || sh == "heart" ? 0.9f : 0.55f);
  return m;
}

Node *make_petal(Builder &b, int level) {
  Node *m = b.add(Kind::Material, level, "Petal");
  if (!m) return nullptr;
  b.set_s(*m, "name", "Petal");
  b.set_choice(*m, "source", "Petal");
  b.set_color(*m, "color", rgb(b.d.flower_color));
  b.set_b(*m, "two_sided", true);
  b.set(*m, "translucency", 0.5f);
  b.set(*m, "roughness", 0.5f);
  return m;
}

Node *make_fruit_material(Builder &b, int level) {
  Node *m = b.add(Kind::Material, level, "Fruit");
  if (!m) return nullptr;
  b.set_s(*m, "name", "Fruit");
  b.set_choice(*m, "source", "None (colour or pictures above)");
  b.set_color(*m, "color", rgb(b.d.fruit_color));
  b.set(*m, "roughness", 0.35f);
  return m;
}

// ---------------------------------------------------------------- seasons

static float ratio(float a, float bb) {
  const float r = a / (bb > 0.02f ? bb : 0.02f);
  return r < 0.f ? 0.f : r > 4.f ? 4.f : r;
}

void season_leaf(Builder &b, Node &part) {
  const float *lc = b.d.leaf_color, *ac = b.d.autumn_color;
  std::vector<GradientStop> stops;
  stops.push_back({0.f, 1.f, 1.f, 1.f, 1.f});
  stops.push_back({0.25f, 1.15f, 1.25f, 0.85f, 1.f}); // spring: lighter, yellower
  stops.push_back({0.5f, 1.f, 1.f, 1.f, 1.f});         // summer: the leaf colour
  if (!b.d.evergreen) {
    stops.push_back({0.7f, 1.f, 1.f, 1.f, 1.f});
    stops.push_back({0.8f, ratio(ac[0], lc[0]), ratio(ac[1], lc[1]), ratio(ac[2], lc[2]), 1.f});
    stops.push_back({0.9f, ratio(ac[0], lc[0]) * 0.8f, ratio(ac[1], lc[1]) * 0.7f, ratio(ac[2], lc[2]) * 0.7f, 1.f});
  }
  stops.push_back({1.f, 1.f, 1.f, 1.f, 1.f});
  b.set_gradient(part, "tint_season", stops);
  Curve presence = b.d.evergreen ? Curve::constant(1.f)
                                 : Curve::through({0.f, 0.f, 0.15f, 0.f, 0.3f, 1.f, 0.75f, 1.f, 0.9f, 0.1f, 0.97f, 0.f, 1.f, 0.f});
  b.set_curve(part, "presence_season", presence);
}

void season_window(Builder &b, Node &part, const float s[2]) {
  const float s0 = s[0] < s[1] ? s[0] : s[1], s1 = s[0] < s[1] ? s[1] : s[0];
  const float e = 0.03f;
  Curve c = Curve::through({0.f, 0.f, s0 - e < 0.01f ? 0.01f : s0 - e, 0.f, s0, 1.f, s1, 1.f,
                            s1 + e > 0.99f ? 0.99f : s1 + e, 0.f, 1.f, 0.f});
  b.set_curve(part, "presence_season", c);
}

// ------------------------------------------------------------- attachment

float attach_angle(float from_axis_deg) { return 90.f - from_axis_deg; }

void arrange(Builder &b, Node &child) {
  const std::string &a = b.d.arrangement;
  if (a == "alternate") {
    b.set_choice(child, "arrangement", "Alternate");
    b.set(child, "coil", 137.5f);
  } else if (a == "opposite") {
    b.set_choice(child, "arrangement", "Opposite");
    b.set(child, "coil", 90.f);
  } else if (a == "whorled") {
    b.set_choice(child, "arrangement", "Spiral");
    b.set(child, "per_whorl", 4.f);
    b.set(child, "coil", 45.f);
  } else {
    b.set_choice(child, "arrangement", "Spiral");
    b.set(child, "coil", 137.5f, b.angle_spread(), 0);
  }
}

// ------------------------------------------------------------------ parts

// What share of its parent's radius a branch carrying `siblings` of its kind
// should have: the pipe model. The wood in a limb is the wood of everything
// it feeds, so the cross-sections add up - n branches of ratio r satisfy
// n*r^2 = 1, and r = 1/sqrt(n). A fixed share instead (every level took about
// half its parent, whatever it carried) left a 28 m pine with sixteen-
// centimetre twigs: as thick as its needle sprays were wide, so the tree read
// as a mass of orange wood with a green fringe rather than as foliage.
float pipe_ratio(float siblings) {
  const float n = siblings < 1.f ? 1.f : siblings;
  const float r = 1.f / std::sqrt(n);
  return r < 0.12f ? 0.12f : r > 0.6f ? 0.6f : r;
}

Node *make_segment(Builder &b, int level, const std::string &label, float length_m, float radius_m,
                   bool user_radius) {
  Node *s = b.add(Kind::Segment, level, label);
  if (!s) return nullptr;
  b.set(*s, "length", length_m, b.spread(), 1);
  if (user_radius) {
    b.set_choice(*s, "radius_mode", "User defined");
    b.set(*s, "radius", radius_m, b.spread() * 0.5f, 1);
  } else {
    b.set_choice(*s, "radius_mode", "Inherit");
    b.set(*s, "inherit_ratio", radius_m);
  }
  b.set(*s, "randomness", b.d.variation * 0.5f);
  return s;
}

Node *make_leaf(Builder &b, int level, Node &parent, int slot, float count) {
  Node *l = b.add(Kind::Leaf, level, "Leaf");
  if (!l) return nullptr;
  const float len = b.d.leaf_size_cm * 0.01f;
  const std::string &sh = b.d.leaf_shape;
  const float aspect = sh == "needle" || sh == "linear" || sh == "blade" ? 0.1f : sh == "lanceolate" ? 0.3f
                       : sh == "round" || sh == "heart" ? 0.9f : 0.6f;
  // A leaf is the size the species says, wherever it grows. Every part scales
  // itself by its parent's scale by default, which is right for wood - a twig
  // is smaller than the branch it came off - and wrong for a leaf: an oak
  // leaf is eleven centimetres on a low bough and on the highest twig alike.
  // Inheriting it put a leaf three levels down at 0.62^3 of its size, so an
  // 11 cm leaf drew at under four. Maturity still scales it, through the
  // plant's own scale, so a young tree keeps small leaves.
  b.set_b(*l, "scale_inherit", false);
  b.set(*l, "length", len, b.spread(), 1);
  b.set(*l, "width", len * aspect, b.spread(), 1);
  b.set(*l, "count", count, b.spread(), 1);
  b.set_choice(*l, "count_mode", "Fixed value");
  b.set(*l, "start", 0.1f);
  b.set(*l, "angle", attach_angle(50.f), b.angle_spread(), 0);
  b.set(*l, "curvature_h", 15.f + b.d.droop * 20.f, 8.f, 0);
  b.set(*l, "midrib_angle", 20.f, 10.f, 0);
  b.set_choice(*l, "positioning", "Skin");
  arrange(b, *l);
  season_leaf(b, *l);
  b.child(*l, parent, slot);
  if (Node *m = make_leaf_material(b, level + 1)) b.material(*m, *l);
  return l;
}

Node *make_flower(Builder &b, int level, Node &parent, int slot, float count) {
  Node *f = b.add(Kind::Flower, level, "Flower");
  if (!f) return nullptr;
  // its size is the one the description names, not a share of the twig it
  // sits on (see make_leaf)
  b.set_b(*f, "scale_inherit", false);
  const float r = b.d.flower_size_cm * 0.005f;
  const std::string &sh = b.d.flower_shape;
  b.set(*f, "radius", r, b.spread(), 1);
  b.set(*f, "length", sh == "tube" || sh == "spike" ? r * 3.f : sh == "disc" ? r * 0.4f : r * 1.2f, b.spread(), 1);
  b.set_choice(*f, "profile_mode", sh == "cup" ? "Cup" : sh == "tube" ? "Trumpet" : sh == "bell" ? "Bell"
                                   : sh == "spike" ? "Cylinder" : "Lobed (inner / outer)");
  b.set_i(*f, "lobes", sh == "cluster" ? 8 : 5);
  b.set(*f, "count", count, b.spread(), 1);
  b.set_choice(*f, "positioning", "Tip");
  b.set(*f, "angle", 90.f);
  b.set(*f, "gravitropism", -0.3f * b.d.droop);
  season_window(b, *f, b.d.flower_season);
  b.child(*f, parent, slot);
  if (Node *m = make_petal(b, level + 1)) b.material(*m, *f);
  return f;
}

Node *make_fruit(Builder &b, int level, Node &parent, int slot, float count) {
  Node *f = b.add(Kind::Ball, level, "Fruit");
  if (!f) return nullptr;
  // its size is the one the description names, not a share of the twig it
  // sits on (see make_leaf)
  b.set_b(*f, "scale_inherit", false);
  b.set(*f, "radius", b.d.fruit_size_cm * 0.005f, b.spread(), 1);
  b.set(*f, "count", count, b.spread(), 1);
  b.set(*f, "start", 0.3f);
  b.set_choice(*f, "positioning", "Skin");
  b.set(*f, "angle", -60.f, 15.f, 0); // hanging
  b.set(*f, "presence", 0.7f);
  // A fruit is a small solid ball and there are a great many places to hang
  // one, so its meshing has to be kept in hand: at the schema's default a
  // pine's cones came to 467,000 triangles - nearly half the tree - and,
  // being the one part with no picture on it, painted the whole crown a flat
  // brown. Six rings is plenty for something a few centimetres across.
  b.set_i(*f, "min_subdiv", 5);
  b.set(*f, "mesh_boost", -1.f);
  season_window(b, *f, b.d.fruit_season);
  b.child(*f, parent, slot);
  if (Node *m = make_fruit_material(b, level + 1)) b.material(*m, *f);
  return f;
}

int foliage(Builder &b, int level, Node &part, int first_slot, float leaf_count) {
  int slot = first_slot;
  if (leaf_count > 0.f && make_leaf(b, level, part, slot, leaf_count)) ++slot;
  if (b.d.flowers && make_flower(b, level, part, slot, 1.f + b.d.density * 3.f)) ++slot;
  if (b.d.fruits && make_fruit(b, level, part, slot, 1.f + b.d.density * 3.f)) ++slot;
  return slot;
}

} // namespace arch
} // namespace plant

// ---------------------------------------------------------------- dispatch
//
// One builder per name in plant_archetypes() (plant_describe.cpp keeps the
// list, because that is where the description's vocabulary lives). The two
// must agree: a name in the list with no builder here is a menu entry that
// grows nothing, which is what the archetype test checks.

uint64_t plant_species_from_description(Graph &g, const PlantDescription &d, float x, float y,
                                        std::string &err) {
  using namespace plant::arch;
  using Fn = uint64_t (*)(Builder &, std::string &);
  static const std::map<std::string, Fn> table = {
      {"broadleaf_tree", build_broadleaf_tree}, {"conifer", build_conifer},
      {"palm", build_palm},                     {"weeping_tree", build_weeping_tree},
      {"dead_tree", build_dead_tree},           {"bonsai", build_bonsai},
      {"shrub", build_shrub},                   {"fern", build_fern},
      {"grass_tuft", build_grass_tuft},         {"flowering_plant", build_flowering_plant},
      {"cactus_columnar", build_cactus_columnar}, {"cactus_paddle", build_cactus_paddle},
      {"succulent_rosette", build_succulent_rosette}, {"vine", build_vine},
      {"bamboo", build_bamboo},                 {"reed", build_reed},
      {"mushroom", build_mushroom},             {"ground_cover", build_ground_cover}};
  auto it = table.find(d.archetype);
  if (it == table.end()) {
    err = "unknown archetype '" + d.archetype + "'";
    return 0;
  }
  Builder b(g, d, x, y);
  const uint64_t root = it->second(b, err);
  if (root) {
    plant::arch::dress_bare_wood(b, root);
    plant::arch::fit_to_height(g, root, d.height_m);
  }
  if (root && !b.missing.empty()) err = "schema keys refused:\n" + b.missing;
  return root;
}

} // namespace gpx
