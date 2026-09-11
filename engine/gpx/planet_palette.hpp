#pragma once
// Geekatplay TerraForge - the landscape palette, on the CPU.
//
// The twin of PL_PALETTE in studio/planet_shaders_common.cpp, written out a
// second time by hand and kept honest by the GPU agreement check
// (studio/planet_gpu_check.cpp, "verify_field_gpu"), exactly as
// gpx::planet::heightf is the twin of pl_height_w.
//
// It exists because the offline renderers need the ground's colour and have
// no GLSL to ask: a path-traced frame that does not use this paints the
// terrain a flat grey, which is not the picture the camera showed. Baking it
// here means the render and the viewport agree by construction rather than
// by two people writing the same constants twice.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpx {
namespace planet {

// mix / smoothstep, spelled out so the port reads against the GLSL line for
// line rather than through a helper that might not mean the same thing
inline float pal_mix(float a, float b, float t) { return a + (b - a) * t; }
inline float pal_smoothstep(float e0, float e1, float x) {
  const float d = e1 - e0;
  float t = d == 0.f ? (x < e0 ? 0.f : 1.f) : (x - e0) / d;
  t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
  return t * t * (3.f - 2.f * t);
}

// GLSL: uvec3 q = uvec3(ivec3(ip)) - the floats are already integral, and a
// negative one wraps into the unsigned range, which is the whole point of
// the hash. Everything below is 32-bit wraparound arithmetic.
inline float pal_hash(float ix, float iy, float iz, uint32_t seed) {
  const uint32_t qx = (uint32_t)(int32_t)ix;
  const uint32_t qy = (uint32_t)(int32_t)iy;
  const uint32_t qz = (uint32_t)(int32_t)iz;
  uint32_t h = qx * 374761393u + qy * 668265263u + qz * 2147483647u +
               seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (float)(h & 0xffffffu) / 16777215.f;
}

inline float pal_noise(float px, float py, float pz, uint32_t seed) {
  const float ix = std::floor(px), iy = std::floor(py), iz = std::floor(pz);
  float fx = px - ix, fy = py - iy, fz = pz - iz;
  fx = fx * fx * (3.f - 2.f * fx);
  fy = fy * fy * (3.f - 2.f * fy);
  fz = fz * fz * (3.f - 2.f * fz);
  const float c000 = pal_hash(ix, iy, iz, seed);
  const float c100 = pal_hash(ix + 1, iy, iz, seed);
  const float c010 = pal_hash(ix, iy + 1, iz, seed);
  const float c110 = pal_hash(ix + 1, iy + 1, iz, seed);
  const float c001 = pal_hash(ix, iy, iz + 1, seed);
  const float c101 = pal_hash(ix + 1, iy, iz + 1, seed);
  const float c011 = pal_hash(ix, iy + 1, iz + 1, seed);
  const float c111 = pal_hash(ix + 1, iy + 1, iz + 1, seed);
  const float x00 = pal_mix(c000, c100, fx), x10 = pal_mix(c010, c110, fx);
  const float x01 = pal_mix(c001, c101, fx), x11 = pal_mix(c011, c111, fx);
  return pal_mix(pal_mix(x00, x10, fy), pal_mix(x01, x11, fy), fz);
}

// The variation grain, on the ground and on a world seen whole. It is not a
// garnish: it picks between grass and meadow outright, so it is the colour.
inline float palette_var(float x, float z) {
  return pal_noise(x * 37.f, 0.37f * 37.f, z * 37.f, 0x5a17u);
}
inline float palette_var_dir(const float dir[3]) {
  return pal_noise(dir[0] * 40.f, dir[1] * 40.f, dir[2] * 40.f, 0x5a17u);
}

// t: altitude, 0 at the water and 1 at the top of the height range.
// slope: 0 flat, 1 vertical. lat: |latitude| / 90. wet: valley floors and
// lake beds, 0..1. snow_line: where the snow starts, in t. var: the grain.
inline void palette(float t, float slope, float lat, float wet,
                    float snow_line, float var, float out[3]) {
  const float sand[3]    = {0.60f, 0.53f, 0.40f};
  const float grass[3]   = {0.17f, 0.27f, 0.08f};
  const float meadow[3]  = {0.32f, 0.38f, 0.14f};
  const float forest[3]  = {0.07f, 0.15f, 0.05f};
  const float scrub[3]   = {0.35f, 0.31f, 0.19f};
  const float rock[3]    = {0.33f, 0.30f, 0.27f};
  const float cliff[3]   = {0.24f, 0.22f, 0.21f};
  const float hirock[3]  = {0.44f, 0.42f, 0.40f};
  const float snow[3]    = {0.90f, 0.92f, 0.95f};
  const float wetsoil[3] = {0.13f, 0.11f, 0.08f};
  t = std::clamp(t, 0.f, 1.f);
  slope = std::clamp(slope, 0.f, 1.f);
  float c[3];
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(grass[k], meadow[k], var);
  const float fw = pal_smoothstep(0.06f, 0.28f, t) *
                   (1.f - pal_smoothstep(0.42f, 0.68f, t)) *
                   (0.35f + 0.65f * var);
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(c[k], forest[k], fw);
  const float sw = pal_smoothstep(0.45f, 0.65f, t);
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(c[k], scrub[k], sw);
  const float hw = pal_smoothstep(0.65f, 0.85f, t);
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(c[k], hirock[k], hw);
  const float bw = pal_smoothstep(0.f, 0.015f, t);
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(sand[k], c[k], bw);
  const float ww = wet * 0.6f * (1.f - pal_smoothstep(0.5f, 0.7f, t));
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(c[k], wetsoil[k], ww);
  const float rw = pal_smoothstep(0.30f, 0.50f, slope);
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(c[k], rock[k], rw);
  const float cw = pal_smoothstep(0.55f, 0.80f, slope);
  for (int k = 0; k < 3; ++k) c[k] = pal_mix(c[k], cliff[k], cw);
  const float sl = snow_line - lat * lat * 0.6f + (var - 0.5f) * 0.06f;
  const float sn = pal_smoothstep(sl - 0.05f, sl + 0.05f, t) *
                   (1.f - pal_smoothstep(0.45f, 0.75f, slope));
  for (int k = 0; k < 3; ++k) out[k] = pal_mix(c[k], snow[k], sn);
}

} // namespace planet
} // namespace gpx
