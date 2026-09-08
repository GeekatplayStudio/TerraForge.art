// Geekatplay TerraForge - FakeStones: Terragen-style boulders as displacement.
//
// Split out of nodes_surface_forms.cpp, which was over the module size, when
// the node grew layers.
//
// A stone field in nature is not one size of rock scattered evenly. It is big
// boulders with smaller, more angular debris gathered around their feet, and
// finer rubble again around that - because the small pieces came off the big
// ones, or were caught by them. One lattice of same-sized domes cannot look
// like that however it is tuned, which is what "add more stones, that can
// cluster around other stones, like layers - small sharp" is asking for.
//
// So the node generates a few *generations*. Each is smaller and denser than
// the one before, and each is placed preferentially where the previous
// generation already put stone. `cluster` is how strongly: at 0 a generation
// ignores its parents and scatters on its own, at 1 it appears only around
// them.
//
// Generations run as whole passes rather than per-stone lookups. A child
// asking "is there a parent near me" is answered by sampling the parent
// generation's coverage buffer, which costs one texel read; doing it by
// re-evaluating the parent lattice would cost eighty-one, per texel, and the
// node would be unusable.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

namespace {

// One generation's worth of settings. Generation 0 is what the node has
// always produced; each later one is derived from it by the layer ratios.
struct StoneGen {
  float scale = 0.03f;   // stone size as a fraction of the tile
  float density = 0.5f;  // share of lattice cells holding a stone
  float sharp = 0.f;     // 0 weathered dome, 1 angular shard
};

// A separable max filter: how much stone lies within `r` texels of each point.
// Two O(r) passes rather than one O(r*r) - at a radius of twenty texels on a
// 512 tile that is the difference between ten million operations and four
// hundred million, per generation.
void dilate(const std::vector<float> &src, std::vector<float> &dst, int w, int h,
            int r) {
  std::vector<float> tmp((size_t)w * h, 0.f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float m = 0.f;
      const int x0 = std::max(0, x - r), x1 = std::min(w - 1, x + r);
      for (int k = x0; k <= x1; ++k) m = std::max(m, src[(size_t)y * w + k]);
      tmp[(size_t)y * w + x] = m;
    }
  dst.assign((size_t)w * h, 0.f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float m = 0.f;
      const int y0 = std::max(0, y - r), y1 = std::min(h - 1, y + r);
      for (int k = y0; k <= y1; ++k) m = std::max(m, tmp[(size_t)k * w + x]);
      dst[(size_t)y * w + x] = m;
    }
}

// The stone height at one point, from one generation's lattice.
//
// Clustering is judged at the candidate stone's *centre* against two views of
// the previous generation: `parent` is where it actually laid stone, and
// `parent_near` is that dilated by about a child's width. A child wants the
// ground that is near a parent but not under one - the apron of debris around
// a boulder's foot, which is where rubble collects. Reading only the parent's
// height at that point instead put the children on the boulder's own slope,
// where in practice almost none of them existed at all.
float stones_at(int x, int y, int w, int h, const StoneGen &g, uint32_t seed,
                float hamp, float vary, float vscale, float sjit,
                float cluster, const std::vector<float> *parent,
                const std::vector<float> *parent_near) {
  const float cell = 1.f / g.scale;
  const float u = x / float(w), v = y / float(h);
  const float cx = u * cell, cy = v * cell;
  const int xi = (int)std::floor(cx), yi = (int)std::floor(cy);
  noise::FbmParams vf;
  vf.octaves = 3;
  float add = 0.f;
  // the 3x3 neighbourhood, so a stone straddling a cell edge is not clipped
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      const int gx2 = xi + dx, gy2 = yi + dy;
      float exist = noise::hash01(gx2, gy2, seed);
      float local_density = g.density;
      if (vary > 0) {
        float pn = noise::fbm(gx2 / cell * vscale, gy2 / cell * vscale,
                              seed ^ 0x55u, vf) * 0.5f + 0.5f;
        local_density *= std::clamp(1.f - vary + vary * pn * 2.f, 0.f, 1.f);
      }
      // stone centre, jittered inside its cell
      float sx = gx2 + 0.2f + 0.6f * noise::hash01(gx2, gy2, seed ^ 3u);
      float sy = gy2 + 0.2f + 0.6f * noise::hash01(gx2, gy2, seed ^ 7u);
      if (parent && parent_near && cluster > 0.f) {
        // Sampled at the centre, not at the texel: a stone belongs where its
        // middle is, or one straddling the edge of a parent's apron would be
        // half placed and half not.
        const int px = std::clamp((int)(sx / cell * w), 0, w - 1);
        const int py = std::clamp((int)(sy / cell * h), 0, h - 1);
        const size_t q = (size_t)py * w + px;
        const bool near_parent = (*parent_near)[q] > 1e-7f;
        const bool on_parent = (*parent)[q] > 1e-7f;
        // Beside a boulder, not on top of it. The multiplier is generous
        // because the apron is a small part of the tile: without it, confining
        // a generation to the aprons would simply delete most of it, which is
        // exactly what the first attempt at this did.
        const float apron = (near_parent && !on_parent) ? 3.f : 0.f;
        local_density *= (1.f - cluster) + cluster * apron;
      }
      if (exist > local_density) continue;
      float r = (0.35f + 0.25f * (noise::hash01(gx2, gy2, seed ^ 11u) - 0.5f) *
                             2.f * sjit);
      float ddx = cx - sx, ddy = cy - sy;
      float d2 = (ddx * ddx + ddy * ddy) / (r * r);
      if (d2 >= 1.f) continue;
      // irregular profile: the silhouette wobbles with angle, and more so
      // the sharper the stone, so shards read as faceted rather than round
      float ang = std::atan2(ddy, ddx);
      float wob = 0.25f + 0.35f * g.sharp;
      float irr = 1.f + wob * std::sin(ang * (3.f + 2.f * g.sharp) +
                                       noise::hash01(gx2, gy2, seed ^ 13u) * 6.28f) *
                            std::sqrt(d2);
      const float t = std::max(1.f - d2 * irr, 0.f);
      // A dome is a weathered boulder; a cone is a broken shard. Sharpness
      // is the blend, and at 0 this is exactly the profile the node always
      // had - which is what keeps every existing project unchanged.
      float profile = std::sqrt(t);
      if (g.sharp > 0.f) {
        float cone = std::max(1.f - std::sqrt(std::max(d2 * irr, 0.f)), 0.f);
        profile = profile * (1.f - g.sharp) + cone * g.sharp;
      }
      add = std::max(add, profile);
    }
  return add;
}

} // namespace

