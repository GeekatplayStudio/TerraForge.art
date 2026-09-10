// Geekatplay TerraForge — the nebula noise volume (space_noise.hpp).
#include "space_noise.hpp"
#include "gpx/parallel.hpp"
#include <glad/gl.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace studio {

namespace {

uint32_t hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float hash3f(int x, int y, int z, uint32_t seed) {
  const uint32_t h = hash_u32((uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^
                              (uint32_t)z * 0xC2B2AE3Du ^ seed);
  return h * (1.f / 4294967295.f);
}

int wrapi(int v, int n) { return ((v % n) + n) % n; }

// periodic value noise in [0,1), `cells` divisions across the unit cube
float value3(float x, float y, float z, int cells, uint32_t seed) {
  const float fx = x * cells, fy = y * cells, fz = z * cells;
  const int xi = (int)std::floor(fx), yi = (int)std::floor(fy), zi = (int)std::floor(fz);
  float tx = fx - xi, ty = fy - yi, tz = fz - zi;
  auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
  tx = fade(tx);
  ty = fade(ty);
  tz = fade(tz);
  float c[8];
  for (int i = 0; i < 8; ++i) {
    const int dx = i & 1, dy = (i >> 1) & 1, dz = (i >> 2) & 1;
    c[i] = hash3f(wrapi(xi + dx, cells), wrapi(yi + dy, cells), wrapi(zi + dz, cells), seed);
  }
  const float x00 = c[0] + (c[1] - c[0]) * tx, x10 = c[2] + (c[3] - c[2]) * tx;
  const float x01 = c[4] + (c[5] - c[4]) * tx, x11 = c[6] + (c[7] - c[6]) * tx;
  const float y0 = x00 + (x10 - x00) * ty, y1 = x01 + (x11 - x01) * ty;
  return y0 + (y1 - y0) * tz;
}

// periodic Worley, 1 - F1 so a high value is a dense core
float worley3(float x, float y, float z, int cells, uint32_t seed) {
  const float fx = x * cells, fy = y * cells, fz = z * cells;
  const int xi = (int)std::floor(fx), yi = (int)std::floor(fy), zi = (int)std::floor(fz);
  float best = 1e9f;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int cx = xi + dx, cy = yi + dy, cz = zi + dz;
        const int wx = wrapi(cx, cells), wy = wrapi(cy, cells), wz = wrapi(cz, cells);
        const float px = cx + hash3f(wx, wy, wz, seed);
        const float py = cy + hash3f(wx, wy, wz, seed ^ 0x51u);
        const float pz = cz + hash3f(wx, wy, wz, seed ^ 0xA3u);
        const float ddx = px - fx, ddy = py - fy, ddz = pz - fz;
        best = std::fmin(best, ddx * ddx + ddy * ddy + ddz * ddz);
      }
  return std::fmax(0.f, 1.f - std::sqrt(best));
}

float fbm(float x, float y, float z, int cells, int oct, uint32_t seed) {
  float s = 0.f, a = 0.5f, n = 0.f;
  int c = cells;
  for (int i = 0; i < oct; ++i) {
    s += value3(x, y, z, c, seed + (uint32_t)i * 7919u) * a;
    n += a;
    a *= 0.5f;
    c *= 2;
  }
  return n > 0.f ? s / n : 0.f;
}

// Ridged: each octave folded about its middle, so the crests become creases.
// This is what draws a nebula's filaments and the edges its shock fronts cut.
float ridged(float x, float y, float z, int cells, int oct, uint32_t seed) {
  float s = 0.f, a = 0.5f, n = 0.f;
  int c = cells;
  for (int i = 0; i < oct; ++i) {
    const float v = value3(x, y, z, c, seed + (uint32_t)i * 6151u);
    s += (1.f - std::fabs(v * 2.f - 1.f)) * a;
    n += a;
    a *= 0.55f;
    c *= 2;
  }
  return n > 0.f ? s / n : 0.f;
}

float worley_fbm(float x, float y, float z, int cells, int oct, uint32_t seed) {
  float s = 0.f, a = 0.6f, n = 0.f;
  int c = cells;
  for (int i = 0; i < oct; ++i) {
    s += worley3(x, y, z, c, seed + (uint32_t)i * 4783u) * a;
    n += a;
    a *= 0.45f;
    c *= 2;
  }
  return n > 0.f ? s / n : 0.f;
}

unsigned g_tex = 0;
bool g_tried = false;

} // namespace

unsigned space_noise_texture() {
  if (g_tried) return g_tex;
  g_tried = true;
  const int N = 96;
  std::vector<uint16_t> v((size_t)N * N * N * 4);
  gpx::parallel_rows(N, [&](int z0, int z1) {
    for (int z = z0; z < z1; ++z)
      for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
          const float u = x / float(N), w = y / float(N), t = z / float(N);
          const float billow = fbm(u, w, t, 3, 5, 1301u);
          const float ridge = ridged(u, w, t, 4, 5, 2477u);
          const float cell = worley_fbm(u, w, t, 4, 3, 3271u);
          const float fine = fbm(u, w, t, 8, 4, 5431u);
          const size_t i = (((size_t)z * N + y) * N + x) * 4;
          auto q = [](float f) {
            return (uint16_t)(std::fmin(std::fmax(f, 0.f), 1.f) * 65535.f);
          };
          v[i + 0] = q(billow);
          v[i + 1] = q(ridge);
          v[i + 2] = q(cell);
          v[i + 3] = q(fine);
        }
  });
  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex) return 0;
  glBindTexture(GL_TEXTURE_3D, tex);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16, N, N, N, 0, GL_RGBA, GL_UNSIGNED_SHORT, v.data());
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  glBindTexture(GL_TEXTURE_3D, 0);
  g_tex = tex;
  return g_tex;
}

void space_noise_release() {
  if (g_tex) glDeleteTextures(1, &g_tex);
  g_tex = 0;
  g_tried = false;
}

} // namespace studio
