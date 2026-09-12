// Geekatplay TerraForge - plants grown from rules.
//
// A plant species is a subgraph of Plant-category nodes: a PlantSpecies
// root, the trunk feeding it, the branches feeding the trunk, leaves,
// flowers, fruit and the selectors between them. Links carry
// DataType::Plant and point from a part to the part it grows on (a leaf
// into its twig, the trunk into the root), so the root is the sink that
// evaluates last and any edit anywhere in the species marks it dirty. The
// root's compute walks its subtree's attributes and grows one individual:
// a mesh with texture coordinates, materials, wind weights and tints, from
// one seed. Another seed is another individual of the same species; the
// same seed is the same bytes on every machine and every thread count.
//
// Every number on a plant node is a Random attribute (gpx/attribute.hpp):
// a value, a spread, when a new draw is made, and two shaping curves. A
// number may also be driven by a field link - any noise, math or curve node
// - evaluated per primitive with the plant's variables on the FieldContext
// (PlantVars below). That is the whole "function graph" of a plant tool,
// reusing the field domain rather than growing a second one.
//
// Engine-only: no GL, no scene, no JSON in the interface. The studio turns a
// PlantMesh into a scene object; the exporters write it; the tests build
// species in a console (tests/cpp/test_plant.cpp).
#pragma once
#include "gpx/node_graph.hpp"
#include "gpx/plant_curve.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gpx {

// ------------------------------------------------------------ plant variables
// What a driven parameter may ask about while a primitive is being grown.
// Set on FieldContext::plant by the builder; read by the PlantVariable and
// PlantVector field nodes. Angles in radians, lengths in metres.
struct PlantVars {
  // where on the current primitive: 0 at its base, 1 at its tip; around it
  // 0..1; distance from its axis in metres
  float primal = 0.f, section_angle = 0.f, radial = 0.f;
  // the plant: age in years, its maximum, age/max, health 0 dying..1
  // thriving, season 0..1 through the year (0 midwinter, 0.25 spring,
  // 0.5 midsummer, 0.75 autumn), scene time in seconds
  float age = 1.f, max_age = 1.f, maturity = 1.f, health = 1.f, season = 0.5f, time = 0.f;
  // where in the plant: levels below the root, distance along the wood from
  // the foot in metres, height above the foot as a share of the plant's
  // height, the loop iteration (0 first .. 1 last) when inside a Repeat
  int depth = 0;
  float dist_root = 0.f, height_frac = 0.f, iteration = 0.f;
  // the parent: where this primitive sits on it (0..1), its radius and
  // length there, what remains of it past this point, and its tilt from the
  // vertical
  float parent_primal = 0.f, parent_radius = 0.f, parent_length = 0.f,
        parent_remaining = 0.f, parent_tilt = 0.f;
  // this primitive's own length and radius, and its azimuth around the
  // parent measured from the direction nearest the zenith (-pi..pi)
  float length = 0.f, radius = 0.f, azimuth = 0.f;
  // positions and directions in the plant's frame (metres, y up)
  float pos[3] = {0, 0, 0}, dir[3] = {0, 1, 0}, axis_dir[3] = {0, 1, 0},
        radial_dir[3] = {1, 0, 0}, parent_dir[3] = {0, 1, 0};
  // one draw per primitive and one per plant, 0..1
  float rnd_instance = 0.f, rnd_plant = 0.f;
  // the level of detail being built and the highest one the species has
  int lod = 0, lod_max = 0;
  bool pruned = false;
  float prune_ratio = 1.f; // length kept after a cut, 1 uncut
};

// ------------------------------------------------------------------ options
struct PlantBuildOptions {
  uint32_t seed = 1;
  // Overrides of the root's own age/health/season/time when >= 0 (a
  // population sets them per individual; the editor's sliders set the root).
  float age = -1.f, health = -1.f, season = -1.f, time = -1.f;
  int lod = 0;            // 0 the full plant; each level halves subdivisions
  float detail = 0.f;     // meshing boost: polygon density scales by 2^detail
  bool wind_weights = true;
  bool tints = true;
  // Where a species' own files (leaf pictures, cut-outs, imported parts) are
  // resolved from when a node names a relative path.
  std::string asset_dir;
  // Camera-facing leaves are baked facing this direction (unit, plant frame)
  float facing[3] = {0.f, 0.f, 1.f};
};

