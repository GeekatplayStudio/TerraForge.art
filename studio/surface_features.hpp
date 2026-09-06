// Geekatplay TerraForge - the surface features that ride on the placed ground.
//
// The tile is placed on its planet by amplitude: a texel that departs from
// the tile's ground by less than `presence` is not a feature, and the
// planet shows through. A house's mould and a layer's stones are exactly
// that small, so they were swallowed. Both are applied here instead, after
// placement, on the ground the viewport shows: first the material's
// displacement (rocks, grass), then the imprint of the objects standing on
// it - with the same gpx::imprint_apply the graph node uses. The ground
// between the two steps is the "natural" surface a grounded object rests
// on (studio/imprint.cpp). This file is pure so the tests can run it.
#pragma once
#include "gpx/heightmap.hpp"
#include "gpx/imprint.hpp"
#include <memory>
#include <string>

namespace studio {

struct SurfaceFeatures {
  std::shared_ptr<const gpx::Heightmap> displacement; // the material's relief, heightmap units; may be null
  std::string footprints;                              // TerrainImprint's text; empty = none
  gpx::ImprintParams imprint;
  bool empty() const { return !displacement && footprints.empty(); }
};

// Apply the features to `ground` in place. `natural`, when given, receives
// the ground after the displacement and before the imprint.
void surface_features_apply(gpx::Heightmap &ground, const SurfaceFeatures &f, gpx::Heightmap *natural);

} // namespace studio
