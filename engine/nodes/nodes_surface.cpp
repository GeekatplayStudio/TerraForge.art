// Geekatplay TerraForge — surface realism nodes, modeled on the reference
// parameter sets of Terragen (Power Fractal, Fake Stones) and Gaea
// (Stratify, Shear, Craggy, Crater, Dunes, Snow). All reimplemented from
// published behavior on a regular heightmap grid.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"

namespace gpx {

// ------------------------------------------------------------ PowerFractal
// Terragen-style multi-scale displacement: independent lead-in / feature /
// smallest scales, displacement offset, roughness, spike limit, slope
// restriction, vertical or along-normal direction.
REGISTER_NODE(
    PowerFractal, "Filter", "Terragen-style multi-scale fractal displacement",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("displacement_map");
      add_choice(n.attrs, "flavor", "Noise flavour",
                 {"Perlin", "Billows", "Ridges", "Voronoi billows", "Voronoi ridges"},
                 2, "Scales");
      add_float(n.attrs, "lead_in", "Lead-in scale", 1.f, 0.05f, 4.f, "Scales")
          .tooltip = "Largest visible variation (fraction of terrain width).\n"
                     "Octaves between lead-in and feature scale ramp in\n"
                     "with reduced amplitude.";
      add_float(n.attrs, "feature", "Feature scale", 0.25f, 0.01f, 2.f, "Scales")
          .tooltip = "Scale of the dominant, full-amplitude features.";
      add_float(n.attrs, "smallest", "Smallest scale", 0.004f, 0.0005f, 0.1f,
                "Scales")
          .tooltip = "Detail cutoff — nothing finer than this is added.";
      add_seed(n.attrs, "seed", "Seed", 0, "Scales");
      add_float(n.attrs, "amplitude", "Displacement amplitude", 0.15f, 0.f, 1.f,
                "Displacement");
      add_float(n.attrs, "disp_offset", "Displacement offset", 0.f, -0.5f, 0.5f,
                "Displacement")
          .tooltip = "Shifts displacement: positive raises plinths,\n"
                     "negative sinks features.";
      add_float(n.attrs, "roughness", "Roughness", 1.f, 0.3f, 1.6f, "Displacement")
          .tooltip = "Per-octave gain multiplier; below 1 smooths high\n"
                     "frequencies, above 1 exaggerates them.";
      add_float(n.attrs, "spike_limit", "Spike limit", 0.7f, 0.05f, 1.f,
                "Displacement")
          .tooltip = "Damps octave contributions on already-steep ground\n"
                     "to prevent needle spikes.";
      add_bool(n.attrs, "along_normal", "Displace along normal", false,
               "Displacement")
          .tooltip = "Scales displacement with slope so cliffs bulge\n"
                     "outward like real overhung rock (approximated).";
      add_range(n.attrs, "slope_band", "Apply on slopes", 0.f, 1.f, 0.f, 1.f,
                "Restriction")
          .tooltip = "Restrict displacement to this normalized slope band\n"
                     "(e.g. 0.4..1 = only on steep faces).";
      add_float(n.attrs, "slope_soft", "Slope softness", 0.15f, 0.01f, 0.5f,
                "Restriction");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &dmap = n.out_hmap("displacement_map");
      out = *in;
      int flavor = n.attrs.get_choice("flavor");
      float lead_in = n.attrs.get_f("lead_in", 1.f);
      float feature = std::min(n.attrs.get_f("feature", 0.25f), lead_in);
      float smallest = std::min(n.attrs.get_f("smallest", 0.004f), feature);
      uint32_t seed = n.attrs.get_seed("seed");
      float amp = n.attrs.get_f("amplitude", 0.15f);
      float doff = n.attrs.get_f("disp_offset", 0.f);
      float rough = n.attrs.get_f("roughness", 1.f);
      float spike = n.attrs.get_f("spike_limit", 0.7f);
      bool along_normal = n.attrs.get_b("along_normal");
      float slo, shi;
      n.attrs.get_range("slope_band", slo, shi);
      float ssoft = n.attrs.get_f("slope_soft", 0.15f);

      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;

      // build octave table: freq from lead-in scale down to smallest
      struct Oct { float freq, amp; uint32_t seed; };
      std::vector<Oct> octs;
      float f0 = 1.f / lead_in, f_feat = 1.f / feature, f_min = 1.f / smallest;
      float a = 1.f;
      int oi = 0;
      for (float f = f0; f <= f_min && octs.size() < 24; f *= 2.f, ++oi) {
        float oa = a;
        if (f < f_feat) // lead-in ramp: reduced amplitude before feature scale
          oa *= 0.35f + 0.65f * (std::log2(f / f0 + 1e-6f) + 1.f) /
                            std::max(std::log2(f_feat / f0 + 1e-6f) + 1.f, 1e-4f);
        else
          a *= 0.5f * rough;
        octs.push_back({f, oa, seed + (uint32_t)oi * 1013u});
      }
      float norm = 0;
      for (auto &o : octs) norm += o.amp;
      if (norm < 1e-6f) norm = 1.f;

      // slope map for restriction / spike limiting
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float gx, gy;
            in->gradient_at(x, y, gx, gy);
            float slope = std::atan(std::sqrt(gx * gx + gy * gy) * in->w / hamp) *
                          0.63662f;
            float u = x / float(out.w), v = y / float(out.h);
            float sum = 0, wsum = 0, running_amp = 1.f;
            for (const Oct &o : octs) {
              float nx = u * o.freq, ny = v * o.freq;
              float nv;
              switch (flavor) {
                case 1: nv = std::fabs(noise::perlin(nx, ny, o.seed)) * 2 - 1; break;
                case 2: nv = 1.f - std::fabs(noise::perlin(nx, ny, o.seed)); nv = nv * 2 - 1; break;
                case 3: case 4: {
                  float f1, f2;
                  noise::worley(nx, ny, o.seed, f1, f2);
                  nv = flavor == 3 ? f1 * 2 - 1 : (f2 - f1) * 2 - 1;
                } break;
                default: nv = noise::perlin(nx, ny, o.seed);
              }
              // spike limit: high frequencies die out on steep ground
              float sl = 1.f - std::clamp((slope - spike) / (1.f - spike + 1e-4f),
                                          0.f, 1.f) * (o.freq > f_feat ? 1.f : 0.3f);
              sum += nv * o.amp * sl;
              wsum += o.amp;
              (void)running_amp;
            }
            float d = (sum / norm + doff) * amp * hamp;
            if (along_normal) d *= 1.f + slope * 1.5f;
            // slope band restriction
            float ba = std::clamp((slope - slo) / ssoft + 0.5f, 0.f, 1.f);
            float bb = std::clamp((shi - slope) / ssoft + 0.5f, 0.f, 1.f);
            float bm = std::min(ba, bb);
            out.at(x, y) = in->at(x, y) + d * bm;
            dmap.at(x, y) = d * bm;
          }
      });
      dmap.remap(0.f, 1.f);
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

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

