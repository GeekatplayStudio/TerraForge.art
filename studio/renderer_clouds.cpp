#include "uniform_cache.hpp"
// Geekatplay TerraForge — the cloud layers, wherever they are seen from.
//
// The sky pass marches the clouds on the rays that reach the sky. What makes
// them one layer everywhere else lives here:
//
//  - the sky's uniforms, uploaded the same way for every program that marches
//    it, so the sky, a panorama and the clouds over the ground can never
//    drift apart (they were three copies of one list);
//  - the clouds over the ground: after the ground and the sea are drawn,
//    every ray that ended on something is marched again up to where it
//    ended, so a cloud stands in front of a mountain, lies under a camera
//    above it and wraps the world seen from orbit (renderer_clouds_over.cpp);
//  - the sky everything reflects: the sky pass itself, clouds and all, shot
//    into a small panorama per view from the water under the eye, again as
//    the clouds drift or the eye moves. The sea used to reflect a gradient
//    with no clouds in it, while its wave-driven foam drifted like cloud
//    reflections; now the clouds in the water are the clouds overhead,
//    moving with them. The ground and meshes read the same picture
//    (SKY_ENV_GLSL): a glossy rock or wet ground under an overcast used to
//    reflect a clear blue gradient.
#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"
#include "renderer_space.hpp"
#include "cloud_noise.hpp"
#include "console.hpp"
#include "gpu_timer.hpp"
#include "perf.hpp"
#include "world_shape.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace studio {

// The sky as everything that reflects it sees it. u_sky_env_on is 0 when the
// view has no panorama (no air, a flat background, or nothing drifting and
// no sea to need one), and every caller keeps its own stand-in for that case.
const char *const SKY_ENV_GLSL = R"GLSL(
uniform sampler2D u_sky_env;
uniform int u_sky_env_on;
// the panorama along R, blurred to the cone `alpha` radians wide
vec3 sky_env(vec3 R, float alpha){
  // Below the horizon the panorama holds no ground - the sky pass draws only
  // what is above it, and down there that came out as a starry night under
  // every glossy object and in the face of every steep wave. What is there
  // instead is ground lit by that sky: the horizon's light, dimmed as the
  // reflection turns to face straight down.
  float below = clamp(-R.y, 0.0, 1.0);
  vec3 Ru = normalize(vec3(R.x, max(R.y, 0.004), R.z));
  vec2 uv = vec2(atan(Ru.x, -Ru.z) / 6.2831853 + 0.5,
                 asin(clamp(Ru.y, -1.0, 1.0)) / 3.14159265 + 0.5);
  // an explicit level: derivatives jump where the longitude wraps round
  float lod = log2(max(alpha * float(textureSize(u_sky_env, 0).x) / 6.2831853, 1.0));
  vec3 c = textureLod(u_sky_env, uv, lod).rgb;
  if (below > 0.0){
    // Not the horizon's detail, stretched: read sharp, the clouds along it
    // were drawn down the lower half as streaks meeting at the bottom. The
    // light over that side of the sky instead - the upper half's row of the
    // 4x2 level, blurred round the horizon - fading in below it.
    vec3 g = textureLod(u_sky_env, vec2(uv.x, 0.75), 7.0).rgb;
    c = mix(c, g, smoothstep(0.0, 0.15, below));
  }
  return c * mix(1.0, 0.3, smoothstep(0.0, 0.25, below));
}
)GLSL";

namespace {

// The reflection panorama: half a degree a texel across, which is about the
// blur the calmest sea already gives the sky (the sun's own width folded
// into its roughness), mipmapped so a rough sea reads a wider cone.
constexpr int ENV_W = 512, ENV_H = 256;
// One per view, kept between frames. Re-shooting every view every frame spent
// a cloud march on pictures that had not moved: the clouds drift a fraction
// of a texel in a frame, and a view that is not flying sees the same sky.
struct EnvShot {
  GLuint fbo = 0, tex = 0;
  bool valid = false;
  double when = -1.0;          // the real time it was shot
  float at[3] = {0.f, 0.f, 0.f}; // the point it was shot from
};
EnvShot g_env[SLOT_COUNT];
int g_env_read = -1; // the shot this frame's sea reads (set by sky_env_update)
// A settings change or the drift shows within this long; below the eye's own
// frame rate, so a still view pays for a third of the frames at most.
constexpr double ENV_REFRESH_S = 1.0 / 15.0;

// radians a pixel of this view spans; 0 for an orthographic view
float view_pixel_k(const FrameCtx &F) {
  if (F.vc.camera != 0) return 0.f;
  return 2.f * std::tan(g_last_fovy * 0.5f) / float(std::max(F.h, 1));
}

} // namespace

