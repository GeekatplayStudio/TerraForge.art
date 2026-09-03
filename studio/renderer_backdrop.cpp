// Geekatplay TerraForge — the HDR backdrop dome on the GPU.
//
// One texture, loaded when the file name or its modification time changes
// (never per frame — performance rule 1), bound on unit 11 in every program
// that carries SKY_FN so that sky, water reflections and terrain reflections
// all see the same dome. The mapping maths lives in the shader (SKY_FN,
// shaders_terrain.cpp); this side only owns the texture and the uniforms.
#include "renderer_internal.hpp"
#include "console.hpp"
#include "hdr_image.hpp"
#include <cmath>
#include <filesystem>
#include <string>

namespace studio {

namespace {

GLuint g_bd_tex = 0;
std::string g_bd_loaded_path;
std::filesystem::file_time_type g_bd_loaded_time;
int g_bd_w = 0, g_bd_h = 0;
std::string g_bd_status; // what the Render panel shows
float g_bd_mean[3] = {0.f, 0.f, 0.f};

// (Re)load when the setting names a different file, or the file changed on
// disk - so an HDRI re-exported from another tool updates without a restart.
void backdrop_refresh(const RenderSettings::Backdrop &b) {
  if (b.file.empty()) {
    if (g_bd_tex) { glDeleteTextures(1, &g_bd_tex); g_bd_tex = 0; }
    g_bd_loaded_path.clear();
    g_bd_status = b.enabled ? "no image chosen" : "";
    return;
  }
  std::error_code ec;
  auto mtime = std::filesystem::last_write_time(b.file, ec);
  if (ec) {
    if (g_bd_loaded_path != b.file) g_bd_status = "file not found: " + b.file;
    if (g_bd_tex) { glDeleteTextures(1, &g_bd_tex); g_bd_tex = 0; }
    g_bd_loaded_path = b.file;
    return;
  }
  if (g_bd_tex && b.file == g_bd_loaded_path && mtime == g_bd_loaded_time) return;
  g_bd_loaded_path = b.file;
  g_bd_loaded_time = mtime;
  HdrImage img;
  std::string err;
  if (!hdr_image_load(b.file, img, err)) {
    if (g_bd_tex) { glDeleteTextures(1, &g_bd_tex); g_bd_tex = 0; }
    g_bd_status = err;
    log_warn("backdrop", err);
    return;
  }
  if (!g_bd_tex) glGenTextures(1, &g_bd_tex);
  glBindTexture(GL_TEXTURE_2D, g_bd_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, img.w, img.h, 0, GL_RGB, GL_FLOAT,
               img.rgb.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  // wrap around the seam of a panorama, never across the poles
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  g_bd_w = img.w;
  g_bd_h = img.h;
  double sum[3] = {0, 0, 0};
  for (size_t i = 0; i < img.rgb.size(); i += 3)
    for (int c = 0; c < 3; ++c) sum[c] += img.rgb[i + c];
  const double n = std::max<double>(1.0, img.rgb.size() / 3);
  for (int c = 0; c < 3; ++c) g_bd_mean[c] = (float)(sum[c] / n);
  char buf[256];
  snprintf(buf, sizeof buf, "%d x %d, mean %.2f %.2f %.2f", img.w, img.h,
           g_bd_mean[0], g_bd_mean[1], g_bd_mean[2]);
  g_bd_status = buf;
  log_info("backdrop", "loaded " + b.file + " (" + buf + ")");
}

} // namespace

void backdrop_bind(GLuint prog) {
  const RenderSettings::Backdrop &b = render_settings().backdrop;
  backdrop_refresh(b);
  const bool on = b.enabled && g_bd_tex != 0;
  glActiveTexture(GL_TEXTURE11);
  glBindTexture(GL_TEXTURE_2D, g_bd_tex);
  unii(prog, "u_backdrop", 11);
  unii(prog, "u_bd_on", on ? 1 : 0);
  if (!on) return;
  unii(prog, "u_bd_mode", b.mapping);
  uni1(prog, "u_bd_aspect", g_bd_h > 0 ? (float)g_bd_w / (float)g_bd_h : 1.f);
  uni1(prog, "u_bd_yaw", b.yaw * 0.017453293f);
  uni1(prog, "u_bd_pitch", b.pitch * 0.017453293f);
  uni1(prog, "u_bd_tanhalf", std::tan(std::clamp(b.vfov, 5.f, 179.f) * 0.5f * 0.017453293f));
  unii(prog, "u_bd_flip", b.flip ? 1 : 0);
  uni1(prog, "u_bd_gain", std::exp2(b.exposure_ev));
  uni3(prog, "u_bd_tint", b.tint);
  uni1(prog, "u_bd_blend", std::clamp(b.blend, 0.f, 1.f));
  uni1(prog, "u_bd_haze", std::clamp(b.haze, 0.f, 1.f));
  unii(prog, "u_bd_hide_sun", b.hide_sun ? 1 : 0);
}

std::string renderer_backdrop_status() { return g_bd_status; }

void upload_fog_uniforms(GLuint prog, const RenderSettings &RS, bool atmosphere) {
  unii(prog, "u_fog_type", atmosphere ? RS.fog_type : 0);
  uni1(prog, "u_fog_density", RS.fog_density);
  uni1(prog, "u_fog_level", RS.fog_level);
  uni1(prog, "u_fog_falloff", RS.fog_falloff);
  uni3(prog, "u_fog_color", RS.fog_color);
  uni3(prog, "u_absorb", RS.absorption_color);
  uni1(prog, "u_fog_scatter", RS.fog_sun_scatter);
}

} // namespace studio
