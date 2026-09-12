#include "uniform_cache.hpp"
// Geekatplay TerraForge — bloom: the light a lens and a sensor spread round
// everything bright.
//
// No glass is perfectly clear and no sensor well is perfectly walled, so a
// bright light leaks into the picture round it: the sun, a glint on water,
// snow in sunlight, the brightest stars. The finished picture is taken down a
// chain of half-size copies - the first keeping only what is brighter than
// the threshold, each after it blurred by the 13-tap filter that does not
// shimmer as bright points cross texel boundaries - and back up again, each
// level laid over the one above it through a tent filter. What comes out is
// a glow as wide as the deepest level, bright where the light was, and the
// optical pass (renderer_post.cpp) screens it over the picture.
//
// The picture it reads is already developed for the display, so a light that
// was far brighter than white arrives clipped at white. That is what decides
// where the glow goes - the sun and the glints are the clipped parts - and a
// threshold in the picture's own terms is what a user can judge by eye.
#include "renderer_internal.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

namespace {

constexpr int MAX_LEVELS = 7; // half size down to 1/128

struct BloomChain {
  GLuint fbo[MAX_LEVELS] = {}, tex[MAX_LEVELS] = {};
  int w[MAX_LEVELS] = {}, h[MAX_LEVELS] = {};
  int base_w = 0, base_h = 0;
};
BloomChain g_chain[SLOT_COUNT];
GLuint g_down = 0, g_up = 0;

const char *const BLOOM_VS = R"(#version 430 core
out vec2 uv;
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// Down one level. The 13 taps are four overlapping 2x2 boxes and a centre
// box, weighted so a bright point moving across the source contributes the
// same total wherever it lands - a plain 2x2 average makes it pulse.
const char *const BLOOM_DOWN_FS = R"(#version 430 core
in vec2 uv;
out vec4 frag;
uniform sampler2D u_src;
uniform vec2 u_texel;       // the source's texel
uniform int u_prefilter;    // 1: the first level, from the finished picture
uniform float u_threshold;  // 0..1 of the picture's white
vec3 tap(vec2 o){ return texture(u_src, uv + o * u_texel).rgb; }
void main(){
  vec3 c = tap(vec2(0.0)) * 0.125
         + (tap(vec2(-1.0, 1.0)) + tap(vec2(1.0, 1.0)) + tap(vec2(-1.0, -1.0)) + tap(vec2(1.0, -1.0))) * 0.125
         + (tap(vec2(0.0, 2.0)) + tap(vec2(-2.0, 0.0)) + tap(vec2(2.0, 0.0)) + tap(vec2(0.0, -2.0))) * 0.0625
         + (tap(vec2(-2.0, 2.0)) + tap(vec2(2.0, 2.0)) + tap(vec2(-2.0, -2.0)) + tap(vec2(2.0, -2.0))) * 0.03125;
  if (u_prefilter == 1){
    // Only the part over the threshold - judged on the picture as it is seen,
    // which is what the dial is set by - with a soft knee so the glow does
    // not switch on at a hard edge of brightness; then back to light.
    c = clamp(c, 0.0, 1.0);
    float t = clamp(u_threshold, 0.0, 0.99);
    float knee = max((1.0 - t) * 0.5, 1e-3);
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - t + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    c = pow(c, vec3(2.2)) * (max(soft, br - t) / max(br, 1e-4));
  }
  frag = vec4(c, 1.0);
})";

// Up one level: a 3x3 tent over the smaller level, added to the larger.
const char *const BLOOM_UP_FS = R"(#version 430 core
in vec2 uv;
out vec4 frag;
uniform sampler2D u_src;
uniform vec2 u_texel;   // the source's texel
uniform float u_radius; // in source texels
void main(){
  vec2 o = u_texel * u_radius;
  vec3 s = texture(u_src, uv + vec2(-o.x, o.y)).rgb + texture(u_src, uv + vec2(0.0, o.y)).rgb * 2.0
         + texture(u_src, uv + o).rgb
         + texture(u_src, uv + vec2(-o.x, 0.0)).rgb * 2.0 + texture(u_src, uv).rgb * 4.0
         + texture(u_src, uv + vec2(o.x, 0.0)).rgb * 2.0
         + texture(u_src, uv - o).rgb + texture(u_src, uv + vec2(0.0, -o.y)).rgb * 2.0
         + texture(u_src, uv + vec2(o.x, -o.y)).rgb;
  frag = vec4(s / 16.0, 1.0);
})";

