// Geekatplay Studio - landform primitives: fake stones, craters, dunes.
// Split from nodes_surface.cpp for the 500-line module rule; the goldens pin
// that nothing moved but the text.
// Geekatplay TerraForge — surface realism nodes, modeled on the reference
// parameter sets of Terragen (Power Fractal, Fake Stones) and Gaea
// (Stratify, Shear, Craggy, Crater, Dunes, Snow). All reimplemented from
// published behavior on a regular heightmap grid.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"

namespace gpx {

// -------------------------------------------------------------- FakeStones
REGISTER_NODE(
    FakeStones, "Primitive", "Terragen-style fake stones: boulders/rocks as displacement",
    [](Node &n) {
      n.add_in("input");
      n.add_in("density_mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("stone_mask");
      add_float(n.attrs, "stone_scale", "Stone scale", 0.03f, 0.004f, 0.25f,
                "Stones")
          .tooltip = "Stone size as a fraction of terrain width;\n"
                     "smaller = more, denser stones.";
      add_float(n.attrs, "density", "Stone density", 0.5f, 0.02f, 1.f, "Stones");
      add_float(n.attrs, "tallness", "Stone tallness", 0.6f, 0.05f, 2.f, "Stones");
      add_float(n.attrs, "pancake", "Pancake effect", 0.3f, 0.f, 1.f, "Stones")
          .tooltip = "Squashes stones flat into slabs while keeping their\n"
                     "footprint — 0 round boulders, 1 flat plates.";
      add_seed(n.attrs, "seed", "Seed", 0, "Stones");
      add_float(n.attrs, "vary_density", "Vary density", 0.6f, 0.f, 1.f,
                "Variation")
          .tooltip = "Large-scale patchiness: clusters of stones with\n"
                     "clear ground between.";
      add_float(n.attrs, "vary_scale", "Density variation scale", 4.f, 1.f, 16.f,
                "Variation");
      add_float(n.attrs, "size_jitter", "Size variation", 0.5f, 0.f, 1.f,
                "Variation");
      add_range(n.attrs, "slope_band", "Grow on slopes", 0.f, 0.6f, 0.f, 1.f,
                "Placement")
          .tooltip = "Stones appear only where terrain slope is inside\n"
                     "this band (rockfall collects on gentler ground).";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &smask = n.out_hmap("stone_mask");
      out = *in;
      float scale = n.attrs.get_f("stone_scale", 0.03f);
      float density = n.attrs.get_f("density", 0.5f);
      float tallness = n.attrs.get_f("tallness", 0.6f);
      float pancake = n.attrs.get_f("pancake", 0.3f);
      uint32_t seed = n.attrs.get_seed("seed");
      float vary = n.attrs.get_f("vary_density", 0.6f);
      float vscale = n.attrs.get_f("vary_scale", 4.f);
      float sjit = n.attrs.get_f("size_jitter", 0.5f);
      float slo, shi;
      n.attrs.get_range("slope_band", slo, shi);
      const Heightmap *dmask = n.in_hmap("density_mask");
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      float cell = 1.f / scale; // cells across terrain
      noise::FbmParams vf;
      vf.octaves = 3;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float cx = u * cell, cy = v * cell;
            int xi = (int)std::floor(cx), yi = (int)std::floor(cy);
            float add = 0;
            // check 3x3 neighbor cells for overlapping stones
            for (int dy = -1; dy <= 1; ++dy)
              for (int dx = -1; dx <= 1; ++dx) {
                int gx2 = xi + dx, gy2 = yi + dy;
                // per-cell existence by density (+ patchiness + mask + slope)
                float exist = noise::hash01(gx2, gy2, seed);
                float local_density = density;
                if (vary > 0) {
                  float pn = noise::fbm(gx2 / cell * vscale, gy2 / cell * vscale,
                                        seed ^ 0x55u, vf) * 0.5f + 0.5f;
                  local_density *= std::clamp(1.f - vary + vary * pn * 2.f, 0.f, 1.f);
                }
                if (exist > local_density) continue;
                // stone center jittered in cell
                float sx = gx2 + 0.2f + 0.6f * noise::hash01(gx2, gy2, seed ^ 3u);
                float sy = gy2 + 0.2f + 0.6f * noise::hash01(gx2, gy2, seed ^ 7u);
                float r = (0.35f + 0.25f * (noise::hash01(gx2, gy2, seed ^ 11u) - 0.5f) *
                                        2.f * sjit);
                float ddx = cx - sx, ddy = cy - sy;
                float d2 = (ddx * ddx + ddy * ddy) / (r * r);
                if (d2 >= 1.f) continue;
                // irregular dome: perturb the profile per angle
                float ang = std::atan2(ddy, ddx);
                float irr = 1.f + 0.25f * std::sin(ang * 3.f +
                                noise::hash01(gx2, gy2, seed ^ 13u) * 6.28f) *
                                std::sqrt(d2);
                float dome = std::sqrt(std::max(1.f - d2 * irr, 0.f));
                float hgt = tallness * scale * hamp * (1.f - pancake * 0.75f);
                add = std::max(add, dome * hgt);
              }
            if (add > 0) {
              // slope placement check at this texel
              float gx3, gy3;
              in->gradient_at(x, y, gx3, gy3);
              float slope = std::atan(std::sqrt(gx3 * gx3 + gy3 * gy3) * in->w / hamp) *
                            0.63662f;
              if (slope < slo || slope > shi) add = 0;
              if (dmask && !dmask->empty())
                add *= std::clamp(dmask->v[(size_t)y * out.w + x], 0.f, 1.f);
            }
            out.at(x, y) = in->at(x, y) + add;
            smask.at(x, y) = add;
          }
      });
      smask.remap(0.f, 1.f);
    })

