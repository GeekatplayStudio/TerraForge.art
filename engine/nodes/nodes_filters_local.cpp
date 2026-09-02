// Geekatplay Studio — locality-aware filters: edge-preserving smoothing,
// directional streaking, trend removal and relative relief. Every output
// pixel depends only on the input, so the row parallelism cannot change the
// result.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

// shared box blur (3 passes ~ gaussian), same shape as the one in
// nodes_filters.cpp but local to this TU
static void lf_blur(const Heightmap &in, Heightmap &out, int radius) {
  if (radius < 1) {
    out = in;
    return;
  }
  Heightmap tmp(in.w, in.h);
  out = in;
  for (int pass = 0; pass < 3; ++pass) {
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

REGISTER_NODE(
    Kuwahara, "Filter", "Edge-preserving smoothing (painterly flats)",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_int(n.attrs, "radius", "Radius (px)", 4, 1, 16, "Kuwahara");
      add_float(n.attrs, "mix", "Mix", 1.f, 0.f, 1.f, "Kuwahara");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int r = n.attrs.get_i("radius", 4);
      float mixv = n.attrs.get_f("mix", 1.f);
      int w = in->w, h = in->h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            // four overlapping quadrants around the pixel; take the mean of
            // the least-varying one — flats smooth, ridges stay ridges
            float best_mean = in->at(x, y), best_var = 1e30f;
            const int qx[4] = {-r, 0, -r, 0}, qy[4] = {-r, -r, 0, 0};
            for (int q = 0; q < 4; ++q) {
              float s = 0, s2 = 0;
              int cnt = 0;
              for (int dy = 0; dy <= r; ++dy)
                for (int dx = 0; dx <= r; ++dx) {
                  float v = in->atc(x + qx[q] + dx, y + qy[q] + dy);
                  s += v;
                  s2 += v * v;
                  ++cnt;
                }
              float mean = s / cnt;
              float var = s2 / cnt - mean * mean;
              if (var < best_var) {
                best_var = var;
                best_mean = mean;
              }
            }
            out.at(x, y) = in->at(x, y) + (best_mean - in->at(x, y)) * mixv;
          }
      });
    })

REGISTER_NODE(
    DirectionalBlur, "Filter", "Streak the surface along a direction",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "angle", "Direction °", 0.f, -180.f, 180.f, "Blur");
      add_float(n.attrs, "length", "Length", 0.05f, 0.002f, 0.5f, "Blur");
      add_bool(n.attrs, "both_ways", "Both directions", true, "Blur");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float a = n.attrs.get_f("angle") * 0.017453293f;
      float dx = std::cos(a), dy = std::sin(a);
      int steps = std::max(2, (int)(n.attrs.get_f("length", 0.05f) * in->w));
      bool both = n.attrs.get_b("both_ways", true);
      int lo = both ? -steps : 0;
      int w = in->w, h = in->h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float s = 0;
            int cnt = 0;
            for (int t = lo; t <= steps; ++t) {
              int sx = x + (int)std::lround(dx * t);
              int sy = y + (int)std::lround(dy * t);
              s += in->atc(sx, sy);
              ++cnt;
            }
            out.at(x, y) = s / cnt;
          }
      });
    })

REGISTER_NODE(
    Detrend, "Filter", "Subtract the best-fit plane",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "amount", "Amount", 1.f, 0.f, 1.f, "Detrend");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      // least-squares plane over the centered unit tile: with symmetric x/y
      // the normal equations decouple into three independent sums
      int w = in->w, h = in->h;
      double sz = 0, sxz = 0, syz = 0, sxx = 0, syy = 0;
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          double u = (x + 0.5) / w - 0.5, v = (y + 0.5) / h - 0.5;
          double z = in->at(x, y);
          sz += z;
          sxz += u * z;
          syz += v * z;
          sxx += u * u;
          syy += v * v;
        }
      double npix = (double)w * h;
      double c = sz / npix, ax = sxz / sxx, ay = syz / syy;
      float k = n.attrs.get_f("amount", 1.f);
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            double u = (x + 0.5) / w - 0.5, v = (y + 0.5) / h - 0.5;
            float plane = (float)(c + ax * u + ay * v);
            out.at(x, y) = in->at(x, y) - k * (plane - (float)c);
          }
      });
    })

REGISTER_NODE(
    RelativeElevation, "Analysis", "Height relative to the neighborhood",
    [](Node &n) {
      n.add_in("input");
      n.add_out("mask");
      add_int(n.attrs, "radius", "Radius (px)", 24, 2, 128, "Relief");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      m = *in;
      Heightmap mean(in->w, in->h);
      lf_blur(*in, mean, n.attrs.get_i("radius", 24));
      // 0.5 = at the local mean; ridges push toward 1, hollows toward 0
      float mx = 1e-12f;
      for (size_t i = 0; i < m.v.size(); ++i)
        mx = std::max(mx, std::fabs(in->v[i] - mean.v[i]));
      for (size_t i = 0; i < m.v.size(); ++i)
        m.v[i] = 0.5f + 0.5f * (in->v[i] - mean.v[i]) / mx;
    })

REGISTER_NODE(
    SmoothFill, "Filter", "Fill hollows up to the smoothed surface",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      n.add_out("fill_depth");
      add_int(n.attrs, "radius", "Radius (px)", 16, 1, 128, "Fill");
      add_choice(n.attrs, "direction", "Direction", {"Fill up", "Shave down"},
                 0, "Fill");
      add_float(n.attrs, "amount", "Amount", 1.f, 0.f, 1.f, "Fill");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &fd = n.out_hmap("fill_depth");
      out = *in;
      fd = *in;
      Heightmap sm(in->w, in->h);
      lf_blur(*in, sm, n.attrs.get_i("radius", 16));
      bool up = n.attrs.get_choice("direction") == 0;
      float k = n.attrs.get_f("amount", 1.f);
      for (size_t i = 0; i < out.v.size(); ++i) {
        float target = up ? std::max(in->v[i], sm.v[i])
                          : std::min(in->v[i], sm.v[i]);
        float moved = (target - in->v[i]) * k;
        out.v[i] = in->v[i] + moved;
        fd.v[i] = std::fabs(moved);
      }
    })

REGISTER_NODE(
    MakeTileable, "Transform", "Blend the tile so it wraps seamlessly",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "feather", "Feather", 1.f, 0.1f, 1.f, "Tiling");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int w = in->w, h = in->h;
      float k = std::max(n.attrs.get_f("feather", 1.f), 0.05f);
      // separable periodic blend: along each axis, crossfade with the
      // half-offset copy under a cosine weight that is itself periodic, so
      // the wrap is continuous by construction (each copy's own seam lands
      // where the other copy holds all the weight). Feather sharpens the
      // crossfade toward the seams.
      auto uw = [&](int i, int nn) {
        float u = 0.5f - 0.5f * std::cos(6.2831853f * i / nn);
        return std::pow(u, 1.f / k);
      };
      Heightmap tmp(w, h);
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float u = uw(x, w);
            tmp.at(x, y) =
                in->at(x, y) * u + in->at((x + w / 2) % w, y) * (1.f - u);
          }
      });
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float u = uw(y, h);
            out.at(x, y) =
                tmp.at(x, y) * u + tmp.at(x, (y + h / 2) % h) * (1.f - u);
          }
      });
    })

} // namespace gpx
