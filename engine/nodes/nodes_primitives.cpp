// Geekatplay Studio — primitive generator nodes.
// One Noise node with a type dropdown (not one node per variant), a Fractal
// node for non-noise fractal methods, and shape primitives.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <random>

namespace gpx {

REGISTER_NODE(
    Noise, "Primitive", "Coherent noise: fBm, ridged, billow, swiss, value, cellular",
    [](Node &n) {
      n.add_in("envelope", DataType::Heightmap, true);
      n.add_out("output");
      add_choice(n.attrs, "type", "Type",
                 {"Perlin fBm", "Ridged", "Billow", "Swiss (eroded ridges)",
                  "Value fBm", "Worley F1", "Worley F2", "Worley edges",
                  "Worley F1*F2", "IQ (damped slopes)", "Jordan (crumpled)",
                  "Pingpong (banded)", "Voronoise (cell blend)"},
                 1, "Noise");
      add_seed(n.attrs, "seed", "Seed", 0, "Noise");
      add_int(n.attrs, "octaves", "Octaves", 9, 1, 16, "Noise");
      add_float(n.attrs, "lacunarity", "Lacunarity", 2.f, 1.2f, 4.f, "Noise");
      add_float(n.attrs, "gain", "Gain", 0.5f, 0.05f, 0.95f, "Noise");
      add_float(n.attrs, "ridge_weight", "Ridge weight", 0.7f, 0.f, 1.f, "Noise");
      add_float(n.attrs, "warp", "Swiss warp", 0.15f, 0.f, 0.6f, "Noise");
      add_float(n.attrs, "jitter", "Cell jitter", 1.f, 0.f, 1.f, "Noise");
      setup_coords(n);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      CoordMap cm;
      cm.from(n);
      uint32_t seed = n.attrs.get_seed("seed");
      int type = n.attrs.get_choice("type");
      noise::FbmParams p;
      p.octaves = n.attrs.get_i("octaves", 9);
      p.lacunarity = n.attrs.get_f("lacunarity", 2.f);
      p.gain = n.attrs.get_f("gain", 0.5f);
      p.weight = n.attrs.get_f("ridge_weight", 0.7f);
      float warp = n.attrs.get_f("warp", 0.15f);
      float jitter = n.attrs.get_f("jitter", 1.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float nx, ny, v = 0;
            cm.map(x, y, out.w, out.h, nx, ny);
            switch (type) {
              case 0: v = noise::fbm(nx, ny, seed, p); break;
              case 1: v = noise::fbm_ridged(nx, ny, seed, p); break;
              case 2: v = noise::fbm_billow(nx, ny, seed, p); break;
              case 3: v = noise::fbm_swiss(nx, ny, seed, p, warp); break;
              case 4: v = noise::fbm_value(nx, ny, seed, p); break;
              case 12: { // voronoise fBm: jitter is u, ridge_weight is v
                float sum = 0, amp = 0.5f, fq = 1;
                for (int o = 0; o < p.octaves; ++o) {
                  sum += amp * noise::voronoise(nx * fq, ny * fq,
                                                seed + (uint32_t)o * 1013u,
                                                jitter, p.weight);
                  amp *= p.gain;
                  fq *= p.lacunarity;
                }
                v = sum;
              } break;
              case 9: v = noise::fbm_iq(nx, ny, seed, p); break;
              case 10: v = noise::fbm_jordan(nx, ny, seed, p); break;
              case 11: v = noise::fbm_pingpong(nx, ny, seed, p); break;
              default: {
                float f1, f2;
                noise::worley(nx, ny, seed, f1, f2, jitter);
                if (type == 5) v = f1;
                else if (type == 6) v = f2;
                else if (type == 7) v = f2 - f1;
                else v = f1 * f2;
              }
            }
            out.at(x, y) = v;
          }
      });
      apply_post(n, out);
      if (const Heightmap *env = n.in_hmap("envelope"))
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            out.v[i] *= std::clamp(env->v[i], 0.f, 1.f);
        });
    })

