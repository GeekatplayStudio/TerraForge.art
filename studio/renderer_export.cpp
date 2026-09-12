#include "uniform_cache.hpp"
// Geekatplay TerraForge - offline outputs: the material preview turntable,
// the sky HDR export, and rendering a view to an image file. Split from
// renderer.cpp for the 500-line module rule.
#include "renderer_internal.hpp"
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "renderer_space.hpp"
#include "world_shape.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "stb_image_write.h"
#include "renderer_shaders.hpp"

namespace studio {


unsigned renderer_material_preview(int size, int shape, float spin) {
  RenderSettings &RS = render_settings();
  if (size < 16) size = 16;
  shape = std::clamp(shape, 0, 2);
  if (size != matprev_size || !matprev_fbo) {
    if (matprev_fbo) {
      glDeleteFramebuffers(1, &matprev_fbo);
      glDeleteTextures(1, &matprev_tex);
      glDeleteRenderbuffers(1, &matprev_depth);
    }
    glGenFramebuffers(1, &matprev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, matprev_fbo);
    glGenTextures(1, &matprev_tex);
    glBindTexture(GL_TEXTURE_2D, matprev_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           matprev_tex, 0);
    glGenRenderbuffers(1, &matprev_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, matprev_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, matprev_depth);
    matprev_size = size;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, matprev_fbo);
  glViewport(0, 0, size, size);
  glClearColor(0.11f, 0.11f, 0.12f, 1.f);
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
  uni3(prog_matprev, "u_sky_light", sky_light_rgb());
  uni1(prog_matprev, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_matprev, "u_grade", g_grade);
    uni1(prog_matprev, "u_sat", g_saturation);
  renderer_material_uniforms(prog_matprev, RS.matp);
  unii(prog_matprev, "u_has_albedo", has_albedo ? 1 : 0);
  unii(prog_matprev, "u_has_normal", has_normal_map ? 1 : 0);
  unii(prog_matprev, "u_has_rough", has_rough_map ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_albedo);
  unii(prog_matprev, "u_albedo", 0);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, tex_normal);
  unii(prog_matprev, "u_normal_map", 5);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, tex_rough);
  unii(prog_matprev, "u_rough_map", 6);
  // gentle turntable for sphere/cube; flat stays facing the camera
  float ax = shape == 2 ? 0.f : spin;
  float tilt = shape == 1 ? 0.42f : (shape == 0 ? 0.18f : 0.f);
  float cy2 = std::cos(ax), sy2 = std::sin(ax);
  float cx2 = std::cos(tilt), sx2 = std::sin(tilt);
  // column-major rotY then rotX
  float rot[9] = {cy2, sy2 * sx2, -sy2 * cx2,
                  0,   cx2,        sx2,
                  sy2, -cy2 * sx2, cy2 * cx2};
  glUniformMatrix3fv(uniform_location(prog_matprev, "u_rot"), 1, GL_FALSE, rot);
  glBindVertexArray(prev_vao[shape]);
  glDrawArrays(GL_TRIANGLES, 0, prev_verts[shape]);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return matprev_tex;
}