// ---------------------------------------------------------------- materials
// One material a plant part wears. Pictures are file paths (absolute, or
// relative to the species folder); an empty path means the colour alone.
struct PlantSeasonLook {
  std::string name;            // "summer", "autumn", "bare"
  std::string color_map, alpha_map, normal_map;
  float shift[3] = {0, 0, 0};  // hue (turns), luminosity, saturation added
  float presence = 1.f;        // share of leaves kept in this look
};
struct PlantMaterial {
  std::string name;
  uint64_t node = 0;           // the PlantMaterial node it came from, 0 built-in
  float color[4] = {0.5f, 0.5f, 0.5f, 1.f};
  std::string color_map, alpha_map, normal_map, roughness_map;
  float roughness = 0.7f, metallic = 0.f, translucency = 0.f, backlight = 0.f;
  bool two_sided = false;
  bool alpha_cutout = false;   // the alpha is a hard cut, not a fade
  float uv_tile[2] = {1.f, 1.f};
  // the cut-out outline in picture space (u,v pairs, 0..1), for cut-out
  // leaves; empty means the whole picture
  std::vector<float> cutout;
  // seasonal looks and which one the season picks (x season -> look index);
  // empty means the material is the same all year
  std::vector<PlantSeasonLook> seasons;
  Curve season_curve;
  // A picture made from rules (a leaf, a bark, a petal) lives here rather
  // than on disk until the species is saved: RGBA, `w` x `h`, empty when
  // the material names files or a colour instead. `normal_rgba` is its
  // normal map when one was made.
  std::vector<uint8_t> rgba, normal_rgba, rough_rgba;
  int w = 0, h = 0;
};

// -------------------------------------------------------------------- mesh
enum class PlantPartKind : uint8_t {
  Body, Cap, Flare, Blade, Leaf, Petal, Fruit, Object, Card
};
struct PlantPart {
  std::string name;
  uint64_t node = 0;           // the node that grew it
  int material = 0;            // index into PlantMesh::materials
  uint32_t first_index = 0, index_count = 0;
  bool double_sided = false;
  PlantPartKind kind = PlantPartKind::Body;
};
struct PlantMesh {
  // per vertex
  std::vector<float> pos;      // xyz metres, plant frame: foot at origin, y up
  std::vector<float> nrm;      // xyz unit
  std::vector<float> uv;       // uv
  // wind weights: x phase 0..1 (per branch), y bend weight (0 at the foot,
  // growing along the wood to the tips), z flutter weight (leaves and
  // blades; 0 on wood), w height 0..1 of the plant
  std::vector<float> wind;
  // tint: rgb multiplier (season and health already applied), a = ambient
  // occlusion (1 open)
  std::vector<float> tint;
  std::vector<uint32_t> idx;   // triangles
  std::vector<PlantPart> parts;
  std::vector<PlantMaterial> materials;
  float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  float height_m = 0.f;
  // what was grown
  int primitives = 0, leaves = 0, cut = 0;
  std::string warnings;        // one line each, for the editor's status
  size_t vertex_count() const { return pos.size() / 3; }
  size_t triangle_count() const { return idx.size() / 3; }
  void clear();
  // Append another mesh (its parts renumbered, materials merged by node id).
  void append(const PlantMesh &m, const float offset[3]);
  void compute_bounds();
};

// --------------------------------------------------------------------- build
// Grow the species whose root is `root`. The root's Random attributes and
// its subtree are read; nothing in the graph is written. False with `err`
// when the subtree is not a plant (no trunk, a loop without a body, a
// node of the wrong type on a Plant port).
bool plant_build(const Graph &g, const Node &root, const PlantBuildOptions &o, PlantMesh &out,
                 std::string &err);

