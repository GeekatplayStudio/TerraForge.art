// Geekatplay Studio — locality-aware filters: edge-preserving smoothing,
// directional streaking, trend removal and relative relief. Every output
// pixel depends only on the input, so the row parallelism cannot change the
// result.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
      add_int(n.attrs, "radius", "Radius (px)", 4, 1, 16, "Kuwahara")
          .tooltip = "How far it looks for a flat neighbourhood to average\n"
                     "instead. Unlike a blur this keeps the edges: it\n"
                     "flattens the ground between features while leaving\n"
                     "the breaks between them sharp.";
      add_float(n.attrs, "mix", "Mix", 1.f, 0.f, 1.f, "Kuwahara")
          .tooltip = "How much of the flattened result replaces the\n"
                     "original. The full effect is strongly painterly;\n"
                     "part of it just calms a noisy surface.";
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
      add_float(n.attrs, "angle", "Direction °", 0.f, -180.f, 180.f, "Blur")
          .tooltip = "Which way the streaks run. Along the prevailing wind\n"
                     "this reads as scouring; down the slope, as material\n"
                     "having run.";
      add_float(n.attrs, "length", "Length", 0.05f, 0.002f, 0.5f, "Blur")
          .tooltip = "How far the smearing reaches, as a fraction of the\n"
                     "tile.";
      add_bool(n.attrs, "both_ways", "Both directions", true, "Blur")
          .tooltip = "On, it smears symmetrically and the surface stays\n"
                     "put. Off, it drags one way only, so features shift\n"
                     "downstream as well as blurring - which is what makes\n"
                     "it look like flow rather than blur.";
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
      add_float(n.attrs, "amount", "Amount", 1.f, 0.f, 1.f, "Detrend")
          .tooltip = "How much of the overall tilt is removed. An imported\n"
                     "heightfield often leans as a whole; taking the plane\n"
                     "out levels it without touching the relief on top.";
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
      add_int(n.attrs, "radius", "Radius (px)", 24, 2, 128, "Relief")
          .tooltip = "How far out the surrounding ground is sampled before\n"
                     "asking how high this point stands above it. Small radii\n"
                     "find local bumps; large ones find whether you are on a\n"
                     "ridge or in a valley at all.";
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
      add_int(n.attrs, "radius", "Radius (px)", 16, 1, 128, "Fill")
          .tooltip = "How large a hollow counts as one worth filling.\n"
                     "Anything narrower than this is levelled; anything\n"
                     "broader is left as terrain.";
      add_choice(n.attrs, "direction", "Direction", {"Fill up", "Shave down"},
                 0, "Fill")
          .tooltip = "Fill up raises hollows to the smoothed surface -\n"
                     "sediment settling into dips. Shave down cuts the\n"
                     "bumps off instead, which is weathering rather than\n"
                     "deposition. The second output reports how much was\n"
                     "moved, which makes a good sediment mask.";
      add_float(n.attrs, "amount", "Amount", 1.f, 0.f, 1.f, "Fill")
          .tooltip = "How much of the way to the smoothed surface it\n"
                     "goes. Part-way leaves the hollow visible but\n"
                     "softened.";
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
    HydraulicBlur, "Erosion", "The erosion look at one percent of the cost",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_int(n.attrs, "radius", "Radius (px)", 8, 1, 64, "Blur")
          .tooltip = "How far the smoothing reaches. This is a blur that follows\n"
                     "the drainage, so it softens the slopes water would have\n"
                     "run down while leaving the ridge lines alone.";
      add_float(n.attrs, "amount", "Amount", 0.7f, 0.f, 1.f, "Blur")
          .tooltip = "How much of the blurred result is mixed in. Full strength\n"
                     "reads as a landscape long weathered; a little takes the\n"
                     "hard edges off fresh erosion.";
      add_float(n.attrs, "keep_ridges", "Keep ridges", 0.7f, 0.f, 1.f, "Blur")
          .tooltip = "Convex ground (ridges, crests) resists the smoothing;\n"
                     "concave ground (gullies, hollows) takes it fully -\n"
                     "which is the shape hydraulic erosion carves.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int r = n.attrs.get_i("radius", 8);
      float amount = n.attrs.get_f("amount", 0.7f);
      float keep = n.attrs.get_f("keep_ridges", 0.7f);
      Heightmap bl(in->w, in->h);
      lf_blur(*in, bl, r);
      // curvature decides who gets smoothed: below the local mean = concave
      // = valley = full blur; above = convex = ridge = protected
      int w = in->w, h = in->h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            float conv = in->v[i] - bl.v[i]; // + on ridges, - in hollows
            float protect = conv > 0.f
                                ? std::min(conv * 40.f, 1.f) * keep
                                : 0.f;
            float k = amount * (1.f - protect);
            out.v[i] = in->v[i] + (bl.v[i] - in->v[i]) * k;
          }
      });
    })

REGISTER_NODE(
    SetBorders, "Transform", "Pin the tile's borders to a level",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "level", "Border level", 0.f, -1.f, 2.f, "Borders")
          .tooltip = "The height the borders are pinned to.";
      add_float(n.attrs, "feather", "Feather", 0.1f, 0.005f, 0.5f, "Borders")
          .tooltip = "How far in from the border the pinning reaches, so the\n"
                     "terrain eases to it rather than dropping at the edge.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float level = n.attrs.get_f("level", 0.f);
      float fe = n.attrs.get_f("feather", 0.1f);
      int w = in->w, h = in->h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float ex = std::min(x, w - 1 - x) / (float)w;
            float ey = std::min(y, h - 1 - y) / (float)h;
            float t = std::clamp(std::min(ex, ey) / fe, 0.f, 1.f);
            t = t * t * (3.f - 2.f * t);
            out.at(x, y) = level + (in->at(x, y) - level) * t;
          }
      });
    })