int sky_cloud_steps(const RenderSettings &RS, bool cinematic) {
  const int cq = std::min(RS.cloud_quality, perf_quality().cloud_quality_cap);
  int steps = cq == 0 ? 24 : (cq == 2 ? 72 : 44);
  if (cinematic) steps = (int)(steps * 1.5f);
  return steps;
}

SkyUpload sky_upload_for_view(const FrameCtx &F) {
  const RenderSettings &RS = F.RS;
  SkyUpload u;
  u.eye = F.view_eye;
  u.sun = F.sun;
  u.sun_intensity = F.sun_intensity;
  // the curvature this view draws the ground with, so the clouds sit on the
  // ground they are seen over
  u.world_r = view_planet_radius(RS, F.vc);
  float space = F.vc.camera == 0 ? F.space_t : 0.f;
  // an inside world's air is a layer on its ground: the sky over it thins to
  // space, and beyond a ring's rim there is nothing but stars
  // (world_shape.hpp); the far shell covers the rest
  if (RS.world_inside && !world_is_flat(RS)) space = std::max(space, 0.75f);
  u.space = space;
  u.clouds = F.clouds_ok;
  u.steps = sky_cloud_steps(RS, F.cinematic);
  u.pixel_k = view_pixel_k(F);
  u.aov = g_aov;
  u.finished = F.slot == SLOT_CAPTURE || F.slot == SLOT_AOV;
  return u;
}