// The PlantSpecies node `any` feeds, following Plant links downstream; null
// when it feeds none (a loose part).
const Node *plant_root_of(const Graph &g, const Node &any);
// Every node feeding `root` through Plant links, root first, parents before
// children. What a species file saves.
std::vector<const Node *> plant_subtree(const Graph &g, const Node &root);
// Whether the node's type is a plant part (grows geometry or arranges it).
bool plant_is_part(const std::string &type);
// The plant nodes' ids in a stable order for `plant_subtree`'s file: the
// root is 0, then breadth-first.

// The last mesh each species root built, kept by node id so the studio can
// pick it up after an evaluation without rebuilding (the root's compute
// stores it; a removed root's entry is dropped by plant_mesh_forget).
std::shared_ptr<const PlantMesh> plant_mesh_for(uint64_t root_id);
void plant_mesh_store(uint64_t root_id, std::shared_ptr<const PlantMesh> mesh);
void plant_mesh_forget(uint64_t root_id);
// A fingerprint of everything that changes a species' build: its subtree's
// attributes and links, and the options. Equal fingerprints, equal bytes.
uint64_t plant_fingerprint(const Graph &g, const Node &root, const PlantBuildOptions &o);

// ---------------------------------------------------------------------- wind
// The wind a plant stands in, from the root's Wind group and the scene.
struct PlantWind {
  float strength = 0.f;        // 0 still .. 1 storm
  float dir[2] = {1.f, 0.f};   // unit, in the ground plane (x, z)
  float breeze = 0.5f;         // ambient waving amplitude
  float breeze_speed = 1.f;    // cycles per second at strength 1
  float breeze_randomness = 0.5f;
  float flutter = 0.5f;        // leaf quiver amplitude
  float flutter_speed = 3.f;
  float gust = 0.3f;           // gust amplitude over the breeze
  float gust_frequency = 0.15f;// gusts per second
};
// The displacement of one vertex at time `t`, given its wind weights: the
// CPU twin of the GLSL function below, same arithmetic. `pos` is the
// vertex in the plant frame; `out` receives the moved position.
void plant_wind_vertex(const PlantWind &w, float t, const float pos[3], const float wind4[4],
                       float height_m, float out[3]);
// The same function as GLSL: `vec3 plant_wind(vec3 pos, vec4 w, float t,
// float height, PlantWindU u)` and the uniform block it reads. Spliced into
// the mesh vertex shader by the studio.
const char *plant_wind_glsl();
// Move every vertex of a mesh: for a bake or an export at a given time.
void plant_wind_apply(const PlantMesh &m, const PlantWind &w, float t, std::vector<float> &pos_out);

// ------------------------------------------------------------ species files
// A species on disk is a folder: species.json (this record plus the
// subtree's nodes and links), its pictures beside it, a thumbnail.
struct PlantPreset {
  std::string name, note, thumb;
  float age = -1.f, health = -1.f, season = -1.f; // -1 keeps the root's
  // published parameter values: "<node index in file>/<key>" -> value
  std::vector<std::pair<std::string, float>> values;
};
struct PlantSpeciesInfo {
  std::string id, name, group, note, thumb, license, authors;
  float height_m = 0.f;
  uint32_t seed = 1;
  std::vector<uint32_t> flagged;   // seeds worth keeping
  std::vector<PlantPreset> presets;
  int polycount = 0;
};
// Write the species whose root is `root` into `dir` (created). Pictures the
// nodes name outside `dir` are copied in and the copies are what the file
// names. Returns the species.json path, or "" with `err`.
std::string plant_species_save(const Graph &g, const Node &root, const PlantSpeciesInfo &info,
                               const std::string &dir, std::string &err);
// Read a species file (its folder or species.json) and add its nodes to
// `g` at (x, y), returning the new root's id, or 0 with `err`. Node ids are
// new; links are remapped; relative picture paths are made absolute.
uint64_t plant_species_load(Graph &g, const std::string &path, float x, float y,
                            PlantSpeciesInfo &info, std::string &err);
// Just the record, without touching a graph.
bool plant_species_read_info(const std::string &path, PlantSpeciesInfo &info, std::string &err);
// The published parameters of a species: node, key, name, group.
struct PlantPublished {
  uint64_t node = 0;
  std::string key, name, group;
};
std::vector<PlantPublished> plant_published(const Graph &g, const Node &root);

