// Geekatplay Studio — selector/mask nodes (slope, altitude, curvature...)
#include "gpx/node_graph.hpp"
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
namespace {

// d[i] = min over j of (i-j)^2 + f[j]. v/z are scratch (size n and n+1).
void edt_line(const float *f, float *d, int *v, float *z, int n) {
  int k = 0;
  v[0] = 0;
  z[0] = -1e18f;
  z[1] = 1e18f;
  for (int q = 1; q < n; ++q) {
    float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) /
              (2.f * q - 2.f * v[k]);
    while (s <= z[k]) {
      --k;
      s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) /
          (2.f * q - 2.f * v[k]);
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = 1e18f;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < (float)q) ++k;
    d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
  }
}

// squared EDT of a binary grid: 0 where inside(i), a large sentinel elsewhere.
// Row passes are independent, then column passes are independent, so the
// parallelism cannot change the result (AGENTS.md engine rule 1).
void edt_2d(std::vector<float> &g, int w, int h) {
  const float FAR = 1e12f; // far greater than any real squared distance
  for (float &v : g) v = v > 0.5f ? 0.f : FAR;
  parallel_rows(h, [&](int y0, int y1) {
    std::vector<float> f(w), d(w), z(w + 1);
    std::vector<int> vv(w);
    for (int y = y0; y < y1; ++y) {
      float *row = g.data() + (size_t)y * w;
      std::copy(row, row + w, f.begin());
      edt_line(f.data(), d.data(), vv.data(), z.data(), w);
      std::copy(d.begin(), d.end(), row);
    }
  });
  parallel_rows(w, [&](int x0, int x1) {
    std::vector<float> f(h), d(h), z(h + 1);
    std::vector<int> vv(h);
    for (int x = x0; x < x1; ++x) {
      for (int y = 0; y < h; ++y) f[y] = g[(size_t)y * w + x];
      edt_line(f.data(), d.data(), vv.data(), z.data(), h);
      for (int y = 0; y < h; ++y) g[(size_t)y * w + x] = d[y];
    }
  });
}

} // namespace

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
        edt_2d(dist_out, in->w, in->h);
        edt_2d(dist_in, in->w, in->h);
        for (size_t i = 0; i < out.v.size(); ++i) {
          float d = std::sqrt(dist_out[i]) - std::sqrt(dist_in[i]);
          out.v[i] = std::clamp(0.5f + 0.5f * d / reach, 0.f, 1.f);
        }
      } else {
        edt_2d(dist_out, in->w, in->h);
        for (size_t i = 0; i < out.v.size(); ++i) {
          float t = std::clamp(std::sqrt(dist_out[i]) / reach, 0.f, 1.f);
          out.v[i] = mode == 1 ? t : 1.f - t;
        }
      }
      apply_post(n, out);
    })


} // namespace gpx
