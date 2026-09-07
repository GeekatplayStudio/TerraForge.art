// Geekatplay TerraForge - see billboard.hpp.
#include "billboard.hpp"
#include "mesh_thumbnail.hpp"
#include "scene.hpp"
#include <algorithm>

namespace studio {

// The card's pixels. 128 is a tree at a few kilometres many times over: at
// the distance a card replaces geometry the copy is a handful of pixels, and
// every scattered mesh pays this once.
static constexpr int CARD_PX = 128;
// mesh_raster fits the model into the square with a five-pixel margin on
// each side; the card therefore covers a little more than the model's own
// span, and the quad has to match or the copies shrink as they turn to cards
static constexpr float CARD_MARGIN = (float)CARD_PX / (float)(CARD_PX - 10);

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
  o.card_rgba = mesh_raster(o, CARD_PX, opt);
  o.card_px = CARD_PX;

  const float sx = o.bmax[0] - o.bmin[0], sy = o.bmax[1] - o.bmin[1];
  const float span = std::max(std::max(sx, sy), 1e-9f);
  o.card_size[0] = o.card_size[1] = span * CARD_MARGIN;
  for (int k = 0; k < 3; ++k) o.card_centre[k] = (o.bmin[k] + o.bmax[k]) * 0.5f;
  return true;
}

} // namespace studio
