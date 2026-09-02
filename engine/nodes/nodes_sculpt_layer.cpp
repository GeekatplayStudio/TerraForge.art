// Geekatplay Studio - the hand-sculpted layer node: viewport brush strokes
// kept apart from the procedural terrain so both stay editable. Split from
// nodes_terrain_fx.cpp for the 500-line module rule.
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

// the small helpers the effect family shares; duplicated as statics
// rather than exported, since they are three short functions
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
