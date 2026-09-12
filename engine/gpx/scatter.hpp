// Geekatplay TerraForge - the distribution engine behind every population.
//
// Vue's EcoSystem, read as mechanism (docs_private/ECOSYSTEM_DESIGN.md):
// density is a rate over an area modulated by functions of the ground;
// which species stands where is a separate decision from whether anything
// stands there; each instance's size, turn, lean and tint is a distribution
// drawn per instance; and a layer above can be attracted to or repelled
// from the instances of the layer below it - primroses around the trees,
// small stones around the boulder, grass everywhere but under the canopy.
//
// Five stages, in this order, and the order is what makes a population
// stable under editing:
//
//   1 CANDIDATES  a lattice of cells; hashed candidates per cell, each with
//                 a stable id = hash(seed, cell, index). No mask reaches
//                 this stage, so changing a mask cannot move a survivor.
//   2 FILTER      presence (mask, altitude, slope, orientation), slope
//                 influence, decay near objects; keep if u(id) < p * rate.
//                 Raising the density only adds points; it never reshuffles.
//   3 INTERACT    affinity with / repulsion from the layer below, then
//                 overlap avoidance in id order.
//   4 SPECIES     by relative weights, or a driving mask cut into intervals.
//   5 TRANSFORM   size, per-axis variation, rotation, direction from the
//                 surface, offset, tint, phase; shrink and lean where the
//                 population is thin.
//
// Everything is a pure function of (parameters, id, ground). No RNG state,
// no thread count, no time reaches a decision. Same inputs, same forest.
#pragma once
#include "gpx/heightmap.hpp"
#include "gpx/points.hpp"
#include <cstdint>
#include <functional>

