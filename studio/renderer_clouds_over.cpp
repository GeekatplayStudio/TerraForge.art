#include "uniform_cache.hpp"
// Geekatplay TerraForge — the clouds over whatever the scene drew in front of
// the sky: the ground, a mesh, the sea.
//
// After the ground and the sea are drawn, every ray that ended on something
// is marched again up to where it ended, so a cloud stands in front of a
// mountain, lies under a camera above it and wraps the world seen from orbit.
// Seen from over the layer, that is every pixel on screen: 9.8 ms at
// 1718x798 looking down on the tile. The views the user works in therefore
// march a half-size picture and bring it up to full size by depth - each
// full pixel takes the half-size texels that were cut near its own distance -
// and a capture or a render pass marches every pixel.
#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"
#include "console.hpp"
#include "gpu_timer.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>

namespace studio {

namespace {

// What the three programs share. The picture a view holds is developed for
// the display (u_linear_target 0), so what is behind the clouds is taken back
// to light first; the clouds' light and the air in front of the nearest of
// them are then laid over it, and the sum developed again.
const char *const OVER_COMMON = R"GLSL(#version 430 core
in vec2 v_ndc;
uniform mat4 u_inv_vp;
uniform vec3 u_cam, u_cam_fwd, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_atmo, u_sun_intensity, u_hscale;
uniform sampler2D u_scene_depth, u_scene_color;
uniform int u_linear_target;
uniform vec2 u_viewport; // the full picture, in pixels
uniform float u_depth_near, u_depth_far;
uniform vec3 u_grade;
uniform float u_sat;
const float PI = 3.14159265;
PL_SPHERE_PLACEHOLDER
SKY_WORLD_PLACEHOLDER
SKY_FN_PLACEHOLDER
FOG_FN_PLACEHOLDER
CLOUD_SHAPE_PLACEHOLDER
CLOUD_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
// what the view's own picture holds, back to light (the water's inverse:
// the display encoding, the curve, the saturation and the grade undone, the
// exposure divided out)
vec3 scene_light(vec2 uv){
  vec3 c = texture(u_scene_color, uv).rgb;
  if (u_linear_target == 1) return c;
  vec3 y = pow(clamp(c, 0.0, 0.999), vec3(2.2));
  vec3 a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, k = 0.14 * y;
  vec3 x = (-b - sqrt(max(b * b - 4.0 * a * k, vec3(0.0)))) / (2.0 * a);
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = lum + (x - lum) / max(u_sat, 1e-3);
  return max(x / max(u_grade, vec3(1e-3)), vec3(0.0)) / max(u_exposure, 1e-6);
}
// the scene's distance along the view's axis, from its depth-buffer value
float lin_depth(float zb){
  return 2.0 * u_depth_near * u_depth_far /
         (u_depth_far + u_depth_near - (2.0 * zb - 1.0) * (u_depth_far - u_depth_near));
}
// the ray through a point of the full picture, from the eye
vec3 pixel_dir(vec2 px){
  vec4 w = u_inv_vp * vec4(px / u_viewport * 2.0 - 1.0, 1.0, 1.0);
  return normalize(w.xyz / w.w - u_cam);
}
// The clouds in front of the scene along `dir`, cut where the scene stands
// (lz along the view's axis): rgb the light they add, with the air in front
// of the nearest of them laid on it, and a how much of the scene shows
// through. The picture is scene * a + rgb: the march is linear in what is
// behind it, so marching it over black gives exactly the part it adds.
vec4 clouds_over(vec3 dir, float lz){
  // One step of the 24-bit depth is lz*lz/(near*2^24) long. Seen from orbit
  // that is longer than the cloud base is high, and a cut that falls short
  // takes the bottom off the layer; running on by the step costs nothing,
  // there is no cloud under the ground to find.
  float dz = lz * lz / (max(u_depth_near, 1e-9) * 16777216.0);
  float t_scene = (lz + 1.5 * dz) / max(dot(dir, u_cam_fwd), 1e-4);
  g_sky_up = world_up(u_cam); // the sky pass's up, so the clouds are lit alike
  vec4 c = march_clouds(u_cam, dir, vec3(0.0), t_scene);
  float front = min(g_cl_front, t_scene);
  float fog_f; vec3 fog_c;
  fog_terms(u_cam + dir * front, u_cam, front, u_hscale, u_sun, u_sun_color, fog_f, fog_c);
  return vec4(apply_fog_terms(c.rgb, fog_f, fog_c) - fog_c * fog_f * c.a, c.a);
}
vec4 develop(vec3 col){
  if (u_aov != 0) return vec4(col, 1.0);
  return vec4(pow(aces(col * u_exposure), vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

// Every pixel marched: captures and render passes.
const char *const OVER_FULL = R"GLSL(
out vec4 frag;
void main(){
  vec2 uv = gl_FragCoord.xy / u_viewport;
  float zb = texture(u_scene_depth, uv).r;
  if (zb >= 1.0) discard; // the sky: its clouds were marched with it
  vec4 c = clouds_over(pixel_dir(gl_FragCoord.xy), lin_depth(zb));
  if (c.a > 0.998) discard; // no cloud in front: leave the pixel exactly as it is
  frag = develop(scene_light(uv) * c.a + c.rgb);
}
)GLSL";

// The half-size march. Each texel stands for a 2x2 block of the picture: on
// one square of a checkerboard it is cut at the nearest surface in the block,
// on the other at the farthest, so every full pixel has a neighbour cut at its
// own depth - the thin branch against far ground as much as the ground beside
// it. The block's sky pixels are the sky pass's and are left out.
const char *const OVER_HALF = R"GLSL(
layout(location = 0) out vec4 frag;    // the clouds: rgb added, a let through
layout(location = 1) out float frag_z; // the distance they were cut at; -1 all sky
void main(){
  ivec2 b = ivec2(gl_FragCoord.xy) * 2;
  ivec2 lim = textureSize(u_scene_depth, 0) - 1;
  bool far_sq = ((int(gl_FragCoord.x) + int(gl_FragCoord.y)) & 1) == 0;
  float pick = far_sq ? -1.0 : 2.0;
  ivec2 at = b;
  for (int k = 0; k < 4; ++k){
    ivec2 p = min(b + ivec2(k & 1, k >> 1), lim);
    float z = texelFetch(u_scene_depth, p, 0).r;
    if (z >= 1.0) continue;
    if (far_sq ? z > pick : z < pick){ pick = z; at = p; }
  }
  if (pick < 0.0 || pick > 1.0){ frag = vec4(0.0, 0.0, 0.0, 1.0); frag_z = -1.0; return; }
  float lz = lin_depth(pick);
  frag = clouds_over(pixel_dir(vec2(at) + 0.5), lz);
  frag_z = lz;
}
)GLSL";

// Up to full size: the four half-size texels round the pixel, weighted as a
// bilinear filter weights them and again by how close to this pixel's own
// distance each was cut. A pixel none of them was cut near - a sliver
// thinner than the checkerboard - is marched on its own.
const char *const OVER_UP = R"GLSL(
out vec4 frag;
uniform sampler2D u_half, u_half_z;
void main(){
  ivec2 px = ivec2(gl_FragCoord.xy);
  float zb = texelFetch(u_scene_depth, px, 0).r;
  if (zb >= 1.0) discard;
  float lz = lin_depth(zb);
  vec2 q = gl_FragCoord.xy * 0.5 - 0.5;
  ivec2 b = ivec2(floor(q));
  vec2 f = q - vec2(b);
  ivec2 lim = textureSize(u_half, 0) - 1;
  vec4 sum = vec4(0.0);
  float wsum = 0.0, near_rel = 1.0e9;
  vec4 nearest = vec4(0.0, 0.0, 0.0, 1.0);
  for (int k = 0; k < 4; ++k){
    ivec2 o = ivec2(k & 1, k >> 1);
    ivec2 t = clamp(b + o, ivec2(0), lim);
    float z = texelFetch(u_half_z, t, 0).r;
    if (z < 0.0) continue;
    vec4 c = texelFetch(u_half, t, 0);
    float rel = abs(z - lz) / max(lz, 1e-9);
    if (rel < near_rel){ near_rel = rel; nearest = c; }
    float bil = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
    float r = rel * 50.0; // half weight two per cent of the distance away
    float w = bil / (1.0 + r * r);
    sum += c * w;
    wsum += w;
  }
  vec4 c;
  if (near_rel > 0.25) c = clouds_over(pixel_dir(gl_FragCoord.xy), lz);
  else c = wsum > 1.0e-6 ? sum / wsum : nearest;
  if (c.a > 0.998) discard;
  frag = develop(scene_light(gl_FragCoord.xy / u_viewport) * c.a + c.rgb);
}
)GLSL";

GLuint g_prog_full = 0, g_prog_half = 0, g_prog_up = 0;

// the half-size picture per view slot, made on first use
struct HalfTarget {
  GLuint fbo = 0, col = 0, z = 0;
  int w = 0, h = 0;
};
HalfTarget g_half[SLOT_COUNT];

GLuint link_over(const char *main_src, const char *what) {
  const std::string src = std::string(OVER_COMMON) + main_src;
  std::string err;
  GLuint p = link_checked(VS_SKY, inject_sky(src.c_str()), err);
  if (!p) log_error("shader", std::string("clouds over the ground (") + what + "): " + err);
  return p;
}

bool ensure_half(int slot, int w, int h) {
  HalfTarget &T = g_half[slot];
  if (T.fbo && T.w == w && T.h == h) return true;
  if (!T.fbo) glGenFramebuffers(1, &T.fbo);
  if (!T.col) glGenTextures(1, &T.col);
  if (!T.z) glGenTextures(1, &T.z);
  auto make = [&](GLuint tex, GLenum fmt, GLenum layout) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, layout, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  glActiveTexture(GL_TEXTURE15);
  make(T.col, GL_RGBA16F, GL_RGBA);
  make(T.z, GL_R32F, GL_RED);
  glActiveTexture(GL_TEXTURE0);
  GLint prev = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
  glBindFramebuffer(GL_FRAMEBUFFER, T.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, T.col, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, T.z, 0);
  const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, bufs);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
  T.w = w;
  T.h = h;
  return ok;
}

// the uniforms all three read: the sky's, the air's, the depth's range
void upload_over(GLuint P, const FrameCtx &F) {
  const RenderSettings &RS = F.RS;
  glUseProgram(P);
  glUniformMatrix4fv(uniform_location(P, "u_inv_vp"), 1, GL_FALSE, F.inv_vp);
  upload_sky_uniforms(P, RS, sky_upload_for_view(F));
  upload_fog_uniforms(P, RS, F.atmosphere);
  uni1(P, "u_hscale", RS.height_scale);
  // where the depth buffer's numbers lie: the near and far planes along the
  // view's axis, measured the way the water pass measures them
  float pn[3], pf[3];
  transform_point(F.inv_vp, 0.f, 0.f, -1.f, pn);
  transform_point(F.inv_vp, 0.f, 0.f, 1.f, pf);
  const float far = dist3(pf, F.view_eye);
  const float fwd[3] = {(pf[0] - F.view_eye[0]) / std::max(far, 1e-12f),
                        (pf[1] - F.view_eye[1]) / std::max(far, 1e-12f),
                        (pf[2] - F.view_eye[2]) / std::max(far, 1e-12f)};
  uni3(P, "u_cam_fwd", fwd);
  uni1(P, "u_depth_near", dist3(pn, F.view_eye));
  uni1(P, "u_depth_far", far);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, scene_depth_copy(F.slot));
  unii(P, "u_scene_depth", 6);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, scene_color_copy(F.slot));
  unii(P, "u_scene_color", 7);
  glActiveTexture(GL_TEXTURE0);
  unii(P, "u_linear_target", g_aov == AOV_BEAUTY_LINEAR ? 1 : 0);
  glUniform2f(uniform_location(P, "u_viewport"), (float)F.w, (float)F.h);
}

} // namespace