REGISTER_NODE(
    DetailEqualizer, "Filter", "Per-band detail gains, like an audio EQ",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "fine", "Fine (1-4 px)", 1.f, 0.f, 3.f, "Bands")
          .tooltip = "The grain of the surface. Turning it down calms a\n"
                     "noisy terrain without softening its shape; turning\n"
                     "it up sharpens the texture without adding relief.";
      add_float(n.attrs, "medium", "Medium (4-16 px)", 1.f, 0.f, 3.f, "Bands")
          .tooltip = "Gullies and small outcrops - the scale that carries\n"
                     "most of a landscape's character.";
      add_float(n.attrs, "coarse", "Coarse (16-64 px)", 1.f, 0.f, 3.f, "Bands")
          .tooltip = "Ridges and valleys: the landforms themselves.";
      add_float(n.attrs, "base", "Base (blur 64 px+)", 1.f, 0.f, 3.f, "Bands")
          .tooltip = "The overall lie of the land under everything else.\n"
                     "Turning this down flattens the map without losing\n"
                     "any of its detail - all four bands together are the\n"
                     "original, so 1 everywhere changes nothing.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      // a blur pyramid: each band is the difference between two blur radii,
      // scaled by its gain, and the deepest blur is the base. Gains of 1
      // reconstruct the input exactly, which is the property that makes this
      // an equalizer rather than a filter.
      float g[4] = {n.attrs.get_f("fine", 1.f), n.attrs.get_f("medium", 1.f),
                    n.attrs.get_f("coarse", 1.f), n.attrs.get_f("base", 1.f)};
      const int radii[3] = {4, 16, 64};
      Heightmap b_prev = *in, b_cur(in->w, in->h);
      for (size_t i = 0; i < out.v.size(); ++i) out.v[i] = 0.f;
      for (int band = 0; band < 3; ++band) {
        lf_blur(*in, b_cur, radii[band]);
        for (size_t i = 0; i < out.v.size(); ++i)
          out.v[i] += (b_prev.v[i] - b_cur.v[i]) * g[band];
        std::swap(b_prev, b_cur);
      }
      for (size_t i = 0; i < out.v.size(); ++i) out.v[i] += b_prev.v[i] * g[3];
    })

REGISTER_NODE(
    Convolve, "Filter", "Convolution by a preset or typed kernel",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_choice(n.attrs, "kernel", "Kernel",
                 {"Sharpen", "Edge (laplacian)", "Emboss NW", "Sobel X",
                  "Sobel Y", "Custom"},
                 0, "Convolve")
          .tooltip = "A small matrix swept over the map. Sharpen boosts\n"
                     "local contrast; the edge and Sobel kernels find\n"
                     "boundaries and make good masks rather than terrain;\n"
                     "Emboss lights it from one side. Custom takes your\n"
                     "own numbers below.";
      add_text(n.attrs, "custom", "Custom (row-major)",
               "0 -1 0  -1 5 -1  0 -1 0")
          .tooltip = "9 or 25 numbers, row-major 3x3 or 5x5, any whitespace.";
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 4.f, "Convolve")
          .tooltip = "Scales the kernel's result before it is used.";
      add_bool(n.attrs, "add_to_input", "Add to input", false, "Convolve")
          .tooltip = "On, the result is added on top of the original\n"
                     "terrain instead of replacing it - which is how an\n"
                     "edge kernel becomes extra relief along the breaks\n"
                     "rather than a picture of them.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      static const float K[5][9] = {
          {0, -1, 0, -1, 5, -1, 0, -1, 0},   // sharpen
          {0, 1, 0, 1, -4, 1, 0, 1, 0},      // laplacian
          {-2, -1, 0, -1, 1, 1, 0, 1, 2},    // emboss
          {-1, 0, 1, -2, 0, 2, -1, 0, 1},    // sobel x
          {-1, -2, -1, 0, 0, 0, 1, 2, 1},    // sobel y
      };
      int which = n.attrs.get_choice("kernel");
      std::vector<float> k;
      int kr = 1;
      if (which < 5) {
        k.assign(K[which], K[which] + 9);
      } else {
        const Attribute *ca = n.attrs.find("custom");
        const char *c = ca ? ca->s.c_str() : "";
        while (*c) {
          char *end = nullptr;
          float v = std::strtof(c, &end);
          if (end == c) { ++c; continue; }
          c = end;
          k.push_back(v);
        }
        if (k.size() >= 25) { k.resize(25); kr = 2; }
        else if (k.size() >= 9) { k.resize(9); kr = 1; }
        else {
          n.error = "custom kernel needs 9 or 25 numbers";
          return;
        }
      }
      float strength = n.attrs.get_f("strength", 1.f);
      bool add_in = n.attrs.get_b("add_to_input");
      int kw = 2 * kr + 1;
      int w = in->w, h = in->h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float s = 0;
            for (int dy = -kr; dy <= kr; ++dy)
              for (int dx = -kr; dx <= kr; ++dx)
                s += in->atc(x + dx, y + dy) *
                     k[(size_t)(dy + kr) * kw + (dx + kr)];
            s *= strength;
            out.at(x, y) = add_in ? in->at(x, y) + s : s;
          }
      });
    })

REGISTER_NODE(
    MakeTileable, "Transform", "Blend the tile so it wraps seamlessly",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "feather", "Feather", 1.f, 0.1f, 1.f, "Tiling")
          .tooltip = "How much of the tile is blended into its opposite edge.\n"
                     "The wider the blend the more seamless the wrap, and the\n"
                     "more of the original is lost.";
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