// --------------------------------------------------------------- fractals
static void diamond_square(Heightmap &m, uint32_t seed, float roughness) {
  int n = m.w; // expects (2^k)+1 square
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-1.f, 1.f);
  m.at(0, 0) = d(rng);
  m.at(n - 1, 0) = d(rng);
  m.at(0, n - 1) = d(rng);
  m.at(n - 1, n - 1) = d(rng);
  float amp = 1.f;
  for (int step = n - 1; step > 1; step /= 2) {
    int half = step / 2;
    // diamond
    for (int y = half; y < n; y += step)
      for (int x = half; x < n; x += step) {
        float avg = (m.at(x - half, y - half) + m.at(x + half, y - half) +
                     m.at(x - half, y + half) + m.at(x + half, y + half)) *
                    0.25f;
        m.at(x, y) = avg + d(rng) * amp;
      }
    // square
    for (int y = 0; y < n; y += half)
      for (int x = ((y / half) % 2 == 0) ? half : 0; x < n; x += step) {
        float sum = 0;
        int cnt = 0;
        if (x >= half) { sum += m.at(x - half, y); cnt++; }
        if (x + half < n) { sum += m.at(x + half, y); cnt++; }
        if (y >= half) { sum += m.at(x, y - half); cnt++; }
        if (y + half < n) { sum += m.at(x, y + half); cnt++; }
        m.at(x, y) = sum / cnt + d(rng) * amp;
      }
    amp *= std::pow(2.f, -roughness);
  }
}

REGISTER_NODE(
    Fractal, "Primitive", "Non-noise fractals: diamond-square, fault lines",
    [](Node &n) {
      n.add_out("output");
      add_choice(n.attrs, "type", "Type", {"Diamond-square", "Fault formation"}, 0);
      add_seed(n.attrs);
      add_float(n.attrs, "roughness", "Roughness", 0.9f, 0.3f, 1.6f);
      add_int(n.attrs, "faults", "Fault count", 200, 10, 2000);
      add_float(n.attrs, "fault_softness", "Fault softness", 0.02f, 0.f, 0.2f);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      uint32_t seed = n.attrs.get_seed("seed");
      if (n.attrs.get_choice("type") == 0) {
        // run on nearest (2^k)+1 grid then resample
        int k = 1;
        while ((1 << k) + 1 < out.w) ++k;
        Heightmap ds((1 << k) + 1, (1 << k) + 1);
        diamond_square(ds, seed, n.attrs.get_f("roughness", 0.9f));
        Heightmap res = ds.resampled(out.w, out.h);
        out.v = std::move(res.v);
      } else {
        int faults = n.attrs.get_i("faults", 200);
        float soft = n.attrs.get_f("fault_softness", 0.02f);
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d01(0.f, 1.f);
        struct FaultLine { float nx, ny, c, amp; };
        std::vector<FaultLine> lines(faults);
        for (int f = 0; f < faults; ++f) {
          float a = d01(rng) * 6.2831853f;
          lines[f] = {std::cos(a), std::sin(a), d01(rng) * 1.4142f - 0.7071f,
                      (d01(rng) - 0.5f) * 2.f / faults};
        }
        parallel_rows(out.h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < out.w; ++x) {
              float u = x / float(out.w) - 0.5f, v = y / float(out.h) - 0.5f;
              float sum = 0;
              for (const auto &L : lines) {
                float sd = u * L.nx + v * L.ny - L.c;
                float s = soft > 0 ? std::clamp(sd / soft * 0.5f + 0.5f, 0.f, 1.f)
                                   : (sd > 0 ? 1.f : 0.f);
                sum += L.amp * (s * 2.f - 1.f);
              }
              out.at(x, y) = sum;
            }
        });
      }
      apply_post(n, out);
    })

