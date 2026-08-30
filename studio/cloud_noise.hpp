// Geekatplay TerraForge — tiling 3D Perlin-Worley noise volumes for the
// volumetric cloud raymarcher (generated on the CPU at startup).
#pragma once

namespace studio {

// Builds and uploads two GL_TEXTURE_3D volumes:
//   shape  : 96^3 RGBA8  (R = Perlin-Worley base, GBA = Worley FBM octaves)
//   detail : 32^3 RGBA8  (Worley erosion octaves)
// Both tile seamlessly. Returns false if generation failed.
bool cloud_noise_build(unsigned &shape_tex, unsigned &detail_tex);

} // namespace studio
