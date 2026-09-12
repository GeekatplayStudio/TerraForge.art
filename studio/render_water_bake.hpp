#pragma once
// Geekatplay TerraForge - the sea the viewport draws, baked for the offline
// renderers (render_water_bake.cpp): one mesh about the camera, displaced by
// the same waves, with the roughness of the waves too small for it.
#include <string>

namespace studio {

struct RenderSettings;

struct BakedWater {
  bool ok = false;
  std::string obj;
  float roughness = 0.02f; // GGX alpha of the waves the mesh does not carry
};

// `eye` is the camera being rendered, world units; `extent` how far the
// ground reaches (tile units) - the sea reaches as far.
BakedWater render_bake_water(const RenderSettings &rs, const float eye[3],
                             const std::string &path, float extent);

} // namespace studio
