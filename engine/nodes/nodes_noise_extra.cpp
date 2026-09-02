// Geekatplay Studio — wavelet-style band-limited noise (after Cook & DeRose):
// a white-noise tile has its low frequencies subtracted (downsample, upsample,
// difference), leaving one clean band; octaves sample that band at doubling
// frequencies. Because each band carries almost no energy outside its octave,
// the sum stays crisp under minification where Perlin fBm goes mushy.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

namespace {

constexpr int WT = 128; // tile side; wraps

// build the band-limited tile for a seed: R - up(down(R))
std::vector<float> wavelet_tile(uint32_t seed) {
  std::vector<float> r((size_t)WT * WT);
  for (int y = 0; y < WT; ++y)
    for (int x = 0; x < WT; ++x)
      r[(size_t)y * WT + x] =
          (planet::pl_hash_bits(x, y, 0, seed) & 0xffffffu) / 8388607.5f - 1.f;
  // half-res box downsample (wrapping), then bilinear upsample
  const int Hh = WT / 2;
  std::vector<float> dn((size_t)Hh * Hh);
  for (int y = 0; y < Hh; ++y)
    for (int x = 0; x < Hh; ++x) {
      int x0 = 2 * x, y0 = 2 * y;
      dn[(size_t)y * Hh + x] =
          0.25f * (r[(size_t)y0 * WT + x0] + r[(size_t)y0 * WT + (x0 + 1) % WT] +
                   r[(size_t)((y0 + 1) % WT) * WT + x0] +
                   r[(size_t)((y0 + 1) % WT) * WT + (x0 + 1) % WT]);
    }
  std::vector<float> tile((size_t)WT * WT);
  for (int y = 0; y < WT; ++y)
    for (int x = 0; x < WT; ++x) {
      float fx = x * 0.5f, fy = y * 0.5f;
      int ix = (int)fx % Hh, iy = (int)fy % Hh;
      int jx = (ix + 1) % Hh, jy = (iy + 1) % Hh;
      float ux = fx - (int)fx, uy = fy - (int)fy;
      float up = dn[(size_t)iy * Hh + ix] * (1 - ux) * (1 - uy) +
                 dn[(size_t)iy * Hh + jx] * ux * (1 - uy) +
                 dn[(size_t)jy * Hh + ix] * (1 - ux) * uy +
                 dn[(size_t)jy * Hh + jx] * ux * uy;
      tile[(size_t)y * WT + x] = r[(size_t)y * WT + x] - up;
    }
  return tile;
}

float tile_sample(const std::vector<float> &t, float x, float y) {
  x -= std::floor(x / WT) * WT;
  y -= std::floor(y / WT) * WT;
  int ix = (int)x % WT, iy = (int)y % WT;
  int jx = (ix + 1) % WT, jy = (iy + 1) % WT;
  float ux = x - (int)x, uy = y - (int)y;
  return t[(size_t)iy * WT + ix] * (1 - ux) * (1 - uy) +
         t[(size_t)iy * WT + jx] * ux * (1 - uy) +
         t[(size_t)jy * WT + ix] * (1 - ux) * uy +
         t[(size_t)jy * WT + jx] * ux * uy;
}

} // namespace

REGISTER_NODE(
    WaveletNoise, "Primitive", "Band-limited noise that stays crisp",
    [](Node &n) {
      n.add_out("output");
      add_seed(n.attrs);
      add_int(n.attrs, "octaves", "Octaves", 6, 1, 12, "Wavelet");
      add_float(n.attrs, "scale", "Scale", 8.f, 1.f, 64.f, "Wavelet");
      add_float(n.attrs, "gain", "Gain", 0.55f, 0.1f, 0.95f, "Wavelet");
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      uint32_t seed = n.attrs.get_seed("seed");
      int oct = n.attrs.get_i("octaves", 6);
      float scale = n.attrs.get_f("scale", 8.f);
      float gain = n.attrs.get_f("gain", 0.55f);
      std::vector<std::vector<float>> bands;
      for (int o = 0; o < oct; ++o)
        bands.push_back(wavelet_tile(seed + (uint32_t)o * 977u));
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w) * scale, v = y / float(out.h) * scale;
            float sum = 0, amp = 1, norm = 0, f = 1;
            for (int o = 0; o < oct; ++o) {
              sum += amp * tile_sample(bands[o], u * f, v * f);
              norm += amp;
              amp *= gain;
              f *= 2.f;
            }
            out.at(x, y) = sum / norm * 0.5f + 0.5f;
          }
      });
      apply_post(n, out);
    })

} // namespace gpx