namespace gpx::scatter {

// ---------------------------------------------------------------- hashing
uint64_t mix64(uint64_t x);
uint64_t hash64(uint64_t a, uint64_t b, uint64_t c, uint64_t d);
// A 0..1 draw from an id and a channel. Channels keep the draws of one
// instance independent: `unit(id, 3)` is not `unit(id, 4)`.
float unit(uint64_t id, uint32_t channel);

// ------------------------------------------------------------- candidates
struct CandidateParams {
  uint32_t seed = 0;
  float spacing = 0.01f;   // cell size, tile units; also the spacing floor
  int per_cell = 4;        // candidates per cell (1..64)
  int mode = 0;            // 0 jittered, 1 uniform in cell, 2 regular grid
  float clump_amount = 0.f; // 0 none, 1 every candidate pulled to a clump
  float clump_size = 0.05f; // clump cell size, tile units
};
// Emits x, y, id (and the full default channel set). Cell-major order.
void candidates(const CandidateParams &p, PointCloud &out);
// The lattice size the params imply (cells per side).
int lattice_n(const CandidateParams &p);

// One cell of a lattice anchored in the WORLD, not in a tile: the ground a
// planet or an infinite terrain offers has no 0..1 domain to divide, so the
// cell index itself is the coordinate. `cell_x`/`cell_z` index a lattice of
// `p.spacing`-sized cells whose origin is the world origin; the points come
// back in the same units the spacing is given in.
//
// A cell's instances depend on nothing but (seed, cell index, candidate
// index), so a cell evaluated when the camera is 10 m away holds exactly
// the instances it held when the camera was 10 km away and the cell was
// last visited. That is what lets a population be generated on demand, in
// pieces, and never shimmer as the pieces are re-entered.
void candidates_cell(const CandidateParams &p, long long cell_x, long long cell_z,
                     PointCloud &out);

// ------------------------------------------------------------- presence
struct Presence {
  const Heightmap *mask = nullptr; // 0..1, multiplies; null = everywhere
  bool invert_mask = false;
  float threshold = 0.f;           // presence below this places nothing
  const Heightmap *terrain = nullptr; // for altitude / slope / orientation
  // A ground with no tile to sample: a planet's surface, an infinite
  // terrain. Takes a position in the same units the candidates are in and
  // returns a height in those units; when set it is used instead of
  // `terrain`, and the slope comes from differences `step` apart. The
  // altitude band is then read as absolute (mode 0 has no range to
  // normalise against), so `alt_lo`/`alt_hi` are heights.
  std::function<float(float, float)> ground;
  float step = 1.f;
  bool use_altitude = false;
  int altitude_mode = 0;           // 0 by terrain range, 1 absolute, 2 above sea
  float alt_lo = 0.f, alt_hi = 1.f, alt_fuzz = 0.08f, sea = 0.f;
  bool use_slope = false;
  float slope_lo = 0.f, slope_hi = 30.f, slope_fuzz = 6.f; // degrees
  // world height of one heightmap unit as a fraction of the tile's width,
  // so a slope in degrees is the slope the viewport shows (1 = the
  // material layer's convention)
  float height_scale = 1.f;
  bool use_orientation = false;
  float orient = 0.f, orient_width = 60.f, orient_fuzz = 20.f; // degrees
  float slope_influence = 0.f;     // 1 = sparser on steep ground (Vue 6)
  const Heightmap *distance = nullptr; // 0 at objects .. 1 far (decay)
  float decay_influence = 0.f;     // 0 none, 1 empty right at the objects
  float decay_falloff = 0.f;       // 0 linear; >0 sudden, <0 gentle
  float decay_reach = 0.1f;        // the distance (0..1) where decay ends
};
// Presence at a tile position, 0..1.
float presence_at(const Presence &p, float u, float v);
// Stage 2: keep a candidate when unit(id, 1) < presence * rate. `rate` is
// the accepted fraction wanted at full presence (target / candidates).
void filter(PointCloud &pc, const Presence &p, float rate);

// ------------------------------------------------------------ interaction
struct Interaction {
  const PointCloud *below = nullptr; // the layer beneath, or null
  float affinity = 0.f;        // -1..1: cling to / stay away from `below`
  float affinity_radius = 0.05f; // tile units
  float repulsion = 0.f;       // -1..1: a sudden void around / only within
  float repulsion_radius = 0.02f;
  bool avoid_overlap = false;  // no two instances closer than their radii
  float overlap_scale = 1.f;   // multiplies every radius in that test
};
// Stage 3. Order-independent: the overlap pass walks candidates by id.
void interact(PointCloud &pc, const Interaction &it);

// ------------------------------------------------------------- transform
struct Transform {
  int species_count = 1;       // 1..8
  float weights[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  const Heightmap *driver = nullptr; // when set, its value picks the species
  float species_scale[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  // Each kind's own size variation and its own lean with the slope, because
  // a stand of boulders is all sizes and lies on the hill while the saplings
  // among them are much of a muchness and stand up out of it. 0 variation and
  // a negative lean mean "take the layer's", which is what every population
  // did before they existed.
  float species_variation[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  float species_lean[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  float scale = 1.f;           // overall size multiplier
  float variation = 0.f;       // 1 = half to twice the size
  float keep_proportions = 1.f; // 1 uniform, 0 axes independent
  float direction = 0.f;       // 0 vertical, 1 perpendicular to the ground
  int rotation = 0;            // 0 up axis, 1 none, 2 driven by the driver
  float rotation_max = 1.f;    // fraction of a full turn
  float offset = 0.f;          // lift, tile units
  float color_variation = 0.f; // 0..1 tint spread
  float phase_range = 1.f;     // animation phase spread, seconds
  float radius = 0.005f;       // footprint radius at scale 1, tile units
  float shrink = 0.f;          // shrink where the population is thin
  float shrink_radius = 0.03f; // "thin" is measured inside this radius
  float lean = 0.f;            // lean out where thin
};
// Stages 4 and 5.
void transform(PointCloud &pc, const Transform &t);

// ------------------------------------------------------------- utilities
// Neighbour count within `radius` of each point (self excluded).
void neighbour_counts(const PointCloud &pc, float radius, std::vector<int> &out);
// Nearest distance from each point of `pc` to any point of `other`, capped
// at `reach`; the other cloud's radius channel is subtracted when present.
void nearest_distance(const PointCloud &pc, const PointCloud &other, float reach,
                      std::vector<float> &out);

} // namespace gpx::scatter
