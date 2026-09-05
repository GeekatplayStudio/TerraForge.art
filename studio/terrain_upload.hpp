#pragma once
#include "gpx/heightmap.hpp"
#include "planet_place.hpp"
#include <memory>
#include <cstdint>

namespace studio {
// An owned CPU result; no graph pointers and no GL handles cross threads.
struct TerrainUpload {
  std::shared_ptr<gpx::Heightmap> height;
  std::shared_ptr<const gpx::TextureRGBA> albedo;
  gpx::Heightmap picking;
  std::vector<float> bounds;
  PlaceResult placement;
  float mean = 0.f;
  uint64_t serial = 0, key = 0;
};
void renderer_set_terrain_prepared(TerrainUpload &upload);
} // namespace studio