// ------------------------------------------------------------------ Crater
REGISTER_NODE(
    Crater, "Primitive", "Impact craters: bowl, rim lip, ejecta blanket (single or field)",
    [](Node &n) {
      n.add_in("input", DataType::Heightmap, true);
      n.add_out("output");
      add_choice(n.attrs, "profile", "Profile", {"Single crater", "Crater field"}, 0,
                 "Craters");
      add_float(n.attrs, "scale", "Scale", 0.3f, 0.02f, 1.f, "Craters");
      add_float(n.attrs, "depth", "Depth", 0.4f, 0.05f, 1.f, "Craters");
      add_float(n.attrs, "lip", "Rim lip", 0.5f, 0.f, 1.f, "Craters")
          .tooltip = "Sharpness/height of the raised rim wall.";
      add_float(n.attrs, "outer_scale", "Ejecta extent", 0.6f, 0.1f, 2.f, "Craters");
      add_float(n.attrs, "floor", "Floor level", 0.15f, 0.f, 1.f, "Craters")
          .tooltip = "Clamps the bowl bottom — flat crater floors.";
      add_float(n.attrs, "irregular", "Rim irregularity", 0.3f, 0.f, 1.f, "Craters");
      add_vec2(n.attrs, "center", "Position", 0.5f, 0.5f, -0.2f, 1.2f, "Craters");
      add_int(n.attrs, "count", "Field count", 12, 2, 64, "Field");
      add_seed(n.attrs, "seed", "Seed", 0, "Field");
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      const Heightmap *in = n.in_hmap("input");
      if (in && !in->empty()) out = *in;
      else std::fill(out.v.begin(), out.v.end(), 0.5f);
      bool field = n.attrs.get_choice("profile") == 1;
      float scale = n.attrs.get_f("scale", 0.3f);
      float depth = n.attrs.get_f("depth", 0.4f);
      float lip = n.attrs.get_f("lip", 0.5f);
      float outer = n.attrs.get_f("outer_scale", 0.6f);
      float floor_lv = n.attrs.get_f("floor", 0.15f);
      float irreg = n.attrs.get_f("irregular", 0.3f);
      uint32_t seed = n.attrs.get_seed("seed");
      int count = field ? n.attrs.get_i("count", 12) : 1;
      float ccx, ccy;
      n.attrs.get_vec2("center", ccx, ccy);
      float mn, mx;
      out.minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      struct Cr { float x, y, r, d; };
      std::vector<Cr> crs;
      for (int c = 0; c < count; ++c) {
        Cr cr;
        if (field) {
          cr.x = noise::hash01(c, 1, seed);
          cr.y = noise::hash01(c, 2, seed);
          // power-law size distribution: many small, few large
          float t = noise::hash01(c, 3, seed);
          cr.r = scale * 0.5f * (0.15f + 0.85f * t * t * t);
        } else {
          cr.x = ccx;
          cr.y = ccy;
          cr.r = scale * 0.5f;
        }
        cr.d = depth * cr.r / (scale * 0.5f);
        crs.push_back(cr);
      }
      noise::FbmParams rf;
      rf.octaves = 3;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float hgt = out.at(x, y);
            for (const Cr &cr : crs) {
              float dx = u - cr.x, dy = v - cr.y;
              float ang = std::atan2(dy, dx);
              float rr = cr.r * (1.f + irreg * 0.2f *
                                 noise::fbm(std::cos(ang) * 2 + cr.x * 40,
                                            std::sin(ang) * 2 + cr.y * 40, seed, rf));
              float d = std::sqrt(dx * dx + dy * dy) / std::max(rr, 1e-5f);
              if (d > 1.f + outer) continue;
              float dd = cr.d * hamp;
              if (d < 1.f) {
                // parabolic bowl with flat floor + rim rise near d=1
                float bowl = -(1.f - d * d) * dd;
                float fmin = -dd * (1.f - floor_lv);
                bowl = std::max(bowl, fmin);
                float rim = std::exp(-std::pow((d - 1.f) / (0.12f + 0.2f * (1 - lip)), 2.f)) *
                            dd * 0.35f * lip;
                hgt += bowl + rim;
              } else {
                // ejecta blanket decays outward
                float e = (d - 1.f) / std::max(outer, 1e-4f);
                float rim = std::exp(-std::pow((d - 1.f) / (0.12f + 0.2f * (1 - lip)), 2.f)) *
                            dd * 0.35f * lip;
                float ej = std::exp(-e * 3.f) * dd * 0.12f;
                hgt += rim + ej;
              }
            }
            out.at(x, y) = hgt;
          }
      });
      apply_post(n, out);
    })

