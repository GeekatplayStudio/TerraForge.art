// Geekatplay TerraForge - placing the terrain tile on its planet.
//
// The home planet's surface is procedural and endless (root-level
// InfiniteSurface layers, gpx/planet_math.hpp); the terrain tile is a
// heightmap the graph produced. Before the tile is uploaded it is composited
// onto that surface so the two read as one landscape:
//
//   - where the tile is flat at its own ground level the planet shows
//     through unchanged, so an empty scene is already a landscape and a
//     lone mountain stamped into a flat tile sits on real ground;
//   - where the tile has a feature - anything that rises or sinks from its
//     ground level - the planet underneath is levelled to its local mean
//     ("flatten") and the feature is placed on top of that, with the tile's
//     ground settled to the planet's own ground level;
//   - the footprint of a feature is feathered outwards, and the tile's own
//     border is feathered too, so nothing ever ends in a step;
//   - a feature that sinks below ground is a basin, and the water plane
//     fills it wherever it reaches below the water level.
//
// The planet relief is evaluated with the same gpx::planet code the
// surround shader mirrors, so the tile edge and the surround meet exactly.
// It costs a few hundred thousand fractal evaluations, so it is cached per
// layer stack and resolution; the per-evaluation part is one pass of mixes.
#pragma once
#include "gpx/heightmap.hpp"
#include "gpx/planet_math.hpp"
#include <vector>

namespace studio {

struct PlaceSettings {
  bool enabled = true;
  float edge = 0.10f;      // feather width, fraction of the tile side
  float flatten = 1.f;     // 1 levels the planet under a feature, 0 adds on top
  float presence = 0.04f;  // relief (heightmap units) at which a feature counts
  // The planet's ground level, heightmap units: the level its relief is
  // built on, and the level a tile's own ground is settled to. It is the
  // planet's, not the tile's - a normalised mountain whose rim happens to sit
  // at 0.6 must not lift the whole world 600 m into the snow.
  float ground = 0.14f;
};

// What the compositing decided, for the Properties panel and for tests.
struct PlaceResult {
  float ground = 0.f;      // the level the surround builds on (heightmap units)
  float tile_ground = 0.f; // the tile's own ground level, settled to `ground`
  float coverage = 0.f;    // fraction of the tile that is feature (0..1)
  bool placed = false;     // false: the tile was passed through untouched
};

// The planet relief under the tile, in heightmap units relative to `ground`
// (the same 1.2 x relief the surround shader applies), at a detail budget
// suited to the map's resolution. `smooth` gets the broad shape only. Pure.
void planet_relief_under_tile(const std::vector<gpx::planet::Layer> &layers,
                              int w, int h, std::vector<float> &relief,
                              std::vector<float> &smooth);

// Composite `tile` onto the planet described by `layers`. Returns the map to
// upload; with placement off or no layers, a copy of the tile. Deterministic:
// the same inputs give the same bits.
gpx::Heightmap planet_place_tile(const gpx::Heightmap &tile,
                                 const std::vector<gpx::planet::Layer> &layers,
                                 const PlaceSettings &s, PlaceResult *out);

// The last result the app uploaded, for the Properties panel.
const PlaceResult &planet_place_last();
void planet_place_set_last(const PlaceResult &r);

} // namespace studio