// ------------------------------------------------------ describing a plant
// The words a person or a language model gives, as a botanical description
// the archetype builders turn into a graph. Every field has a default so a
// partial description still grows.
struct PlantDescription {
  std::string name = "Plant";
  std::string archetype = "broadleaf_tree"; // plant_archetypes()
  std::string group = "trees";              // the library shelf
  float height_m = 10.f;
  float width_ratio = 0.8f;                 // crown width / height
  std::string crown = "round";              // round|spreading|columnar|conical|weeping|vase|irregular|umbrella|shrubby
  // trunk and wood
  std::string bark = "fissured";            // fissured|plated|smooth|birch|ringed|peeling|scaly|fibrous
  float bark_color[3] = {0.35f, 0.28f, 0.2f};
  float trunk_ratio = 0.035f;               // trunk radius / height
  float straightness = 0.8f;                // 0 gnarled .. 1 straight
  int forks = 0;                            // trunks splitting near the base
  // branching
  int levels = 3;
  float density = 0.6f;
  float branch_angle_deg = 45.f;
  float droop = 0.f;                        // -1 rising .. 1 weeping
  std::string arrangement = "spiral";       // alternate|opposite|whorled|spiral
  // leaves
  std::string leaf_shape = "ovate";         // ovate|lanceolate|lobed|palmate|needle|scale|pinnate|heart|linear|round|elliptic|frond|blade
  float leaf_size_cm = 8.f;
  float leaf_color[3] = {0.25f, 0.45f, 0.15f};
  float autumn_color[3] = {0.75f, 0.4f, 0.1f};
  bool evergreen = false;
  float leaf_density = 0.7f;
  std::string leaf_cluster = "single";      // single|pairs|rosette|frond|tuft
  // flowers and fruit
  bool flowers = false;
  std::string flower_shape = "disc";        // disc|cup|tube|bell|spike|cluster
  float flower_color[3] = {0.95f, 0.85f, 0.3f};
  float flower_size_cm = 3.f;
  float flower_season[2] = {0.25f, 0.5f};
  bool fruits = false;
  float fruit_color[3] = {0.8f, 0.2f, 0.1f};
  float fruit_size_cm = 2.f;
  float fruit_season[2] = {0.6f, 0.85f};
  // individuals
  float variation = 0.3f;                   // how much one plant differs from the next
  float age_years = 30.f, max_age_years = 80.f;
  // how it is doing and when it is seen. These are the species root's live
  // settings, not baked colours: a plant described "dry, in autumn" is a
  // healthy plant shown dry in autumn, and the sliders (and the timeline)
  // take it back through the year.
  float health = 1.f;                       // 0 dead .. 1 thriving
  float season = 0.5f;                      // 0 midwinter, 0.25 spring, 0.5 midsummer, 0.75 autumn
  std::string note;
};
// How grown a plant of this age is, 0..1, and the share of its full size it
// has reached.
//
// A plant is full-grown long before it dies: an oak of a hundred and forty
// is a whole tree, not a quarter of one, though it may stand six hundred
// years. So growth is measured against the age it comes into its own - a
// little under half the span - and everything past that is age, not size.
// Both the engine (which scales and thins by it) and the archetype builders
// (which cancel it, so a description's height is the height you get) read
// these, and they must agree.
float plant_maturity(float age_years, float max_age_years);
float plant_size_at(float maturity);

// The archetypes the builders know, in menu order.
const std::vector<std::string> &plant_archetypes();
// A JSON description (what the assistant returns), tolerant of missing
// fields. False with `err` when it is not JSON or names no archetype we know.
bool plant_description_parse(const std::string &json_text, PlantDescription &out, std::string &err);
std::string plant_description_json(const PlantDescription &d);
// The description schema and the archetype list as text, for the model's
// instructions.
std::string plant_description_schema();
// From plain words with no model at all: "old oak", "young birch", "date
// palm", "lavender", "saguaro". A knowledge table of common plants; a word
// it does not know falls back to the nearest archetype it can guess, and
// the return says whether anything matched.
bool plant_describe_from_words(const std::string &words, PlantDescription &out);
// Build the species graph for a description into `g` at (x, y); the root's
// id, or 0 with `err`. The nodes are laid out for the editor.
uint64_t plant_species_from_description(Graph &g, const PlantDescription &d, float x, float y,
                                        std::string &err);

