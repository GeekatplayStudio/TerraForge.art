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

} // namespace gpx
