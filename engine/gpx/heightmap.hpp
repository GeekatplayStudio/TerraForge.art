// Geekatplay Studio — core heightfield types
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gpx {

struct Heightmap {
  int w = 0, h = 0;
  std::vector<float> v;

  Heightmap() = default;
  Heightmap(int w_, int h_, float fill = 0.f) : w(w_), h(h_), v((size_t)w_ * h_, fill) {}

  bool empty() const { return v.empty(); }
  size_t size() const { return v.size(); }
  float &at(int x, int y) { return v[(size_t)y * w + x]; }
  float at(int x, int y) const { return v[(size_t)y * w + x]; }

  // clamped access
  float atc(int x, int y) const {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return v[(size_t)y * w + x];
  }

  // bilinear sample, uv in [0,1]
  float sample(float u, float t) const {
    float fx = u * (w - 1), fy = t * (h - 1);
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float ax = fx - x0, ay = fy - y0;
    float v00 = atc(x0, y0), v10 = atc(x0 + 1, y0);
    float v01 = atc(x0, y0 + 1), v11 = atc(x0 + 1, y0 + 1);
    return (v00 * (1 - ax) + v10 * ax) * (1 - ay) + (v01 * (1 - ax) + v11 * ax) * ay;
  }

  void minmax(float &mn, float &mx) const;
  void remap(float lo = 0.f, float hi = 1.f);
  void clamp01();
  Heightmap resampled(int nw, int nh) const;

  // central-difference gradient (dx, dy) in height units per texel
  void gradient_at(int x, int y, float &dx, float &dy) const {
    dx = (atc(x + 1, y) - atc(x - 1, y)) * 0.5f;
    dy = (atc(x, y + 1) - atc(x, y - 1)) * 0.5f;
  }
};

struct RGBA {
  uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct TextureRGBA {
  int w = 0, h = 0;
  std::vector<float> v; // rgba interleaved, 0..1

  TextureRGBA() = default;
  TextureRGBA(int w_, int h_) : w(w_), h(h_), v((size_t)w_ * h_ * 4, 0.f) {}
  bool empty() const { return v.empty(); }
  float *px(int x, int y) { return &v[((size_t)y * w + x) * 4]; }
  const float *px(int x, int y) const { return &v[((size_t)y * w + x) * 4]; }
  std::vector<uint8_t> to_u8() const;
};

} // namespace gpx
