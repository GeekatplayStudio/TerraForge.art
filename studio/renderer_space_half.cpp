#include "uniform_cache.hpp"
// Geekatplay TerraForge — the nebulas at half size.
//
// Deep space is a star field, a band and the nebulas, and the nebulas are
// nearly all of its cost: a volume march per pixel through every cloud the
// pixel's ray crosses. In the poster sky (space_presets.cpp) that was 30 ms of
// the frame at 1718x798, and the stars and the band together were 2. A cloud
// of gas is soft where a star is a point, so a working view marches the
// nebulas into a half-size picture of their own - what they add, and what
// they let through - just before the sky pass, which then reads it and draws
// the stars at full size behind it. The nebulas are a function of direction
// alone, at infinity, so bringing that picture up needs no depth: a plain
// bilinear read is the whole of it. A capture and a render pass march every
// pixel (u_neb_mode 0), as they do the clouds.
#include "renderer_internal.hpp"
#include "renderer_space.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cstdlib>

namespace studio {

namespace {

struct NebTarget {
  GLuint fbo = 0, add = 0, tr = 0;
  int w = 0, h = 0;
};
NebTarget g_neb[SLOT_COUNT];

bool ensure_target(int slot, int w, int h) {
  NebTarget &T = g_neb[slot];
  if (T.fbo && T.w == w && T.h == h) return true;
  if (!T.fbo) glGenFramebuffers(1, &T.fbo);
  if (!T.add) glGenTextures(1, &T.add);
  if (!T.tr) glGenTextures(1, &T.tr);
  glActiveTexture(GL_TEXTURE15);
  for (GLuint tex : {T.add, T.tr}) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glActiveTexture(GL_TEXTURE0);
  GLint prev = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
  glBindFramebuffer(GL_FRAMEBUFFER, T.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, T.add, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, T.tr, 0);
  const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, bufs);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
  T.w = w;
  T.h = h;
  return ok;
}

// a cloud to march: an emission or a dark nebula, visible (the discs of
// the galaxies are a lookup, not a march, and cost nothing to leave where
// they are)
bool any_cloud_nebula() {
  const SceneState &sc = scene();
  for (const SceneObject &o : sc.objects)
    if (o.type == SceneObject::Nebula && o.nebula.type <= 1 && sc.object_visible(o)) return true;
  return false;
}

} // namespace

int space_nebulas_half(const FrameCtx &F, GLuint prog, const SkyUpload &u) {
  const RenderSettings &RS = F.RS;
  // GPX_NEBULAS_HALF=0 marches every pixel everywhere, =2 halves the
  // captures too, so a script can compare the two (as GPX_CLOUDS_HALF does)
  static const int mode = [] {
    const char *e = std::getenv("GPX_NEBULAS_HALF");
    return e && *e ? std::atoi(e) : 1;
  }();
  if (mode == 0 || (mode == 1 && u.finished)) return 0;
  if (!RS.space.on || F.w < 16 || F.h < 16 || !any_cloud_nebula()) return 0;
  const int hw = (F.w + 1) / 2, hh = (F.h + 1) / 2;
  if (!ensure_target(F.slot, hw, hh)) return 0;
  GLint scene_fbo = 0, vp[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &scene_fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  const bool depth_test = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
  glDisable(GL_DEPTH_TEST);
  // The program reads the picture it is about to draw on the units the last
  // frame left it on; a texture bound to a sampler while it is also being
  // drawn into is a feedback loop, whether or not the branch reads it.
  glActiveTexture(GL_TEXTURE15);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE12);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, g_neb[F.slot].fbo);
  glViewport(0, 0, hw, hh);
  glUseProgram(prog);
  glUniformMatrix4fv(uniform_location(prog, "u_inv_vp"), 1, GL_FALSE, F.inv_vp);
  upload_sky_uniforms(prog, RS, u);
  unii(prog, "u_neb_mode", 1);
  // Half again as many steps: the picture costs a quarter of the view, and
  // the dither's grain, brought up to full size, is twice as coarse - more
  // steps are what keep it as fine as the full march's.
  unii(prog, "u_sp_steps", std::min(space_view_steps(u.finished) * 3 / 2, 72));
  glBindVertexArray(vao_quad);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)scene_fbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (depth_test) glEnable(GL_DEPTH_TEST);
  // for the sky pass that follows
  glActiveTexture(GL_TEXTURE15);
  glBindTexture(GL_TEXTURE_2D, g_neb[F.slot].add);
  unii(prog, "u_neb_add", 15);
  glActiveTexture(GL_TEXTURE12);
  glBindTexture(GL_TEXTURE_2D, g_neb[F.slot].tr);
  unii(prog, "u_neb_tr", 12);
  glActiveTexture(GL_TEXTURE0);
  glUniform2f(uniform_location(prog, "u_neb_px"), (float)F.w, (float)F.h);
  return 2;
}

void space_half_shutdown() {
  for (NebTarget &T : g_neb) {
    if (T.fbo) glDeleteFramebuffers(1, &T.fbo);
    if (T.add) glDeleteTextures(1, &T.add);
    if (T.tr) glDeleteTextures(1, &T.tr);
    T = NebTarget();
  }
}

} // namespace studio
