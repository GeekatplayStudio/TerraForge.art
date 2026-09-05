#include "uniform_cache.hpp"
// Geekatplay TerraForge - the optical pass: what the lens does to the picture
// after the scene has been drawn.
//
// Everything here is a screen-space warp or a falloff, which is why it cannot
// live in the surface shaders: distortion moves a pixel, and a fragment
// shader drawing a mountain has no way to move itself somewhere else. So the
// view is drawn into its own target as before, and this pass reads that
// target and writes the picture the camera would actually have made.
//
// It costs nothing when the camera's optical simulation is off: the pass is
// skipped and the scene target is handed straight to the panel, exactly as it
// was before this file existed.
#include "renderer_internal.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace studio {

namespace {

GLuint post_fbo[SLOT_COUNT] = {0}, post_tex[SLOT_COUNT] = {0};
int post_w[SLOT_COUNT] = {0}, post_h[SLOT_COUNT] = {0};
GLuint prog_post = 0;

const char *POST_VS = R"(#version 430 core
out vec2 uv;
void main(){
  // one oversized triangle: no vertex buffer, no seam down the middle
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

const char *POST_FS = R"(#version 430 core
in vec2 uv;
out vec4 frag;
uniform sampler2D u_src;
uniform vec2  u_texel;       // 1 / resolution
uniform float u_k1;          // radial distortion; + barrel, - pincushion
uniform float u_vignette;    // 0 none .. 1 strong
uniform float u_chromatic;   // lateral fringing, in pixels at the corner
uniform float u_flare;       // 0 none .. 1 strong
uniform vec2  u_sun;         // sun in screen space, or (-1,-1) when behind
uniform vec2  u_blur;        // camera motion, in uv units, 0 when still
uniform float u_aspect;

// The inverse of r' = r(1 + k1 r^2): for the pixel we are writing, where in
// the source image did that light land? Solved by two Newton steps, which is
// exact enough at these magnitudes and costs nothing next to a texture fetch.
float solve_r(float rd, float k) {
  float r = rd;
  for (int i = 0; i < 3; ++i) {
    float f = r * (1.0 + k * r * r) - rd;
    float df = 1.0 + 3.0 * k * r * r;
    r -= f / max(df, 1e-4);
  }
  return r;
}

vec2 undistort(vec2 c, float k) {
  if (abs(k) < 1e-5) return c;
  vec2 q = c * vec2(u_aspect, 1.0);
  float rd = length(q);
  if (rd < 1e-6) return c;
  float r = solve_r(rd, k);
  // A pincushion lens reads past the corner of the source, which would draw
  // a black border around the picture. A real camera does not show that: the
  // sensor crops what the lens delivers. So does this - the whole frame is
  // scaled by exactly what the corner needs and nothing else changes.
  float rc = length(vec2(0.5 * u_aspect, 0.5));
  float fit = min(1.0, rc / max(solve_r(rc, k), 1e-6));
  return q * (r / rd) * fit / vec2(u_aspect, 1.0);
}

vec3 sample_rgb(vec2 c) {
  // Chromatic aberration is lateral: the three channels focus at slightly
  // different scales, so they are read at slightly different radii rather
  // than at an offset, which is what makes fringes grow toward the corners
  // and vanish in the middle.
  if (u_chromatic <= 0.0) return texture(u_src, c + 0.5).rgb;
  float s = u_chromatic * 0.002;
  return vec3(texture(u_src, c * (1.0 - s) + 0.5).r,
              texture(u_src, c + 0.5).g,
              texture(u_src, c * (1.0 + s) + 0.5).b);
}

void main(){
  vec2 c = uv - 0.5;                 // centred, -0.5 .. 0.5
  vec2 src = undistort(c, u_k1);
  // Outside the frame after the warp: black, as a real lens circle would be.
  vec2 t = src + 0.5;
  vec3 col;
  if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0)
    col = vec3(0.0);
  else
    col = sample_rgb(src);

  // Camera-motion blur: a short walk along what the camera did this frame.
  if (u_blur.x != 0.0 || u_blur.y != 0.0) {
    vec3 sum = col;
    float n = 1.0;
    for (int i = 1; i <= 6; ++i) {
      vec2 o = u_blur * (float(i) / 6.0);
      vec2 p = src - o;
      if (p.x > -0.5 && p.x < 0.5 && p.y > -0.5 && p.y < 0.5) {
        sum += sample_rgb(p);
        n += 1.0;
      }
    }
    col = sum / n;
  }

  // Lens flare: ghosts along the line from the sun through the centre, plus
  // a halo. Only when the sun is actually in frame - a flare from a sun
  // behind the camera is the thing that gives cheap flares away.
  if (u_flare > 0.0 && u_sun.x >= 0.0) {
    vec2 s = u_sun - 0.5;
    vec2 dir = -s;
    vec3 ghosts = vec3(0.0);
    for (int i = 1; i <= 5; ++i) {
      vec2 g = s + dir * (float(i) * 0.4);
      float d = length((c - g) * vec2(u_aspect, 1.0));
      // Small and tight. The first version used a fifth of the frame per
      // ghost and a halo half as wide as the picture, which on a long lens
      // washed the whole frame white instead of reading as a flare.
      float w = smoothstep(0.075, 0.0, d);
      // each ghost a different tint, as the coatings make them
      vec3 tint = vec3(1.0 - 0.12 * float(i), 0.92, 0.78 + 0.05 * float(i));
      ghosts += tint * w * (0.30 / float(i));
    }
    float hd = length((c - s) * vec2(u_aspect, 1.0));
    float halo = smoothstep(0.16, 0.0, hd) * 0.5 + smoothstep(0.5, 0.1, hd) * 0.06;
    vec3 add = (ghosts + vec3(1.0, 0.95, 0.85) * halo) * u_flare;
    // A flare adds light; it does not replace the picture. The cap is what
    // stops a bright sky plus a flare from going flat white.
    col += min(add, vec3(0.75));
  }

  // Vignette last: it darkens everything the lens produced, flare included.
  if (u_vignette > 0.0) {
    float r = length(c * vec2(u_aspect, 1.0)) * 1.41421;
    float v = 1.0 - u_vignette * smoothstep(0.35, 1.15, r);
    col *= max(v, 0.0);
  }
  frag = vec4(col, 1.0);
})";

