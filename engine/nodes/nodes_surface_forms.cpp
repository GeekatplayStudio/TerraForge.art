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

// -------------------------------------------------------------- Landform
REGISTER_NODE(
    Landform, "Primitive", "Geological set pieces: island, mountain, caldera, rift, mesa",
    [](Node &n) {
      n.add_out("output");
      add_choice(n.attrs, "type", "Type",
                 {"Island", "Mountain", "Caldera", "Rift valley", "Mesa"}, 0)
          .tooltip = "A whole landform in one node, for when you want a\n"
                     "particular thing in a particular place rather than\n"
                     "whatever the noise happens to give. The rim is\n"
                     "wobbled by noise, so none of them reads as a\n"
                     "compass drawing.";
      add_vec2(n.attrs, "center", "Center", 0.5f, 0.5f, -0.5f, 1.5f)
          .tooltip = "Where it sits, as a fraction of the tile. Outside\n"
                     "0..1 pushes it off the edge, so you get a coast or a\n"
                     "flank rather than the whole thing.";
      add_float(n.attrs, "radius", "Radius", 0.35f, 0.05f, 1.f)
          .tooltip = "How far it reaches, as a fraction of the tile.";
      add_float(n.attrs, "relief", "Relief", 0.5f, 0.f, 1.f)
          .tooltip = "How broken the form is. 0 is a clean geometric\n"
                     "shape; higher adds ridged detail to the flanks and\n"
                     "wobbles the outline further.";
      add_float(n.attrs, "angle", "Direction °", 0.f, -180.f, 180.f)
          .tooltip = "Which way the form points. The rift valley and the\n"
                     "mesa use it; the round ones ignore it.";
      add_seed(n.attrs);
      setup_post(n);
    },
    [](Node &n) {
      Heightmap &out = n.out_hmap("output");
      int type = n.attrs.get_choice("type");
      float cx, cy;
      n.attrs.get_vec2("center", cx, cy);
      float R = n.attrs.get_f("radius", 0.35f);
      float relief = n.attrs.get_f("relief", 0.5f);
      float ang = n.attrs.get_f("angle") * 0.017453293f;
      float ca = std::cos(ang), sa = std::sin(ang);
      uint32_t seed = n.attrs.get_seed("seed");
      noise::FbmParams p;
      p.octaves = 8;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), vv = y / float(out.h);
            float dx = u - cx, dy = vv - cy;
            // the rim is wobbled by noise so nothing reads as a compass rose
            float wob =
                1.f + 0.25f * relief * noise::fbm(u * 3.f, vv * 3.f, seed, p);
            float d = std::sqrt(dx * dx + dy * dy) / (R * wob);
            float detail =
                noise::fbm_ridged(u * 6.f, vv * 6.f, seed + 7u, p) * relief;
            float v = 0;
            switch (type) {
              case 0: { // island: smooth dome falling to a shore shelf
                float base = std::clamp(1.f - d, 0.f, 1.f);
                base = base * base * (3.f - 2.f * base);
                v = base * (0.55f + 0.45f * detail);
              } break;
              case 1: { // mountain: sharper cone, ridged flanks
                float base = std::max(1.f - d, 0.f);
                v = std::pow(base, 1.4f) * (0.4f + 0.6f * (detail * 0.5f + 0.5f));
              } break;
              case 2: { // caldera: a volcano with the top ring blown out
                float base = std::max(1.f - d, 0.f);
                float cone = std::pow(base, 1.2f);
                float crater = std::exp(-d * d * 16.f) * 0.8f;
                v = cone - crater + detail * 0.15f * base;
              } break;
              case 3: { // rift: a directional trench through the tile
                float sd = std::fabs(dx * -sa + dy * ca) / R;
                float trench = std::exp(-sd * sd * 4.f);
                v = 0.6f + detail * 0.2f - trench * (0.45f + 0.15f * relief);
              } break;
              case 4: { // mesa: flat top, steep skirt, talus foot
                float t = std::clamp((d - 0.7f) / 0.25f, 0.f, 1.f);
                t = t * t * (3.f - 2.f * t);
                v = (1.f - t) * (0.85f + detail * 0.1f) + (1.f - d) * 0.05f;
                v = std::max(v, 0.f);
              } break;
            }
            out.at(x, y) = v;
          }
      });
      apply_post(n, out);
    })

// FakeStones lives in nodes_fake_stones.cpp: it grew layers and this file was
// already over the module size.

