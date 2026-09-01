// Geekatplay Studio — terrain effects.
//
// The "global effects" and erosion variants a terrain editor is expected to
// offer as one-click operations (Vue calls these Effects; World Machine and
// Gaea spread the same jobs across several nodes). Each one is a plain,
// deterministic filter so it can be applied repeatedly and still reproduce
// bit-identical results.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include <algorithm>
#include <vector>

namespace gpx {

static const int FX_DX8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int FX_DY8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// separable box blur, iterated to approximate a gaussian
static void fx_blur(const Heightmap &in, Heightmap &out, int radius) {
  if (radius < 1) {
    out = in;
    return;
  }
  Heightmap tmp(in.w, in.h);
  out = in;
  const float inv = 1.f / (2 * radius + 1);
  for (int pass = 0; pass < 3; ++pass) {
    parallel_rows(in.h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y) {
        float sum = 0;
        for (int x = -radius; x <= radius; ++x) sum += out.atc(x, y);
        for (int x = 0; x < in.w; ++x) {
          tmp.at(x, y) = sum * inv;
          sum += out.atc(x + radius + 1, y) - out.atc(x - radius, y);
        }
      }
    });
    parallel_rows(in.w, [&](int x0, int x1) {
      for (int x = x0; x < x1; ++x) {
        float sum = 0;
        for (int y = -radius; y <= radius; ++y) sum += tmp.atc(x, y);
        for (int y = 0; y < in.h; ++y) {
          out.at(x, y) = sum * inv;
          sum += tmp.atc(x, y + radius + 1) - tmp.atc(x, y - radius);
        }
      }
    });
  }
}

// normalized slope magnitude in 0..1, for effects that follow steepness
static void fx_slope(const Heightmap &m, std::vector<float> &out) {
  out.assign(m.v.size(), 0.f);
  float peak = 1e-9f;
  std::vector<float> rowmax((size_t)m.h, 0.f);
  parallel_rows(m.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      float rm = 0;
      for (int x = 0; x < m.w; ++x) {
        float dx, dy;
        m.gradient_at(x, y, dx, dy);
        float s = std::sqrt(dx * dx + dy * dy);
        out[(size_t)y * m.w + x] = s;
        rm = std::max(rm, s);
      }
      rowmax[y] = rm;
    }
  });
  for (float r : rowmax) peak = std::max(peak, r);
  float inv = 1.f / peak;
  parallel_index(out.size(), [&](size_t i0, size_t i1) {
    for (size_t i = i0; i < i1; ++i) out[i] = std::min(out[i] * inv, 1.f);
  });
}

