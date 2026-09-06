// Geekatplay TerraForge - moulding ground to the objects standing on it.
//
// The TerrainImprint node does this to the graph's heightmap; the studio
// does the same to the ground the viewport shows, after the tile has been
// placed on its planet - one function, so the two cannot drift. Footprints
// are text, one per line: "base sink margin blend n x0 z0 x1 z1 ..." (see
// engine/nodes/nodes_imprint.cpp).
#pragma once
#include "gpx/heightmap.hpp"
#include <string>

namespace gpx {

struct ImprintParams {
  float width = 1.5f;      // blend, as a multiple of the footprint's radius, when a footprint sets none
  float smoothness = 0.5f; // 0 a firm shoulder .. 1 a long tail
  float retain = 0.35f;    // how much small relief survives in the blend
  float flatten = 1.f;     // how flat the ground is under the footprint
  float strength = 1.f;
};

// Mould `ground` in place. `mask`, when given, receives how much each texel
// was moved (0..1). Pure and deterministic; an unparsable line is skipped.
void imprint_apply(Heightmap &ground, const std::string &footprints, const ImprintParams &p,
                   Heightmap *mask);

// Distance from every texel to the nearest footprint's wall, in tile
// fractions, 0 inside and clamped at 1; a 128x128 raster (all 1 with no
// footprints). What a population reads to keep clear of the objects.
void imprint_distance(const std::string &footprints, Heightmap &out);

} // namespace gpx
