// Geekatplay TerraForge - what the archetype builders share.
//
// Not a public header. A description (gpx/plant.hpp PlantDescription) is
// turned into a species graph by one builder per archetype; the builders
// live in plant_archetypes_trees.cpp and plant_archetypes_small.cpp and
// the dispatcher, the node builder and the material makers in
// plant_archetypes.cpp. This is the contract between the three.
//
// The Builder writes attributes by key and refuses a key the schema does
// not know rather than inventing one: `missing` collects every refusal, so
// a test can assert it stays empty and a builder can never quietly depend
// on a parameter that is not there. Layout is by level: the root sits at
// the builder's origin, each level of the tree one column to the left,
// siblings stacked down a column.
#pragma once
#include "gpx/plant.hpp"
#include "plant/plant_schema.hpp"
#include <string>
#include <vector>

namespace gpx {
namespace plant {
namespace arch {

constexpr float COL = 260.f; // px between a part and the part it grows on
constexpr float ROW = 160.f; // px between siblings in one column

struct RGB {
  float r = 0.5f, g = 0.5f, b = 0.5f;
};
inline RGB rgb(const float *c) { return {c[0], c[1], c[2]}; }

struct Builder {
  Graph &g;
  const PlantDescription &d;
  float ox = 0.f, oy = 0.f;
  std::vector<int> rows;      // next free row per level
  std::string missing;        // keys the schema refused, one per line

  Builder(Graph &graph, const PlantDescription &desc, float x, float y)
      : g(graph), d(desc), ox(x), oy(y) {}

  // A node of `k` in column `level` (0 = the root's), on the next free row.
  Node *add(Kind k, int level, const std::string &label = "");
  // Attributes by key. Random takes a value, a spread and its mode (0
  // absolute, 1 relative, 2 gaussian); Float takes the value alone.
  bool set(Node &n, const char *key, float value, float spread = 0.f, int mode = 1);
  bool set_i(Node &n, const char *key, int value);
  bool set_b(Node &n, const char *key, bool value);
  bool set_s(Node &n, const char *key, const std::string &value);
  bool set_choice(Node &n, const char *key, const char *label);
  bool set_color(Node &n, const char *key, RGB c);
  bool set_curve(Node &n, const char *key, const Curve &c);
  // The hierarchy curve of a Random attribute: how the value changes with
  // where the part sits on its parent (x = 0 at the parent's base).
  bool set_hier(Node &n, const char *key, const Curve &c);
  bool set_along(Node &n, const char *key, const Curve &c);
  bool set_gradient(Node &n, const char *key, const std::vector<GradientStop> &stops);
  // Links: a child's `plant` into a parent's `child N`; a material into a
  // part's material port; the base part into the root's trunk.
  bool child(Node &c, Node &parent, int slot);
  bool material(Node &m, Node &part, const char *port = "material");
  bool trunk(Node &part, Node &root);
  bool tail(Node &t, Node &repeat);

  // Relative spread from the description's variation (0..1 of the value).
  float spread() const { return d.variation * 0.4f; }
  float angle_spread() const { return d.variation * 12.f; } // degrees
};

// The root with the description's name, age, seed and wind defaults.
Node *make_root(Builder &b);
// Materials made from rules: bark, a leaf with its seasonal tint and
// presence set on `part` (the leaf-like node that wears it), a petal, a
// fruit colour. Each returns the PlantMaterial node, placed one column
// left of `level`.
// The share of its parent's radius a branch carrying `siblings` of its kind
// takes, by the pipe model (plant_archetypes.cpp).
float pipe_ratio(float siblings);
Node *make_bark(Builder &b, int level);
// After an archetype is built: every wood part with no material of its own
// wears the species' bark (plant_archetypes.cpp).
void dress_bare_wood(Builder &b, uint64_t root_id);
// Grow the species once and correct the root's scale so it stands the height
// the description asked for (plant_archetypes.cpp).
void fit_to_height(Graph &g, uint64_t root_id, float wanted_m);
Node *make_leaf_material(Builder &b, int level, const char *shape = nullptr);
Node *make_petal(Builder &b, int level);
Node *make_fruit_material(Builder &b, int level);
// The seasonal look of a leaf-like part: tint over the year and presence
// (deciduous parts drop in winter; evergreen ones stay).
void season_leaf(Builder &b, Node &part);
// The presence window of a flower or a fruit: 1 inside [s0, s1], 0 outside.
void season_window(Builder &b, Node &part, const float s[2]);
// Attachment helpers: the description's arrangement and coil on a child;
// the angle from the parent's axis in degrees (the schema's angle is 0 when
// orthogonal to the parent and 90 along it, so 90 - from_axis).
void arrange(Builder &b, Node &child);
float attach_angle(float from_axis_deg);
// A leaf node (PlantLeaf) sized from the description, with a material.
Node *make_leaf(Builder &b, int level, Node &parent, int slot, float count);
// A flower head with a petal material, or a fruit ball, on `parent`.
Node *make_flower(Builder &b, int level, Node &parent, int slot, float count);
Node *make_fruit(Builder &b, int level, Node &parent, int slot, float count);
// Leaves, then flowers and fruit when the description has them, on `part`
// from `first_slot` up; returns the next free slot.
int foliage(Builder &b, int level, Node &part, int first_slot, float leaf_count);
// A basic segment: length and radius, spreads from the variation.
Node *make_segment(Builder &b, int level, const std::string &label, float length_m,
                   float radius_m, bool user_radius);

// The archetypes. Each returns the root's id or 0 with `err`.
uint64_t build_broadleaf_tree(Builder &b, std::string &err);
uint64_t build_conifer(Builder &b, std::string &err);
uint64_t build_palm(Builder &b, std::string &err);
uint64_t build_weeping_tree(Builder &b, std::string &err);
uint64_t build_dead_tree(Builder &b, std::string &err);
uint64_t build_bonsai(Builder &b, std::string &err);
uint64_t build_shrub(Builder &b, std::string &err);
uint64_t build_fern(Builder &b, std::string &err);
uint64_t build_grass_tuft(Builder &b, std::string &err);
uint64_t build_flowering_plant(Builder &b, std::string &err);
uint64_t build_cactus_columnar(Builder &b, std::string &err);
uint64_t build_cactus_paddle(Builder &b, std::string &err);
uint64_t build_succulent_rosette(Builder &b, std::string &err);
uint64_t build_vine(Builder &b, std::string &err);
uint64_t build_bamboo(Builder &b, std::string &err);
uint64_t build_reed(Builder &b, std::string &err);
uint64_t build_mushroom(Builder &b, std::string &err);
uint64_t build_ground_cover(Builder &b, std::string &err);

} // namespace arch
} // namespace plant
} // namespace gpx