void ensure_post_target(int slot, int w, int h) {
  if (post_fbo[slot] && post_w[slot] == w && post_h[slot] == h) return;
  if (!post_fbo[slot]) glGenFramebuffers(1, &post_fbo[slot]);
  if (!post_tex[slot]) glGenTextures(1, &post_tex[slot]);
  glBindTexture(GL_TEXTURE_2D, post_tex[slot]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, post_fbo[slot]);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         post_tex[slot], 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  post_w[slot] = w;
  post_h[slot] = h;
}

} // namespace

bool optics_active(const LensOptics &o) {
  return o.on && (std::fabs(o.k1) > 1e-4f || o.vignette > 0.001f ||
                  o.chromatic > 0.001f || o.flare > 0.001f ||
                  o.blur[0] != 0.f || o.blur[1] != 0.f);
}

unsigned renderer_post_process(int slot, int w, int h, const LensOptics &o) {
  if (slot < 0 || slot >= SLOT_COUNT || !optics_active(o))
    return fbo_color[slot];
  if (!prog_post) {
    prog_post = link_prog(POST_VS, POST_FS);
    if (!prog_post) return fbo_color[slot];
  }
  ensure_post_target(slot, w, h);
  glBindFramebuffer(GL_FRAMEBUFFER, post_fbo[slot]);
  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glUseProgram(prog_post);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, fbo_color[slot]);
  unii(prog_post, "u_src", 0);
  glUniform2f(uniform_location(prog_post, "u_texel"), 1.f / (float)w,
              1.f / (float)h);
  uni1(prog_post, "u_k1", o.k1);
  uni1(prog_post, "u_vignette", o.vignette);
  uni1(prog_post, "u_chromatic", o.chromatic);
  uni1(prog_post, "u_flare", o.flare);
  glUniform2f(uniform_location(prog_post, "u_sun"), o.sun[0], o.sun[1]);
  glUniform2f(uniform_location(prog_post, "u_blur"), o.blur[0], o.blur[1]);
  uni1(prog_post, "u_aspect", h > 0 ? (float)w / (float)h : 1.f);
  glBindVertexArray(vao_quad);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return post_tex[slot];
}

} // namespace studio