// ---------------------------------------------------------------- Stratify
REGISTER_NODE(
    Stratify, "Filter", "Tilted rock strata exposed on cliff faces",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      add_float(n.attrs, "strength", "Strength", 0.6f, 0.05f, 1.f, "Strata");
      add_int(n.attrs, "layers", "Layer count", 18, 4, 80, "Strata");
      add_float(n.attrs, "tilt", "Tilt", 0.15f, 0.f, 0.8f, "Strata")
          .tooltip = "Strata are tilted planes, not horizontal bands —\n"
                     "the single most important realism control.";
      add_float(n.attrs, "tilt_dir", "Tilt direction °", 30.f, -180.f, 180.f,
                "Strata");
      add_float(n.attrs, "warp", "Warp", 0.2f, 0.f, 1.f, "Strata");
      add_float(n.attrs, "substrata", "Substrata", 0.4f, 0.f, 1.f, "Strata")
          .tooltip = "Finer secondary layering nested inside each stratum.";
      add_float(n.attrs, "slope_min", "Only on slopes above", 0.25f, 0.f, 1.f,
                "Restriction");
      add_float(n.attrs, "slope_soft", "Slope softness", 0.15f, 0.02f, 0.5f,
                "Restriction");
      add_seed(n.attrs);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float strength = n.attrs.get_f("strength", 0.6f);
      int layers = n.attrs.get_i("layers", 18);
      float tilt = n.attrs.get_f("tilt", 0.15f);
      float tdir = n.attrs.get_f("tilt_dir", 30.f) * 0.017453293f;
      float warp = n.attrs.get_f("warp", 0.2f);
      float sub = n.attrs.get_f("substrata", 0.4f);
      float smin = n.attrs.get_f("slope_min", 0.25f);
      float ssoft = n.attrs.get_f("slope_soft", 0.15f);
      uint32_t seed = n.attrs.get_seed("seed");
      float ca = std::cos(tdir), sa = std::sin(tdir);
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      noise::FbmParams wf;
      wf.octaves = 4;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float t = (in->at(x, y) - mn) / hamp;
            // tilted + warped stratification field
            float e = t + tilt * (u * ca + v * sa) +
                      warp * 0.15f * noise::fbm(u * 6, v * 6, seed, wf);
            float le = e * layers;
            float fl = std::floor(le), fr = le - fl;
            float snapped = (fl + (fr < 0.5f ? fr * 0.3f : 1.f - (1.f - fr) * 0.3f)) /
                            layers;
            // substrata: finer quantization inside the layer
            if (sub > 0) {
              float se = fr * 5.f;
              float sfl = std::floor(se), sfr = se - sfl;
              float ssnap = (sfl + (sfr < 0.5f ? sfr * 0.5f : 1.f - (1.f - sfr) * 0.5f)) / 5.f;
              snapped += (ssnap - fr) / layers * sub * 0.6f;
            }
            float target = mn + (snapped - tilt * (u * ca + v * sa) -
                                 warp * 0.15f * noise::fbm(u * 6, v * 6, seed, wf)) *
                                    hamp;
            // apply only on slopes
            float gx, gy;
            in->gradient_at(x, y, gx, gy);
            float slope = std::atan(std::sqrt(gx * gx + gy * gy) * in->w / hamp) *
                          0.63662f;
            float sm = std::clamp((slope - smin) / ssoft + 0.5f, 0.f, 1.f);
            out.at(x, y) = in->at(x, y) + (target - in->at(x, y)) * strength * sm;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ------------------------------------------------------------------ Shear
REGISTER_NODE(
    Shear, "Transform", "Directional rock shearing / folding (Gaea-style)",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "scale", "Shear scale", 0.15f, 0.02f, 1.f);
      add_float(n.attrs, "amount", "Shear amount", 0.05f, 0.f, 0.3f);
      add_float(n.attrs, "folding", "Folding", 0.3f, 0.f, 1.f);
      add_float(n.attrs, "direction", "Direction °", 0.f, -180.f, 180.f);
      add_bool(n.attrs, "self_modulated", "Self modulated", true)
          .tooltip = "Height drives shear strength — bands show on\n"
                     "slopes, flats stay intact.";
      add_seed(n.attrs);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float scale = n.attrs.get_f("scale", 0.15f);
      float amount = n.attrs.get_f("amount", 0.05f);
      float folding = n.attrs.get_f("folding", 0.3f);
      float dir = n.attrs.get_f("direction", 0.f) * 0.017453293f;
      bool selfmod = n.attrs.get_b("self_modulated", true);
      uint32_t seed = n.attrs.get_seed("seed");
      float ca = std::cos(dir), sa = std::sin(dir);
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      noise::FbmParams sf;
      sf.octaves = 4;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float perp = -u * sa + v * ca; // coordinate perpendicular to dir
            float w1 = noise::fbm(perp / scale, 0.37f, seed, sf);
            float w2 = folding * std::sin(perp / scale * 6.28f +
                                          noise::fbm(u * 3, v * 3, seed ^ 5u, sf) * 3.f);
            float s = amount * (w1 + w2);
            if (selfmod) s *= std::clamp((in->at(x, y) - mn) / hamp, 0.f, 1.f);
            float su = std::clamp(u + ca * s, 0.f, 1.f);
            float sv = std::clamp(v + sa * s, 0.f, 1.f);
            out.at(x, y) = in->sample(su, sv);
          }
      });
    })