REGISTER_NODE(
    Shape, "Primitive", "Geometric base shapes: slope, bump, crater, cone, ridge line",
    [](Node &n) {
      n.add_out("output");
      add_choice(n.attrs, "type", "Type",
                 {"Slope plane", "Bump", "Crater", "Cone", "Ridge line",
                  "Border falloff", "Wave sine", "Wave square", "Wave triangle",
                  "Step", "Band", "Paraboloid"},
                 1);
      add_vec2(n.attrs, "center", "Center", 0.5f, 0.5f, -0.5f, 1.5f);
      add_float(n.attrs, "radius", "Radius", 0.35f, 0.01f, 1.5f);
      add_float(n.attrs, "hardness", "Hardness", 1.f, 0.2f, 8.f);
      add_float(n.attrs, "angle", "Direction °", 0.f, -180.f, 180.f);
      add_float(n.attrs, "frequency", "Frequency", 4.f, 0.25f, 64.f);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      int type = n.attrs.get_choice("type");
      float cx, cy;
      n.attrs.get_vec2("center", cx, cy);
      float r = n.attrs.get_f("radius", 0.35f);
      float hard = n.attrs.get_f("hardness", 1.f);
      float a = n.attrs.get_f("angle") * 0.017453293f;
      float ca = std::cos(a), sa = std::sin(a);
      float freq = n.attrs.get_f("frequency", 4.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), vv = y / float(out.h);
            float dx = u - cx, dy = vv - cy;
            float d = std::sqrt(dx * dx + dy * dy) / r;
            float v = 0;
            switch (type) {
              case 0: v = u * ca + vv * sa; break;
              case 1: v = d >= 1 ? 0 : std::pow(1 - d * d, hard); break;
              case 2: {
                float b = d >= 1 ? 0 : std::pow(1 - d * d, hard);
                v = b * (1 - b) * 4.f;
              } break;
              case 3: v = std::max(1.f - d, 0.f); break;
              case 4: {
                // distance to the line through center with direction angle
                float sd = std::fabs(dx * -sa + dy * ca) / r;
                v = std::max(1.f - sd, 0.f);
                v = std::pow(v, hard);
              } break;
              case 5: {
                // border falloff mask (island maker)
                float bx = std::min(u, 1.f - u), by = std::min(vv, 1.f - vv);
                float b = std::clamp(std::min(bx, by) / std::max(r * 0.5f, 1e-4f),
                                     0.f, 1.f);
                v = b * b * (3.f - 2.f * b);
              } break;
              case 6: // waves ride the rotated axis through the center
              case 7:
              case 8: {
                float t = (dx * ca + dy * sa) * freq;
                float ph = t - std::floor(t); // 0..1 phase
                if (type == 6) v = 0.5f + 0.5f * std::sin(ph * 6.2831853f);
                else if (type == 7) v = ph < 0.5f ? 1.f : 0.f;
                else v = ph < 0.5f ? ph * 2.f : 2.f - ph * 2.f;
              } break;
              case 9: { // step: a smooth jump across the center line; radius
                        // is the transition width, hardness sharpens it
                float sd = (dx * ca + dy * sa) / std::max(r, 1e-4f);
                float s = std::clamp(sd * 0.5f + 0.5f, 0.f, 1.f);
                s = s * s * (3.f - 2.f * s);
                v = std::pow(s, 1.f / hard);
              } break;
              case 10: { // band: 1 within radius of the center line, soft edge
                float sd = std::fabs(dx * ca + dy * sa) / std::max(r, 1e-4f);
                float s = std::clamp(1.f - sd, 0.f, 1.f);
                v = std::pow(s * s * (3.f - 2.f * s), 1.f / hard);
              } break;
              case 11: // paraboloid dome
                v = std::max(1.f - d * d, 0.f);
                break;
            }
            out.at(x, y) = v;
          }
      });
      apply_post(n, out);
    })

