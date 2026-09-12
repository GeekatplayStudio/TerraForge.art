// Geekatplay TerraForge - what the plant builders share (engine/plant/*.cpp).
//
// Not a public header. The public face is gpx/plant.hpp; this is the maths
// the modules agree on: a vector, a frame, the deterministic hash every
// random draw comes from, the primitive instance the walker hands each
// builder, the sockets a builder returns for its children, and the mesh
// sink that writes vertices with their wind weights and tints.
//
// Determinism is the contract that matters most. Nothing here reads a
// clock, a global, or a thread id: every random number is a hash of
// (plant seed, instance id, parameter key, draw index), so one seed is one
// plant on every machine and every thread count, and adding a sibling node
// never reshuffles another node's instances (instance ids hash the parent's
// id, the node's id and the slot, never a running counter).
#pragma once
#include "gpx/plant.hpp"
#include "plant/plant_schema.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gpx {
namespace plant {

constexpr float PI = 3.14159265358979f;
constexpr float TAU = 6.28318530717959f;
constexpr float DEG = 0.01745329251994f;

// ----------------------------------------------------------------- vectors
struct V3 {
  float x = 0, y = 0, z = 0;
  V3() = default;
  V3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
  explicit V3(const float *p) : x(p[0]), y(p[1]), z(p[2]) {}
  void to(float *p) const { p[0] = x; p[1] = y; p[2] = z; }
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(float s, V3 a) { return a * s; }
inline V3 &operator+=(V3 &a, V3 b) { a = a + b; return a; }
inline V3 &operator-=(V3 &a, V3 b) { a = a - b; return a; }
inline V3 &operator*=(V3 &a, float s) { a = a * s; return a; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a, V3 fallback = V3(0, 1, 0)) {
  const float l = length(a);
  return l > 1e-12f ? a * (1.f / l) : fallback;
}
inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float smoothstep(float e0, float e1, float x) {
  const float t = clampf((x - e0) / (e1 - e0 + 1e-20f), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}
// Rotate `v` about unit axis `k` by `angle` radians (Rodrigues).
V3 rotate(V3 v, V3 k, float angle);
// Any unit vector perpendicular to `d`.
V3 perpendicular(V3 d);

// A right-handed frame: `o` the origin, `z` the primitive's axis (its
// growth direction), `x` and `y` across it. Plant frame is metres, y up.
struct Frame {
  V3 o, x{1, 0, 0}, y{0, 0, -1}, z{0, 1, 0}; // x cross y = z
  V3 local(V3 p) const { return {dot(p - o, x), dot(p - o, y), dot(p - o, z)}; }
  V3 world(V3 p) const { return o + x * p.x + y * p.y + z * p.z; }
  V3 dir(V3 d) const { return x * d.x + y * d.y + z * d.z; }
  // A frame whose z is `axis`, with x and y chosen to twist as little as
  // possible from `ref` (parallel transport).
  static Frame along(V3 origin, V3 axis, const Frame *ref);
  Frame rotated(V3 axis, float angle) const;
};

// ------------------------------------------------------------------ hashing
// One 32-bit hash, the same on every platform (lowbias32 mixing).
uint32_t hash_u32(uint32_t a);
uint32_t hash_u32(uint32_t a, uint32_t b);
uint32_t hash_u32(uint32_t a, uint32_t b, uint32_t c);
uint32_t hash_u32(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
uint32_t hash_str(const std::string &s);
inline float hash_unit(uint32_t h) { return (float)(h & 0xffffffu) / 16777216.f; } // [0,1)
// A stream of draws for one (instance, key): draw i is hash(seed, inst, key, i).
struct Draw {
  uint32_t base = 0;
  uint32_t i = 0;
  Draw(uint32_t seed, uint32_t inst, uint32_t key) : base(hash_u32(seed, inst, key)) {}
  float unit() { return hash_unit(hash_u32(base, i++)); }      // [0,1)
  float signed_unit() { return unit() * 2.f - 1.f; }           // [-1,1)
  float range(float a, float b) { return a + (b - a) * unit(); }
  float gaussian();                                            // mean 0, sd 1
  uint32_t pick(uint32_t n) { return n ? (uint32_t)(unit() * (float)n) % n : 0; }
};

// ------------------------------------------------------------ the species
struct BuildCtx;
struct Instance;

// A material the plant wears, resolved from a PlantMaterial node (or made
// up when a part has none), with its index in the mesh.
struct MaterialRef {
  int index = 0;
  uint64_t node = 0;
};

// The mesh sink. Every builder appends triangles here; the sink keeps the
// part runs and the wind and tint streams in step with the vertices.
struct MeshOut {
  PlantMesh &m;
  const BuildCtx &ctx;
  explicit MeshOut(PlantMesh &mesh, const BuildCtx &c) : m(mesh), ctx(c) {}
  // Begin a part: everything appended until the next begin() belongs to it.
  void begin(const Instance &inst, PlantPartKind kind, int material, bool double_sided,
             const std::string &name);
  // Append a vertex; returns its index. `wind4` = {phase, bend, flutter,
  // height} - height is filled in by finish() from the position when < 0.
  //
  // A part must not come away from the part it grows on. The wind moves a
  // vertex by these four numbers alone, so two vertices in the same place
  // must carry the same four or the wind pulls them apart: `phase` is a
  // function of distance along the wood (wind_phase_at) and so already
  // agrees, `bend` comes from the parent at the attachment point, and
  // `flutter` MUST be zero where the part meets its parent and grow away
  // from it. A flutter floor under a whole leaf shook 92,000 of them free
  // of their twigs.
  uint32_t vertex(V3 p, V3 n, float u, float v, const float wind4[4], const float tint4[4]);
  void tri(uint32_t a, uint32_t b, uint32_t c);
  void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d); // two triangles, a-b-c, a-c-d
  // Close the current part (drops it when it got no triangles).
  void end();
  // Recompute normals of the current part from its triangles (Geometric
  // normal mode), smooth across shared vertices.
  void geometric_normals();
  size_t part_first_vertex = 0;
  uint32_t part_first_index = 0;
  int part_index = -1;
};

// One primitive instance: a node grown once, at a place, with a frame.
struct Instance {
  uint64_t node = 0;          // the graph node
  Kind kind = Kind::COUNT;
  uint32_t id = 0;            // hash(parent id, node id, slot, index): stable
  const Instance *parent = nullptr;
  int depth = 0;              // levels below the root (trunk = 0)
  int slot = 0;               // which "child N" of the parent it came through
  int index = 0;              // which of its siblings from that slot
  int iteration = 0, iterations = 1; // inside a Repeat
  Frame frame;                // where it starts and which way it grows
  float scale = 1.f;          // inherited scale (Transform / Inherited properties)
  float sap = 1.f;            // inherited sap 0..1 (shortens, thins)
  float density = 1.f;        // inherited density 0..1 (fewer children)
  int lod = 0;                // the level being built
  // where it sits on its parent
  float parent_primal = 0.f;  // 0 base .. 1 tip of the parent's axis
  float parent_radius = 0.f;  // the parent's radius there
  float parent_length = 0.f, parent_remaining = 0.f;
  float azimuth = 0.f;        // around the parent, from the zenith-most direction
  float dist_root = 0.f;      // metres of wood from the foot to its base
  float height_frac = 0.f;    // its base's height as a share of the plant's
  // what it turned out to be (filled by its builder)
  float length = 0.f, radius = 0.f;
  bool pruned = false;
  float prune_ratio = 1.f;
  float cut_length = 0.f;     // the length a cut allows (metres), when pruned
  float cut_radius = 1.f;     // the radius factor a cut applies
  int positioning = 2;        // the Attachment positioning it was placed with
  float twist_total = 0.f;    // turns its axis twists (Twist bias), for children
  float presence = 1.f;       // 0..1 after season/health/age (0 = not grown)
  // per-instance colour shift and wind identity
  float tint[4] = {1.f, 1.f, 1.f, 1.f};
  float wind_phase = 0.f;     // 0..1
  float wind_bend = 0.f;      // bend weight at its base (accumulated)
  // its own random draws
  float rnd = 0.f;            // one 0..1 per instance
  // the axis its builder made, for children to sit on (segments; empty for
  // parts that carry children only at their tip or on a sphere)
  struct AxisSample {
    V3 p;                 // position
    V3 t;                 // unit tangent
    V3 n;                 // a unit normal across it (the frame's x there)
    float radius = 0.f;   // body radius there (after profile, sap, shrink)
    float primal = 0.f;   // 0..1 along the (uncut) axis
    float dist = 0.f;     // metres from the base
      // The wind at this point of the axis. The ring of wood here is stamped
    // with these and so is any socket at or between these samples, so a
    // child can never start out of step with the wood it grows from.
    float wind_phase = 0.f, wind_bend = 0.f;
  };
  std::vector<AxisSample> axis;
};

// Where a child may sit on its parent: what a builder returns, one per
// "connection" the child's attachment asks for. The walker turns each into
// a child Instance.
struct Socket {
  Frame frame;                // origin on/in the parent, z = the outward default direction
  float primal = 0.f;         // where along the parent
  float radius = 0.f;         // the parent's radius there
  float side_radius = 0.f;    // the smaller radius across the child's direction
  float azimuth = 0.f;        // around the parent from the zenith-most direction
  float remaining = 0.f;      // metres of parent past this point
  float dist = 0.f;           // metres of wood from the foot
  float scale = 1.f;          // a soft-insert or scale-shift factor for the child
  int whorl_index = 0;        // which of the whorl this is
  // The wind at this point of the parent, which the child starts from. The
  // child must not work these out for itself: the parent stamped its own
  // vertices with them, and two vertices in the same place that disagree by
  // even a hundredth are pulled apart by the wind. Left at -1 by a builder
  // that has not filled them, and the walker then fills them in from the
  // part's own.
  float wind_phase = -1.f, wind_bend = -1.f;
};

// The plant being built: the graph, the root's settings, the options, the
// resolved materials, the field contexts, the stats and the warnings.
struct BuildCtx {
  const Graph *graph = nullptr;
  const Node *root = nullptr;
  PlantBuildOptions opt;
  uint32_t seed = 1;
  // the root's settings, with the options' overrides applied
  float age = 30.f, max_age = 80.f, maturity = 0.375f, health = 1.f, season = 0.5f, time = 0.f;
  float gravity = 1.f, scale = 1.f;
  int lod = 0, lod_max = 0;
  float detail = 0.f;         // 2^detail polygon density
  bool manual_meshing = false;
  int geometry_target = 0;
  // the global biases, in order (PlantBias nodes fed to the root)
  std::vector<const Node *> biases;
  // materials by node id (0 = the default wood), index into mesh.materials
  std::map<uint64_t, int> material_index;
  PlantMesh *mesh = nullptr;
  // running estimate of the plant's height, for height_frac (grows as the
  // trunk is built; parts read it as they come)
  float height_est = 1.f;
  // the plant's own place in the gust, drawn from its seed: two plants of
  // one species standing side by side do not sway in step
  float wind_phase_base = 0.f;
  // safety: instances grown so far, and the cap that stops a runaway graph
  int instances = 0, instance_cap = 400000;
  size_t vertex_cap = 40000000;
  std::vector<std::string> warnings;
  // a slot from the wood parts' sockets to the position on the plant, so
  // ambient occlusion can be estimated (filled by the walker; optional)
  std::vector<V3> crown_samples;
  // the nodes feeding each Plant input of each node, cached
  const Node *upstream_plant(const Node &n, const std::string &port) const;
  // the PlantMaterial node on a part's "material" slots (may be empty)
  std::vector<const Node *> materials_of(const Node &part) const;
  // resolve (and register) the material a part wears on slot `slot`
  int material_for(const Node &part, int slot);
  // A part's number within the species, counted by walking the graph from
  // the root - never the node's id. Ids are handed out by the graph and are
  // renumbered when a species is saved and loaded back, and an individual
  // must not change when that happens, so every hash uses this instead.
  uint32_t key_of(const Node &n) const;
  uint32_t key_of(uint64_t node_id) const;
  std::map<uint64_t, uint32_t> part_key; // filled once, from the root
};

// ------------------------------------------------------------ parameters
// Reading a node's Random attribute for an instance: value, spread by mode
// and scope, the along-curve at `primal`, the hierarchy curve at the
// instance's position on its parent, and a field link overriding all of it
// when the port is connected. Every builder reads its numbers through this.
struct ParamReader {
  const BuildCtx &ctx;
  const Node &node;
  const Instance &inst;
  ParamReader(const BuildCtx &c, const Node &n, const Instance &i) : ctx(c), node(n), inst(i) {}
  // A Random attribute at `primal` (0..1 along the instance). `draw_index`
  // distinguishes several draws of one key on one instance (scope "each
  // time"). Falls back to `def` when the key is missing.
  float random(const char *key, float primal = 0.f, uint32_t draw_index = 0, float def = 0.f) const;
  float random(const std::string &key, float primal = 0.f, uint32_t draw_index = 0, float def = 0.f) const {
    return random(key.c_str(), primal, draw_index, def);
  }
  // Plain attributes
  float f(const char *key, float def = 0.f) const;
  int i(const char *key, int def = 0) const;
  bool b(const char *key, bool def = false) const;
  int choice(const char *key, int def = 0) const;
  std::string s(const char *key) const;
  void color(const char *key, float out[4]) const;
  const Curve &curve(const char *key) const;               // primary (or picked by seed)
  float curve_at(const char *key, float x) const;
  // A gradient sampled at t: rgb multiplier
  void gradient(const char *key, float t, float rgb[3]) const;
  // Whether a field port drives this key, and its value for the instance
  bool driven(const char *key) const;
  float field(const char *key, float primal, float def) const;
  // The plant variables for a field evaluation at `primal`
  PlantVars vars(float primal) const;
  PlantVars vars(float primal, float section_angle, float radial) const;
  // A driven key evaluated with explicit variables (per vertex: section
  // angle and radial distance set); the attribute's value when not driven.
  float field_at(const char *key, const PlantVars &v, float def) const;
  // A key's Random attribute, or null when the node has none by that name.
  const Attribute *attr(const char *key) const;
};

// The random-range draw itself, shared with the tests: value + spread by
// mode, from one uniform `u` in [0,1) and one gaussian `g`.
float random_range(float value, float spread, int spread_mode, float u, float g);

// ------------------------------------------------------------- builders
// Each part's builder grows one instance: appends its geometry to `out`,
// fills `inst.length/radius/axis`, and returns the sockets its children
// may use per child slot (the walker asks for `sockets(slot)` after build).
// A builder never recurses into children; the walker does, so selectors,
// loops, LOD and presence are decided in one place.
struct Sockets {
  // by positioning method: sockets along the axis for Axis/Skin/Orthogonal/
  // Below skin placement, one at the tip, one at the bottom
  std::vector<Socket> along;  // ordered by primal
  Socket tip, bottom;
  bool has_tip = false, has_bottom = false;
};
void build_segment(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_leaf(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_cutout_leaf(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_warpboard(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_object(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_urchin(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_hydra(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_ball(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
void build_flower(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);
// Growth is different: it grows a whole branch system and returns the
// sockets where leaves go (per child slot, all slots share them).
void build_growth(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk);

// The walker (plant_eval.cpp): grow `inst` from its node, then its children
// through every child slot, per the children's attachment parameters.
void grow(BuildCtx &ctx, Instance &inst, MeshOut &out);

// Attachment (plant_attach.cpp): the child instances a child node asks for
// on a parent's sockets. Handles count, range, density, soft insert,
// arrangement, positioning, roll, coil, whorl, angle, rotation, tropism,
// randomness, pruning by probability, inherited density/scale/sap.
std::vector<Instance> attach(BuildCtx &ctx, const Node &child_node, int slot, const Instance &parent,
                             const Sockets &sk, const Node &parent_node);
// One connection the attachment plans on a parent's axis: where (primal),
// the soft-insert scale, which member of a pair it is, and what it does to
// the parent (shrink radius, zig-zag bending). The plan is what
// build_segment reads BEFORE meshing and what attach() turns into
// instances afterwards; both hash the same keys, so they agree.
struct Connection {
  float primal = 0.f;
  float scale = 1.f;
  int pair = 0;               // 0 first (or only) member, 1 the second of a pair
  int index = 0;              // running connection index (for coil)
  float shrink = 0.f, bending = 0.f;
};
std::vector<Connection> attach_plan(BuildCtx &ctx, const Node &child, int slot, const Instance &parent,
                                    float length);
std::vector<float> attach_positions(BuildCtx &ctx, const Node &child, int slot, const Instance &parent,
                                    float length);
// The provisional child an attachment reads its parameters through: parent
// fields set, id hashed from (parent, node, slot) so per-slot draws hold.
Instance provisional_child(const BuildCtx &ctx, const Node &child, int slot, const Instance &parent);
// One instance at one socket, honouring the child's angle, rotation,
// tropism and transform, but not its count or range (selectors, growth
// leaves, urchin and hydra sockets).
Instance attach_at(BuildCtx &ctx, const Node &child, int slot, const Instance &parent, const Socket &s,
                   const Node &parent_node, int index);
// The child's Transform group: its own scale, offsets and rotations.
// Turn a frame's z toward a direction by a share of the angle between them,
// and the two orientation-tropism parameters every part carries. One
// definition (plant_types.cpp): a part that leans must lean the same way
// whichever builder made it.
void lean(Frame &f, V3 target, float amount);
void tropism_block(const ParamReader &pr, Frame &f);

void apply_transform(const BuildCtx &ctx, const Node &n, Instance &inst);

// The axis (plant_axis.cpp): a segment's centre line from its parameters,
// biases (root's global and the segment's local), tropism, gravity,
// perturbation noise and smoothing. `samples` are spaced by the meshing;
// the axis length is `length` unless it is cut.
struct AxisParams {
  float length = 1.f;
  float radius = 0.1f;
  int mode = 0;               // axis_mode
  float bend_deg = 0.f;
  const Curve *custom = nullptr;
  float tropism = 0.f;        // vertical force (segment's Tropism)
  float gravity = 1.f;        // root gravity * gust_gravity
  float perturb_strength = 0.f, perturb_planar = 0.f, perturb_frequency = 2.f;
  bool perturb_keep_tip = false, perturb_smooth_start = true;
  int perturb_apply = 1;
  float smoothing = 0.f;
  bool prevent_backfolds = true;
  int sampling_boost = 0;
  float global_bias_strength = 1.f;
  float orient_vertical = 0.f, orient_horizontal = 0.f;
  // local bias
  int bias_type = 0;
  float bias_strength = 0.f;
  V3 bias_dir{0, 1, 0}, bias_origin;
  bool bias_local = false, bias_length_agnostic = false, bias_repeller = false;
  float bias_cone_angle = 90.f, bias_base_length = 1.f;
  bool twist_planar = false;
  int twist_symmetry = 2;
  float twist_target = 0.f;
  uint32_t noise_seed = 0;
  // the root's global biases, already resolved for this instance (strength
  // drawn, direction turned into the plant frame)
  struct Bias {
    int type = 0;             // PlantBias bias_type: 0 Direction .. 6 Twist
    float strength = 0.f;
    V3 dir{0, 1, 0}, origin;
    bool local = false, relative = true, repeller = false;
    float cone_angle = 90.f, base_length = 1.f;
    bool twist_planar = false;
    int twist_symmetry = 2;
    float twist_target = 0.f;
  };
  std::vector<Bias> global_biases;
  // zig-zag kinks children ask for (Influence on parent: bending): the
  // axis turns by `angle` radians at `primal`, alternating sides
  struct Kink {
    float primal = 0.f, angle = 0.f;
  };
  std::vector<Kink> kinks;
  float kink_smooth = 0.5f;
};
void build_axis(const BuildCtx &ctx, const AxisParams &p, const Frame &start, int samples,
                std::vector<Instance::AxisSample> &out, float &twist_total);

// What the segment modules share once the body is resolved
// (plant_segment.cpp fills it; plant_segment_extra.cpp caps and flares it;
// plant_blades.cpp hangs blades on it).
struct SegmentShape {
  float length = 1.f, radius = 0.1f;          // built length and base radius
  std::vector<Instance::AxisSample> rings;    // the meshed rings, base to tip
  int radial = 8;                             // columns around
  float twist_total = 0.f;                    // turns over the length
  // radius factor by angle (0..1 around) and primal: section, squash, twist
  std::function<float(float, float)> section;
  // radial displacement in metres at (angle 0..1, primal, dist m)
  std::function<float(float, float, float)> rdisp;
  int body_material = 0, cap_material = 0;
  float wind_phase = 0.f, wind_flex = 1.f;
  float tint[4] = {1, 1, 1, 1};
  // the first and last body ring's vertices (positions, normals) for caps
  std::vector<V3> top_ring, top_nrm, bottom_ring, bottom_nrm;
  float top_v = 0.f, bottom_v = 0.f;          // the body's v there (extend to caps)
  float u_rep = 1.f;                          // u repetitions around
  bool skinned = true;
};
void segment_caps(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, const SegmentShape &sh);
void segment_blades(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, const SegmentShape &sh);
// Root flares as a radial bump: metres added at (angle 0..1, primal).
struct FlareParams {
  int number = 0;
  float randomness = 0.3f, height = 1.f, swell = 0.5f, depth = 0.8f, width = 0.5f;
  const Curve *shape = nullptr;
  uint32_t seed = 0;
};
float segment_flare(const FlareParams &f, float radius, float length, float angle, float primal);

// The wind the root describes (its Wind group), for the studio's uniforms
// and for a bake.
PlantWind plant_wind_from_root(const Node &root);

// Wind weights (plant_wind.cpp): the bend weight a child inherits at a
// socket, from the parent's weight, the segment's flexibility and the
// root's boosts on thin and long segments.
float wind_bend_at(const BuildCtx &ctx, const Instance &parent, float primal, float flexibility);
// Where the gust has got to, `dist_m` metres of wood out from the foot.
// Every part reads its phase from this and nothing else, so a child and the
// parent it grows on always agree at the joint (plant_wind.cpp).
float wind_phase_at(const BuildCtx &ctx, float dist_m);

// Season and health (plant_season.cpp): the presence (0..1) of a part from
// its Seasons group, and the tint it wears; and the droop/shrink of a dry
// leaf-like part.
struct SeasonLook {
  float presence = 1.f;
  float tint[3] = {1.f, 1.f, 1.f};
  float droop_deg = 0.f;
  float shrink = 0.f;
};
SeasonLook season_look(const BuildCtx &ctx, const ParamReader &pr);
// Apply an HLS shift to an rgb (hue in turns, luminosity and saturation added)
void hls_shift(float rgb[3], float hue, float lum, float sat);

// Subdivision counts from a part's meshing parameters and the root's boost
// and LOD: never below `mn`, halved per LOD level.
int subdiv(const BuildCtx &ctx, float number, int mn, int lod_shift = 0);

// Geometry helpers (plant_mesh_util.cpp)
// A profile-of-revolution lathe about the frame's z: `profile(t)` gives
// (radius, height) for t in 0..1; `section(a)` scales the radius by angle
// a in 0..1. Writes the surface with UVs (u around, v along) and returns
// the vertex index of ring `i` column `j` through `ring_index`.
struct LatheOpts {
  int rings = 8, columns = 16;
  bool cap_top = false, cap_bottom = false;
  bool double_sided = false;
  bool invert = false;        // flip normals and winding
  float u_tile = 1.f, v_tile = 1.f, u_offset = 0.f, v_offset = 0.f;
  int uv_mode = 0;            // 0 cylinder (u around, v along), 1 disc projected
};
void lathe(MeshOut &out, const Frame &f, const std::function<void(float, float &, float &)> &profile,
           const std::function<float(float, float)> &section, const LatheOpts &o, const float wind4[4],
           const float tint4[4]);
// A grid (w x h quads) over a parametric surface `at(u, v) -> position,
// normal`, with UVs u,v; double-sided emits a second copy facing the other way.
void grid(MeshOut &out, int w, int h, const std::function<void(float, float, V3 &, V3 &)> &at,
          bool double_sided, const std::function<void(float, float, float wind4[4])> &wind,
          const float tint4[4], float u0 = 0.f, float u1 = 1.f, float v0 = 0.f, float v1 = 1.f);

// The default materials when a part has none: wood and leaf.
int default_wood_material(BuildCtx &ctx);
int default_leaf_material(BuildCtx &ctx);
int default_petal_material(BuildCtx &ctx);

// A node's kind, or COUNT for a non-plant node.
inline Kind kind_of(const Node &n) { return kind_of_type(n.type); }

} // namespace plant
} // namespace gpx
