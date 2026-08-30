// Geekatplay Studio — per-node preview thumbnails (hillshaded relief)
#include "app.hpp"
#include <glad/gl.h>
#include <cmath>
#include <map>
#include <vector>

namespace studio {

struct Preview {
  unsigned tex = 0;
  int w = 0, h = 0;
};
static std::map<uint64_t, Preview> PREVIEWS;
static const int PW = 112;

static void upload_preview(uint64_t id, const std::vector<uint8_t> &rgba, int w,
                           int h) {
  Preview &p = PREVIEWS[id];
  if (!p.tex) glGenTextures(1, &p.tex);
  glBindTexture(GL_TEXTURE_2D, p.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba.data());
  p.w = w;
  p.h = h;
}

void previews_update(App &a) {
  for (auto &n : a.graph.nodes) {
    gpx::Port *pt = n->first_out(gpx::DataType::Texture);
    if (pt && pt->tex && !pt->tex->empty()) {
      gpx::TextureRGBA small(PW, PW);
      const gpx::TextureRGBA &src = *pt->tex;
      for (int y = 0; y < PW; ++y)
        for (int x = 0; x < PW; ++x) {
          int sx = x * src.w / PW, sy = y * src.h / PW;
          const float *sp = src.px(sx, sy);
          float *dp = small.px(x, y);
          for (int k = 0; k < 4; ++k) dp[k] = sp[k];
        }
      upload_preview(n->id, small.to_u8(), PW, PW);
      continue;
    }
    gpx::Port *ph = n->first_out(gpx::DataType::Heightmap);
    if (ph && ph->hmap && !ph->hmap->empty()) {
      gpx::Heightmap small = ph->hmap->resampled(PW, PW);
      small.remap(0.f, 1.f);
      std::vector<uint8_t> rgba((size_t)PW * PW * 4);
      for (int y = 0; y < PW; ++y)
        for (int x = 0; x < PW; ++x) {
          float dx, dy;
          small.gradient_at(x, y, dx, dy);
          float nx = -dx * PW * 0.5f, ny = -dy * PW * 0.5f, nz = 1.f;
          float len = std::sqrt(nx * nx + ny * ny + nz * nz);
          float shade =
              std::fmax((nx * -0.5f + ny * -0.5f + nz * 0.7071f) / len, 0.f);
          float v = small.at(x, y);
          float g = (0.25f + 0.75f * v) * (0.35f + 0.65f * shade);
          uint8_t b = (uint8_t)std::fmin(g * 255.f, 255.f);
          size_t i = ((size_t)y * PW + x) * 4;
          rgba[i] = rgba[i + 1] = rgba[i + 2] = b;
          rgba[i + 3] = 255;
        }
      upload_preview(n->id, rgba, PW, PW);
    }
  }
  // drop previews of deleted nodes
  for (auto it = PREVIEWS.begin(); it != PREVIEWS.end();) {
    if (!a.graph.find_node(it->first)) {
      glDeleteTextures(1, &it->second.tex);
      it = PREVIEWS.erase(it);
    } else
      ++it;
  }
}

void previews_clear() {
  for (auto &[id, p] : PREVIEWS)
    glDeleteTextures(1, &p.tex);
  PREVIEWS.clear();
}

unsigned previews_get(uint64_t node_id, int *w, int *h) {
  auto it = PREVIEWS.find(node_id);
  if (it == PREVIEWS.end()) return 0;
  if (w) *w = it->second.w;
  if (h) *h = it->second.h;
  return it->second.tex;
}

} // namespace studio