REGISTER_NODE(
    GaborNoise, "Primitive", "Oriented sparse-kernel noise (streaked rock)",
    [](Node &n) {
      n.add_out("output");
      add_seed(n.attrs);
      add_int(n.attrs, "octaves", "Octaves", 3, 1, 6, "Gabor");
      add_float(n.attrs, "frequency", "Kernel frequency", 3.f, 0.5f, 16.f,
                "Gabor");
      add_float(n.attrs, "orientation", "Orientation °", 30.f, -180.f, 180.f,
                "Gabor");
      add_float(n.attrs, "anisotropy", "Anisotropy", 0.85f, 0.f, 1.f, "Gabor")
          .tooltip = "1 locks every kernel to the orientation - streaks.\n"
                     "0 draws orientations at random - isotropic grain.";
      add_float(n.attrs, "cell_scale", "Scale", 6.f, 1.f, 32.f, "Gabor");
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      uint32_t seed = n.attrs.get_seed("seed");
      int oct = n.attrs.get_i("octaves", 3);
      float freq = n.attrs.get_f("frequency", 3.f);
      float orient = n.attrs.get_f("orientation", 30.f) * 0.017453293f;
      float aniso = n.attrs.get_f("anisotropy", 0.85f);
      float scale = n.attrs.get_f("cell_scale", 6.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w) * scale, v = y / float(out.h) * scale;
            float sum = 0, amp = 1, norm = 0, f = 1;
            for (int o = 0; o < oct; ++o) {
              sum += amp * noise::gabor(u * f, v * f, seed + (uint32_t)o * 131u,
                                        freq, orient, aniso);
              norm += amp;
              amp *= 0.5f;
              f *= 2.f;
            }
            out.at(x, y) = sum / norm * 0.5f + 0.5f;
          }
      });
      apply_post(n, out);
    })

REGISTER_NODE(
    WhiteNoise, "Primitive", "Raw per-cell white noise",
    [](Node &n) {
      n.add_out("output");
      add_seed(n.attrs);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      uint32_t seed = n.attrs.get_seed("seed");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x)
            out.at(x, y) =
                (planet::pl_hash_bits(x, y, 0, seed) & 0xffffffu) / 16777215.f;
      });
      apply_post(n, out);
    })

REGISTER_NODE(
    Constant, "Primitive", "Constant level",
    [](Node &n) {
      n.add_out("output");
      add_float(n.attrs, "value", "Value", 0.5f, -1.f, 2.f);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      std::fill(out.v.begin(), out.v.end(), n.attrs.get_f("value", 0.5f));
    })

REGISTER_NODE(
    GeologicalStrata, "Primitive", "Layered rock strata from an input heightmap",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_int(n.attrs, "layers", "Layers", 8, 2, 32);
      add_float(n.attrs, "hardness", "Layer hardness", 2.5f, 1.f, 8.f);
      add_seed(n.attrs);
      add_float(n.attrs, "variation", "Thickness variation", 0.5f, 0.f, 1.f);
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      int layers = n.attrs.get_i("layers", 8);
      float hard = n.attrs.get_f("hardness", 2.5f);
      float var = n.attrs.get_f("variation", 0.5f);
      uint32_t seed = n.attrs.get_seed("seed");
      std::vector<float> edges(layers + 1);
      float acc = 0;
      for (int l = 0; l < layers; ++l) {
        edges[l] = acc;
        acc += 1.f + var * (noise::hash01(l, 17, seed) * 2.f - 1.f);
      }
      edges[layers] = acc;
      for (auto &e : edges) e /= acc;
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = (in->v[i] - mn) / d;
          int l = 0;
          while (l < layers - 1 && t > edges[l + 1]) ++l;
          float lt = (t - edges[l]) / std::max(edges[l + 1] - edges[l], 1e-6f);
          lt = std::pow(std::clamp(lt, 0.f, 1.f), hard);
          out.v[i] = mn + (edges[l] + lt * (edges[l + 1] - edges[l])) * d;
        }
      });
      apply_post(n, out);
    })

} // namespace gpx