REGISTER_NODE(
    FakeStones, "Primitive", "Terragen-style fake stones: boulders/rocks as displacement",
    [](Node &n) {
      n.add_in("input");
      n.add_in("density_mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("stone_mask");
      n.add_out("displacement"); // the stones alone: output - input
      add_float(n.attrs, "stone_scale", "Stone scale", 0.03f, 0.004f, 0.25f,
                "Stones")
          .tooltip = "Stone size as a fraction of terrain width;\n"
                     "smaller = more, denser stones.";
      add_float(n.attrs, "density", "Stone density", 0.5f, 0.02f, 1.f, "Stones")
          .tooltip = "The share of lattice cells that hold a stone. This\n"
                     "node writes into the heightmap, so its smallest\n"
                     "possible stone is a couple of texels - about 14 m on\n"
                     "a 5 km tile. For stones you can stand next to, use\n"
                     "Stone field, which is a function and has no\n"
                     "resolution.";
      add_float(n.attrs, "tallness", "Stone tallness", 0.6f, 0.05f, 2.f, "Stones")
          .tooltip = "A stone's height as a fraction of its radius. Every\n"
                     "stone here gets the same one, which is this node's\n"
                     "most visible tell.";
      add_float(n.attrs, "pancake", "Pancake effect", 0.3f, 0.f, 1.f, "Stones")
          .tooltip = "Squashes stones flat into slabs while keeping their\n"
                     "footprint — 0 round boulders, 1 flat plates.";
      add_float(n.attrs, "sharpness", "Sharpness", 0.f, 0.f, 1.f, "Stones")
          .tooltip = "Weathered or broken. 0 is a rounded boulder; 1 is an\n"
                     "angular shard with a faceted silhouette. Rockfall\n"
                     "close to the cliff it came off is sharp; the same\n"
                     "stone at the bottom of a valley is not.";
      add_seed(n.attrs, "seed", "Seed", 0, "Stones");

      // ------------------------------------------------------------ layers
      add_int(n.attrs, "layers", "Stone layers", 1, 1, 4, "Layers")
          .tooltip = "Generations of stones, each smaller than the one\n"
                     "before and gathered around it. One is a plain\n"
                     "scatter; two or three give big boulders with debris\n"
                     "collected at their feet, which is what real scree\n"
                     "looks like.\n\nLeave it at 1 and the node behaves\n"
                     "exactly as it always did.";
      add_float(n.attrs, "layer_scale", "Size per layer", 0.45f, 0.15f, 0.9f,
                "Layers")
          .tooltip = "How big each generation is next to the one before.\n"
                     "0.45 makes every layer a little under half the size\n"
                     "of its parent, which reads as a natural size range;\n"
                     "near 1 the layers are all the same and the effect is\n"
                     "just more stones.";
      add_float(n.attrs, "layer_density", "Density per layer", 1.4f, 0.5f, 3.f,
                "Layers")
          .tooltip = "How much denser each generation is than the one\n"
                     "before. Small stones are more numerous than large\n"
                     "ones, so above 1 is the usual choice.";
      add_float(n.attrs, "cluster", "Cluster on parents", 0.75f, 0.f, 1.f,
                "Layers")
          .tooltip = "How strongly each generation gathers around the\n"
                     "stones of the one before. 0 scatters it\n"
                     "independently - just more stones; 1 puts it only\n"
                     "around the skirts of the larger ones, never on top\n"
                     "of them.";
      add_float(n.attrs, "layer_sharpness", "Sharper per layer", 0.5f, 0.f, 1.f,
                "Layers")
          .tooltip = "How much more angular each generation is than the\n"
                     "one before. The small pieces are the broken ones, so\n"
                     "raising this is what gives you sharp gravel around\n"
                     "weathered boulders.";

      add_float(n.attrs, "vary_density", "Vary density", 0.6f, 0.f, 1.f,
                "Variation")
          .tooltip = "Large-scale patchiness: clusters of stones with\n"
                     "clear ground between.";
      add_float(n.attrs, "vary_scale", "Density variation scale", 4.f, 1.f, 16.f,
                "Variation")
          .tooltip = "How large the patches of more and fewer stones are.\n"
                     "Low gives a couple of broad drifts across the map;\n"
                     "high breaks it into many small clusters.";
      add_float(n.attrs, "size_jitter", "Size variation", 0.5f, 0.f, 1.f,
                "Variation")
          .tooltip = "How much stones differ in size from one another. 0\n"
                     "makes every stone identical, which nothing in nature\n"
                     "is.";
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
      const float scale = n.attrs.get_f("stone_scale", 0.03f);
      const float density = n.attrs.get_f("density", 0.5f);
      const float tallness = n.attrs.get_f("tallness", 0.6f);
      const float pancake = n.attrs.get_f("pancake", 0.3f);
      const float sharp0 = n.attrs.get_f("sharpness", 0.f);
      const uint32_t seed = n.attrs.get_seed("seed");
      const int layers = std::clamp(n.attrs.get_i("layers", 1), 1, 4);
      const float lscale = n.attrs.get_f("layer_scale", 0.45f);
      const float ldens = n.attrs.get_f("layer_density", 1.4f);
      const float cluster = n.attrs.get_f("cluster", 0.75f);
      const float lsharp = n.attrs.get_f("layer_sharpness", 0.5f);
      const float vary = n.attrs.get_f("vary_density", 0.6f);
      const float vscale = n.attrs.get_f("vary_scale", 4.f);
      const float sjit = n.attrs.get_f("size_jitter", 0.5f);
      float slo, shi;
      n.attrs.get_range("slope_band", slo, shi);
      const Heightmap *dmask = n.in_hmap("density_mask");
      float mn, mx;
      in->minmax(mn, mx);
      const float hamp = (mx - mn) > 1e-9f ? mx - mn : 1.f;

      const int w = out.w, h = out.h;
      std::vector<float> total((size_t)w * h, 0.f);
      std::vector<float> prev, cur, prev_near;

      for (int L = 0; L < layers; ++L) {
        StoneGen g;
        g.scale = scale;
        g.density = density;
        g.sharp = sharp0;
        for (int k = 0; k < L; ++k) {
          g.scale *= lscale;
          g.density = std::min(g.density * ldens, 1.f);
          g.sharp = std::min(g.sharp + lsharp, 1.f);
        }
        // A stone's height follows its own size, so the small generations are
        // low as well as fine - scaling only the footprint would give spikes.
        const float hgt = tallness * g.scale * hamp * (1.f - pancake * 0.75f);
        // Each generation gets its own seed, or every layer would place its
        // stones in the same cells as the first and they would stack.
        //
        // The first keeps the seed exactly as given: it is the field this node
        // has always produced, and every saved project and golden depends on
        // it not moving. Mixing the seed even for layer 0 changed
        // surface_geology.gpxt, which is what the regression lock is for.
        const uint32_t gseed =
            L == 0 ? seed : (seed ^ (0x9e3779b9u * (uint32_t)L));

        // The apron: the ground within about one of this generation's stones
        // of a parent. Wide enough that the debris reads as gathered around
        // the boulder rather than glued to it, narrow enough that it is still
        // clearly a pile and not a scatter.
        const std::vector<float> *parent = nullptr, *near = nullptr;
        if (L > 0 && cluster > 0.f) {
          const int r = std::max(1, (int)(g.scale * 0.9f * w));
          dilate(prev, prev_near, w, h, r);
          parent = &prev;
          near = &prev_near;
        }
        cur.assign((size_t)w * h, 0.f);
        parallel_rows(h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x)
              cur[(size_t)y * w + x] =
                  stones_at(x, y, w, h, g, gseed, hamp, vary, vscale, sjit,
                            cluster, parent, near) * hgt;
        });
        for (size_t q = 0; q < total.size(); ++q)
          total[q] = std::max(total[q], cur[q]);
        // Each generation clusters on the one before it, so what the next
        // child sees is this layer alone, not everything laid so far.
        prev.swap(cur);
      }

      // Placement is judged once, on the finished field: the slope band and
      // the mask are about where stone may lie at all, not about which
      // generation put it there.
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            float add = total[(size_t)y * w + x];
            if (add > 0) {
              float gx3, gy3;
              in->gradient_at(x, y, gx3, gy3);
              float slope = std::atan(std::sqrt(gx3 * gx3 + gy3 * gy3) * in->w / hamp) *
                            0.63662f;
              if (slope < slo || slope > shi) add = 0;
              if (dmask && !dmask->empty())
                add *= std::clamp(dmask->v[(size_t)y * w + x], 0.f, 1.f);
            }
            out.at(x, y) = in->at(x, y) + add;
            smask.at(x, y) = add;
          }
      });
      smask.remap(0.f, 1.f);
      // the stones alone, for a material layer to raise where it is present
      {
        Heightmap &dsp = n.out_hmap("displacement");
        dsp = Heightmap(out.w, out.h);
        for (size_t q = 0; q < dsp.v.size() && q < in->v.size(); ++q)
          dsp.v[q] = out.v[q] - in->v[q];
      }
    })

} // namespace gpx
