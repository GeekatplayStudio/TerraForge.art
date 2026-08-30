#include "gpx/heightmap.hpp"
#include "gpx/parallel.hpp"
#include <limits>

namespace gpx {

void Heightmap::minmax(float &mn, float &mx) const {
  mn = std::numeric_limits<float>::max();
  mx = std::numeric_limits<float>::lowest();
  for (float f : v) {
    mn = std::min(mn, f);
    mx = std::max(mx, f);
  }
  if (v.empty()) mn = mx = 0.f;
}

void Heightmap::remap(float lo, float hi) {
  float mn, mx;
  minmax(mn, mx);
  float d = mx - mn;
  if (d < 1e-12f) {
    std::fill(v.begin(), v.end(), lo);
    return;
  }
  float s = (hi - lo) / d;
  parallel_index(v.size(), [&](size_t i0, size_t i1) {
    for (size_t i = i0; i < i1; ++i) v[i] = lo + (v[i] - mn) * s;
  });
}

void Heightmap::clamp01() {
  for (float &f : v) f = std::clamp(f, 0.f, 1.f);
}

Heightmap Heightmap::resampled(int nw, int nh) const {
  Heightmap out(nw, nh);
  if (empty()) return out;
  parallel_rows(nh, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < nw; ++x)
        out.at(x, y) = sample(x / float(nw - 1), y / float(nh - 1));
  });
  return out;
}

std::vector<uint8_t> TextureRGBA::to_u8() const {
  std::vector<uint8_t> out(v.size());
  for (size_t i = 0; i < v.size(); ++i)
    out[i] = (uint8_t)std::clamp(v[i] * 255.f + 0.5f, 0.f, 255.f);
  return out;
}

} // namespace gpx
