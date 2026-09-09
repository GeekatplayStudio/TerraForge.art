// Geekatplay TerraForge — the rest of Vue's terrain-editor effects.
//
// Vue's Effects tab (Reference Manual p539) lists twelve global effects.
// Grit, Gravel, Peaks, Sharpen, Cracks live in nodes_terrain_fx.cpp; Stones
// is FakeStones, Plateaus is Plateau, Terraces and Stairs are Terrace,
// Craters is Crater. The three that had no node are here - Pebbles, Fir
// trees - and Invert, which Vue keeps on the toolbar. Deterministic,
// masked, repeatable, like the others.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

namespace {

float fx2_amp(const Heightmap &m) {
  float mn, mx;
  m.minmax(mn, mx);
  return (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
}

void fx2_setup(Node &n) {
  n.add_in("input");
  n.add_in("mask", DataType::Heightmap, true);
  n.add_out("output");
}

uint32_t fx2_hash(uint32_t x, uint32_t y, uint32_t seed) {
  uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return h;
}
float fx2_unit(uint32_t h) { return (h & 0xffffffu) / 16777216.f; }

// One round or pointed bump per grid cell, jittered inside it, with a
// random size: the shape is `pointed` (a cone) or a dome. Every cell is
// visited once per texel neighbourhood, so the result is the same whatever
// the thread count.
void fx2_stamps(const Heightmap &in, Heightmap &out, float cells, float height,
                float size_jitter, bool pointed, uint32_t seed) {
  const int w = in.w, h = in.h;
  const float amp = fx2_amp(in);
  const float cell_px = std::max((float)std::max(w, h) / std::max(cells, 1.f), 2.f);
  parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        const int cx = (int)std::floor(x / cell_px), cy = (int)std::floor(y / cell_px);
        float add = 0.f;
        for (int oy = -1; oy <= 1; ++oy)
          for (int ox = -1; ox <= 1; ++ox) {
            const uint32_t hh = fx2_hash((uint32_t)(cx + ox + 4096), (uint32_t)(cy + oy + 4096), seed);
            const float jx = fx2_unit(hh), jy = fx2_unit(hh * 7u + 1u);
            const float sz = 0.5f * cell_px * (1.f - size_jitter * fx2_unit(hh * 13u + 5u));
            const float px = (cx + ox + jx) * cell_px, py = (cy + oy + jy) * cell_px;
            const float dx = x - px, dy = y - py;
            const float d = std::sqrt(dx * dx + dy * dy) / std::max(sz, 1.f);
            if (d >= 1.f) continue;
            const float prof = pointed ? (1.f - d) : (1.f - d * d) * (1.f - d * d);
            add = std::max(add, prof * height * amp * (sz / cell_px) * 2.f);
          }
        out.at(x, y) = in.at(x, y) + add;
      }
  });
}

} // namespace

// --------------------------------------------------------------- Pebbles
REGISTER_NODE(
    Pebbles, "Effect", "Randomly scattered rounded pebbles over the whole surface - a pebble beach",
    [](Node &n) {
      fx2_setup(n);
      add_float(n.attrs, "amount", "Height", 0.02f, 0.f, 0.3f, "Pebbles")
          .tooltip = "How thick the pebbles are, as a fraction of the terrain's\n"
                     "own range. Vue's 'keep the button down' is a larger value.";
      add_float(n.attrs, "density", "Density", 120.f, 8.f, 512.f, "Pebbles")
          .tooltip = "Pebbles across the tile. More is smaller and denser.";
      add_float(n.attrs, "size_jitter", "Size variation", 0.6f, 0.f, 1.f, "Pebbles")
          .tooltip = "0 every pebble the same size; 1 from full size down to nothing.";
      add_seed(n.attrs, "seed", "Seed", 0, "Pebbles").tooltip = "Another arrangement of the same pebbles.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      fx2_stamps(*in, out, n.attrs.get_f("density", 120.f), n.attrs.get_f("amount", 0.02f),
                 n.attrs.get_f("size_jitter", 0.6f), false, n.attrs.get_seed("seed"));
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// -------------------------------------------------------------- FirTrees
REGISTER_NODE(
    FirTrees, "Effect", "Tiny random cones all over the surface - a distant forest in the relief",
    [](Node &n) {
      fx2_setup(n);
      add_float(n.attrs, "amount", "Height", 0.03f, 0.f, 0.3f, "Fir trees")
          .tooltip = "How tall the cones are, as a fraction of the terrain's own range.";
      add_float(n.attrs, "density", "Density", 160.f, 8.f, 512.f, "Fir trees")
          .tooltip = "Trees across the tile.";
      add_float(n.attrs, "size_jitter", "Size variation", 0.5f, 0.f, 1.f, "Fir trees")
          .tooltip = "How much the trees differ in size.";
      add_seed(n.attrs, "seed", "Seed", 0, "Fir trees").tooltip = "Another arrangement of the same trees.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      fx2_stamps(*in, out, n.attrs.get_f("density", 160.f), n.attrs.get_f("amount", 0.03f),
                 n.attrs.get_f("size_jitter", 0.5f), true, n.attrs.get_seed("seed"));
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ---------------------------------------------------------------- Invert
REGISTER_NODE(
    Invert, "Effect", "Turns the terrain upside down within its own range: valleys become ridges",
    [](Node &n) {
      fx2_setup(n);
      add_float(n.attrs, "amount", "Amount", 1.f, 0.f, 1.f, "Invert")
          .tooltip = "1 inverts fully; less blends toward the inverted terrain.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      in->minmax(mn, mx);
      const float k = std::clamp(n.attrs.get_f("amount", 1.f), 0.f, 1.f);
      const float sum = mn + mx;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float v = in->at(x, y);
            out.at(x, y) = v + ((sum - v) - v) * k;
          }
      });
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx
