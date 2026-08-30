// Geekatplay Studio — selector/mask nodes (slope, altitude, curvature...)
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"

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

} // namespace gpx
