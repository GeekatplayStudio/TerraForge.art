// Geekatplay TerraForge - offline outputs: the material preview turntable,
// the sky HDR export, and rendering a view to an image file. Split from
// renderer.cpp for the 500-line module rule.
#include "renderer_internal.hpp"
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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
  uni1(prog_matprev, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_matprev, "u_grade", g_grade);
    uni1(prog_matprev, "u_sat", g_saturation);
  uni1(prog_matprev, "u_roughness", RS.mat_roughness);
  uni1(prog_matprev, "u_metallic", RS.mat_metallic);
  uni1(prog_matprev, "u_specular", RS.mat_specular);
  uni1(prog_matprev, "u_reflection", RS.mat_reflection);
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
  glUniformMatrix3fv(glGetUniformLocation(prog_matprev, "u_rot"), 1, GL_FALSE, rot);
  glBindVertexArray(prev_vao[shape]);
  glDrawArrays(GL_TRIANGLES, 0, prev_verts[shape]);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return matprev_tex;
}


// Renders the live sky (gradient + sun tint + volumetric clouds) into an
// equirectangular HDR so the offline path tracer lights the scene with the
// exact same environment the viewport shows.
bool renderer_export_sky_hdr(const std::string &path, int w, int h) {
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
    float eye[3] = {0.5f, RS.cloud_altitude * 0.35f, 0.5f};
    uni3(prog_sky, "u_cam", eye);
    uni3(prog_sky, "u_sun", sun);
    uni3(prog_sky, "u_sun_color", RS.sun_color);
    uni1(prog_sky, "u_sun_intensity", RS.sun_intensity);
    uni1(prog_sky, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_sky, "u_grade", g_grade);
    uni1(prog_sky, "u_sat", g_saturation);
    uni3(prog_sky, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_sky, "u_sky_horizon", RS.sky_horizon);
    uni1(prog_sky, "u_atmo", RS.atmosphere_density);
    unii(prog_sky, "u_fog_type", RS.fog_type);
    uni3(prog_sky, "u_fog_color", RS.fog_color);
    uni1(prog_sky, "u_fog_density", RS.fog_density);
    unii(prog_sky, "u_clouds", RS.clouds_on ? 1 : 0);
    unii(prog_sky, "u_cl_steps", 72);
    unii(prog_sky, "u_cl_type", RS.cloud_type);
    uni1(prog_sky, "u_cl_cov", RS.cloud_coverage);
    uni1(prog_sky, "u_cl_den", RS.cloud_density);
    uni1(prog_sky, "u_cl_alt", RS.cloud_altitude);
    uni1(prog_sky, "u_cl_thick", RS.cloud_thickness);
    uni1(prog_sky, "u_cl_detail_amt", RS.cloud_detail);
    uni1(prog_sky, "u_cl_time", cloud_time);
    uni1(prog_sky, "u_cl_ambient", RS.cloud_ambient);
    uni1(prog_sky, "u_cl_anvil", RS.cloud_anvil);
    float wr = RS.cloud_wind_dir * 0.017453293f;
    float wind[2] = {std::cos(wr) * RS.cloud_wind_speed,
                     std::sin(wr) * RS.cloud_wind_speed};
    glUniform2fv(glGetUniformLocation(prog_sky, "u_cl_wind"), 1, wind);
    uni3(prog_sky, "u_cl_color", RS.cloud_color);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
    unii(prog_sky, "u_cl_shape", 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_detail);
    unii(prog_sky, "u_cl_detail", 4);
    // The ray-march dither. An unbound sampler reads black, which would give
    // every ray the same zero offset and put the banding straight back.
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, blue_noise_texture());
    unii(prog_sky, "u_blue_noise", 9);
    unii(prog_sky, "u_cl_octaves", std::clamp(RS.cloud_scatter_octaves, 1, 4));
    uni1(prog_sky, "u_cl_ms_depth", std::clamp(RS.cloud_scatter_depth, 0.05f, 0.99f));
    unii(prog_sky, "u_panorama", 1);
    unii(prog_sky, "u_hdr", 1);
    unii(prog_sky, "u_no_sun", 1); // the sun is emitted separately
    uni1(prog_sky, "u_space", 0.f); // panoramas are always shot from the ground
    glBindVertexArray(vao_quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::vector<float> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
    // flip vertically into RGB for the HDR writer (row 0 = top = +90 deg)
    std::vector<float> rgb((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float *s = &px[(((size_t)y * w) + x) * 4];
        float *d = &rgb[(((size_t)(h - 1 - y) * w) + x) * 3];
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
      }
    ok = stbi_write_hdr(path.c_str(), w, h, 3, rgb.data()) != 0;
    glEnable(GL_DEPTH_TEST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &f);
  glDeleteTextures(1, &t);
  return ok;
}


bool renderer_render_to_file(const std::string &path, int w, int h) {
  int rw = w * 2, rh = h * 2;
  ensure_fbo(5, rw, rh);
  RenderSettings::ViewConfig vc = render_settings().views[0];
  vc.camera = 0;
  vc.display = 2;
  vc.atmosphere = true;
  vc.grid = false;
  float eye[3], mvp[16], inv_vp[16];
  camera_matrices(rw, rh, eye, mvp, inv_vp);
  draw_scene(5, vc, rw, rh, 0.f, eye, mvp, inv_vp);
  std::vector<unsigned char> big((size_t)rw * rh * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[5]);
  glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, big.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
