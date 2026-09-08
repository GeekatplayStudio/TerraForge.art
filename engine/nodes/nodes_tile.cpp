// Geekatplay TerraForge — tiling a terrain across a larger area.
//
// Repeating a map by simply copying it gives a lattice: the same ridge in the
// same place every tile, which the eye finds in a second. Turning each copy a
// random quarter-turn (and, if asked, mirroring it) breaks the lattice - the
// features are the same but never line up - and a feather across each seam
// hides the join. The result reads as a region built from one tile without
// ever looking built.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

// One integer per tile, stable for a seed, so the same tile always turns the
// same way and the result is bit-identical between runs.
uint32_t tile_hash(int ix, int iy, uint32_t seed) {
  uint32_t h = seed * 0x9E3779B1u;
  h ^= (uint32_t)ix * 0x85EBCA6Bu;
  h ^= (uint32_t)iy * 0xC2B2AE35u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}

// Local tile coordinates turned by `turns` quarter-turns and mirrored if
// `mirror`, still in 0..1 of the tile. Coordinates outside 0..1 (a neighbour
// evaluated across the seam for the feather) turn the same way, so the
// continuation is the neighbour's own edge rather than a discontinuity.
void orient(float &u, float &v, int turns, bool mirror) {
  if (mirror) u = 1.f - u;
  for (int k = 0; k < (turns & 3); ++k) {
    const float t = u;
    u = 1.f - v;
    v = t;
  }
}

} // namespace

REGISTER_NODE(
    TileRotate, "Transform",
    "Repeat the terrain as tiles, each turned a random quarter-turn so no lattice shows",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_int(n.attrs, "across", "Tiles across", 2, 1, 16)
          .tooltip = "How many copies side by side. The output is still one\n"
                     "map at the graph's resolution, so more tiles means each\n"
                     "gets fewer pixels.";
      add_int(n.attrs, "down", "Tiles down", 2, 1, 16)
          .tooltip = "How many copies top to bottom.";
      add_choice(n.attrs, "turn", "Each tile",
                 {"Random quarter-turn", "Random quarter-turn + mirror", "As is"}, 0)
          .tooltip = "What happens to each copy. Random turns break up the\n"
                     "lattice a plain repeat makes; adding mirroring doubles\n"
                     "the number of ways a tile can appear. 'As is' is the\n"
                     "plain repeat, for a map that was made seamless.";
      add_seed(n.attrs, "seed", "Seed");
      add_float(n.attrs, "feather", "Seam feather", 0.08f, 0.f, 0.4f)
          .tooltip = "Width of the cross-fade across each seam, as a fraction\n"
                     "of a tile. Zero is a hard join; a turned tile almost\n"
                     "never matches its neighbour at the edge, so some is\n"
                     "usually wanted.";
      add_choice(n.attrs, "whole", "Then turn the whole",
                 {"0°", "90°", "180°", "270°"}, 0)
          .tooltip = "A final quarter-turn of the tiled result, for when the\n"
                     "region needs to face another way. Any angle is the\n"
                     "Transform node.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      const int nx = std::max(1, n.attrs.get_i("across", 2));
      const int ny = std::max(1, n.attrs.get_i("down", 2));
      const int mode = n.attrs.get_choice("turn");
      const uint32_t seed = n.attrs.get_seed("seed");
      const float feather = n.attrs.get_f("feather", 0.08f);
      const int whole = n.attrs.get_choice("whole");

      // The height of one tile at global tile-space coordinates (gx, gy),
      // where the integer part picks the tile and the fraction the position
      // in it. Asking for a position outside a tile's own square gives that
      // tile's map continued past its edge (the input clamps), which is what
      // the feather blends toward.
      auto tile_at = [&](int ix, int iy, float lu, float lv) {
        const uint32_t h = tile_hash(ix, iy, seed);
        int turns = 0;
        bool mirror = false;
        if (mode == 0) turns = (int)(h & 3);
        if (mode == 1) {
          turns = (int)(h & 3);
          mirror = (h >> 2) & 1;
        }
        orient(lu, lv, turns, mirror);
        return in->sample(std::clamp(lu, 0.f, 1.f), std::clamp(lv, 0.f, 1.f));
      };

      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = (x + 0.5f) / out.w, v = (y + 0.5f) / out.h;
            // the final quarter-turn, applied to where we read from
            for (int k = 0; k < whole; ++k) {
              const float t = u;
              u = 1.f - v;
              v = t;
            }
            const float gx = u * nx, gy = v * ny;
            const int ix = std::min((int)gx, nx - 1), iy = std::min((int)gy, ny - 1);
            const float lu = gx - ix, lv = gy - iy;
            float val = tile_at(ix, iy, lu, lv);
            if (feather > 0.f) {
              // Cross-fade toward each neighbour inside the feather band:
              // the neighbour's map, continued across the seam, at the same
              // global point. The weight reaches one half exactly on the
              // seam, so both sides agree there.
              auto blend = [&](float dist, int nix, int niy, float nlu, float nlv) {
                if (dist >= feather || nix < 0 || niy < 0 || nix >= nx || niy >= ny) return;
                const float w = 0.5f * (1.f - dist / feather);
                val += (tile_at(nix, niy, nlu, nlv) - val) * w;
              };
              blend(lu, ix - 1, iy, lu + 1.f, lv);        // left seam
              blend(1.f - lu, ix + 1, iy, lu - 1.f, lv);  // right seam
              blend(lv, ix, iy - 1, lu, lv + 1.f);        // top seam
              blend(1.f - lv, ix, iy + 1, lu, lv - 1.f);  // bottom seam
            }
            out.at(x, y) = val;
          }
      });
    })

} // namespace gpx
