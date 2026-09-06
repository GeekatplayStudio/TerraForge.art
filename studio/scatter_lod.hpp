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
  float scale = 1.f;      // the governor's multiplier on far_m and cull_m
};

// The share of a cell to draw at `dist_m`: 1 inside full, falling smoothly
// to min_keep at far, 0 past cull. Monotone non-increasing.
float scatter_keep(float dist_m, const LodParams &p);

// How much a survivor grows so the thinned crowd still covers the ground:
// sqrt(1/keep), capped so a far tree does not become a tower.
float scatter_grow(float keep);

// The mesh level for a distance: 0 full, 1 reduced, 2 coarse.
int scatter_lod_level(float dist_m, const LodParams &p);

// Distance from a point to an axis-aligned box (0 inside).
float aabb_distance(const float p[3], const float lo[3], const float hi[3]);

} // namespace studio
