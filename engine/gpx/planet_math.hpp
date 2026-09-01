// Geekatplay TerraForge — planetary surface math.
//
// A planet's relief is a sum of infinite procedural layers evaluated in 3D
// space on the unit sphere direction (the Vue manual is explicit about why:
// only a 3D-domain function stays continuous over the whole planet — any 2D
// parameterization pinches at the poles). The same function runs here on the
// CPU (tests, picking, altitude queries) and as a GLSL mirror in the
// renderer, so what you click is what you see.
//
// Everything is derived from the layer parameters and a seed: a planet costs
// no memory beyond its ~100-byte description, which is what makes an
// unlimited number of planets (each with unlimited surface detail) possible.
#pragma once
#include <cmath>
#include <cstdint>

namespace gpx::planet {

// one infinite procedural layer on a planet (or on the home ground plane)
struct Layer {
  uint32_t seed = 1;
  int type = 1;          // 0 rolling hills, 1 ridged mountains, 2 billow dunes
  float frequency = 3.f; // features per planet radius
  float amplitude = 1.f; // relative weight within the planet's relief budget
  int octaves = 6;       // detail depth for CPU evaluation (GPU picks its own)
  float coverage = 1.f;  // 0..1 fraction of the sphere the layer occupies
  float mask_scale = 1.5f; // how large the covered regions are
};

static const int MAX_LAYERS = 6;

// ---- 3D value noise (mirrored in GLSL as pl_hash/pl_vnoise/pl_fbm) --------
inline float pl_hash(float x, float y, float z, uint32_t seed) {
  // integer lattice hash; stable across platforms
  int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
  uint32_t h = (uint32_t)xi * 374761393u + (uint32_t)yi * 668265263u +
               (uint32_t)zi * 2147483647u + seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xffffff) / 16777215.f;
}

// The same lattice hash, before it is squeezed into 0..1. Cellular noise needs
// three independent numbers per cell and one hash carries enough entropy for
// all three, so it slices this rather than hashing three times.
inline uint32_t pl_hash_bits(int xi, int yi, int zi, uint32_t seed) {
  uint32_t h = (uint32_t)xi * 374761393u + (uint32_t)yi * 668265263u +
               (uint32_t)zi * 2147483647u + seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return h;
}

// ---- 3D cellular (Worley) noise -------------------------------------------
// Scatters one feature point per lattice cell and reports the distance to the
// nearest (f1) and second nearest (f2), plus a stable random value for the
// nearest cell (id). Those three cover what cellular noise is actually used
// for: f1 is bubbles and craters, f2-f1 draws the cell walls that read as
// cracked ground and basalt columns, and id paints flat plates.
//
// Mirrored in GLSL as gpxf_cell (engine/field_glsl.cpp). The two must agree —
// the CPU/GPU agreement check exists to keep them honest.
//
// metric: 0 Euclidean (round cells), 1 Manhattan (diamond), 2 Chebyshev (square).
inline void pl_cell(float x, float y, float z, uint32_t seed, float jitter,
                    int metric, float &f1, float &f2, float &id) {
  const int xi = (int)std::floor(x), yi = (int)std::floor(y),
            zi = (int)std::floor(z);
  const float fx = x - (float)xi, fy = y - (float)yi, fz = z - (float)zi;
  f1 = 1e9f;
  f2 = 1e9f;
  id = 0.f;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const uint32_t h = pl_hash_bits(xi + dx, yi + dy, zi + dz, seed);
        // three offsets out of one hash, from disjoint bit ranges
        const float ox = (float)(h & 0x3ffu) * (1.f / 1023.f);
        const float oy = (float)((h >> 10) & 0x3ffu) * (1.f / 1023.f);
        const float oz = (float)((h >> 20) & 0x3ffu) * (1.f / 1023.f);
        const float px = (float)dx + 0.5f + (ox - 0.5f) * jitter - fx;
        const float py = (float)dy + 0.5f + (oy - 0.5f) * jitter - fy;
        const float pz = (float)dz + 0.5f + (oz - 0.5f) * jitter - fz;
        float d;
        if (metric == 1)
          d = std::fabs(px) + std::fabs(py) + std::fabs(pz);
        else if (metric == 2)
          d = std::fmax(std::fabs(px), std::fmax(std::fabs(py), std::fabs(pz)));
        else
          d = std::sqrt(px * px + py * py + pz * pz);
        if (d < f1) {
          f2 = f1;
          f1 = d;
          id = (float)(h & 0xffffffu) * (1.f / 16777215.f);
        } else if (d < f2) {
          f2 = d;
        }
      }
}