bool ensure_chain(int slot, int w, int h, int levels) {
  BloomChain &C = g_chain[slot];
  if (C.base_w == w && C.base_h == h && C.fbo[levels - 1]) return true;
  int lw = w, lh = h;
  for (int i = 0; i < MAX_LEVELS; ++i) {
    lw = std::max(lw / 2, 1);
    lh = std::max(lh / 2, 1);
    if (i >= levels && !C.tex[i]) continue;
    if (!C.tex[i]) glGenTextures(1, &C.tex[i]);
    if (!C.fbo[i]) glGenFramebuffers(1, &C.fbo[i]);
    glBindTexture(GL_TEXTURE_2D, C.tex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, lw, lh, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, C.fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, C.tex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
    }
    C.w[i] = lw;
    C.h[i] = lh;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  C.base_w = w;
  C.base_h = h;
  return true;
}

} // namespace

unsigned renderer_bloom(int slot, int w, int h, unsigned src, float threshold,
                        float size, int *levels_out) {
  if (slot < 0 || slot >= SLOT_COUNT || w < 8 || h < 8 || !src) return 0;
  if (!g_down) g_down = link_prog(BLOOM_VS, BLOOM_DOWN_FS);
  if (!g_up) g_up = link_prog(BLOOM_VS, BLOOM_UP_FS);
  if (!g_down || !g_up) return 0;
  // how far it spreads: three levels (an eighth of the picture) to seven, and
  // never down to a level smaller than a few texels
  int levels = 3 + (int)std::lround(std::clamp(size, 0.f, 1.f) * 4.f);
  while (levels > 1 && (std::min(w, h) >> levels) < 4) --levels;
  if (!ensure_chain(slot, w, h, levels)) return 0;
  BloomChain &C = g_chain[slot];
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(vao_quad);
  glActiveTexture(GL_TEXTURE0);
  // down
  glUseProgram(g_down);
  unii(g_down, "u_src", 0);
  uni1(g_down, "u_threshold", threshold);
  for (int i = 0; i < levels; ++i) {
    const int sw = i == 0 ? w : C.w[i - 1], sh = i == 0 ? h : C.h[i - 1];
    glBindFramebuffer(GL_FRAMEBUFFER, C.fbo[i]);
    glViewport(0, 0, C.w[i], C.h[i]);
    glBindTexture(GL_TEXTURE_2D, i == 0 ? src : C.tex[i - 1]);
    glUniform2f(uniform_location(g_down, "u_texel"), 1.f / (float)sw, 1.f / (float)sh);
    unii(g_down, "u_prefilter", i == 0 ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  // and up, each level added over the one above it
  glUseProgram(g_up);
  unii(g_up, "u_src", 0);
  uni1(g_up, "u_radius", 1.f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  for (int i = levels - 1; i > 0; --i) {
    glBindFramebuffer(GL_FRAMEBUFFER, C.fbo[i - 1]);
    glViewport(0, 0, C.w[i - 1], C.h[i - 1]);
    glBindTexture(GL_TEXTURE_2D, C.tex[i]);
    glUniform2f(uniform_location(g_up, "u_texel"), 1.f / (float)C.w[i], 1.f / (float)C.h[i]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (levels_out) *levels_out = levels;
  return C.tex[0];
}

void renderer_bloom_shutdown() {
  for (BloomChain &C : g_chain) {
    for (int i = 0; i < MAX_LEVELS; ++i) {
      if (C.fbo[i]) glDeleteFramebuffers(1, &C.fbo[i]);
      if (C.tex[i]) glDeleteTextures(1, &C.tex[i]);
    }
    C = BloomChain();
  }
  if (g_down) delete_program(g_down);
  if (g_up) delete_program(g_up);
  g_down = g_up = 0;
}

} // namespace studio
