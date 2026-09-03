// Geekatplay TerraForge — rendering to files: the beauty image in the chosen
// format, and the G-buffer passes beside it.
//
// Every pass is the same frame drawn again with `g_aov` set: each shader
// writes the requested quantity instead of its shaded colour and returns
// before tone mapping, so a pass costs one draw of the scene and nothing is
// approximated from the beauty afterwards. Passes render into a float
// framebuffer (slot 7) and are written as linear EXR; the beauty goes to
// PNG, EXR or HDR as asked. Planets, gizmos and overlays are skipped while a
// pass is drawn (see draw_scene) - they carry beauty colours only.
#include "renderer_internal.hpp"
#include "hdr_image.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "stb_image_write.h"

namespace studio {

bool renderer_render_to_file(const std::string &path, int w, int h); // renderer_export.cpp

int g_aov = 0; // 0 beauty; 1..RENDER_PASS_COUNT a pass; AOV_BEAUTY_LINEAR

namespace {

// Draw the scene through the free camera (or the active scene camera) into
// slot 7 at the given size and read it back as floats, top row first.
void draw_readback(int w, int h, std::vector<float> &px) {
  ensure_fbo(7, w, h, true);
  RenderSettings::ViewConfig vc = render_settings().views[0];
  vc.camera = 0;
  vc.display = 2;
  vc.atmosphere = true;
  vc.grid = false;
  vc.outlines = false;
  float eye[3], mvp[16], inv_vp[16];
  camera_matrices(w, h, eye, mvp, inv_vp);
  draw_scene(7, vc, w, h, 0.f, eye, mvp, inv_vp);
  px.resize((size_t)w * h * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[7]);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // GL rows start at the bottom; every writer wants the top first
  std::vector<float> row((size_t)w * 4);
  for (int y = 0; y < h / 2; ++y) {
    float *a = &px[(size_t)y * w * 4], *b = &px[(size_t)(h - 1 - y) * w * 4];
    std::copy(a, a + (size_t)w * 4, row.begin());
    std::copy(b, b + (size_t)w * 4, a);
    std::copy(row.begin(), row.end(), b);
  }
}

std::string stem_of(const std::string &path, std::string &ext) {
  size_t dot = path.find_last_of('.');
  size_t slash = path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    ext.clear();
    return path;
  }
  ext = path.substr(dot);
  return path.substr(0, dot);
}

} // namespace

bool renderer_render_passes(const std::string &beauty_path, int w, int h,
                            int pass_mask, int format, std::string &report) {
  report.clear();
  const RenderSettings &RS = render_settings();
  std::vector<float> px;
  std::string err, ext;
  std::string stem = stem_of(beauty_path, ext);
  bool ok = true;

  // ---- beauty
  if (format == 0) {
    // the tone-mapped 8-bit path, supersampled 2x like the viewport export
    ok = renderer_render_to_file(beauty_path, w, h);
    report += ok ? "beauty: " + beauty_path + "\n" : "beauty failed\n";
  } else {
    g_aov = AOV_BEAUTY_LINEAR;
    draw_readback(w, h, px);
    g_aov = 0;
    std::string p = stem + (format == 1 ? ".exr" : ".hdr");
    ok = format == 1 ? exr_write(p, w, h, 4, px.data(), err)
                     : hdr_write(p, w, h, 4, px.data(), err);
    report += ok ? "beauty (linear): " + p + "\n" : "beauty failed: " + err + "\n";
  }

  // ---- passes
  for (int i = 0; i < RENDER_PASS_COUNT; ++i) {
    if (!(pass_mask & (1 << i))) continue;
    g_aov = i + 1;
    draw_readback(w, h, px);
    g_aov = 0;
    // depth and position come back in tile units; the file promises metres
    if ((1 << i) == PASS_DEPTH || (1 << i) == PASS_POSITION) {
      const float k = RS.terrain_size_m;
      for (size_t j = 0; j < px.size(); j += 4) {
        px[j] *= k;
        if ((1 << i) == PASS_POSITION) { px[j + 1] *= k; px[j + 2] *= k; }
      }
    }
    std::string p = stem + "_" + render_pass_name(i) + ".exr";
    if (exr_write(p, w, h, 4, px.data(), err)) report += render_pass_name(i) + std::string(": ") + p + "\n";
    else { report += render_pass_name(i) + std::string(" failed: ") + err + "\n"; ok = false; }
  }
  return ok;
}

} // namespace studio
