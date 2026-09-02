// Geekatplay Studio — selector/mask nodes (slope, altitude, curvature...)
#include "gpx/node_graph.hpp"
#include "gpx/distance.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

static void setup_selector(Node &n) {
  n.add_in("input");
  n.add_out("mask");
  add_float(n.attrs, "smoothing", "Edge softness", 0.1f, 0.001f, 1.f, "Selection");
  add_bool(n.attrs, "invert", "Invert", false, "Selection");
}

static void finish_mask(Node &n, Heightmap &m) {
  m.remap(0.f, 1.f);
  if (n.attrs.get_b("invert"))
    for (auto &v : m.v) v = 1.f - v;
}

REGISTER_NODE(
    SelectAltitude, "Mask", "Select by height band",
    [](Node &n) {
      setup_selector(n);
      add_range(n.attrs, "band", "Altitude band", 0.5f, 1.f, 0.f, 1.f, "Selection");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      float lo, hi;
      n.attrs.get_range("band", lo, hi);
      float soft = n.attrs.get_f("smoothing", 0.1f);
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_index(m.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = (in->v[i] - mn) / d;
          float a = std::clamp((t - lo) / soft + 0.5f, 0.f, 1.f);
          float b = std::clamp((hi - t) / soft + 0.5f, 0.f, 1.f);
          m.v[i] = std::min(a, b);
        }
      });
      finish_mask(n, m);
    })

REGISTER_NODE(
    SelectMidrange, "Mask", "Select the middle elevations",
    [](Node &n) {
      setup_selector(n);
      add_float(n.attrs, "center", "Center", 0.5f, 0.f, 1.f, "Selection");
      add_float(n.attrs, "width", "Width", 0.25f, 0.02f, 1.f, "Selection");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      m = *in;
      float c = n.attrs.get_f("center", 0.5f);
      float wdt = n.attrs.get_f("width", 0.25f);
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_index(m.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = ((in->v[i] - mn) / d - c) / wdt;
          m.v[i] = std::exp(-t * t * 4.f); // gaussian bell over the band
        }
      });
      finish_mask(n, m);
    })

REGISTER_NODE(
    SelectTransitions, "Mask", "Select where two surfaces trade places",
    [](Node &n) {
      n.add_in("input A");
      n.add_in("input B");
      n.add_out("mask");
      add_float(n.attrs, "tolerance", "Tolerance", 0.05f, 0.001f, 0.5f,
                "Selection");
      add_bool(n.attrs, "invert", "Invert", false, "Selection");
    },
    [](Node &n) {
      const Heightmap *a = require_in(n, "input A");
      const Heightmap *b = n.in_hmap("input B");
      if (!a || !b) return;
      Heightmap &m = n.out_hmap("mask");
      m = *a;
      float tol = n.attrs.get_f("tolerance", 0.05f);
      bool inv = n.attrs.get_b("invert");
      size_t nn = std::min(a->v.size(), b->v.size());
      parallel_index(nn, [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = std::clamp(1.f - std::fabs(a->v[i] - b->v[i]) / tol, 0.f, 1.f);
          t = t * t * (3.f - 2.f * t);
          m.v[i] = inv ? 1.f - t : t;
        }
      });
    })

REGISTER_NODE(
    SelectBorder, "Mask", "A band along a mask's boundary",
    [](Node &n) {
      n.add_in("input");
      n.add_out("mask");
      add_float(n.attrs, "threshold", "Threshold", 0.5f, 0.f, 1.f, "Selection");
      add_float(n.attrs, "reach", "Reach", 0.05f, 0.002f, 0.5f, "Selection");
      add_choice(n.attrs, "side", "Side", {"Both", "Inward", "Outward"}, 0,
                 "Selection");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      m = *in;
      float thr = n.attrs.get_f("threshold", 0.5f);
      float reach = std::max(n.attrs.get_f("reach", 0.05f), 1e-4f) * in->w;
      int side = n.attrs.get_choice("side");
      // distance to the inside and to the outside; the boundary is where
      // either is small on the wrong side of the threshold
      std::vector<float> din(in->v.size()), dout(in->v.size());
      for (size_t i = 0; i < in->v.size(); ++i) {
        din[i] = in->v[i] >= thr ? 1.f : 0.f;  // -> distance to the shape
        dout[i] = in->v[i] >= thr ? 0.f : 1.f; // -> distance to the outside
      }
      edt_squared(din, in->w, in->h);
      edt_squared(dout, in->w, in->h);
      parallel_index(m.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          bool inside = in->v[i] >= thr;
          float d = std::sqrt(inside ? dout[i] : din[i]);
          float t = std::clamp(1.f - d / reach, 0.f, 1.f);
          if (side == 1 && !inside) t = 0.f;
          if (side == 2 && inside) t = 0.f;
          m.v[i] = t * t * (3.f - 2.f * t);
        }
      });
    })

REGISTER_NODE(
    SelectSlope, "Mask", "Select by slope steepness",
    [](Node &n) {
      setup_selector(n);
      add_range(n.attrs, "band", "Slope band", 0.3f, 1.f, 0.f, 1.f, "Selection");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      float lo, hi;
      n.attrs.get_range("band", lo, hi);
      float soft = n.attrs.get_f("smoothing", 0.1f);
      float mn, mx;
      in->minmax(mn, mx);
      float amp = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      // slope normalized by amplitude
      Heightmap slope(in->w, in->h);
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float dx, dy;
            in->gradient_at(x, y, dx, dy);
            slope.at(x, y) = std::sqrt(dx * dx + dy * dy) * in->w / amp;
          }
      });
      slope.remap(0.f, 1.f);
      parallel_index(m.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = slope.v[i];
          float a = std::clamp((t - lo) / soft + 0.5f, 0.f, 1.f);
          float b = std::clamp((hi - t) / soft + 0.5f, 0.f, 1.f);
          m.v[i] = std::min(a, b);
        }
      });
      finish_mask(n, m);
    })