void clouds_over_init() {
  g_prog_full = link_over(OVER_FULL, "every pixel");
  g_prog_half = link_over(OVER_HALF, "half size");
  g_prog_up = link_over(OVER_UP, "brought up");
}

void clouds_over_shutdown() {
  for (GLuint *p : {&g_prog_full, &g_prog_half, &g_prog_up}) {
    if (*p) delete_program(*p);
    *p = 0;
  }
  for (HalfTarget &T : g_half) {
    if (T.fbo) glDeleteFramebuffers(1, &T.fbo);
    if (T.col) glDeleteTextures(1, &T.col);
    if (T.z) glDeleteTextures(1, &T.z);
    T = HalfTarget();
  }
}

void pass_clouds(const FrameCtx &F) {
  const RenderSettings &RS = F.RS;
  if (!g_prog_full || !F.clouds_ok || !F.atmosphere || RS.background_mode != 0) return;
  // an orthographic view has no eye for a cloud to stand in front of
  if (F.vc.camera != 0) return;
  // the picture and the linear beauty; a geometry pass wants the surfaces
  if (!(g_aov == 0 || g_aov == AOV_BEAUTY_LINEAR)) return;
  copy_scene_depth(F.slot, F.w, F.h);
  copy_scene_color(F.slot, F.w, F.h);
  const bool depth_test = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND); // the shaders composite what is behind themselves
  glBindVertexArray(vao_quad);
  // A capture and a render pass are the finished picture: every pixel.
  // GPX_CLOUDS_HALF=0 marches every pixel everywhere, =2 halves the captures
  // too - which is how the two are compared, since a script can only look at
  // captures.
  static const int mode = [] {
    const char *e = std::getenv("GPX_CLOUDS_HALF");
    return e && *e ? std::atoi(e) : 1;
  }();
  const bool finished = F.slot == SLOT_CAPTURE || F.slot == SLOT_AOV;
  const bool half = (mode == 2 || (mode == 1 && !finished)) && g_prog_half && g_prog_up &&
                    F.w >= 16 && F.h >= 16;
  const int hw = (F.w + 1) / 2, hh = (F.h + 1) / 2;
  auto draw = [&]() {
    if (!half || !ensure_half(F.slot, hw, hh)) {
      upload_over(g_prog_full, F);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      return;
    }
    GLint scene_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &scene_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_half[F.slot].fbo);
    glViewport(0, 0, hw, hh);
    upload_over(g_prog_half, F);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)scene_fbo);
    glViewport(0, 0, F.w, F.h);
    upload_over(g_prog_up, F);
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, g_half[F.slot].col);
    unii(g_prog_up, "u_half", 15);
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, g_half[F.slot].z);
    unii(g_prog_up, "u_half_z", 12);
    glActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  };
  if (pass_timed(F.slot)) {
    GpuTimer::Scope s(gpu_timer("clouds over ground"));
    draw();
  } else {
    draw();
  }
  glActiveTexture(GL_TEXTURE0);
  glDepthMask(GL_TRUE);
  if (depth_test) glEnable(GL_DEPTH_TEST);
}

} // namespace studio
