// Geekatplay TerraForge — the noise volume a nebula is made of.
//
// The nebula shader marches a ray through a cloud of gas and dust
// (shaders_space_neb.cpp), and sampling a fractal by hand at every step
// costs dozens of hashes. One tiling 3D texture holds four fields instead,
// so a step is four filtered texture reads:
//
//   R  billow    value-noise fbm, the body of the cloud
//   G  ridged    1-|2n-1| fbm, the filaments and shock fronts
//   B  cellular  Worley fbm, the clumps dust gathers in
//   A  fine      a higher-frequency fbm for the last of the detail
//
// It tiles, so the shader warps its lookups rather than sampling far out of
// the unit cube. Built once, on the first frame that needs it, on every
// core; 96^3 RGBA16 is 7 MB and no scene ever needs a second one.
#pragma once

namespace studio {

// The volume, built on the first call. 0 if it could not be made.
unsigned space_noise_texture();
// Free it (context teardown).
void space_noise_release();

} // namespace studio
