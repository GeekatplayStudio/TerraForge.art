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

REGISTER_NODE(
    LineNoise, "Primitive", "Cellular noise seeded by line segments",
    [](Node &n) {
      n.add_out("output");
      n.add_out("cracks");
      add_seed(n.attrs);
      add_int(n.attrs, "lines", "Line count", 40, 4, 400, "Lines");
      add_float(n.attrs, "length", "Segment length", 0.18f, 0.02f, 0.6f,
                "Lines");
      add_float(n.attrs, "reach", "Reach", 0.08f, 0.01f, 0.5f, "Lines");
      add_float(n.attrs, "angle", "Direction °", 0.f, -180.f, 180.f, "Lines");
      add_float(n.attrs, "angle_jitter", "Direction jitter", 1.f, 0.f, 1.f,
                "Lines")
          .tooltip = "0 aligns every segment to the direction - bedding\n"
                     "planes. 1 scatters them freely - shattered rock.";
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      Heightmap &cracks = n.out_hmap("cracks");
      uint32_t seed = n.attrs.get_seed("seed");
      int count = n.attrs.get_i("lines", 40);
      float seg_len = n.attrs.get_f("length", 0.18f);
      float reach = std::max(n.attrs.get_f("reach", 0.08f), 1e-3f);
      float base_ang = n.attrs.get_f("angle") * 0.017453293f;
      float jitter = n.attrs.get_f("angle_jitter", 1.f);
      int w = out.w, h = out.h;
      // hashed segments: midpoint, direction, length
      struct Seg { float ax, ay, bx, by; };
      std::vector<Seg> segs((size_t)count);
      for (int i = 0; i < count; ++i) {
        auto rnd = [&](int k) {
          return (planet::pl_hash_bits(i, k, 0, seed) & 0xffffffu) /
                 16777215.f;
        };
        float mx = rnd(1), my = rnd(2);
        float ang = base_ang + (rnd(3) - 0.5f) * 6.2831853f * jitter;
        float hl = seg_len * (0.5f + rnd(4)) * 0.5f;
        segs[i] = {mx - std::cos(ang) * hl, my - std::sin(ang) * hl,
                   mx + std::cos(ang) * hl, my + std::sin(ang) * hl};
      }
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float px = (x + 0.5f) / w, py = (y + 0.5f) / h;
            float d1 = 1e30f, d2 = 1e30f;
            for (const Seg &s : segs) {
              float vx = s.bx - s.ax, vy = s.by - s.ay;
              float t = ((px - s.ax) * vx + (py - s.ay) * vy) /
                        std::max(vx * vx + vy * vy, 1e-12f);
              t = std::clamp(t, 0.f, 1.f);
              float dx = px - (s.ax + vx * t), dy = py - (s.ay + vy * t);
              float d = dx * dx + dy * dy;
              if (d < d1) { d2 = d1; d1 = d; }
              else if (d < d2) { d2 = d; }
            }
            float f1 = std::sqrt(d1), f2 = std::sqrt(d2);
            out.at(x, y) = std::min(f1 / reach, 1.f);
            // ridge between territories: F2-F1 is zero on the boundary
            float seam = std::clamp((f2 - f1) / (reach * 0.5f), 0.f, 1.f);
            cracks.at(x, y) = 1.f - seam * seam * (3.f - 2.f * seam);
          }
      });
      apply_post(n, out);
    })

} // namespace gpx