// Renders the live sky - gradient, sun tint and volumetric clouds - into a
// float image, in whatever projection `pano` asks for: 1 the equirectangular
// panorama the offline path tracer lights the scene with, 2 the irradiance
// probe whose plain average is the light the sky casts (shaders_sky.cpp).
// Linear radiance either way, never tonemapped: it is light, not a picture.
static bool sky_render_pano(int w, int h, int pano, const float *from,
                            std::vector<float> &px) {
  RenderSettings &RS = render_settings();
  float sun[3];
  compute_sun_dir(RS, sun);
  GLuint f = 0, t = 0;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (ok) {
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(prog_sky);
    // Where the panorama is shot from. A cloud layer is at a finite
    // altitude, so an environment map is only right for the point it was
    // taken at: shot from the middle of the world and used by a camera a
    // mile away, the clouds sit in the wrong part of the sky and the render
    // shows a different sky from the one the camera was looking at. A
    // camera render passes its own eye.
    float eye[3] = {0.5f, RS.cloud_altitude * 0.35f, 0.5f};
    if (from) { eye[0] = from[0]; eye[1] = from[1]; eye[2] = from[2]; }
    SkyUpload u;
    u.eye = eye;
    u.sun = sun;
    u.sun_intensity = RS.sun_intensity;
    u.world_r = RS.planet_radius;
    u.space = 0.f; // panoramas are always shot from the ground
    u.clouds = RS.clouds_on;
    u.steps = 72;
    u.panorama = pano;
    u.hdr = 1;
    u.no_sun = 1; // the sun is emitted separately
    upload_sky_uniforms(prog_sky, RS, u);
    glBindVertexArray(vao_quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    px.assign((size_t)w * h * 4, 0.f);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
    glEnable(GL_DEPTH_TEST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &f);
  glDeleteTextures(1, &t);
  return ok;
}

bool renderer_export_sky_hdr(const std::string &path, int w, int h,
                             const float *from) {
  std::vector<float> px;
  if (!sky_render_pano(w, h, 1, from, px)) return false;
  // flip vertically into RGB for the HDR writer (row 0 = top = +90 deg)
  std::vector<float> rgb((size_t)w * h * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float *s = &px[(((size_t)y * w) + x) * 4];
      float *d = &rgb[(((size_t)(h - 1 - y) * w) + x) * 3];
      d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
    }
  return stbi_write_hdr(path.c_str(), w, h, 3, rgb.data()) != 0;
}

// The light the sky casts on level ground, as one radiance: reflected
// radiance is albedo times this, because the cosine-weighted integral and
// the division by pi cancel. Small on purpose - 32 x 16 is 512 rays through
// the cloud march, which is nothing beside a frame, and it is only taken
// again when the sky itself changes (renderer_passes.cpp).
bool renderer_sky_light(float out_rgb[3]) {
  std::vector<float> px;
  if (!sky_render_pano(32, 16, 2, nullptr, px)) return false;
  double s[3] = {0, 0, 0};
  const size_t n = px.size() / 4;
  if (!n) return false;
  for (size_t i = 0; i < n; ++i)
    for (int k = 0; k < 3; ++k) s[k] += px[i * 4 + (size_t)k];
  for (int k = 0; k < 3; ++k) out_rgb[k] = (float)(s[k] / (double)n);
  return true;
}


// `camera` says whose picture this is: a scene camera's index, or -3 to take
// the first view's own choice, which is what the File menu wants. The matrices
// come from renderer_camera_override the way a drawn view's do - without
// setting it the capture built its matrices from whichever camera happened to
// be active, so a view locked to one camera, and every scripted request for a
// named one, photographed something else.
bool renderer_render_to_file(const std::string &path, int w, int h, int camera) {
  int rw = w * 2, rh = h * 2;
  const int slot = SLOT_CAPTURE;
  ensure_fbo(slot, rw, rh);
  RenderSettings::ViewConfig vc = render_settings().views[0];
  vc.camera = 0;
  vc.display = 2;
  vc.atmosphere = true;
  vc.grid = false;
  // a photograph, not the editing view: no grid, no gizmos, no outlines
  vc.outlines = false;
  if (camera != -3) vc.scene_camera = camera;
  const int was = renderer_camera_override();
  renderer_camera_override() = vc.scene_camera;
  float eye[3], mvp[16], inv_vp[16];
  camera_matrices(rw, rh, eye, mvp, inv_vp);
  draw_scene(slot, vc, rw, rh, renderer_anim_time(), eye, mvp, inv_vp);
  renderer_camera_override() = was;
  // The lens applies to the file too. A capture that skipped it would be a
  // picture of a scene the user is not looking at: the whole point of the
  // optical simulation is that the viewport and the output agree.
  LensOptics optics = camera_optics_for_view(vc, mvp);
  GLuint read_fbo = fbo[slot];
  if (optics.on) {
    unsigned tex = renderer_post_process(slot, rw, rh, optics);
    if (tex && tex != fbo_color[slot]) {
      // read from whichever target the pass wrote into
      static GLuint grab = 0;
      if (!grab) glGenFramebuffers(1, &grab);
      glBindFramebuffer(GL_FRAMEBUFFER, grab);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex, 0);
      read_fbo = grab;
    }
  }
  std::vector<unsigned char> big((size_t)rw * rh * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, read_fbo);
  glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, big.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // GPX_CAPTURE_RAW keeps the picture at the size it was drawn, before the
  // 2x2 average: that average is the size of anything drawn at half
  // resolution (the clouds over the ground) and hides exactly what a
  // comparison of the two is looking for.
  static const bool raw = [] {
    const char *e = std::getenv("GPX_CAPTURE_RAW");
    return e && *e && *e != '0';
  }();
  if (raw) {
    std::vector<unsigned char> flip((size_t)rw * rh * 4);
    for (int y = 0; y < rh; ++y)
      std::memcpy(&flip[(size_t)(rh - 1 - y) * rw * 4], &big[(size_t)y * rw * 4], (size_t)rw * 4);
    return stbi_write_png(path.c_str(), rw, rh, 4, flip.data(), rw * 4) != 0;
  }
  std::vector<unsigned char> out((size_t)w * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c) {
        int sum = 0;
        for (int sy = 0; sy < 2; ++sy)
          for (int sx2 = 0; sx2 < 2; ++sx2)
            sum += big[(((size_t)(y * 2 + sy) * rw) + x * 2 + sx2) * 4 + c];
        out[(((size_t)(h - 1 - y) * w) + x) * 4 + c] = (unsigned char)(sum / 4);
      }
  return stbi_write_png(path.c_str(), w, h, 4, out.data(), w * 4) != 0;
}


void renderer_settings_ui() {
  RenderSettings &RS = render_settings();
  ImGui::SetNextItemWidth(140);
  ImGui::SliderFloat("Height scale", &RS.height_scale, 0.02f, 0.8f);
  ImGui::SetNextItemWidth(140);
  ImGui::SliderFloat("Exposure", &RS.exposure, 0.3f, 3.f);
  studio::Checkbox("Wireframe", &RS.wireframe);
  ImGui::SameLine();
  studio::Checkbox("Graph albedo", &RS.use_albedo);
  studio::Checkbox("Shadows", &RS.shadows);
}

} // namespace studio