// terrain amplitude, so effect strengths read the same on any input range
static float fx_amp(const Heightmap &m) {
  float mn, mx;
  m.minmax(mn, mx);
  return (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
}

static void fx_setup(Node &n) {
  n.add_in("input");
  n.add_in("mask", DataType::Heightmap, true);
  n.add_out("output");
}

// ------------------------------------------------------------------ Grit
REGISTER_NODE(
    Grit, "Effect", "Fine random bumps and holes over the whole surface",
    [](Node &n) {
      fx_setup(n);
      add_float(n.attrs, "amount", "Amount", 0.02f, 0.f, 0.25f, "Grit")
          .tooltip = "Bump height as a fraction of the terrain's own range.";
      add_float(n.attrs, "scale", "Grain size", 90.f, 8.f, 400.f, "Grit")
          .tooltip = "Higher values give finer, denser grain.";
      add_seed(n.attrs, "seed", "Seed", 0, "Grit");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float amt = n.attrs.get_f("amount", 0.02f) * fx_amp(*in);
      float sc = n.attrs.get_f("scale", 90.f);
      uint32_t seed = n.attrs.get_seed("seed");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float g = noise::value_noise(u * sc, v * sc, seed) - 0.5f;
            out.at(x, y) += g * 2.f * amt;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ---------------------------------------------------------------- Gravel
REGISTER_NODE(
    Gravel, "Effect", "Loose debris that gathers on slopes and leaves flats clean",
    [](Node &n) {
      fx_setup(n);
      add_float(n.attrs, "amount", "Amount", 0.03f, 0.f, 0.25f, "Gravel");
      add_float(n.attrs, "scale", "Grain size", 120.f, 8.f, 400.f, "Gravel");
      add_float(n.attrs, "slope_bias", "Slope bias", 1.5f, 0.f, 4.f, "Gravel")
          .tooltip = "How strongly the debris prefers steep ground.\n"
                     "0 spreads it evenly, high values keep it on slopes.";
      add_seed(n.attrs, "seed", "Seed", 0, "Gravel");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float amt = n.attrs.get_f("amount", 0.03f) * fx_amp(*in);
      float sc = n.attrs.get_f("scale", 120.f);
      float bias = n.attrs.get_f("slope_bias", 1.5f);
      uint32_t seed = n.attrs.get_seed("seed");
      std::vector<float> slope;
      fx_slope(*in, slope);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float g = noise::value_noise(u * sc, v * sc, seed) - 0.5f;
            float s = std::pow(slope[(size_t)y * out.w + x], bias);
            out.at(x, y) += g * 2.f * amt * s;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ----------------------------------------------------------------- Peaks
REGISTER_NODE(
    Peaks, "Effect", "Lifts high ground and digs the valleys deeper",
    [](Node &n) {
      fx_setup(n);
      add_float(n.attrs, "strength", "Strength", 0.5f, 0.f, 1.f, "Peaks");
      add_float(n.attrs, "pivot", "Pivot altitude", 0.45f, 0.f, 1.f, "Peaks")
          .tooltip = "Ground above this rises, ground below sinks.\n"
                     "Lower it to keep more of the terrain high.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      float k = n.attrs.get_f("strength", 0.5f);
      float pivot = std::clamp(n.attrs.get_f("pivot", 0.45f), 0.001f, 0.999f);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = (in->v[i] - mn) / d;
          // a smooth S-curve hinged at the pivot: above rises, below falls
          float s;
          if (t < pivot) {
            float a = t / pivot;
            s = pivot * (a * a);
          } else {
            float a = (t - pivot) / (1.f - pivot);
            s = pivot + (1.f - pivot) * std::sqrt(a);
          }
          out.v[i] = mn + (t + (s - t) * k) * d;
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// --------------------------------------------------------------- Sharpen
REGISTER_NODE(
    Sharpen, "Effect", "Makes steep ground steeper — crisp ridges and crests",
    [](Node &n) {
      fx_setup(n);
      add_float(n.attrs, "amount", "Amount", 0.6f, 0.f, 3.f, "Sharpen");
      add_float(n.attrs, "radius", "Radius", 0.01f, 0.002f, 0.1f, "Sharpen")
          .tooltip = "Size of the detail that gets emphasized.";
      add_float(n.attrs, "slope_bias", "Steep areas only", 1.f, 0.f, 3.f,
                "Sharpen")
          .tooltip = "0 sharpens everything evenly; higher values leave\n"
                     "flat ground untouched.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float amt = n.attrs.get_f("amount", 0.6f);
      float bias = n.attrs.get_f("slope_bias", 1.f);
      int r = std::max(1, (int)(n.attrs.get_f("radius", 0.01f) * in->w / 3.f));
      Heightmap soft;
      fx_blur(*in, soft, r);
      std::vector<float> slope;
      if (bias > 1e-6f) fx_slope(*in, slope);
      out = *in;
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float w = slope.empty() ? 1.f : std::pow(slope[i], bias);
          out.v[i] = in->v[i] + (in->v[i] - soft.v[i]) * amt * w;
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ---------------------------------------------------------------- Cracks
REGISTER_NODE(
    Cracks, "Effect", "Narrow fissures cut into the surface, as after a quake",
    [](Node &n) {
      fx_setup(n);
      add_float(n.attrs, "depth", "Depth", 0.06f, 0.f, 0.4f, "Cracks");
      add_float(n.attrs, "width", "Width", 0.35f, 0.05f, 1.f, "Cracks")
          .tooltip = "Thickness of the fissures. Low values give hairlines.";
      add_float(n.attrs, "scale", "Scale", 6.f, 0.5f, 40.f, "Cracks")
          .tooltip = "How many fissures cross the terrain.";
      add_float(n.attrs, "warp", "Wander", 0.35f, 0.f, 2.f, "Cracks")
          .tooltip = "Makes the fissures meander instead of running straight.";
      add_seed(n.attrs, "seed", "Seed", 0, "Cracks");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float depth = n.attrs.get_f("depth", 0.06f) * fx_amp(*in);
      float width = n.attrs.get_f("width", 0.35f);
      float sc = n.attrs.get_f("scale", 6.f);
      float warp = n.attrs.get_f("warp", 0.35f);
      uint32_t seed = n.attrs.get_seed("seed");
      noise::FbmParams fp;
      fp.octaves = 4;
      fp.lacunarity = 2.f;
      fp.gain = 0.5f;
      // Build the ridge field first, then normalize it. The raw range of the
      // ridged fBm is an implementation detail (it is not 0..1), so thresholding
      // it directly would make `width` mean nothing — and at some settings carve
      // nothing at all. Normalizing first makes width literally "the top
      // fraction of the field that becomes a fissure".
      Heightmap ridge(out.w, out.h);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            // wander the sampling position so fissures meander
            float wx = warp * noise::perlin(u * 3.f, v * 3.f, seed ^ 0x9e37u);
            float wy = warp * noise::perlin(u * 3.f + 5.2f, v * 3.f + 1.3f, seed ^ 0x85ebu);
            ridge.at(x, y) = noise::fbm_ridged((u + wx) * sc, (v + wy) * sc, seed, fp);
          }
      });
      ridge.remap(0.f, 1.f);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          // keep only the crests: the top `width` of the normalized field
          float t = (ridge.v[i] - (1.f - width)) / std::max(width, 1e-4f);
          t = std::clamp(t, 0.f, 1.f);
          out.v[i] -= t * t * depth;
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ------------------------------------------------------------ Glaciation
REGISTER_NODE(
    Glaciation, "Erosion", "Glacial carving — broad U-shaped valleys, ridges left intact",
    [](Node &n) {
      fx_setup(n);
      add_float(n.attrs, "strength", "Strength", 0.6f, 0.f, 1.f, "Glacier");
      add_float(n.attrs, "snowline", "Ice line", 0.55f, 0.f, 1.f, "Glacier")
          .tooltip = "Ground below this altitude is carved by ice;\n"
                     "peaks above it keep their sharp profile.";
      add_float(n.attrs, "width", "Valley width", 0.03f, 0.005f, 0.15f, "Glacier");
      add_float(n.attrs, "hardness", "Rock hardness", 0.4f, 0.f, 1.f, "Glacier")
          .tooltip = "Hard rock resists the ice and keeps more relief.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      float k = n.attrs.get_f("strength", 0.6f);
      float line = n.attrs.get_f("snowline", 0.55f);
      float hard = n.attrs.get_f("hardness", 0.4f);
      int r = std::max(1, (int)(n.attrs.get_f("width", 0.03f) * in->w / 3.f));
      Heightmap soft;
      fx_blur(*in, soft, r);
      out = *in;
      float scale = k * (1.f - hard * 0.8f);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = (in->v[i] - mn) / d;
          // ice sits low: full carving at the bottom, none above the ice line
          float w = 1.f - std::clamp(t / std::max(line, 1e-4f), 0.f, 1.f);
          w = w * w * (3.f - 2.f * w);
          out.v[i] = in->v[i] + (soft.v[i] - in->v[i]) * w * scale;
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// --------------------------------------------------------------- Dissolve
// Rainwater gathers downhill and flushes material away; the effect is
// strongest low down, where the streams have collected the most water.
REGISTER_NODE(
    Dissolve, "Erosion", "Rainwater dissolves the surface into streams, strongest low down",
    [](Node &n) {
      fx_setup(n);
      n.add_out("flow_map");
      add_float(n.attrs, "amount", "Amount", 0.25f, 0.f, 1.f, "Dissolve");
      add_float(n.attrs, "hardness", "Rock hardness", 0.5f, 0.f, 1.f, "Dissolve")
          .tooltip = "Hard rock keeps the streams narrow and incised;\n"
                     "soft rock lets them spread and flatten the surface.";
      add_float(n.attrs, "altitude_bias", "Low ground bias", 1.f, 0.f, 3.f,
                "Dissolve")
          .tooltip = "How much the effect concentrates at low altitude.";
      add_float(n.attrs, "smooth", "Smoothing", 0.15f, 0.f, 1.f, "Dissolve");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &flow = n.out_hmap("flow_map");
      out = *in;
      int w = out.w, h = out.h;
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      float amt = n.attrs.get_f("amount", 0.25f);
      float hard = n.attrs.get_f("hardness", 0.5f);
      float abias = n.attrs.get_f("altitude_bias", 1.f);
      float sm = n.attrs.get_f("smooth", 0.15f);

      // D8 receivers, then accumulate downhill in height-descending order.
      // Processing in a fixed sorted order keeps this reproducible.
      std::vector<int> order((size_t)w * h), receiver((size_t)w * h);
      std::vector<float> area((size_t)w * h, 1.f);
      for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
      std::stable_sort(order.begin(), order.end(),
                       [&](int a, int b) { return in->v[a] > in->v[b]; });
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            int i = y * w + x;
            float hgt = in->v[i];
            int best = -1;
            float bestdrop = 0;
            for (int k = 0; k < 8; ++k) {
              int nx = x + FX_DX8[k], ny = y + FX_DY8[k];
              if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
              float dist = (FX_DX8[k] && FX_DY8[k]) ? 1.41421356f : 1.f;
              float drop = (hgt - in->at(nx, ny)) / dist;
              if (drop > bestdrop) {
                bestdrop = drop;
                best = ny * w + nx;
              }
            }
            receiver[i] = best;
          }
      });
      for (int idx : order) {
        int r = receiver[idx];
        if (r >= 0) area[r] += area[idx];
      }

      // carve proportionally to accumulated water, biased to low ground
      flow = Heightmap(w, h);
      float maxa = 1.f;
      for (float a : area) maxa = std::max(maxa, a);
      float inv_log = 1.f / std::log(maxa + 1.f);
      float carve = amt * d * (1.f - hard * 0.75f);
      parallel_index(area.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float f = std::log(area[i] + 1.f) * inv_log; // 0..1
          flow.v[i] = f;
          float t = (in->v[i] - mn) / d;
          float low = std::pow(1.f - t, abias);
          out.v[i] = in->v[i] - f * low * carve * 0.35f;
        }
      });
      if (sm > 1e-4f) {
        Heightmap soft;
        fx_blur(out, soft, std::max(1, (int)(sm * w / 120.f)));
        float mix = sm * (1.f - hard * 0.5f);
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            out.v[i] += (soft.v[i] - out.v[i]) * mix;
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ------------------------------------------------------------ TerrainClip
// Clipping altitudes: everything under the low plane becomes a hole, and
// everything above the high plane is cut flat. Cheaper and far more
// controllable than modelling the same shapes with booleans.
REGISTER_NODE(
    TerrainClip, "Effect", "Clip altitudes — flat tops above, holes below",
    [](Node &n) {
      fx_setup(n);
      n.add_out("clip_mask");
      add_range(n.attrs, "clip", "Clip range", 0.f, 1.f, 0.f, 1.f, "Clipping")
          .tooltip = "Ground below the low mark is cut away, ground above\n"
                     "the high mark is flattened. Normalized altitudes.";
      add_choice(n.attrs, "low_mode", "Below low mark",
                 {"Leave alone", "Flatten", "Cut away (hole)"}, 1, "Clipping");
      add_choice(n.attrs, "high_mode", "Above high mark",
                 {"Leave alone", "Flatten"}, 1, "Clipping");
      add_float(n.attrs, "softness", "Edge softness", 0.f, 0.f, 0.2f, "Clipping")
          .tooltip = "Blends the cut instead of leaving a hard step.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &mask = n.out_hmap("clip_mask");
      out = *in;
      mask = Heightmap(in->w, in->h, 1.f);
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      float lo, hi;
      n.attrs.get_range("clip", lo, hi);
      if (hi < lo) std::swap(lo, hi);
      int lmode = n.attrs.get_choice("low_mode");
      int hmode = n.attrs.get_choice("high_mode");
      float soft = n.attrs.get_f("softness", 0.f);
      float lo_v = mn + lo * d, hi_v = mn + hi * d;
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float v = in->v[i];
          if (v < lo_v) {
            float t = soft > 1e-6f
                          ? std::clamp((lo_v - v) / (soft * d), 0.f, 1.f)
                          : 1.f;
            if (lmode == 1) out.v[i] = v + (lo_v - v) * t;
            else if (lmode == 2) {
              out.v[i] = v + (lo_v - v) * t;
              mask.v[i] = 1.f - t; // 0 where the terrain is cut away
            }
          } else if (v > hi_v) {
            float t = soft > 1e-6f
                          ? std::clamp((v - hi_v) / (soft * d), 0.f, 1.f)
                          : 1.f;
            if (hmode == 1) out.v[i] = v + (hi_v - v) * t;
          }
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// --------------------------------------------------------- TerrainSculpt
// The hand-edited layer. Brush strokes accumulate into the "delta" field
// rather than being baked into the heightmap, which is what keeps the rest of
// the graph procedural: change the mountain generator upstream and your
// hand-carved riverbed is still there, sitting on top of the new shape.
//
// (Vue reaches the same conclusion from the other direction — painting on a
// heightfield terrain there creates a "User Touch-up" node in the graph.)
REGISTER_NODE(
    TerrainSculpt, "Effect", "Hand-sculpted layer — brush strokes kept separate from the procedural terrain",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("stroke_mask");
      add_field(n.attrs, "delta", "Sculpted relief", 512, 512, -1.f, 1.f,
                "Sculpt")
          .tooltip = "Painted in the viewport with the Terrain Editor brushes.\n"
                     "Stored with the project; erasing it resets the sculpt.";
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 2.f, "Sculpt")
          .tooltip = "Scales the whole sculpted layer — dial your edits\n"
                     "back without losing them.";
      add_float(n.attrs, "smooth", "Soften", 0.f, 0.f, 0.05f, "Sculpt")
          .tooltip = "Blurs the sculpted layer only, leaving the terrain\n"
                     "underneath crisp.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &smask = n.out_hmap("stroke_mask");
      out = *in;
      smask = Heightmap(in->w, in->h);
      const Attribute *fa = n.attrs.find("delta");
      if (!fa || fa->field.empty() || fa->fw <= 0 || fa->fh <= 0) return;

      // the sculpt is authored at its own resolution; resample to the terrain
      Heightmap layer(fa->fw, fa->fh);
      layer.v = fa->field;
      if (layer.w != in->w || layer.h != in->h)
        layer = layer.resampled(in->w, in->h);

      float sm = n.attrs.get_f("smooth", 0.f);
      if (sm > 1e-5f) {
        Heightmap soft;
        fx_blur(layer, soft, std::max(1, (int)(sm * in->w / 3.f)));
        layer = soft;
      }
      float k = n.attrs.get_f("strength", 1.f) * fx_amp(*in);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          out.v[i] = in->v[i] + layer.v[i] * k;
          smask.v[i] = std::min(std::fabs(layer.v[i]) * 4.f, 1.f);
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx

