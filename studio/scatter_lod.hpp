// Geekatplay TerraForge - level of detail for scattered instances, without
// GL, so the numbers can be tested.
//
// A population is bucketed into a grid of cells over the tile when it is
// rebuilt; inside a cell the instances are sorted by their LOD key (a
// uniform hash per instance), so any prefix of a cell is a uniform
// subsample of it. Each frame a cell's distance from the camera picks a
// keep fraction, and the pass draws that prefix: near cells every
// instance, far cells a thinned crowd grown slightly to keep the coverage,
// past the cull distance nothing. The far crowd is what "reduce quality
// far away" means for grass and rocks: fewer, not smaller.
#pragma once
#include <cstdint>
#include <vector>

namespace studio {

struct InstanceCell {
  int first = 0, count = 0; // instances, into the interleaved stream
  float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0}; // world bounds of the copies' feet
};

// Reorder `inst` (floats per copy = `stride`; x,y,z at 0..2, LOD key at
// `key_at`) cell-major over a `grid` x `grid` lattice of the unit tile in xz,
// each cell sorted by key. Cells with no instances are omitted.
void scatter_bucket(std::vector<float> &inst, int stride, int key_at, int grid,
                    std::vector<InstanceCell> &cells);

struct LodParams {
  float full_m = 150.f;   // every instance inside this
  float far_m = 1500.f;   // min_keep reached here
  float cull_m = 6000.f;  // nothing beyond
  float min_keep = 0.15f; // the far crowd's share
  float billboard_m = 2500.f; // past this a copy is a flat card, not geometry
  float scale = 1.f;      // the governor's multiplier on the distances
};

// The share of a cell to draw at `dist_m`: 1 inside full, falling smoothly
// to min_keep at far, 0 past cull. Monotone non-increasing.
float scatter_keep(float dist_m, const LodParams &p);

// How much a survivor grows so the thinned crowd still covers the ground:
// sqrt(1/keep), capped so a far tree does not become a tower.
float scatter_grow(float keep);

// The level for a distance: 0 the mesh itself, 1 reduced, 2 coarse,
// 3 a camera-facing card baked from the mesh (Vue's ladder ends the same
// way - smooth, flat, box, billboard, none).
inline constexpr int SCATTER_LOD_BILLBOARD = 3;
int scatter_lod_level(float dist_m, const LodParams &p);

// Distance from a point to an axis-aligned box (0 inside).
float aabb_distance(const float p[3], const float lo[3], const float hi[3]);

// ------------------------------------------------- cells around the camera
// A population with no tile (a planet, an infinite terrain) is generated in
// pieces: the cells of a world-anchored lattice that are near enough to the
// camera to be worth having. Which cells those are depends on the camera;
// what is *in* a cell never does (engine/gpx/scatter.hpp), so a cell
// re-entered from another direction holds what it held before.
struct WorldCell {
  long long x = 0, z = 0;
  float dist = 0.f; // from the camera to the cell's nearest edge, in metres
};

// Cells whose nearest edge is within `radius_m` of (eye_x, eye_z), nearest
// first, at most `budget` of them. Deterministic: ties break on the cell
// index, so the same camera always asks for the same cells in the same
// order and a budget cuts the same tail.
void visible_cells(float eye_x, float eye_z, float radius_m, float cell_m,
                   size_t budget, std::vector<WorldCell> &out);

} // namespace studio
