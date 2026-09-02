// Geekatplay Studio — filter nodes (smoothing, terrace, recurve, clamp...)
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include <cstdint>

namespace gpx {

// separable box blur iterated 3x ~ gaussian
static void blur(const Heightmap &in, Heightmap &out, int radius) {
  if (radius < 1) {
    out = in;
    return;
  }
  Heightmap tmp(in.w, in.h);
  out = in;
  for (int pass = 0; pass < 3; ++pass) {
    // horizontal
    parallel_rows(in.h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y) {
        float sum = 0;
        for (int x = -radius; x <= radius; ++x) sum += out.atc(x, y);
        for (int x = 0; x < in.w; ++x) {
          tmp.at(x, y) = sum / (2 * radius + 1);
          sum += out.atc(x + radius + 1, y) - out.atc(x - radius, y);
        }
      }
    });
    // vertical (iterate columns per row-band of transposed logic)
    parallel_rows(in.w, [&](int x0, int x1) {
      for (int x = x0; x < x1; ++x) {
        float sum = 0;
        for (int y = -radius; y <= radius; ++y) sum += tmp.atc(x, y);
        for (int y = 0; y < in.h; ++y) {
          out.at(x, y) = sum / (2 * radius + 1);
          sum += tmp.atc(x, y + radius + 1) - tmp.atc(x, y - radius);
        }
      }
    });
  }
}

static void setup_masked_filter(Node &n) {
  n.add_in("input");
  n.add_in("mask", DataType::Heightmap, true);
  n.add_out("output");
}

