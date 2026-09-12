// Geekatplay TerraForge - coverage-preserving mipmaps (alpha_mips.hpp).
#include "alpha_mips.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

bool alpha_has_cut(const std::vector<uint8_t> &rgba, int w, int h) {
  if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * (size_t)h * 4) return false;
  bool over = false, under = false;
  for (size_t i = 0, n = (size_t)w * (size_t)h; i < n && !(over && under); ++i) {
    if (rgba[i * 4 + 3] >= 128) over = true;
    else under = true;
  }
  return over && under;
}

float alpha_coverage(const std::vector<uint8_t> &rgba, int w, int h, float scale) {
  const size_t n = (size_t)std::max(w, 0) * (size_t)std::max(h, 0);
  if (!n || rgba.size() < n * 4) return 0.f;
  size_t past = 0;
  for (size_t i = 0; i < n; ++i)
    if (rgba[i * 4 + 3] * scale >= 127.5f) ++past;
  return (float)past / (float)n;
}

std::vector<std::vector<uint8_t>> alpha_coverage_mips(const std::vector<uint8_t> &rgba, int w,
                                                      int h, std::vector<int> &sizes) {
  std::vector<std::vector<uint8_t>> out;
  sizes.clear();
  if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * (size_t)h * 4) return out;
  const float target = alpha_coverage(rgba, w, h);
  const std::vector<uint8_t> *src = &rgba;
  int sw = w, sh = h;
  while (sw > 1 || sh > 1) {
    const int dw = std::max(sw / 2, 1), dh = std::max(sh / 2, 1);
    std::vector<uint8_t> dst((size_t)dw * (size_t)dh * 4);
    for (int y = 0; y < dh; ++y)
      for (int x = 0; x < dw; ++x) {
        // the 2x2 texels under this one, clamped at an odd edge
        const int x0 = std::min(x * 2, sw - 1), x1 = std::min(x * 2 + 1, sw - 1);
        const int y0 = std::min(y * 2, sh - 1), y1 = std::min(y * 2 + 1, sh - 1);
        for (int c = 0; c < 4; ++c) {
          const int s = (*src)[((size_t)y0 * sw + x0) * 4 + c] + (*src)[((size_t)y0 * sw + x1) * 4 + c] +
                        (*src)[((size_t)y1 * sw + x0) * 4 + c] + (*src)[((size_t)y1 * sw + x1) * 4 + c];
          dst[((size_t)y * dw + x) * 4 + c] = (uint8_t)((s + 2) / 4);
        }
      }
    // Scale this level's alpha until its coverage is the base level's: more
    // coverage rises with the scale, so a bisection over it finds it.
    float lo = 0.f, hi = 8.f;
    for (int it = 0; it < 20; ++it) {
      const float mid = 0.5f * (lo + hi);
      if (alpha_coverage(dst, dw, dh, mid) < target) lo = mid;
      else hi = mid;
    }
    // Coverage is a staircase in the scale, so the two ends of the last step
    // are the only candidates; keep the nearer, and never round across it.
    // A level that would lose every leaf is worse than one with a few too
    // many, so an end with under half the share is not a candidate.
    const float cov_lo = alpha_coverage(dst, dw, dh, lo), cov_hi = alpha_coverage(dst, dw, dh, hi);
    const float scale = (cov_lo >= 0.5f * target &&
                         std::fabs(cov_lo - target) <= std::fabs(cov_hi - target))
                            ? lo
                            : hi;
    for (size_t i = 0, n = (size_t)dw * (size_t)dh; i < n; ++i) {
      const float a = dst[i * 4 + 3] * scale;
      // what passed the cut at this scale still passes once stored as a byte
      long b = std::lround(a);
      if (a >= 127.5f) b = std::max(b, 128L);
      else b = std::min(b, 127L);
      dst[i * 4 + 3] = (uint8_t)std::clamp(b, 0L, 255L);
    }
    out.push_back(std::move(dst));
    sizes.push_back(dw);
    sizes.push_back(dh);
    src = &out.back();
    sw = dw;
    sh = dh;
  }
  return out;
}

} // namespace studio