void upload_sky_uniforms(GLuint prog, const RenderSettings &RS, const SkyUpload &u) {
  uni3(prog, "u_cam", u.eye);
  uni3(prog, "u_sun", u.sun);
  uni3(prog, "u_sun_color", RS.sun_color);
  uni1(prog, "u_sun_intensity", u.sun_intensity);
  uni1(prog, "u_exposure", RS.exposure * g_exposure_mult);
  uni3(prog, "u_grade", g_grade);
  uni1(prog, "u_sat", g_saturation);
  uni3(prog, "u_sky_zenith", RS.sky_zenith);
  uni3(prog, "u_sky_horizon", RS.sky_horizon);
  uni1(prog, "u_atmo", RS.atmosphere_density);
  uni1(prog, "u_sun_angle", RS.sun_angle_deg);
  uni1(prog, "u_sun_glow", RS.sun_glow);
  uni1(prog, "u_sun_glow_size", RS.sun_glow_size);
  unii(prog, "u_fog_type", RS.fog_type);
  uni3(prog, "u_fog_color", RS.fog_color);
  uni1(prog, "u_fog_density", RS.fog_density);
  unii(prog, "u_panorama", u.panorama);
  unii(prog, "u_hdr", u.hdr);
  unii(prog, "u_no_sun", u.no_sun);
  uni1(prog, "u_space", u.space);
  unii(prog, "u_aov", u.aov);
  // the nebulas marched in place unless a caller has just made their
  // half-size picture and says so after this (renderer_space_half.cpp); the
  // panoramas and the light probe must never read a view's picture
  unii(prog, "u_neb_mode", 0);
  // the world the air lies on: the cloud layers are bands over its surface
  // and the sky's up is the surface's up (world_shape.hpp)
  const gpx::planet::Shape S = world_shape(RS);
  upload_world_shape(prog, S);
  uni1(prog, "u_world_r", u.world_r);
  uni1(prog, "u_world_w", RS.world_width);
  unii(prog, "u_world_outline", RS.world_outline);
  unii(prog, "u_sun_mode", (RS.world_sun_inside && S.inside) ? 1 : 0);
  uni1(prog, "u_atm_h", RS.atmosphere_height);
  uni1(prog, "u_atm_falloff", RS.atmosphere_falloff);
  upload_space_uniforms(prog, u.finished); // the stars, the galaxy, the nebulas
  // the cloud layers
  unii(prog, "u_clouds", u.clouds ? 1 : 0);
  unii(prog, "u_cl_steps", u.steps);
  uni1(prog, "u_cl_pixel_k", u.pixel_k);
  unii(prog, "u_cl_type", RS.cloud_type);
  unii(prog, "u_cl_volumetric", RS.cloud_volumetric ? 1 : 0);
  uni1(prog, "u_cl_weather", RS.cloud_weather);
  uni1(prog, "u_cl_weather_scale", RS.cloud_weather_scale);
  uni1(prog, "u_cl_scale", RS.cloud_scale);
  uni1(prog, "u_cl_cov", RS.cloud_coverage);
  uni1(prog, "u_cl_den", RS.cloud_density);
  uni1(prog, "u_cl_alt", RS.cloud_altitude);
  uni1(prog, "u_cl_thick", RS.cloud_thickness);
  uni1(prog, "u_cl_detail_amt", RS.cloud_detail);
  uni1(prog, "u_cl_time", cloud_time);
  uni1(prog, "u_cl_ambient", RS.cloud_ambient);
  uni1(prog, "u_cl_anvil", RS.cloud_anvil);
  unii(prog, "u_cl2", RS.cloud2_on ? 1 : 0);
  unii(prog, "u_cl2_type", RS.cloud2_type);
  uni1(prog, "u_cl2_cov", RS.cloud2_coverage);
  uni1(prog, "u_cl2_den", RS.cloud2_density);
  uni1(prog, "u_cl2_alt", RS.cloud2_altitude);
  uni1(prog, "u_cl2_thick", RS.cloud2_thickness);
  {
    // the extra layers from CloudLayer nodes, as arrays
    const int n = std::min((int)RS.cloud_layers.size(), RenderSettings::MAX_CLOUD_LAYERS);
    int types[RenderSettings::MAX_CLOUD_LAYERS] = {};
    float cov[RenderSettings::MAX_CLOUD_LAYERS] = {}, den[RenderSettings::MAX_CLOUD_LAYERS] = {},
          alt[RenderSettings::MAX_CLOUD_LAYERS] = {}, thick[RenderSettings::MAX_CLOUD_LAYERS] = {};
    for (int i = 0; i < n; ++i) {
      const auto &L = RS.cloud_layers[(size_t)i];
      types[i] = L.type; cov[i] = L.coverage; den[i] = L.density; alt[i] = L.altitude; thick[i] = L.thickness;
    }
    unii(prog, "u_clx_n", n);
    glUniform1iv(uniform_location(prog, "u_clx_type"), RenderSettings::MAX_CLOUD_LAYERS, types);
    glUniform1fv(uniform_location(prog, "u_clx_cov"), RenderSettings::MAX_CLOUD_LAYERS, cov);
    glUniform1fv(uniform_location(prog, "u_clx_den"), RenderSettings::MAX_CLOUD_LAYERS, den);
    glUniform1fv(uniform_location(prog, "u_clx_alt"), RenderSettings::MAX_CLOUD_LAYERS, alt);
    glUniform1fv(uniform_location(prog, "u_clx_thick"), RenderSettings::MAX_CLOUD_LAYERS, thick);
  }
  const float wr = RS.cloud_wind_dir * 0.017453293f;
  const float wind[2] = {std::cos(wr) * RS.cloud_wind_speed, std::sin(wr) * RS.cloud_wind_speed};
  glUniform2fv(uniform_location(prog, "u_cl_wind"), 1, wind);
  // Where the deck has actually drifted to, integrated a frame at a time, so
  // a gust or a turn moves the clouds on instead of teleporting them
  // (RenderSettings::wind_advance).
  glUniform2fv(uniform_location(prog, "u_cl_drift"), 1, RS.wind_drift_hi);
  uni1(prog, "u_cl_drift_len", RS.wind_drift_hi_len);
  uni3(prog, "u_cl_color", RS.cloud_color);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
  unii(prog, "u_cl_shape", 3);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_3D, tex_cloud_detail);
  unii(prog, "u_cl_detail", 4);
  // The ray-march dither. An unbound sampler reads black, which would give
  // every ray the same zero offset and put the banding straight back.
  glActiveTexture(GL_TEXTURE9);
  glBindTexture(GL_TEXTURE_2D, blue_noise_texture());
  unii(prog, "u_blue_noise", 9);
  unii(prog, "u_cl_octaves", std::clamp(RS.cloud_scatter_octaves, 1, 4));
  uni1(prog, "u_cl_ms_depth", std::clamp(RS.cloud_scatter_depth, 0.05f, 0.99f));
  backdrop_bind(prog); // the dome is part of the sky too
  glActiveTexture(GL_TEXTURE0);
}

void clouds_init() {
  clouds_over_init(); // renderer_clouds_over.cpp
}

void clouds_shutdown() {
  clouds_over_shutdown();
  space_half_shutdown(); // the sky's half-size nebulas (renderer_space_half.cpp)
  for (EnvShot &e : g_env) {
    if (e.fbo) glDeleteFramebuffers(1, &e.fbo);
    if (e.tex) glDeleteTextures(1, &e.tex);
    e = EnvShot();
  }
  g_env_read = -1;
}