REGISTER_NODE(
    SelectCurvature, "Mask", "Select concave (valleys) or convex (ridges)",
    [](Node &n) {
      setup_selector(n);
      add_choice(n.attrs, "mode", "Mode", {"Convex (ridges)", "Concave (valleys)"}, 0,
                 "Selection");
      add_float(n.attrs, "scale", "Feature scale", 0.01f, 0.002f, 0.1f, "Selection");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      int r = std::max(1, (int)(n.attrs.get_f("scale", 0.01f) * in->w));
      bool concave = n.attrs.get_choice("mode") == 1;
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float lap = in->atc(x - r, y) + in->atc(x + r, y) + in->atc(x, y - r) +
                        in->atc(x, y + r) - 4.f * in->at(x, y);
            m.at(x, y) = concave ? lap : -lap;
          }
      });
      finish_mask(n, m);
    })

REGISTER_NODE(
    SelectCavities, "Mask", "Ambient-occlusion-like cavity map",
    [](Node &n) {
      setup_selector(n);
      add_float(n.attrs, "radius", "Radius", 0.02f, 0.005f, 0.1f, "Selection");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      int r = std::max(1, (int)(n.attrs.get_f("radius", 0.02f) * in->w));
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float hgt = in->at(x, y), occ = 0;
            const int K = 8;
            for (int k = 0; k < K; ++k) {
              float a = k * 6.2831853f / K;
              int sx = x + (int)(std::cos(a) * r), sy = y + (int)(std::sin(a) * r);
              occ += std::max(in->atc(sx, sy) - hgt, 0.f);
            }
            m.at(x, y) = occ / K;
          }
      });
      finish_mask(n, m);
    })


// ------------------------------------------------------- distance transform
// Exact Euclidean distance to a shape, in one linear pass per axis
// (Felzenszwalb & Huttenlocher 2004, "Distance transforms of sampled
// functions": the squared distance along a line is the lower envelope of
// parabolas, and the envelope is computable left to right).
//
// This is the mask tool that unlocks the rest: distance to the shoreline is
// a beach gradient, distance to a river mask is a wetness falloff, distance
// to a ridge mask fades scree with altitude. Anything that should happen
// "near" something starts here.

REGISTER_NODE(
    DistanceField, "Mask",
    "Distance to a shape - shoreline gradients, wetness falloffs, anything that happens near something",
    [](Node &n) {
      n.add_in("input");
      n.add_out("mask");
      add_float(n.attrs, "threshold", "Shape threshold", 0.5f, 0.f, 1.f,
                "Shape")
          .tooltip = "Where the input counts as being the shape. Feed a mask\n"
                     "and leave it at 0.5; feed a heightmap and this becomes\n"
                     "the altitude the distance is measured from.";
      add_bool(n.attrs, "invert_input", "Measure from the outside", false,
               "Shape")
          .tooltip = "Swap what counts as the shape: distance from dry land\n"
                     "instead of distance from the water.";
      add_choice(n.attrs, "mode", "Output",
                 {"Fade from the shape", "Distance from the shape",
                  "Signed distance"},
                 0, "Distance")
          .tooltip = "Fade: 1 at the shape, falling to 0 at the reach - a\n"
                     "ready-made falloff mask.\n"
                     "Distance: 0 at the shape, 1 at the reach.\n"
                     "Signed: 0.5 on the edge, below inside, above outside.";
      add_float(n.attrs, "reach", "Reach", 0.15f, 0.005f, 1.f, "Distance")
          .tooltip = "How far the field extends, as a fraction of the tile.\n"
                     "Everything further than this saturates.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("mask");
      out = *in; // sized from the input, whatever the graph resolution is

      const float thr = n.attrs.get_f("threshold", 0.5f);
      const bool from_outside = n.attrs.get_b("invert_input", false);
      const int mode = n.attrs.get_choice("mode");
      const float reach =
          std::max(n.attrs.get_f("reach", 0.15f), 1e-4f) * (float)in->w;

      // squared distance to the shape...
      std::vector<float> dist_out(in->v.size());
      for (size_t i = 0; i < in->v.size(); ++i) {
        bool inside = in->v[i] >= thr;
        if (from_outside) inside = !inside;
        dist_out[i] = inside ? 1.f : 0.f;
      }
      if (mode == 2) {
        // ...and, for the signed form, to its complement as well
        std::vector<float> dist_in(dist_out);
        for (float &v : dist_in) v = 1.f - v;
        edt_squared(dist_out, in->w, in->h);
        edt_squared(dist_in, in->w, in->h);
        for (size_t i = 0; i < out.v.size(); ++i) {
          float d = std::sqrt(dist_out[i]) - std::sqrt(dist_in[i]);
          out.v[i] = std::clamp(0.5f + 0.5f * d / reach, 0.f, 1.f);
        }
      } else {
        edt_squared(dist_out, in->w, in->h);
        for (size_t i = 0; i < out.v.size(); ++i) {
          float t = std::clamp(std::sqrt(dist_out[i]) / reach, 0.f, 1.f);
          out.v[i] = mode == 1 ? t : 1.f - t;
        }
      }
      apply_post(n, out);
    })


} // namespace gpx