REGISTER_NODE(
    Smooth, "Filter", "Gaussian-like smoothing",
    [](Node &n) {
      setup_masked_filter(n);
      add_float(n.attrs, "radius", "Radius", 0.01f, 0.f, 0.2f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      int r = std::max(1, (int)(n.attrs.get_f("radius", 0.01f) * in->w / 3.f));
      blur(*in, out, r);
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

REGISTER_NODE(
    Terrace, "Filter", "Stratified terraces: uneven layers, warped edges, altitude band",
    [](Node &n) {
      setup_masked_filter(n);
      add_int(n.attrs, "levels", "Levels", 8, 2, 64, "Steps");
      add_float(n.attrs, "shape", "Edge sharpness", 3.f, 0.5f, 12.f, "Steps");
      add_float(n.attrs, "cliff_bias", "Cliff bias", 0.f, -1.f, 1.f, "Steps")
          .tooltip = "Skews each step: negative = wide flats with sharp\n"
                     "cliffs above; positive = sharp base, sloped tops.";
      add_float(n.attrs, "mix", "Strength", 1.f, 0.f, 1.f, "Steps");
      add_seed(n.attrs, "seed", "Seed", 0, "Variation");
      add_float(n.attrs, "level_jitter", "Level thickness jitter", 0.3f, 0.f, 1.f,
                "Variation")
          .tooltip = "Randomizes each layer's thickness — natural geological\n"
                     "strata are never evenly spaced.";
      add_float(n.attrs, "edge_noise", "Edge warp", 0.15f, 0.f, 1.f, "Variation")
          .tooltip = "Warps terrace edges with noise so contour lines\n"
                     "wander instead of following exact heights.";
      add_float(n.attrs, "edge_noise_scale", "Edge warp scale", 12.f, 2.f, 64.f,
                "Variation");
      add_range(n.attrs, "band", "Altitude band", 0.f, 1.f, 0.f, 1.f, "Range")
          .tooltip = "Only terrace heights inside this normalized band;\n"
                     "terrain outside is left untouched.";
      add_float(n.attrs, "band_soft", "Band softness", 0.1f, 0.01f, 0.5f, "Range");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      int levels = n.attrs.get_i("levels", 8);
      float shape = n.attrs.get_f("shape", 3.f);
      float bias = n.attrs.get_f("cliff_bias", 0.f);
      float mix = n.attrs.get_f("mix", 1.f);
      uint32_t seed = n.attrs.get_seed("seed");
      float jitter = n.attrs.get_f("level_jitter", 0.3f);
      float ewarp = n.attrs.get_f("edge_noise", 0.15f);
      float escale = n.attrs.get_f("edge_noise_scale", 12.f);
      float band_lo, band_hi;
      n.attrs.get_range("band", band_lo, band_hi);
      float band_soft = n.attrs.get_f("band_soft", 0.1f);
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      // uneven layer edges
      std::vector<float> edges(levels + 1);
      float acc = 0;
      for (int l = 0; l < levels; ++l) {
        edges[l] = acc;
        acc += 1.f + jitter * (noise::hash01(l, 91, seed) * 2.f - 1.f);
      }
      edges[levels] = acc;
      for (auto &e : edges) e /= acc;
      noise::FbmParams fp;
      fp.octaves = 4;
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            size_t i = (size_t)y * in->w + x;
            float t = (in->v[i] - mn) / d;
            // edge warp: shift the sampled height by noise before terracing
            if (ewarp > 0) {
              float u = x / float(in->w), v = y / float(in->h);
              t += noise::fbm(u * escale, v * escale, seed ^ 0x9E37u, fp) *
                   ewarp * 0.5f / levels * 4.f;
              t = std::clamp(t, 0.f, 1.f);
            }
            // find layer
            int l = 0;
            while (l < levels - 1 && t > edges[l + 1]) ++l;
            float span = std::max(edges[l + 1] - edges[l], 1e-6f);
            float fr = std::clamp((t - edges[l]) / span, 0.f, 1.f);
            // biased smoothstep edge
            float pivot = std::clamp(0.5f - bias * 0.4f, 0.05f, 0.95f);
            float s = fr < pivot
                          ? pivot * std::pow(fr / pivot, shape)
                          : 1.f - (1.f - pivot) *
                                      std::pow((1.f - fr) / (1.f - pivot), shape);
            float terr = mn + (edges[l] + s * span) * d;
            // altitude band restriction
            float tin = (in->v[i] - mn) / d;
            float ba = std::clamp((tin - band_lo) / band_soft + 0.5f, 0.f, 1.f);
            float bb = std::clamp((band_hi - tin) / band_soft + 0.5f, 0.f, 1.f);
            float bm = std::min(ba, bb) * mix;
            out.v[i] = in->v[i] * (1.f - bm) + terr * bm;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

REGISTER_NODE(
    Clamp, "Filter", "Clamp with optional smooth shoulders",
    [](Node &n) {
      setup_masked_filter(n);
      add_range(n.attrs, "range", "Clamp range", 0.1f, 0.9f, -1.f, 2.f);
      add_float(n.attrs, "smoothing", "Shoulder softness", 0.f, 0.f, 0.5f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float lo, hi;
      n.attrs.get_range("range", lo, hi);
      float k = n.attrs.get_f("smoothing", 0.f);
      parallel_index(in->v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float v = in->v[i];
          if (k <= 1e-6f) {
            v = std::clamp(v, lo, hi);
          } else {
            // softplus shoulders: smooth-max against lo, smooth-min against hi
            v = v + k * std::log1p(std::exp((lo - v) / k));
            v = v - k * std::log1p(std::exp((v - hi) / k));
          }
          out.v[i] = v;
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

REGISTER_NODE(
    Remap, "Filter", "Remap value range",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_range(n.attrs, "range", "Target range", 0.f, 1.f, -2.f, 2.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float lo, hi;
      n.attrs.get_range("range", lo, hi);
      out.remap(lo, hi);
    })

REGISTER_NODE(
    GammaCorrection, "Filter", "Power-curve contrast",
    [](Node &n) {
      setup_masked_filter(n);
      add_float(n.attrs, "gamma", "Gamma", 1.f, 0.05f, 6.f, "", true);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float g = n.attrs.get_f("gamma", 1.f);
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_index(in->v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i)
          out.v[i] = mn + std::pow((in->v[i] - mn) / d, g) * d;
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

REGISTER_NODE(
    Plateau, "Filter", "Flatten tops above a level",
    [](Node &n) {
      setup_masked_filter(n);
      add_float(n.attrs, "level", "Level", 0.7f, 0.f, 1.f);
      add_float(n.attrs, "softness", "Softness", 0.1f, 0.01f, 1.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float level = n.attrs.get_f("level", 0.7f);
      float soft = n.attrs.get_f("softness", 0.1f);
      float mn, mx;
      in->minmax(mn, mx);
      float lv = mn + level * (mx - mn);
      float k = soft * (mx - mn) + 1e-6f;
      parallel_index(in->v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float v = in->v[i];
          // smooth-min against the plateau level
          out.v[i] = lv - k * std::log(1.f + std::exp((lv - v) / k));
        }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

REGISTER_NODE(
    ExpandShrink, "Filter", "Morphological dilate / erode",
    [](Node &n) {
      setup_masked_filter(n);
      add_float(n.attrs, "radius", "Radius", 0.01f, 0.001f, 0.05f);
      add_bool(n.attrs, "shrink", "Shrink (erode)", false);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      int r = std::max(1, (int)(n.attrs.get_f("radius", 0.01f) * in->w));
      bool shrink = n.attrs.get_b("shrink");
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float best = in->at(x, y);
            for (int dy = -r; dy <= r; ++dy)
              for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy > r * r) continue;
                float v = in->atc(x + dx, y + dy);
                best = shrink ? std::min(best, v) : std::max(best, v);
              }
            out.at(x, y) = best;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

REGISTER_NODE(
    Fold, "Filter", "Fold values around midline — creates ridged detail",
    [](Node &n) {
      setup_masked_filter(n);
      add_int(n.attrs, "iterations", "Iterations", 1, 1, 6);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int iters = n.attrs.get_i("iterations", 1);
      for (int it = 0; it < iters; ++it) {
        float mn, mx;
        out.minmax(mn, mx);
        float mid = 0.5f * (mn + mx);
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            out.v[i] = mx - std::fabs(out.v[i] - mid) * 2.f + (mx - mid) * 0.f;
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })


// ------------------------------------------------------------------ median
// The rank filter erosion output has been missing: it removes salt-and-pepper
// spikes - the single-cell pits and needles a particle simulation leaves -
// without softening the edges around them, which is exactly what a Gaussian
// blur cannot do. Deterministic and embarrassingly parallel: every output
// pixel depends only on its own neighbourhood of the input.
REGISTER_NODE(
    Median, "Filter",
    "Removes single-cell spikes and pits without softening edges",
    [](Node &n) {
      setup_masked_filter(n);
      add_int(n.attrs, "radius", "Radius", 1, 1, 3)
          .tooltip = "1 looks at 3x3 cells, 2 at 5x5, 3 at 7x7. Larger wipes\n"
                     "bigger artifacts and more real detail with them.";
      add_int(n.attrs, "passes", "Passes", 1, 1, 4)
          .tooltip = "Applying it again flattens what one pass left; a few\n"
                     "passes approach a stable, blocky simplification.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      const int r = std::clamp(n.attrs.get_i("radius", 1), 1, 3);
      const int passes = std::clamp(n.attrs.get_i("passes", 1), 1, 4);
      const int w = in->w, h = in->h;
      Heightmap src = out;
      for (int pass = 0; pass < passes; ++pass) {
        std::swap(src.v, out.v);
        parallel_rows(h, [&](int y0, int y1) {
          std::vector<float> win((2 * r + 1) * (2 * r + 1));
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
              int k = 0;
              for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                  int nx = std::clamp(x + dx, 0, w - 1);
                  int ny = std::clamp(y + dy, 0, h - 1);
                  win[k++] = src.v[(size_t)ny * w + nx];
                }
              std::nth_element(win.begin(), win.begin() + k / 2,
                               win.begin() + k);
              out.v[(size_t)y * w + x] = win[k / 2];
            }
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
      apply_post(n, out);
    })

// ---------------------------------------------------------------- equalize
// Histogram equalisation: spreads the elevations so every band of the range
// gets used. A terrain that has drifted into using a third of its range -
// which is what a long chain of blends and erosions tends to leave - comes
// back with its full contrast, without anyone hand-tuning levels.
REGISTER_NODE(
    Equalize, "Filter",
    "Spreads elevations across the full range - contrast back after a long chain",
    [](Node &n) {
      setup_masked_filter(n);
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 1.f)
          .tooltip = "1 is full equalisation; lower blends back toward the\n"
                     "original distribution.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      in->minmax(mn, mx);
      const float span = mx - mn;
      if (span > 1e-12f) {
        // histogram -> cumulative distribution -> remap through it
        const int BINS = 2048;
        std::vector<uint32_t> hist(BINS, 0);
        for (float v : in->v) {
          int b = (int)((v - mn) / span * (BINS - 1));
          hist[std::clamp(b, 0, BINS - 1)]++;
        }
        std::vector<float> cdf(BINS);
        uint64_t acc = 0;
        const float inv_n = 1.f / (float)in->v.size();
        for (int b = 0; b < BINS; ++b) {
          acc += hist[b];
          cdf[b] = (float)acc * inv_n;
        }
        const float k = n.attrs.get_f("strength", 1.f);
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            float t = (in->v[i] - mn) / span;
            int b = std::clamp((int)(t * (BINS - 1)), 0, BINS - 1);
            float eq = mn + cdf[b] * span;
            out.v[i] = in->v[i] + (eq - in->v[i]) * k;
          }
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
      apply_post(n, out);
    })


// ------------------------------------------------------------------- curve
// A point-based tone curve for elevations, using the gradient editor that
// already exists: the stops' brightness is the transfer function. Photoshop's
// Curves for terrain - crush the midlands, lift the peaks, flatten a plateau
// band - drawn rather than parameterised.
REGISTER_NODE(
    Curve, "Filter",
    "Remaps elevations through a drawn curve - the gradient's brightness is the transfer function",
    [](Node &n) {
      setup_masked_filter(n);
      add_gradient(n.attrs, "curve", "Curve",
                   {{0.f, 0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 1.f, 1.f, 1.f}});
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 1.f);
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      in->minmax(mn, mx);
      const float span = mx - mn;
      if (span > 1e-12f) {
        const Attribute *g = n.attrs.find("curve");
        const float k = n.attrs.get_f("strength", 1.f);
        auto lum_at = [&](float t) {
          if (!g || g->stops.empty()) return t; // identity with no stops
          const auto &st = g->stops;
          if (t <= st.front().t)
            return (st.front().r + st.front().g + st.front().b) / 3.f;
          for (size_t i = 0; i + 1 < st.size(); ++i)
            if (t <= st[i + 1].t) {
              float f = (t - st[i].t) / std::max(st[i + 1].t - st[i].t, 1e-6f);
              float a = (st[i].r + st[i].g + st[i].b) / 3.f;
              float b = (st[i + 1].r + st[i + 1].g + st[i + 1].b) / 3.f;
              return a + (b - a) * f;
            }
          return (st.back().r + st.back().g + st.back().b) / 3.f;
        };
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            float t = (in->v[i] - mn) / span;
            float mapped = mn + lum_at(t) * span;
            out.v[i] = in->v[i] + (mapped - in->v[i]) * k;
          }
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
      apply_post(n, out);
    })

} // namespace gpx
