// Geekatplay TerraForge - the growth simulation's own records.
//
// Not a public header. build_growth (plant_growth.cpp) runs the simulation
// (plant_growth_sim.cpp) and hands its shoots to the tube mesher
// (plant_growth_mesh.cpp); this is what the three agree on. A Growth part is
// not a segment with children but a whole branch system grown bud by bud,
// so it keeps its own skeleton - a flat list of shoots, each a chain of
// internodes - rather than an Instance per branch: ten thousand twigs as
// Instances would cost more than the mesh they make.
//
// Determinism: a shoot's draws come from Draw(seed, inst.id ^ shoot index,
// hash_str("growth")) with the shoot's own running counter, so the same
// seed is the same tree and adding a leaf node never reshuffles the wood.
// Shoots are appended in creation order and never erased during the
// simulation (removed ones are flagged), so a shoot's index is a stable
// identity and every child's index is greater than its parent's - the pipe
// model walks the list backwards and meets every child before its parent.
#pragma once
#include "plant/plant_internal.hpp"
#include <vector>

namespace gpx {
namespace plant {

// What the manual calls a branch: a chain of internodes grown from one bud.
struct Shoot {
  int parent = -1;            // the shoot it grew from, -1 for the trunk
  int parent_node = 0;        // the internode of the parent it sits on
  V3 start;                   // where it begins (the parent's internode)
  V3 dir{0, 1, 0};            // the direction its tip grows in (unit)
  std::vector<V3> nodes;      // the internodes' end positions, in growth order
  std::vector<int> node_iter; // the iteration each internode grew in
  std::vector<float> bud_az;  // the azimuth of the bud at each internode
  std::vector<float> radii;   // the radius at each internode (pipe model)
  float radius = 0.f;         // its radius at the base
  bool alive = true;          // still growing (false: dead, cut or stopped)
  bool removed = false;       // decayed away or cut off: no wood, no leaves
  int dead_since = -1;        // the iteration it died in, -1 while alive
  int rank = 0;               // generations from the trunk (trunk 0)
  int born = 0;               // the iteration it sprouted in
  float light = 1.f;          // the resource at its tip last iteration
  float azimuth = 0.f;        // the running phyllotaxis azimuth of its buds
  float dist_base = 0.f;      // metres of wood from the foot to its base
  uint32_t draws = 0;         // how many draws it has taken so far
  // the plant-frame radial directions its buds are measured from: the
  // internode's tangent is rotated toward radial(azimuth) to make a lateral
  V3 tangent_at(int k) const;
  V3 radial_at(int k, float az) const;
};

// The shadow the crown casts on itself: a coarse grid of shadow_size cells
// over the plant's box. Every living internode darkens the cells below it,
// so a bud reads how much wood and leaf stands above it.
struct ShadowGrid {
  V3 origin;                  // the grid's minimum corner
  float cell = 0.3f;
  int nx = 0, ny = 0, nz = 0;
  std::vector<float> shadow;  // per cell, accumulated
  void reset(V3 lo, V3 hi, float cell_size);
  void cast(V3 p, float strength);   // darken the cells below p
  float light_at(V3 p) const;        // 1 open .. 0 fully shaded
};

// The parameters that hold still through the simulation; the ones the
// manual's age filter may vary are read per iteration by the simulation
// itself through ParamReader.
struct GrowthParams {
  int iterations = 12;
  bool over_parent = false;   // start = Over the parent segment
  float internode = 0.2f;
  float bud_radius = 0.006f;
  float shadow_strength = 0.5f, shadow_size = 0.3f;
  bool vertical_trunk = true;
  int bottom_cut_mode = 1, bottom_cut_rank = 0;
  float bottom_cut_height = 0.f;
  bool profile_cut = false;
  int profile_cut_mode = 1;
  float profile_bottom = 0.2f, profile_top = 1.f, profile_radius = 0.6f;
  const Curve *profile = nullptr;
  float parent_radius = 0.f;  // the cap when over_parent
  float scale = 1.f;          // inst.scale * ctx.scale: multiplies lengths and radii
};

// Grow the shoots from the instance's frame. True when the shoot cap was
// hit (the caller writes the warning: the context is read-only here).
bool growth_simulate(const BuildCtx &ctx, const Node &n, const Instance &inst, GrowthParams p,
                     std::vector<Shoot> &shoots);

// Mesh every kept shoot as a tube with a cap at its tip.
struct GrowthMeshParams {
  float axial_per_m = 6.f, angular_per_m = 12.f;
  float mesh_boost = 0.f;
  float flexibility = 1.f, gravity = 1.f, wind = 1.f;
  int material = 0;
};
void growth_mesh(const BuildCtx &ctx, const Instance &inst, const std::vector<Shoot> &shoots,
                 const GrowthMeshParams &p, MeshOut &out);

} // namespace plant
} // namespace gpx
