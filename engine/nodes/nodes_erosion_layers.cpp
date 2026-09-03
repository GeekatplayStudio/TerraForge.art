// Geekatplay TerraForge — erosion that also decides what grows where.
//
// The research dossier (03-world-generation-simulation.md §4.5, research.txt
// "Erosion-driven splat maps") makes the point that layer weights are derived
// fields, not painted texture slots: the same simulation that carves the
// valley knows where the silt settled, where the bedrock was scoured bare and
// where the scree fell. ErosionLayers runs the shared erosion kernels and
// turns their side channels into a stack of material masks with a physical
// priority order — water and exposed rock first, then what settles on top —
// normalised so the layers always sum to one. MaterialStack is the other end
// of the wire: up to six masks and albedos into one height-aware blend that
// feeds MaterialOutput. Both live in one graph, so the Terrain workspace's
// erosion node is the Materials workspace's source.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/erosion_kernels.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

inline float ss(float e0, float e1, float x) {
  float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

// Remap a side channel to 0..1 by its own maximum (all are non-negative).
void unit01(Heightmap &m) {
  float mx = 0;
  for (float v : m.v) mx = std::max(mx, v);
  if (mx > 1e-12f)
    for (float &v : m.v) v /= mx;
}

// Small separable box blur; wetness spreads beyond the channel line.
void box_blur(Heightmap &m, int r) {
  if (r < 1) return;
  Heightmap tmp(m.w, m.h);
  parallel_rows(m.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < m.w; ++x) {
        float s = 0;
        for (int k = -r; k <= r; ++k) s += m.atc(x + k, y);
        tmp.at(x, y) = s / (2 * r + 1);
      }
  });
  parallel_rows(m.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < m.w; ++x) {
        float s = 0;
        for (int k = -r; k <= r; ++k) s += tmp.atc(x, y + k);
        m.at(x, y) = s / (2 * r + 1);
      }
  });
}

const char *LAYER_NAMES[7] = {"snow",    "riverbed", "sediment", "bedrock",
                              "scree",   "grass",    "soil"};

} // namespace