inline float pl_fade(float t) { return t * t * (3.f - 2.f * t); }

inline float pl_vnoise(float x, float y, float z, uint32_t seed) {
  float fx = x - std::floor(x), fy = y - std::floor(y), fz = z - std::floor(z);
  float ux = pl_fade(fx), uy = pl_fade(fy), uz = pl_fade(fz);
  auto c = [&](int dx, int dy, int dz) {
    return pl_hash(std::floor(x) + dx, std::floor(y) + dy, std::floor(z) + dz,
                   seed);
  };
  float x00 = c(0, 0, 0) + (c(1, 0, 0) - c(0, 0, 0)) * ux;
  float x10 = c(0, 1, 0) + (c(1, 1, 0) - c(0, 1, 0)) * ux;
  float x01 = c(0, 0, 1) + (c(1, 0, 1) - c(0, 0, 1)) * ux;
  float x11 = c(0, 1, 1) + (c(1, 1, 1) - c(0, 1, 1)) * ux;
  float y0 = x00 + (x10 - x00) * uy;
  float y1 = x01 + (x11 - x01) * uy;
  return y0 + (y1 - y0) * uz;
}

// fBm in one of the three layer styles; returns roughly -0.5..0.5
inline float pl_fbm(float x, float y, float z, uint32_t seed, int octaves,
                    int type) {
  float sum = 0.f, amp = 1.f, norm = 0.f;
  float fx = x, fy = y, fz = z;
  for (int i = 0; i < octaves && i < 12; ++i) {
    float n = pl_vnoise(fx, fy, fz, seed + (uint32_t)i * 101u);
    if (type == 1) n = 1.f - std::fabs(n * 2.f - 1.f);       // ridged
    else if (type == 2) n = std::fabs(n * 2.f - 1.f);        // billow
    sum += n * amp;
    norm += amp;
    amp *= 0.5f;
    fx *= 2.03f; fy *= 2.03f; fz *= 2.03f; // irrational-ish: avoids banding
  }
  float v = norm > 0.f ? sum / norm : 0.f;
  if (type == 1) v = v * v; // sharpen ridges
  return v - 0.5f;
}

// smoothstep coverage mask: which part of the sphere this layer occupies
inline float pl_mask(float x, float y, float z, const Layer &L) {
  if (L.coverage >= 0.999f) return 1.f;
  if (L.coverage <= 0.001f) return 0.f;
  float m = pl_fbm(x * L.mask_scale, y * L.mask_scale, z * L.mask_scale,
                   L.seed ^ 0x9e3779b9u, 3, 0) + 0.5f; // 0..1
  float edge = 1.f - L.coverage;
  float t = (m - (edge - 0.12f)) / 0.24f;
  t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
  return t * t * (3.f - 2.f * t);
}

// Signed relief at a unit direction, as a fraction of the planet's relief
// budget (the renderer multiplies by radius * relief). `dir` need not be
// perfectly normalized.
inline float height(const float dir[3], const Layer *layers, int count,
                    int octave_cap = 12) {
  float total = 0.f, wsum = 0.f;
  for (int i = 0; i < count && i < MAX_LAYERS; ++i) {
    const Layer &L = layers[i];
    if (L.amplitude <= 0.f) continue;
    float m = pl_mask(dir[0], dir[1], dir[2], L);
    if (m <= 0.f) continue;
    int oct = L.octaves < octave_cap ? L.octaves : octave_cap;
    float h = pl_fbm(dir[0] * L.frequency, dir[1] * L.frequency,
                     dir[2] * L.frequency, L.seed, oct, L.type);
    total += h * L.amplitude * m;
    wsum += L.amplitude;
  }
  return wsum > 0.f ? total / wsum : 0.f;
}

} // namespace gpx::planet