// ------------------------------------------------------------------ Craggy
REGISTER_NODE(
    Craggy, "Filter", "Slope-targeted rocky detail; flats stay clean",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      add_float(n.attrs, "detail_scale", "Detail scale", 24.f, 4.f, 128.f);
      add_float(n.attrs, "strength", "Strength", 0.06f, 0.f, 0.3f);
      add_float(n.attrs, "slope_min", "Slope threshold", 0.3f, 0.f, 1.f);
      add_float(n.attrs, "slope_soft", "Threshold softness", 0.2f, 0.02f, 0.6f);
      add_int(n.attrs, "octaves", "Octaves", 5, 2, 9);
      add_seed(n.attrs);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float dscale = n.attrs.get_f("detail_scale", 24.f);
      float strength = n.attrs.get_f("strength", 0.06f);
      float smin = n.attrs.get_f("slope_min", 0.3f);
      float ssoft = n.attrs.get_f("slope_soft", 0.2f);
      uint32_t seed = n.attrs.get_seed("seed");
      noise::FbmParams p;
      p.octaves = n.attrs.get_i("octaves", 5);
      float mn, mx;
      in->minmax(mn, mx);
      float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float gx, gy;
            in->gradient_at(x, y, gx, gy);
            float slope = std::atan(std::sqrt(gx * gx + gy * gy) * in->w / hamp) *
                          0.63662f;
            float m = std::clamp((slope - smin) / ssoft + 0.5f, 0.f, 1.f);
            float u = x / float(out.w), v = y / float(out.h);
            float nv = noise::fbm_ridged(u * dscale, v * dscale, seed, p);
            out.at(x, y) = in->at(x, y) + nv * strength * hamp * m;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
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
