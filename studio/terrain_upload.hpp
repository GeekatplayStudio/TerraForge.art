#pragma once
#include "gpx/heightmap.hpp"
#include "planet_place.hpp"
#include <memory>
#include <cstdint>

namespace gpx { struct Node; }
namespace studio {
// An owned CPU result; no graph pointers and no GL handles cross threads.
struct TerrainUpload {
  std::shared_ptr<gpx::Heightmap> height;
  std::shared_ptr<const gpx::TextureRGBA> albedo;
  gpx::Heightmap picking;
  std::vector<float> bounds;
  PlaceResult placement;
  // the placed ground before the objects' imprint (after the material's
  // relief): what a grounded object's base rests on
  std::shared_ptr<gpx::Heightmap> natural;
  float mean = 0.f;
  uint64_t serial = 0, key = 0;
};
void renderer_set_terrain_prepared(TerrainUpload &upload);

// The placement settings the tiles share (app_upload.cpp): the render
// settings, plus the blend mask fed into `out` (or the first Terrain
// Output when null). And the key that says they changed.
struct App;
// `object` is the Terrain object whose transform the tile stands in; -1
// means the first Terrain object (tile 0).
PlaceSettings app_place_settings(App &a, gpx::Node *out, int object);
// The placement key with that object's transform folded in.
uint64_t app_placement_key_for(int object);
uint64_t app_placement_key();

// Every tile after the first (app_upload_tiles.cpp).
void extra_tiles_prepare(App &a);   // under the graph lock, after terrain_tiles_bind
void extra_tiles_service(App &a);   // main thread, every frame
void extra_tiles_shutdown();
} // namespace studio
