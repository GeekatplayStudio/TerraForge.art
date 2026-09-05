#include "uniform_cache.hpp"
// Geekatplay TerraForge - a preview of one particular material.
//
// renderer_material_preview() draws whatever material the terrain is wearing,
// because that is what the Properties tab asks about. The Material Studio and
// the browser ask a different question - "what does THIS material look like"
// - for a material that may not be assigned to anything. So this keeps a
// small cache of GPU textures per material, uploaded only when the material's
// evaluation actually changed, and draws the same lit sphere from them. The
// terrain's own maps are never touched.
//
// Responsiveness comes from the cache, not from drawing less: a spinning
// preview redraws every frame, but it re-uploads nothing, and a browser full
// of thumbnails uploads each material once.
#include "renderer_internal.hpp"
#include "render_settings.hpp"
#include "gpx/heightmap.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace studio {

namespace {

struct Slot {
  unsigned long long key = 0, version = 0;
  GLuint albedo = 0, normal = 0, rough = 0;
  bool has_albedo = false, has_normal = false, has_rough = false;
  unsigned last_use = 0;
};
std::vector<Slot> g_slots;
unsigned g_tick = 0;
GLuint g_fbo = 0, g_tex = 0, g_depth = 0;
int g_size = 0;

void upload(GLuint &tex, const gpx::TextureRGBA *t, bool &flag) {
  flag = t && !t->empty();
  if (!flag) return;
  if (!tex) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA, GL_FLOAT,
               t->v.data());
  glGenerateMipmap(GL_TEXTURE_2D);
}

Slot &slot_for(const MaterialPreviewSpec &spec) {
  ++g_tick;
  for (Slot &s : g_slots)
    if (s.key == spec.key) {
      s.last_use = g_tick;
      if (s.version != spec.version) {
        s.version = spec.version;
        upload(s.albedo, (const gpx::TextureRGBA *)spec.albedo, s.has_albedo);
        upload(s.normal, (const gpx::TextureRGBA *)spec.normal, s.has_normal);
        upload(s.rough, (const gpx::TextureRGBA *)spec.rough, s.has_rough);
      }
      return s;
    }
  // Not cached. Evict the least recently shown once the cache is full: a
  // browser page shows a few dozen at once, and 48 keeps a page resident.
  if (g_slots.size() >= 48) {
    size_t victim = 0;
    for (size_t i = 1; i < g_slots.size(); ++i)
      if (g_slots[i].last_use < g_slots[victim].last_use) victim = i;
    Slot &v = g_slots[victim];
    if (v.albedo) glDeleteTextures(1, &v.albedo);
    if (v.normal) glDeleteTextures(1, &v.normal);
    if (v.rough) glDeleteTextures(1, &v.rough);
    g_slots.erase(g_slots.begin() + (long)victim);
  }
  Slot s;
  s.key = spec.key;
  s.version = spec.version;
  s.last_use = g_tick;
  upload(s.albedo, (const gpx::TextureRGBA *)spec.albedo, s.has_albedo);
  upload(s.normal, (const gpx::TextureRGBA *)spec.normal, s.has_normal);
  upload(s.rough, (const gpx::TextureRGBA *)spec.rough, s.has_rough);
  g_slots.push_back(s);
  return g_slots.back();
}

void ensure_target(int size) {
  if (g_fbo && g_size == size) return;
  if (g_fbo) {
    glDeleteFramebuffers(1, &g_fbo);
    glDeleteTextures(1, &g_tex);
    glDeleteRenderbuffers(1, &g_depth);
  }
  glGenFramebuffers(1, &g_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         g_tex, 0);
  glGenRenderbuffers(1, &g_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, g_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            g_depth);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  g_size = size;
}

} // namespace

unsigned renderer_material_preview_of(const MaterialPreviewSpec &spec, int size,
                                      int shape, float spin) {
  RenderSettings &RS = render_settings();
  if (size < 16) size = 16;
  shape = std::clamp(shape, 0, 2);
  Slot &s = slot_for(spec);
  ensure_target(size);

  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glViewport(0, 0, size, size);
  // Vue's preview backgrounds: uniform, in a choice of tones. The grey is
  // the default because both a bright and a dark material read against it.
  const float bg[3][3] = {{0.11f, 0.11f, 0.12f}, {0.42f, 0.42f, 0.44f},
                          {0.86f, 0.86f, 0.88f}};
  const float *c = bg[std::clamp(spec.background, 0, 2)];
  glClearColor(c[0], c[1], c[2], 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  float sun[3];
  compute_sun_dir(RS, sun);
  glUseProgram(prog_matprev);
  uni3(prog_matprev, "u_sun", sun);
  uni3(prog_matprev, "u_sun_color", RS.sun_color);
  uni1(prog_matprev, "u_sun_intensity", RS.sun_intensity);
  uni3(prog_matprev, "u_sky_zenith", RS.sky_zenith);
  uni3(prog_matprev, "u_sky_horizon", RS.sky_horizon);
  uni1(prog_matprev, "u_ambient", RS.ambient_intensity);
  uni1(prog_matprev, "u_exposure", RS.exposure * g_exposure_mult);
  uni3(prog_matprev, "u_grade", g_grade);
  uni1(prog_matprev, "u_sat", g_saturation);
  // this material's own surface, not the terrain's
  renderer_material_uniforms(prog_matprev, spec.params);
  unii(prog_matprev, "u_has_albedo", s.has_albedo ? 1 : 0);
  unii(prog_matprev, "u_has_normal", s.has_normal ? 1 : 0);
  unii(prog_matprev, "u_has_rough", s.has_rough ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, s.albedo);
  unii(prog_matprev, "u_albedo", 0);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, s.normal);
  unii(prog_matprev, "u_normal_map", 5);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, s.rough);
  unii(prog_matprev, "u_rough_map", 6);
  float ax = shape == 2 ? 0.f : spin;
  float tilt = shape == 1 ? 0.42f : (shape == 0 ? 0.18f : 0.f);
  float cy2 = std::cos(ax), sy2 = std::sin(ax);
  float cx2 = std::cos(tilt), sx2 = std::sin(tilt);
  float rot[9] = {cy2, sy2 * sx2, -sy2 * cx2, 0, cx2, sx2, sy2, -cy2 * sx2,
                  cy2 * cx2};
  glUniformMatrix3fv(uniform_location(prog_matprev, "u_rot"), 1, GL_FALSE,
                     rot);
  glBindVertexArray(prev_vao[shape]);
  glDrawArrays(GL_TRIANGLES, 0, prev_verts[shape]);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return g_tex;
}

// A thumbnail that outlives the shared target: rendered, then copied into a
// texture of its own, so a browser can show forty at once.
unsigned renderer_material_thumbnail(const MaterialPreviewSpec &spec, int size,
                                     unsigned existing) {
  renderer_material_preview_of(spec, size, 0, 0.6f);
  GLuint tex = existing;
  if (!tex) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, size, size, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return tex;
}

} // namespace studio