void sky_env_update(const FrameCtx &F) {
  const RenderSettings &RS = F.RS;
  g_env_read = -1;
  // A view with a sky pays for the shot when something reflects what the
  // gradient cannot stand in for: a sea, whose reflection is most of what it
  // looks like, or clouds, which glossy ground and meshes reflect too. With
  // neither, the ground's reflection stays the gradient - the same clear sky.
  // A flat background keeps every reflection on its gradient.
  const bool sea = RS.show_water && F.vc.show_water_view && F.show_water_obj;
  if (!(sea || F.clouds_ok) || !F.atmosphere || RS.background_mode != 0 || !prog_sky) return;
  if (F.slot < 0 || F.slot >= SLOT_COUNT) return;
  EnvShot &E = g_env[F.slot];
  SkyUpload u = sky_upload_for_view(F);
  // Shot from the sea under the eye, not from the eye: the water sees the
  // clouds from below whatever height the camera is at, and a cloud layer
  // is at a finite height, so the point it is seen from moves it about the
  // sky.
  const float level = RS.water_level * RS.height_scale;
  float at[3] = {F.view_eye[0], level, F.view_eye[2]};
  const gpx::planet::Shape S = world_shape(RS);
  if (u.world_r > 0.f && u.world_r <= 1.0e5f && !gpx::planet::shape_is_flat(S)) {
    float up[3];
    gpx::planet::world_up(F.view_eye, u.world_r, S, up);
    const float alt = gpx::planet::world_alt(F.view_eye, u.world_r, S) - level;
    for (int k = 0; k < 3; ++k) at[k] = F.view_eye[k] - up[k] * alt;
  }
  // Still good while it is recent and shot from nearly here: a move shifts
  // the clouds overhead by about its length over their height, and under a
  // quarter texel (0.2 degrees) the sea cannot show it.
  const double now = ImGui::GetTime();
  const float moved = dist3(at, E.at);
  const float still = 0.003f * std::max(RS.cloud_altitude, 0.05f);
  if (E.valid && now - E.when >= 0.0 && now - E.when < ENV_REFRESH_S && moved < still) {
    g_env_read = F.slot;
    return;
  }
  if (!E.tex) {
    glGenTextures(1, &E.tex);
    glBindTexture(GL_TEXTURE_2D, E.tex);
    glTexStorage2D(GL_TEXTURE_2D, 10, GL_RGBA16F, ENV_W, ENV_H);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // round the horizon
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &E.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, E.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, E.tex, 0);
  }
  GLint prev_fbo = 0, vp[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  const bool depth_test = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
  glBindFramebuffer(GL_FRAMEBUFFER, E.fbo);
  glViewport(0, 0, ENV_W, ENV_H);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glUseProgram(prog_sky);
  u.eye = at;
  u.space = 0.f; // the sea is under the air
  u.steps = std::min(u.steps, 32);
  // The view's own pixel for the level of detail, not the panorama's
  // coarser one: far from the eye the march trades the weather's push for
  // the volume's mean by footprint, which changes the clouds' shapes, and
  // the clouds in the water must be the clouds in the sky. The panorama's
  // mipmaps do its filtering.
  if (u.pixel_k <= 0.f) u.pixel_k = 0.001f;
  u.panorama = 1;
  u.hdr = 1;
  u.no_sun = 1; // the sea draws its own glint of the sun
  u.aov = 0;
  upload_sky_uniforms(prog_sky, RS, u);
  glBindVertexArray(vao_quad);
  if (pass_timed(F.slot)) {
    GpuTimer::Scope s(gpu_timer("sky reflection"));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  } else {
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glBindTexture(GL_TEXTURE_2D, E.tex);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glDepthMask(GL_TRUE);
  if (depth_test) glEnable(GL_DEPTH_TEST);
  E.valid = true;
  E.when = now;
  for (int k = 0; k < 3; ++k) E.at[k] = at[k];
  g_env_read = F.slot;
}

unsigned sky_env_texture() {
  return g_env_read >= 0 && g_env[g_env_read].valid ? g_env[g_env_read].tex : 0;
}

// Unit 14: the terrain program holds the placement weight on 12 and the dome
// on 13, and the sea's old unit for this (12) was taken there.
void bind_sky_env(GLuint prog) {
  const unsigned env = sky_env_texture();
  glActiveTexture(GL_TEXTURE14);
  glBindTexture(GL_TEXTURE_2D, env);
  unii(prog, "u_sky_env", 14);
  unii(prog, "u_sky_env_on", env ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
}

} // namespace studio
