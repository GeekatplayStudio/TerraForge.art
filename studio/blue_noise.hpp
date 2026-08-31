// Geekatplay TerraForge — blue-noise dither pattern.
//
// Ray marching a volume at a fixed step produces visible depth slices, because
// adjacent pixels sample at exactly the same distances and a density boundary
// falling between two steps registers for all of them at once. Offsetting each
// ray's start by a fraction of a step breaks the bands into noise instead.
//
// Which noise matters. Ours was `fract(sin(dot(gl_FragCoord.xy, ...)))` —
// white noise, where nearby pixels can land on similar values, so the dither
// clumps into visible blobs. Blue noise has no low-frequency content by
// construction: neighbouring pixels are guaranteed to differ, so the pattern
// is even and the eye smooths it out. Same cost, better picture.
//
// Generated with Ulichney's void-and-cluster, which is deterministic given a
// seed, so the texture is identical on every machine and every run.
#pragma once
#include <cstdint>
#include <vector>

namespace studio {

// A square blue-noise pattern in [0,1), row-major, `n` x `n`.
// Sizes are small by nature (64 is the usual choice) and it is tiled.
std::vector<float> blue_noise_pattern(int n, uint32_t seed = 1u);

// The GL upload lives in cloud_noise.hpp, with the other generated noise
// textures — this header stays free of GL so the pattern can be tested
// without a context.

} // namespace studio
