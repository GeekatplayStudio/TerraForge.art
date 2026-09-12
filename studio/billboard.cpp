// Geekatplay TerraForge - see billboard.hpp.
#include "billboard.hpp"
#include "mesh_thumbnail.hpp"
#include "scene.hpp"
#include <algorithm>
#include <vector>

namespace studio {

// The card's pixels. 128 is a tree at a few kilometres many times over: at
// the distance a card replaces geometry the copy is a handful of pixels, and
// every scattered mesh pays this once.
static constexpr int CARD_PX = 128;
// mesh_raster fits the model into the square with a five-pixel margin on
// each side; the card therefore covers a little more than the model's own
// span, and the quad has to match or the copies shrink as they turn to cards
static constexpr float CARD_MARGIN = (float)CARD_PX / (float)(CARD_PX - 10);
// How much bigger the card is drawn before it is shrunk to its own size.
static constexpr int SUPER = 4;

bool billboard_build(SceneObject &o) {
  o.card_tried = true;
  o.card_rgba.clear();
  o.card_px = 0;
  if (o.vert_count < 3 || o.verts.size() < (size_t)o.vert_count * 6) return false;

  MeshRasterOptions opt;
  opt.yaw_deg = 0.f;   // straight on: a card is seen from the ground
  opt.pitch_deg = 0.f;
  opt.plate = false;   // everything the mesh did not cover stays transparent
  opt.alpha_cutout = true;
  // Drawn big and shrunk, never drawn small.
  //
  // A tree's foliage is thin: at 128 pixels a needle spray or a leaf is a
  // fraction of one, and a cut-out tested texel by texel either takes the
  // whole texel or none of it. Almost none of them pass, so the foliage
  // vanishes and what survives is the wood - a pine wood went brown from the
  // middle distance out while the trees in front of it were green. Drawn at
  // four times the size and boxed down, each final pixel is the average of
  // the sixteen behind it weighted by how much of each was covered, so a
  // pixel that is a third needle comes out needle-coloured at a third
  // strength. The cards are baked once per plant, so the sixteen-fold raster
  // costs nothing that matters.
  const int big = CARD_PX * SUPER;
  const std::vector<uint8_t> hi = mesh_raster(o, big, opt);
  o.card_rgba.assign((size_t)CARD_PX * CARD_PX * 4, 0);
  o.card_px = CARD_PX;
  if (hi.size() < (size_t)big * big * 4) return false;
  for (int y = 0; y < CARD_PX; ++y)
    for (int x = 0; x < CARD_PX; ++x) {
      float acc[3] = {0.f, 0.f, 0.f};
      float cover = 0.f;
      for (int sy = 0; sy < SUPER; ++sy)
        for (int sx = 0; sx < SUPER; ++sx) {
          const uint8_t *p = &hi[(((size_t)(y * SUPER + sy) * big) + (size_t)(x * SUPER + sx)) * 4];
          const float a = p[3] / 255.f;
          cover += a;
          for (int k = 0; k < 3; ++k) acc[k] += p[k] * a;
        }
      uint8_t *dst = &o.card_rgba[((size_t)y * CARD_PX + x) * 4];
      if (cover <= 1e-4f) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
        continue;
      }
      for (int k = 0; k < 3; ++k)
        dst[k] = (uint8_t)std::clamp(acc[k] / cover, 0.f, 255.f);
      // The card shader draws opaque or not at all, so coverage decides only
      // whether the pixel is there. A quarter covered is enough: less than
      // that and a crown's edge frays away to nothing at exactly the distance
      // the card takes over.
      const float frac = cover / (float)(SUPER * SUPER);
      dst[3] = frac >= 0.25f ? 255 : 0;
    }

  const float sx = o.bmax[0] - o.bmin[0], sy = o.bmax[1] - o.bmin[1];
  const float span = std::max(std::max(sx, sy), 1e-9f);
  o.card_size[0] = o.card_size[1] = span * CARD_MARGIN;
  for (int k = 0; k < 3; ++k) o.card_centre[k] = (o.bmin[k] + o.bmax[k]) * 0.5f;
  return true;
}

} // namespace studio