// ------------------------------------------------------------------- Dunes
REGISTER_NODE(
    Dunes, "Primitive", "Sand dunes: asymmetric slip faces, crest chaos, ripples",
    [](Node &n) {
      n.add_in("envelope", DataType::Heightmap, true);
      n.add_out("output");
      add_float(n.attrs, "wind_dir", "Wind direction °", 30.f, -180.f, 180.f, "Dunes");
      add_float(n.attrs, "wavelength", "Dune wavelength", 0.12f, 0.02f, 0.5f, "Dunes");
      add_float(n.attrs, "asymmetry", "Asymmetry", 0.75f, 0.5f, 0.95f, "Dunes")
          .tooltip = "Windward slope is long and gentle; the slip face is\n"
                     "short and steep (real dunes ~0.8).";
      add_float(n.attrs, "chaos", "Crest chaos", 0.5f, 0.f, 1.f, "Dunes");
      add_float(n.attrs, "ripples", "Ripples", 0.25f, 0.f, 1.f, "Detail")
          .tooltip = "Secondary small-scale ripple field on top.";
      add_float(n.attrs, "ripple_scale", "Ripple scale", 6.f, 2.f, 20.f, "Detail");
      add_seed(n.attrs);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      float wd = n.attrs.get_f("wind_dir", 30.f) * 0.017453293f;
      float wl = n.attrs.get_f("wavelength", 0.12f);
      float asym = n.attrs.get_f("asymmetry", 0.75f);
      float chaos = n.attrs.get_f("chaos", 0.5f);
      float ripples = n.attrs.get_f("ripples", 0.25f);
      float rscale = n.attrs.get_f("ripple_scale", 6.f);
      uint32_t seed = n.attrs.get_seed("seed");
      float ca = std::cos(wd), sa = std::sin(wd);
      noise::FbmParams cf;
      cf.octaves = 4;
      auto dune_profile = [&](float p, float a) {
        p = p - std::floor(p);
        if (p < a) {
          float t = p / a; // long windward rise
          return t * t * (3.f - 2.f * t);
        }
        float t = (p - a) / (1.f - a); // steep slip face
        return 1.f - t * t * (3.f - 2.f * t);
      };
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float along = u * ca + v * sa;
            float across = -u * sa + v * ca;
            float warp = chaos * 0.35f *
                         noise::fbm(along * 2.f, across * 3.f, seed, cf);
            float p = along / wl + warp / wl * 0.5f +
                      0.3f * std::sin(across / wl * 1.7f + warp * 4.f);
            float hgt = dune_profile(p, asym);
            // amplitude modulation so dune heights vary
            float am = 0.6f + 0.4f * (noise::fbm(along * 1.3f + 7.f, across * 1.3f,
                                                 seed ^ 9u, cf) * 0.5f + 0.5f);
            hgt *= am;
            if (ripples > 0) {
              float rp = dune_profile(along / wl * rscale +
                                      warp * 2.f + hgt * 2.f, asym);
              hgt += rp * ripples * 0.08f;
            }
            out.at(x, y) = hgt;
          }
      });
      apply_post(n, out);
      if (const Heightmap *env = n.in_hmap("envelope"))
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            out.v[i] *= std::clamp(env->v[i], 0.f, 1.f);
        });
    })

