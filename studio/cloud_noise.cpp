#include "cloud_noise.hpp"
#include "gpx/parallel.hpp"
#include <glad/gl.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace studio {

static inline uint32_t hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static inline float hash3f(int x, int y, int z, uint32_t seed) {
  uint32_t h = hash_u32((uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^
                        (uint32_t)z * 0xC2B2AE3Du ^ seed);
  return h * (1.f / 4294967295.f);
}

static inline int wrapi(int v, int n) { return ((v % n) + n) % n; }

// periodic value noise in [0,1); `cells` grid divisions across the volume
static float value3(float x, float y, float z, int cells, uint32_t seed) {
  float fx = x * cells, fy = y * cells, fz = z * cells;
  int xi = (int)std::floor(fx), yi = (int)std::floor(fy), zi = (int)std::floor(fz);
  float tx = fx - xi, ty = fy - yi, tz = fz - zi;
  auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
  tx = fade(tx); ty = fade(ty); tz = fade(tz);
  float c[8];
  for (int i = 0; i < 8; ++i) {
    int dx = i & 1, dy = (i >> 1) & 1, dz = (i >> 2) & 1;
    c[i] = hash3f(wrapi(xi + dx, cells), wrapi(yi + dy, cells),
                  wrapi(zi + dz, cells), seed);
  }
  float x00 = c[0] + (c[1] - c[0]) * tx;
  float x10 = c[2] + (c[3] - c[2]) * tx;
  float x01 = c[4] + (c[5] - c[4]) * tx;
  float x11 = c[6] + (c[7] - c[6]) * tx;
  float y0 = x00 + (x10 - x00) * ty;
  float y1 = x01 + (x11 - x01) * ty;
  return y0 + (y1 - y0) * tz;
}

// periodic Worley (cellular) — returns 1-F1 so high values = dense cores
static float worley3(float x, float y, float z, int cells, uint32_t seed) {
  float fx = x * cells, fy = y * cells, fz = z * cells;
  int xi = (int)std::floor(fx), yi = (int)std::floor(fy), zi = (int)std::floor(fz);
  float best = 1e9f;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        int cx = xi + dx, cy = yi + dy, cz = zi + dz;
        int wx = wrapi(cx, cells), wy = wrapi(cy, cells), wz = wrapi(cz, cells);
        float px = cx + hash3f(wx, wy, wz, seed);
        float py = cy + hash3f(wx, wy, wz, seed ^ 0x51u);
        float pz = cz + hash3f(wx, wy, wz, seed ^ 0xA7u);
        float ddx = px - fx, ddy = py - fy, ddz = pz - fz;
        float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
        if (d2 < best) best = d2;
      }
  float d = std::sqrt(best);
  return 1.f - (d > 1.f ? 1.f : d);
}

static float worley_fbm(float x, float y, float z, int base, uint32_t seed) {
  return worley3(x, y, z, base, seed) * 0.625f +
         worley3(x, y, z, base * 2, seed + 17u) * 0.25f +
         worley3(x, y, z, base * 4, seed + 41u) * 0.125f;
}

static float perlin_fbm(float x, float y, float z, int base, uint32_t seed) {
  float sum = 0, amp = 0.5f, norm = 0;
  int cells = base;
  for (int o = 0; o < 4; ++o) {
    sum += value3(x, y, z, cells, seed + (uint32_t)o * 131u) * amp;
    norm += amp;
    amp *= 0.5f;
    cells *= 2;
  }
  return sum / norm;
}

static unsigned upload3d(const std::vector<uint8_t> &data, int n) {
  unsigned tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_3D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, n, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               data.data());
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  glBindTexture(GL_TEXTURE_3D, 0);
  return tex;
}

bool cloud_noise_build(unsigned &shape_tex, unsigned &detail_tex) {
  const int NS = 96, ND = 32;
  std::vector<uint8_t> shape((size_t)NS * NS * NS * 4);
  std::vector<uint8_t> detail((size_t)ND * ND * ND * 4);

  gpx::parallel_rows(NS, [&](int z0, int z1) {
    for (int z = z0; z < z1; ++z)
      for (int y = 0; y < NS; ++y)
        for (int x = 0; x < NS; ++x) {
          float u = x / float(NS), v = y / float(NS), w = z / float(NS);
          // R holds the raw Perlin FBM; the shader combines it with the Worley
          // octaves (GBA) into the Perlin-Worley shape, so do not pre-remap it
          float p = perlin_fbm(u, v, w, 4, 7u);
          float w1 = worley_fbm(u, v, w, 4, 3u);
          float w2 = worley_fbm(u, v, w, 8, 5u);
          float w3 = worley_fbm(u, v, w, 14, 9u);
          size_t i = (((size_t)z * NS + y) * NS + x) * 4;
          shape[i + 0] = (uint8_t)(std::fmin(std::fmax(p, 0.f), 1.f) * 255.f);
          shape[i + 1] = (uint8_t)(w1 * 255.f);
          shape[i + 2] = (uint8_t)(w2 * 255.f);
          shape[i + 3] = (uint8_t)(w3 * 255.f);
        }
  });

  gpx::parallel_rows(ND, [&](int z0, int z1) {
    for (int z = z0; z < z1; ++z)
      for (int y = 0; y < ND; ++y)
        for (int x = 0; x < ND; ++x) {
          float u = x / float(ND), v = y / float(ND), w = z / float(ND);
          float d1 = worley_fbm(u, v, w, 4, 23u);
          float d2 = worley_fbm(u, v, w, 8, 29u);
          float d3 = worley_fbm(u, v, w, 12, 31u);
          size_t i = (((size_t)z * ND + y) * ND + x) * 4;
          detail[i + 0] = (uint8_t)(d1 * 255.f);
          detail[i + 1] = (uint8_t)(d2 * 255.f);
          detail[i + 2] = (uint8_t)(d3 * 255.f);
          detail[i + 3] = 255;
        }
  });

  shape_tex = upload3d(shape, NS);
  detail_tex = upload3d(detail, ND);
  return shape_tex != 0 && detail_tex != 0;
}

} // namespace studio
