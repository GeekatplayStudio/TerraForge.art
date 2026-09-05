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
#include <chrono>
#include <filesystem>
#include <future>
#include <string>

namespace studio {

namespace {

GLuint g_bd_tex = 0;
std::string g_bd_loaded_path;
std::filesystem::file_time_type g_bd_loaded_time;
int g_bd_w = 0, g_bd_h = 0;
std::string g_bd_status; // what the Render panel shows
float g_bd_mean[3] = {0.f, 0.f, 0.f};
bool g_bd_attempted = false;
std::string g_bd_checked_path;
std::chrono::steady_clock::time_point g_bd_next_check;

// (Re)load when the setting names a different file, or the file changed on
// disk - so an HDRI re-exported from another tool updates without a restart.
struct DecodedBackdrop {
  HdrImage image;
  std::string path, error;
  float mean[3] = {};
};
std::future<DecodedBackdrop> g_bd_pending;

void backdrop_refresh(const RenderSettings::Backdrop &b) {
  // The decoder owns its pixels. Only this context-owning thread creates or
  // replaces GL textures, and an obsolete request never replaces a new path.
  if (g_bd_pending.valid()) {
    if (g_bd_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    DecodedBackdrop result;
    try { result = g_bd_pending.get(); }
    catch (const std::exception &e) { g_bd_status = e.what(); }
    if (result.path == b.file && !result.path.empty()) {
      if (!result.error.empty()) {
        g_bd_status = result.error;
        log_warn("backdrop", result.error);
      } else {
        const auto &img = result.image;
        if (!g_bd_tex) glGenTextures(1, &g_bd_tex);
        glBindTexture(GL_TEXTURE_2D, g_bd_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, img.w, img.h, 0, GL_RGB,
                     GL_FLOAT, img.rgb.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_bd_w = img.w;
        g_bd_h = img.h;
        std::copy(result.mean, result.mean + 3, g_bd_mean);
        char buf[256];
        snprintf(buf, sizeof buf, "%d x %d, mean %.2f %.2f %.2f", img.w, img.h,
                 g_bd_mean[0], g_bd_mean[1], g_bd_mean[2]);
        g_bd_status = buf;
        renderer_invalidate_views();
        log_info("backdrop", "loaded " + b.file + " (" + buf + ")");
      }
    }
  }
  if (b.file.empty()) {
    if (g_bd_tex) { glDeleteTextures(1, &g_bd_tex); g_bd_tex = 0; }
    g_bd_loaded_path.clear();
    g_bd_checked_path.clear();
    g_bd_attempted = false;
    g_bd_status = b.enabled ? "no image chosen" : "";
    return;
  }
  auto now = std::chrono::steady_clock::now();
  if (b.file == g_bd_checked_path && now < g_bd_next_check) return;
  g_bd_checked_path = b.file;
  g_bd_next_check = now + std::chrono::milliseconds(500);
  std::error_code ec;
  auto mtime = std::filesystem::last_write_time(b.file, ec);
  if (ec) {
    g_bd_status = "file not found: " + b.file;
    g_bd_loaded_path = b.file;
    g_bd_attempted = false;
    return;
  }
  if (g_bd_attempted && b.file == g_bd_loaded_path && mtime == g_bd_loaded_time) return;
  g_bd_attempted = true;
  g_bd_loaded_path = b.file;
  g_bd_loaded_time = mtime;
  g_bd_pending = std::async(std::launch::async, [path = b.file] {
    DecodedBackdrop result;
    result.path = path;
    if (!hdr_image_load(path, result.image, result.error)) return result;
    double sum[3] = {};
    for (size_t i = 0; i < result.image.rgb.size(); i += 3)
      for (int c = 0; c < 3; ++c) sum[c] += result.image.rgb[i + c];
    double n = std::max<double>(1.0, result.image.rgb.size() / 3);
    for (int c = 0; c < 3; ++c) result.mean[c] = (float)(sum[c] / n);
    return result;
  });
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