// ------------------------------------------------------------ node presets
// A fresh plant node set up as a trunk, a branch, a stem, a palm frond, a
// twig, a leaf, a billboard leaf, a growth or a flower: the values a person
// would otherwise dial in first. `preset` names one of plant_presets().
const std::vector<std::string> &plant_presets();
bool plant_preset_apply(Node &n, const std::string &preset);
// The node type a preset makes ("PlantSegment" for "trunk").
std::string plant_preset_type(const std::string &preset);

// --------------------------------------------------------------- textures
// Leaf and bark pictures made from rules, so a species needs no file it did
// not make itself. RGBA, top-left origin, `w` x `h`.
struct PlantLeafTexture {
  std::string shape = "ovate";      // as PlantDescription::leaf_shape
  float color[3] = {0.25f, 0.45f, 0.15f};
  float vein_color[3] = {0.55f, 0.65f, 0.35f};
  float serration = 0.f;            // 0 smooth edge .. 1 toothed
  float lobes = 0.f;                // lobed/palmate: depth of the lobes 0..1
  int lobe_count = 5;
  float aspect = 0.55f;             // width / length
  float vein_strength = 0.5f;
  float mottle = 0.3f;
  float wilt = 0.f;                 // 0 fresh .. 1 dry (health)
  uint32_t seed = 1;
};
struct PlantBarkTexture {
  std::string kind = "fissured";    // as PlantDescription::bark
  float color[3] = {0.35f, 0.28f, 0.2f};
  float crack_color[3] = {0.12f, 0.09f, 0.07f};
  float scale = 1.f;
  float roughness = 0.6f;
  uint32_t seed = 1;
};
// A leaf's picture, and the two maps that make it read as a leaf rather than
// as a printed card.
//
// Colour alone is a flat thing. What a real leaf gives the eye is its
// SURFACE: a midrib standing proud with the blade dished either side of it,
// secondary veins ridged across that, and a waxy cuticle that is glossy on
// the blade and dull along the veins and at a dry edge. None of that is in
// the colour - it is in how the surface turns and how sharply it reflects -
// so a leaf without a normal map is lit as one flat facet however good its
// picture is, and reads as paper. The bark has had one all along.
//
// `normal_rgba` is tangent space, the same convention the bark uses;
// `rough_rgba` is greyscale roughness. Both optional: null for colour alone.
bool plant_texture_leaf(const PlantLeafTexture &t, int w, int h, std::vector<uint8_t> &rgba,
                        std::vector<float> &cutout_uv,
                        std::vector<uint8_t> *normal_rgba = nullptr,
                        std::vector<uint8_t> *rough_rgba = nullptr);
bool plant_texture_bark(const PlantBarkTexture &t, int w, int h, std::vector<uint8_t> &rgba,
                        std::vector<uint8_t> &normal_rgba);
// A petal picture: a soft-edged, veined petal in `color` fading to `base`.
bool plant_texture_petal(const float color[3], const float base[3], uint32_t seed, int w, int h,
                         std::vector<uint8_t> &rgba, std::vector<float> &cutout_uv);
// Trace the outline of a picture's alpha into a polygon (u,v pairs, 0..1),
// simplified to about `max_points` vertices. For cut-out leaves from any
// picture with an alpha.
bool plant_trace_cutout(const uint8_t *rgba, int w, int h, int max_points, std::vector<float> &cutout_uv);
bool plant_texture_write_png(const std::string &path, const std::vector<uint8_t> &rgba, int w, int h);

// ------------------------------------------------------------------ export
// Write the mesh as OBJ + MTL (pictures referenced by path) or as a binary
// glTF with the pictures embedded, by the extension of `path`.
bool plant_mesh_write(const PlantMesh &m, const std::string &path, std::string &err);

} // namespace gpx