// ------------------------------------------------------------------ Crater
REGISTER_NODE(
    Crater, "Primitive", "Impact craters: bowl, rim lip, ejecta blanket (single or field)",
    [](Node &n) {
      n.add_in("input", DataType::Heightmap, true);
      n.add_out("output");
      add_choice(n.attrs, "profile", "Profile", {"Single crater", "Crater field"}, 0,
                 "Craters")
          .tooltip = "One crater placed where you say, or a scattered\n"
                     "field of them at varying sizes - a cratered plain\n"
                     "rather than an impact site.";
      add_float(n.attrs, "scale", "Scale", 0.3f, 0.02f, 1.f, "Craters")
          .tooltip = "How wide the crater is, as a fraction of the tile.\n"
                     "In field mode this is the largest; the rest vary\n"
                     "below it.";
      add_float(n.attrs, "depth", "Depth", 0.4f, 0.05f, 1.f, "Craters")
          .tooltip = "How far the floor sits below the surrounding\n"
                     "ground.";
      add_float(n.attrs, "lip", "Rim lip", 0.5f, 0.f, 1.f, "Craters")
          .tooltip = "Sharpness/height of the raised rim wall.";
      add_float(n.attrs, "outer_scale", "Ejecta extent", 0.6f, 0.1f, 2.f, "Craters")
          .tooltip = "How far the thrown-out debris blanket reaches past\n"
                     "the rim, as a multiple of the radius. It is the\n"
                     "apron of raised ground that makes an impact read as\n"
                     "an impact rather than a hole.";
      add_float(n.attrs, "floor", "Floor level", 0.15f, 0.f, 1.f, "Craters")
          .tooltip = "Clamps the bowl bottom — flat crater floors.";
      add_float(n.attrs, "irregular", "Rim irregularity", 0.3f, 0.f, 1.f, "Craters")
          .tooltip = "How far the rim departs from a circle. 0 is a\n"
                     "drawing-compass ring, which no impact leaves.";
      add_vec2(n.attrs, "center", "Position", 0.5f, 0.5f, -0.2f, 1.2f, "Craters")
          .tooltip = "Where the single crater sits, as a fraction of the\n"
                     "tile. Ignored in field mode.";
      add_int(n.attrs, "count", "Field count", 12, 2, 64, "Field")
          .tooltip = "How many craters the field scatters. Field mode\n"
                     "only.";
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
      add_float(n.attrs, "wind_dir", "Wind direction °", 30.f, -180.f, 180.f, "Dunes")
          .tooltip = "Which way the wind blows. Dunes run across it, and\n"
                     "their steep slip face is on the downwind side -\n"
                     "which is what tells a viewer which way the wind was\n"
                     "going.";
      add_float(n.attrs, "wavelength", "Dune wavelength", 0.12f, 0.02f, 0.5f, "Dunes")
          .tooltip = "The distance from one crest to the next, as a\n"
                     "fraction of the tile.";
      add_float(n.attrs, "asymmetry", "Asymmetry", 0.75f, 0.5f, 0.95f, "Dunes")
          .tooltip = "Windward slope is long and gentle; the slip face is\n"
                     "short and steep (real dunes ~0.8).";
      add_float(n.attrs, "chaos", "Crest chaos", 0.5f, 0.f, 1.f, "Dunes")
          .tooltip = "How much the crests wander and break up along their\n"
                     "length. 0 gives parallel corduroy; high gives the\n"
                     "broken crescents of a real dune field.";
      add_float(n.attrs, "ripples", "Ripples", 0.25f, 0.f, 1.f, "Detail")
          .tooltip = "Secondary small-scale ripple field on top.";
      add_float(n.attrs, "ripple_scale", "Ripple scale", 6.f, 2.f, 20.f, "Detail")
          .tooltip = "How many small wind ripples ride across each dune.\n"
                     "These are the centimetre-scale corrugations on the\n"
                     "sand, not the dunes themselves.";
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
      add_float(n.attrs, "amount", "Snow amount", 0.06f, 0.f, 0.3f, "Snow")
          .tooltip = "How thick the snow lies, in heightmap units. It is added\n"
                     "on top of the terrain, so this also softens whatever it\n"
                     "covers.";
      add_float(n.attrs, "snowline", "Snowline", 0.55f, 0.f, 1.f, "Snow")
          .tooltip = "The height above which snow settles, as a fraction of the\n"
                     "terrain's range. Below it the ground stays bare.";
      add_float(n.attrs, "falloff", "Snowline falloff", 0.15f, 0.02f, 0.6f, "Snow")
          .tooltip = "How gradually the snowline is crossed. A hard line looks\n"
                     "painted on; real snow thins out over a band, and thins\n"
                     "faster on the sunnier side.";
      add_float(n.attrs, "slip_angle", "Slip-off slope", 0.55f, 0.1f, 1.f, "Snow")
          .tooltip = "Snow cannot cling to slopes steeper than this.";
      add_int(n.attrs, "settle", "Settle-thaw iterations", 12, 0, 60, "Snow")
          .tooltip = "Lets snow slide into hollows and compact —\n"
                     "smooth, wind-packed accumulation.";
      add_float(n.attrs, "melt", "Melt (low areas)", 0.3f, 0.f, 1.f, "Snow")
          .tooltip = "How much snow disappears from the low, sheltered ground -\n"
                     "the hollows where it goes first. 0 leaves an even blanket\n"
                     "above the snowline.";
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