// -------------------------------------------------------------------- Snow
REGISTER_NODE(
    Snow, "Filter", "Snow cover: snowline, settle-thaw, slip-off; outputs depth mask",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      n.add_out("snow_mask");
      add_float(n.attrs, "amount", "Snow amount", 0.06f, 0.f, 0.3f, "Snow");
      add_float(n.attrs, "snowline", "Snowline", 0.55f, 0.f, 1.f, "Snow");
      add_float(n.attrs, "falloff", "Snowline falloff", 0.15f, 0.02f, 0.6f, "Snow");
      add_float(n.attrs, "slip_angle", "Slip-off slope", 0.55f, 0.1f, 1.f, "Snow")
          .tooltip = "Snow cannot cling to slopes steeper than this.";
      add_int(n.attrs, "settle", "Settle-thaw iterations", 12, 0, 60, "Snow")
          .tooltip = "Lets snow slide into hollows and compact —\n"
                     "smooth, wind-packed accumulation.";
      add_float(n.attrs, "melt", "Melt (low areas)", 0.3f, 0.f, 1.f, "Snow");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &mask = n.out_hmap("snow_mask");
      out = *in;
      float amount = n.attrs.get_f("amount", 0.06f);
      float snowline = n.attrs.get_f("snowline", 0.55f);
      float falloff = n.attrs.get_f("falloff", 0.15f);
      float slip = n.attrs.get_f("slip_angle", 0.55f);
      int settle = n.attrs.get_i("settle", 12);
      float melt = n.attrs.get_f("melt", 0.3f);
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      Heightmap depth(in->w, in->h);
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float t = (in->at(x, y) - mn) / hamp;
            float gx, gy;
            in->gradient_at(x, y, gx, gy);
            float slope = std::atan(std::sqrt(gx * gx + gy * gy) * in->w / hamp) *
                          0.63662f;
            float d = std::clamp((t - snowline) / falloff + 0.5f, 0.f, 1.f);
            d *= std::clamp((slip - slope) / 0.15f + 0.5f, 0.f, 1.f);
            d *= 1.f - melt * std::clamp((snowline - t) / falloff + 0.5f, 0.f, 1.f);
            depth.at(x, y) = d * amount * hamp;
          }
      });
      // settle: thermal relaxation on the snow layer only
      float talus = 0.35f * hamp / in->w;
      Heightmap delta(in->w, in->h);
      for (int it = 0; it < settle; ++it) {
        // Two-pass gather, not a scatter: each cell computes what it gives away
        // and what it receives by reading its neighbours, so a worker only ever
        // writes its own cell. Scattering into neighbours races across the row
        // bands and makes the result non-deterministic.
        parallel_rows(in->h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < in->w; ++x) {
              float surf = in->at(x, y) + depth.at(x, y);
              float d_here = depth.at(x, y);
              static const int dx4[4] = {-1, 1, 0, 0}, dy4[4] = {0, 0, -1, 1};
              float acc = 0.f;
              for (int k = 0; k < 4; ++k) {
                int nx2 = std::clamp(x + dx4[k], 0, in->w - 1);
                int ny2 = std::clamp(y + dy4[k], 0, in->h - 1);
                float nsurf = in->at(nx2, ny2) + depth.at(nx2, ny2);
                float d_there = depth.at(nx2, ny2);
                // this cell sheds onto the neighbour
                float out_diff = surf - nsurf - talus;
                if (out_diff > 0 && d_here > 0)
                  acc -= std::min(out_diff * 0.2f, d_here * 0.5f);
                // the neighbour sheds onto this cell
                float in_diff = nsurf - surf - talus;
                if (in_diff > 0 && d_there > 0)
                  acc += std::min(in_diff * 0.2f, d_there * 0.5f);
              }
              delta.at(x, y) = acc;
            }
        });
        parallel_index(depth.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            depth.v[i] = std::max(depth.v[i] + delta.v[i], 0.f);
        });
      }
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          out.v[i] += depth.v[i];
          mask.v[i] = depth.v[i];
        }
      });
      mask.remap(0.f, 1.f);
    })

} // namespace gpx
