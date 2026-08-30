// Geekatplay Studio — hash-based coherent noise (no LUT allocations,
// world-coordinate based so results are resolution-independent).
#pragma once
#include <cmath>
#include <cstdint>

namespace gpx::noise {

inline uint32_t hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

inline uint32_t hash2(int32_t x, int32_t y, uint32_t seed) {
  return hash_u32((uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ seed);
}

inline float hash01(int32_t x, int32_t y, uint32_t seed) {
  return hash2(x, y, seed) * (1.f / 4294967295.f);
}

inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// gradient from hashed angle
inline void grad(int32_t x, int32_t y, uint32_t seed, float &gx, float &gy) {
  uint32_t h = hash2(x, y, seed);
  float a = h * (6.2831853f / 4294967296.f);
  gx = std::cos(a);
  gy = std::sin(a);
}

// Perlin gradient noise, output approx [-1, 1]
inline float perlin(float x, float y, uint32_t seed) {
  int32_t xi = (int32_t)std::floor(x), yi = (int32_t)std::floor(y);
  float xf = x - xi, yf = y - yi;
  float u = fade(xf), v = fade(yf);
  float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
  grad(xi, yi, seed, g00x, g00y);
  grad(xi + 1, yi, seed, g10x, g10y);
  grad(xi, yi + 1, seed, g01x, g01y);
  grad(xi + 1, yi + 1, seed, g11x, g11y);
  float d00 = g00x * xf + g00y * yf;
  float d10 = g10x * (xf - 1) + g10y * yf;
  float d01 = g01x * xf + g01y * (yf - 1);
  float d11 = g11x * (xf - 1) + g11y * (yf - 1);
  return 1.4142f * lerp(lerp(d00, d10, u), lerp(d01, d11, u), v);
}

struct FbmParams {
  int octaves = 8;
  float lacunarity = 2.f;
  float gain = 0.5f;
  float weight = 0.7f; // ridged/weighted amplitude feedback
};

inline float fbm(float x, float y, uint32_t seed, const FbmParams &p) {
  float sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < p.octaves; ++o) {
    sum += amp * perlin(x, y, seed + (uint32_t)o * 1013u);
    norm += amp;
    amp *= p.gain;
    x *= p.lacunarity;
    y *= p.lacunarity;
  }
  return norm > 0 ? sum / norm : 0;
}

inline float fbm_ridged(float x, float y, uint32_t seed, const FbmParams &p) {
  float sum = 0, amp = 0.5f, norm = 0, w = 1.f;
  for (int o = 0; o < p.octaves; ++o) {
    float n = perlin(x, y, seed + (uint32_t)o * 1013u);
    n = 1.f - std::fabs(n);
    n *= n * w;
    w = n * p.weight;
    if (w > 1) w = 1;
    if (w < 0) w = 0;
    sum += n * amp;
    norm += amp;
    amp *= p.gain;
    x *= p.lacunarity;
    y *= p.lacunarity;
  }
  return norm > 0 ? sum / norm * 2.f - 1.f : 0;
}

inline float fbm_billow(float x, float y, uint32_t seed, const FbmParams &p) {
  float sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < p.octaves; ++o) {
    sum += amp * (std::fabs(perlin(x, y, seed + (uint32_t)o * 1013u)) * 2.f - 1.f);
    norm += amp;
    amp *= p.gain;
    x *= p.lacunarity;
    y *= p.lacunarity;
  }
  return norm > 0 ? sum / norm : 0;
}

// Value noise fBm (blockier character than gradient noise)
inline float value_noise(float x, float y, uint32_t seed) {
  int32_t xi = (int32_t)std::floor(x), yi = (int32_t)std::floor(y);
  float u = fade(x - xi), v = fade(y - yi);
  float v00 = hash01(xi, yi, seed) * 2 - 1, v10 = hash01(xi + 1, yi, seed) * 2 - 1;
  float v01 = hash01(xi, yi + 1, seed) * 2 - 1,
        v11 = hash01(xi + 1, yi + 1, seed) * 2 - 1;
  return lerp(lerp(v00, v10, u), lerp(v01, v11, u), v);
}

inline float fbm_value(float x, float y, uint32_t seed, const FbmParams &p) {
  float sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < p.octaves; ++o) {
    sum += amp * value_noise(x, y, seed + (uint32_t)o * 1013u);
    norm += amp;
    amp *= p.gain;
    x *= p.lacunarity;
    y *= p.lacunarity;
  }
  return norm > 0 ? sum / norm : 0;
}

// "Swiss" turbulence: ridged fBm whose coordinates drift along the
// accumulated gradient — produces realistic eroded-looking ridge networks.
inline float fbm_swiss(float x, float y, uint32_t seed, const FbmParams &p,
                       float warp = 0.15f) {
  float sum = 0, amp = 1, norm = 0;
  float dx_sum = 0, dy_sum = 0;
  float freq = 1.f;
  const float e = 0.01f;
  for (int o = 0; o < p.octaves; ++o) {
    float px = x * freq + warp * dx_sum, py = y * freq + warp * dy_sum;
    uint32_t s = seed + (uint32_t)o * 1013u;
    float n = perlin(px, py, s);
    float r = 1.f - std::fabs(n);
    sum += amp * r;
    norm += amp;
    // numeric gradient of the ridge
    float gx = (1.f - std::fabs(perlin(px + e, py, s)) - r) / e;
    float gy = (1.f - std::fabs(perlin(px, py + e, s)) - r) / e;
    dx_sum += -gx * amp * 0.5f;
    dy_sum += -gy * amp * 0.5f;
    amp *= p.gain * std::clamp(r + 0.5f, 0.f, 1.f); // ridged amplitude feedback
    freq *= p.lacunarity;
  }
  return norm > 0 ? sum / norm * 2.f - 1.f : 0;
}

// Worley cellular: returns F1, F2 distances (cell coords scaled by freq)
inline void worley(float x, float y, uint32_t seed, float &f1, float &f2,
                   float jitter = 1.f) {
  int32_t xi = (int32_t)std::floor(x), yi = (int32_t)std::floor(y);
  f1 = f2 = 1e9f;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      int32_t cx = xi + dx, cy = yi + dy;
      float px = cx + jitter * hash01(cx, cy, seed);
      float py = cy + jitter * hash01(cx, cy, seed ^ 0xA341u);
      float ddx = px - x, ddy = py - y;
      float d = ddx * ddx + ddy * ddy;
      if (d < f1) {
        f2 = f1;
        f1 = d;
      } else if (d < f2) {
        f2 = d;
      }
    }
  f1 = std::sqrt(f1);
  f2 = std::sqrt(f2);
}

} // namespace gpx::noise
