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
      add_choice(n.attrs, "flavor", "Flavor",
                 {"Gabor (amplitude)", "Phasor sawtooth", "Phasor sine",
                  "Phasor square"},
                 0, "Gabor")
          .tooltip = "Phasor keeps only the phase of the kernel field, so\n"
                     "the wave profile stays crisp everywhere - sawtooth\n"
                     "reads as bedding planes, square as strata steps.";
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
      int flavor = n.attrs.get_choice("flavor");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w) * scale, v = y / float(out.h) * scale;
            float sum = 0, amp = 1, norm = 0, f = 1;
            for (int o = 0; o < oct; ++o) {
              uint32_t s = seed + (uint32_t)o * 131u;
              float b;
              if (flavor == 0) {
                b = noise::gabor(u * f, v * f, s, freq, orient, aniso);
              } else {
                float ph = noise::phasor(u * f, v * f, s, freq, orient, aniso);
                if (flavor == 1) b = ph;                          // sawtooth
                else if (flavor == 2) b = std::sin(ph * 3.14159265f);
                else b = ph >= 0.f ? 1.f : -1.f;                  // square
              }
              sum += amp * b;
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
    DiffusionLimited, "Primitive", "Branching dendrites by particle aggregation",
    [](Node &n) {
      n.add_out("output");
      n.add_out("mask");
      add_seed(n.attrs);
      add_int(n.attrs, "particles", "Particles", 1500, 100, 8000, "Growth");
      add_float(n.attrs, "stickiness", "Stickiness", 1.f, 0.1f, 1.f, "Growth")
          .tooltip = "1 sticks on first contact - wispy branches. Lower\n"
                     "values let particles slide deeper before settling,\n"
                     "thickening the arms.";
      add_float(n.attrs, "smooth_radius", "Smoothing", 0.008f, 0.f, 0.05f,
                "Growth");
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      Heightmap &mask = n.out_hmap("mask");
      const int w = out.w, h = out.h;
      uint32_t seed = n.attrs.get_seed("seed");
      int particles = n.attrs.get_i("particles", 1500);
      float stick = n.attrs.get_f("stickiness", 1.f);
      // the aggregate grid; age recorded per cell so branches can fade
      // toward their tips
      std::vector<int> age((size_t)w * h, -1);
      int cx0 = w / 2, cy0 = h / 2;
      age[(size_t)cy0 * w + cx0] = 0;
      float cluster_r = 2.f;
      auto occupied = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h &&
               age[(size_t)y * w + x] >= 0;
      };
      uint32_t ctr = 0;
      auto rnd = [&]() {
        return (planet::pl_hash_bits((int)(ctr++), 0, 0, seed) & 0xffffffu) /
               16777215.f;
      };
      // single-threaded by construction: each particle's walk depends on the
      // aggregate the previous ones built, and the hash stream is one line
      for (int p = 0; p < particles; ++p) {
        float ang = rnd() * 6.2831853f;
        float r = cluster_r + 4.f;
        float px = cx0 + std::cos(ang) * r, py = cy0 + std::sin(ang) * r;
        for (int step = 0; step < 4000; ++step) {
          float dxc = px - cx0, dyc = py - cy0;
          float dc = std::sqrt(dxc * dxc + dyc * dyc);
          if (dc > cluster_r + 24.f) {
            // far out: leap most of the way back toward the action
            float t = (dc - cluster_r - 8.f) / dc;
            px -= dxc * t;
            py -= dyc * t;
            continue;
          }
          float wang = rnd() * 6.2831853f;
          px += std::cos(wang);
          py += std::sin(wang);
          int ix = (int)std::lround(px), iy = (int)std::lround(py);
          if (ix < 1 || iy < 1 || ix >= w - 1 || iy >= h - 1) break;
          bool touch = occupied(ix + 1, iy) || occupied(ix - 1, iy) ||
                       occupied(ix, iy + 1) || occupied(ix, iy - 1);
          if (touch && (stick >= 1.f || rnd() < stick)) {
            age[(size_t)iy * w + ix] = p;
            float dr = std::sqrt((float)((ix - cx0) * (ix - cx0) +
                                         (iy - cy0) * (iy - cy0)));
            cluster_r = std::max(cluster_r, dr);
            break;
          }
        }
        if (cluster_r > std::min(w, h) * 0.48f) break; // reached the frame
      }
      // older cells (trunk) high, younger tips low - reads as tapering relief
      for (size_t i = 0; i < age.size(); ++i) {
        mask.v[i] = age[i] >= 0 ? 1.f : 0.f;
        out.v[i] =
            age[i] >= 0 ? 1.f - 0.7f * (float)age[i] / (float)particles : 0.f;
      }
      int sr = (int)(n.attrs.get_f("smooth_radius", 0.008f) * w);
      if (sr > 0) {
        // a light separable box pass turns the one-cell skeleton into relief
        for (int pass = 0; pass < 2; ++pass) {
          Heightmap tmp = out;
          parallel_rows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
              for (int x = 0; x < w; ++x) {
                float s = 0;
                int c = 0;
                for (int d = -sr; d <= sr; ++d) {
                  int xx = pass == 0 ? x + d : x;
                  int yy = pass == 0 ? y : y + d;
                  if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                  s += tmp.v[(size_t)yy * w + xx];
                  ++c;
                }
                out.at(x, y) = s / c;
              }
          });
        }
      }
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