REGISTER_NODE(
    ErosionLayers, "Erosion",
    "Erode the terrain and derive material layer masks from what the water and rock did",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("bedrock");
      n.add_out("scree");
      n.add_out("soil");
      n.add_out("grass");
      n.add_out("sediment");
      n.add_out("riverbed");
      n.add_out("snow");
      n.add_out("wetness");
      n.add_out("flow");
      n.add_out("splat A", DataType::Texture);
      n.add_out("splat B", DataType::Texture);
      add_choice(n.attrs, "method", "Erosion",
                 {"Droplets", "Shallow water", "Thermal only", "Thermal + droplets",
                  "Thermal + shallow water"},
                 3, "Erosion")
          .tooltip = "Thermal weathering first drops scree below the cliffs;\n"
                     "the hydraulic pass then carves channels and settles silt.";
      add_seed(n.attrs);
      add_float(n.attrs, "strength", "Strength", 1.f, 0.1f, 3.f, "Erosion")
          .tooltip = "Scales droplet count / solver iterations.";
      add_float(n.attrs, "talus", "Talus angle", 1.2f, 0.05f, 4.f, "Erosion");
      add_int(n.attrs, "thermal_iters", "Thermal iterations", 40, 1, 300, "Erosion");
      add_float(n.attrs, "relief", "Relief (height / width)", 0.2f, 0.02f, 1.f, "Layers")
          .tooltip = "How tall the terrain is compared with the tile width.\n"
                     "A heightmap is 0..1 over a 0..1 tile; real ground rises\n"
                     "a fifth of its width or less. Slopes are measured\n"
                     "against this, so 0.5 means 45° on the real terrain.";
      add_float(n.attrs, "rock_slope", "Bedrock slope", 0.45f, 0.05f, 0.95f, "Layers")
          .tooltip = "Slope (0 flat .. 1 vertical, 0.5 = 45°) above which\n"
                     "soil cannot hold and rock is exposed.";
      add_float(n.attrs, "grass_slope", "Grass slope limit", 0.25f, 0.02f, 0.9f,
                "Layers");
      add_float(n.attrs, "sediment_thr", "Sediment threshold", 0.25f, 0.02f, 0.95f,
                "Layers")
          .tooltip = "How much deposited material makes a cell sand/silt.";
      add_float(n.attrs, "stream_thr", "Stream threshold", 0.55f, 0.1f, 0.98f,
                "Layers")
          .tooltip = "Drainage (log scale, 0..1) above which the cell is\n"
                     "a riverbed.";
      add_float(n.attrs, "snowline", "Snowline", 1.f, 0.f, 1.f, "Layers")
          .tooltip = "Height above which snow lies on gentle ground.\n"
                     "1 = no snow.";
      add_float(n.attrs, "softness", "Edge softness", 0.08f, 0.005f, 0.4f, "Layers");
      add_float(n.attrs, "wet_spread", "Wetness spread", 0.01f, 0.f, 0.05f, "Layers")
          .tooltip = "Blur radius (fraction of the map) that lets moisture\n"
                     "reach past the channel itself.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      const int w = in->w, h = in->h;
      const size_t N = (size_t)w * h;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      out.minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      for (auto &v : out.v) v = (v - mn) / amp;

      // ---- 1. the simulation, on the shared kernels
      int method = n.attrs.get_choice("method");
      float strength = n.attrs.get_f("strength", 1.f);
      Heightmap ero(w, h), dep(w, h), talus_dep(w, h), water(w, h);
      if (method >= 2) {
        float talus = n.attrs.get_f("talus", 1.2f) / w; // amplitude is 1 here
        thermal_relax(out, talus, n.attrs.get_i("thermal_iters", 40), 0.5f, false,
                      &talus_dep);
      }
      if (method == 0 || method == 3) {
        DropletParams p;
        p.num_particles = (int)(120000 * strength * (w / 512.f) * (h / 512.f));
        p.brush_radius = std::max(1, (int)(3 * w / 512.f));
        erode_droplets(out, p, n.attrs.get_seed("seed"), &ero, &dep);
      } else if (method == 1 || method == 4) {
        PipeParams p;
        p.iterations = std::max(10, (int)(120 * strength));
        p.capacity_k = 0.01f;
        p.evaporation = 0.15f;
        erode_pipes(out, p, &ero, &dep, &water);
      }
      // The surface the fields read: eroded, and renormalised to 0..1 so the
      // snowline means the same fraction of the relief whatever the solver
      // deposited above or carved below the original range.
      Heightmap hn = out;
      hn.remap(0.f, 1.f);

      // ---- 2. fields
      std::vector<float> area = flow_accumulate(hn, nullptr, true);
      float amax = 1.f;
      for (float a : area) amax = std::max(amax, a);
      float lg = std::log(amax);
      Heightmap &flow = n.out_hmap("flow");
      Heightmap slope(w, h);
      const float relief = n.attrs.get_f("relief", 0.2f);
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            flow.v[i] = lg > 1e-6f ? std::log(area[i]) / lg : 0.f;
            float dx, dy;
            hn.gradient_at(x, y, dx, dy);
            // rise/run on the real terrain: the normalised gradient scaled
            // by how tall the relief is against the tile width
            float s = std::sqrt(dx * dx + dy * dy) * w * relief;
            slope.v[i] = s / (1.f + s); // 0 flat .. 1 vertical, 0.5 = 45°
          }
      });
      unit01(ero);
      unit01(dep);
      unit01(talus_dep);
      unit01(water);
      Heightmap &wet = n.out_hmap("wetness");
      for (size_t i = 0; i < N; ++i)
        wet.v[i] = std::clamp(flow.v[i] * 0.7f + water.v[i] * 0.6f + dep.v[i] * 0.3f,
                              0.f, 1.f) *
                   (1.f - 0.5f * slope.v[i]);
      box_blur(wet, (int)(n.attrs.get_f("wet_spread", 0.01f) * w));

      // ---- 3. layers: raw memberships, then priority normalisation
      float rock_s = n.attrs.get_f("rock_slope", 0.45f);
      float grass_s = n.attrs.get_f("grass_slope", 0.25f);
      float sed_t = n.attrs.get_f("sediment_thr", 0.25f);
      float str_t = n.attrs.get_f("stream_thr", 0.55f);
      float snowline = n.attrs.get_f("snowline", 1.f);
      float soft = n.attrs.get_f("softness", 0.08f);
      Heightmap *L[7];
      for (int k = 0; k < 7; ++k) {
        L[k] = &n.out_hmap(LAYER_NAMES[k]);
        *L[k] = Heightmap(w, h);
      }
      parallel_index(N, [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float s = slope.v[i], z = hn.v[i];
          float raw[7];
          raw[0] = snowline < 1.f ? ss(snowline - soft, snowline + soft, z) *
                                        (1.f - ss(0.45f, 0.7f, s))
                                  : 0.f;
          raw[1] = ss(str_t - soft, str_t + soft, flow.v[i]);
          raw[2] = ss(sed_t - soft, sed_t + soft, dep.v[i]) *
                   (1.f - ss(rock_s * 0.6f, rock_s, s));
          raw[3] = std::max(ss(rock_s - soft, rock_s + soft, s), ss(0.5f, 0.9f, ero.v[i]));
          raw[4] = ss(0.12f, 0.6f, talus_dep.v[i]) * (1.f - ss(rock_s, rock_s + 0.2f, s));
          raw[5] = (1.f - ss(grass_s - soft, grass_s + soft, s)) *
                   ss(0.05f, 0.35f, wet.v[i]) *
                   (snowline < 1.f ? 1.f - ss(snowline - 0.15f, snowline, z) : 1.f);
          raw[6] = 1.f;
          // Physical priority: what is on top claims the cell first, the
          // rest share what remains. The sum is exactly one by construction.
          float remaining = 1.f;
          for (int k = 0; k < 6; ++k) {
            float wgt = std::clamp(raw[k], 0.f, 1.f) * remaining;
            L[k]->v[i] = wgt;
            remaining -= wgt;
          }
          L[6]->v[i] = std::max(remaining, 0.f);
        }
      });

      // ---- 4. packed splats for SplatMaterial: A = bedrock/scree/soil/grass,
      // B = sediment/riverbed/snow/wetness
      TextureRGBA &sa = n.out_tex("splat A");
      TextureRGBA &sb = n.out_tex("splat B");
      const Heightmap *A[4] = {L[3], L[4], L[6], L[5]};
      const Heightmap *B[4] = {L[2], L[1], L[0], &wet};
      const bool same = sa.w == w && sa.h == h;
      parallel_rows(sa.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < sa.w; ++x) {
            float u = x / float(std::max(sa.w - 1, 1)), v = y / float(std::max(sa.h - 1, 1));
            float *pa = sa.px(x, y), *pb = sb.px(x, y);
            for (int k = 0; k < 4; ++k) {
              pa[k] = same ? A[k]->at(x, y) : A[k]->sample(u, v);
              pb[k] = same ? B[k]->at(x, y) : B[k]->sample(u, v);
            }
          }
      });

      for (auto &v : out.v) v = mn + v * amp;
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ------------------------------------------------------------ MaterialStack
REGISTER_NODE(
    MaterialStack, "Material",
    "Blend up to six material layers by mask, height-aware, into albedo + roughness",
    [](Node &n) {
      for (int k = 1; k <= 6; ++k) {
        n.add_in("mask " + std::to_string(k), DataType::Heightmap, true);
        n.add_in("albedo " + std::to_string(k), DataType::Texture, true);
      }
      n.add_out("albedo", DataType::Texture);
      n.add_out("roughness", DataType::Texture);
      add_float(n.attrs, "height_blend", "Height blend", 0.5f, 0.f, 1.f, "Blending")
          .tooltip = "0: plain weighted mix. 1: the layer whose texture is\n"
                     "highest at this texel wins — silt fills the cracks of\n"
                     "the rock before it covers the ridges.";
      add_float(n.attrs, "blend_depth", "Blend depth", 0.25f, 0.02f, 1.f, "Blending")
          .tooltip = "How far below the winning layer others still show.";
      for (int k = 1; k <= 6; ++k)
        add_float(n.attrs, "rough_" + std::to_string(k), "Roughness", 0.8f, 0.f, 1.f,
                  "Layer " + std::to_string(k));
    },
    [](Node &n) {
      const Heightmap *M[6];
      const TextureRGBA *T[6];
      float R[6];
      bool any = false;
      for (int k = 0; k < 6; ++k) {
        std::string s = std::to_string(k + 1);
        M[k] = n.in_hmap("mask " + s);
        T[k] = n.in_tex("albedo " + s);
        if (M[k] && M[k]->empty()) M[k] = nullptr;
        if (T[k] && T[k]->empty()) T[k] = nullptr;
        R[k] = n.attrs.get_f("rough_" + s, 0.8f);
        any = any || T[k];
      }
      if (!any) {
        n.error = "connect at least one albedo";
        return;
      }
      TextureRGBA &alb = n.out_tex("albedo");
      TextureRGBA &rgh = n.out_tex("roughness");
      float hb = n.attrs.get_f("height_blend", 0.5f);
      float depth = n.attrs.get_f("blend_depth", 0.25f);
      parallel_rows(alb.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < alb.w; ++x) {
            float u = x / float(alb.w), v = y / float(alb.h);
            float sc[6], col[6][3], hgt[6];
            float top = -1.f;
            for (int k = 0; k < 6; ++k) {
              float wgt = M[k] ? std::clamp(M[k]->sample(u, v), 0.f, 1.f)
                               : (k == 0 ? 1.f : 0.f);
              if (T[k]) {
                int sx = std::min((int)(u * T[k]->w), T[k]->w - 1);
                int sy = std::min((int)(v * T[k]->h), T[k]->h - 1);
                const float *p = T[k]->px(sx, sy);
                col[k][0] = p[0]; col[k][1] = p[1]; col[k][2] = p[2];
                hgt[k] = p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f;
              } else {
                wgt = 0.f;
                col[k][0] = col[k][1] = col[k][2] = 0.f;
                hgt[k] = 0.f;
              }
              // the micro-height contest: a layer's claim rises with the
              // brightness of its own texture where it is present at all
              sc[k] = wgt * (1.f - hb + hb * (hgt[k] + 0.1f) * 1.8f);
              top = std::max(top, sc[k]);
            }
            float sum = 0, b[6];
            for (int k = 0; k < 6; ++k) {
              b[k] = hb > 0.f ? std::max(sc[k] - (top - depth), 0.f) * (sc[k] > 0.f)
                              : sc[k];
              sum += b[k];
            }
            float *pa = alb.px(x, y), *pr = rgh.px(x, y);
            pa[0] = pa[1] = pa[2] = 0.f;
            float r = 0.f;
            if (sum > 1e-6f) {
              for (int k = 0; k < 6; ++k) {
                float f = b[k] / sum;
                pa[0] += col[k][0] * f;
                pa[1] += col[k][1] * f;
                pa[2] += col[k][2] * f;
                r += R[k] * f;
              }
            } else {
              for (int k = 0; k < 6; ++k)
                if (T[k]) {
                  pa[0] = col[k][0]; pa[1] = col[k][1]; pa[2] = col[k][2];
                  r = R[k];
                  break;
                }
            }
            pa[3] = 1.f;
            pr[0] = pr[1] = pr[2] = r;
            pr[3] = 1.f;
          }
      });
    })

} // namespace gpx
