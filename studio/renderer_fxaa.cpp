#include "uniform_cache.hpp"
// Geekatplay TerraForge — anti-aliasing for the finished view.
//
// The viewport draws into a plain single-sample target: no multisampling, no
// temporal filter. Every silhouette - a ridge against the sky, a crater's rim,
// a shoreline, a tree - came out as a staircase of whole pixels, and a
// staircase crawls as the camera moves. FXAA (Lottes) finds the edges from
// the picture's own contrast and blends across them along the edge's
// direction: one pass over the finished image, a fraction of a millisecond,
// and it needs nothing from the passes that drew the picture.
//
// Only on the picture: a render pass (an AOV) holds numbers - depths,
// normals, object ids - that must not be blended with their neighbours.
#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"
#include "console.hpp"
#include "gpu_timer.hpp"
#include <algorithm>

namespace studio {

namespace {

const char *const FS_FXAA = R"GLSL(#version 430 core
in vec2 v_ndc;
out vec4 frag;
uniform sampler2D u_src;
uniform vec2 u_texel;
float luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }
void main(){
  const float SPAN_MAX = 8.0;
  const float REDUCE_MUL = 1.0 / 8.0;
  const float REDUCE_MIN = 1.0 / 128.0;
  vec2 uv = gl_FragCoord.xy * u_texel;
  vec3 rgbM = texture(u_src, uv).rgb;
  // north is +y: the target's rows run up (the diagonal edges blend along
  // themselves only with the corners named the way the texture lies)
  vec3 rgbNW = texture(u_src, uv + vec2(-1.0,  1.0) * u_texel).rgb;
  vec3 rgbNE = texture(u_src, uv + vec2( 1.0,  1.0) * u_texel).rgb;
  vec3 rgbSW = texture(u_src, uv + vec2(-1.0, -1.0) * u_texel).rgb;
  vec3 rgbSE = texture(u_src, uv + vec2( 1.0, -1.0) * u_texel).rgb;
  float lM = luma(rgbM), lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE);
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  // no edge worth the name: the pixel as it is
  if (lMax - lMin < max(0.0312, lMax * 0.125)){ frag = vec4(rgbM, 1.0); return; }
  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
  float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * REDUCE_MUL, REDUCE_MIN);
  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcp, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * u_texel;
  vec3 a = 0.5 * (texture(u_src, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                  texture(u_src, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
  vec3 b = a * 0.5 + 0.25 * (texture(u_src, uv - dir * 0.5).rgb +
                             texture(u_src, uv + dir * 0.5).rgb);
  float lB = luma(b);
  frag = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
)GLSL";

GLuint g_prog = 0;
bool g_failed = false;
GLuint g_src[SLOT_COUNT] = {};
int g_src_w[SLOT_COUNT] = {}, g_src_h[SLOT_COUNT] = {};

} // namespace

void renderer_fxaa(int slot, int w, int h) {
  if (slot < 0 || slot >= SLOT_COUNT || !fbo[slot] || g_failed) return;
  if (!g_prog) {
    std::string err;
    g_prog = link_checked(VS_SKY, FS_FXAA, err);
    if (!g_prog) {
      g_failed = true;
      log_error("shader", "anti-aliasing: " + err);
      return;
    }
  }
  // the picture as it stands, into a texture the pass reads while it writes
  // the target
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[slot]);
  GLint type = GL_UNSIGNED_NORMALIZED;
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &type);
  if (type == GL_FLOAT) return; // a pass target: numbers, not a picture
  glActiveTexture(GL_TEXTURE0);
  if (!g_src[slot] || g_src_w[slot] != w || g_src_h[slot] != h) {
    if (!g_src[slot]) glGenTextures(1, &g_src[slot]);
    glBindTexture(GL_TEXTURE_2D, g_src[slot]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_src_w[slot] = w;
    g_src_h[slot] = h;
  }
  glBindTexture(GL_TEXTURE_2D, g_src[slot]);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
  glViewport(0, 0, w, h);
  const bool depth_test = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
  const bool blend = glIsEnabled(GL_BLEND) == GL_TRUE;
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDepthMask(GL_FALSE);
  glUseProgram(g_prog);
  unii(g_prog, "u_src", 0);
  glUniform2f(uniform_location(g_prog, "u_texel"), 1.f / float(std::max(w, 1)),
              1.f / float(std::max(h, 1)));
  glBindVertexArray(vao_quad);
  if (pass_timed(slot)) {
    GpuTimer::Scope s(gpu_timer("anti-aliasing"));
    glDrawArrays(GL_TRIANGLES, 0, 3);
  } else {
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDepthMask(GL_TRUE);
  if (depth_test) glEnable(GL_DEPTH_TEST);
  if (blend) glEnable(GL_BLEND);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void renderer_fxaa_shutdown() {
  if (g_prog) delete_program(g_prog);
  g_prog = 0;
  for (int s = 0; s < SLOT_COUNT; ++s) {
    if (g_src[s]) glDeleteTextures(1, &g_src[s]);
    g_src[s] = 0;
    g_src_w[s] = g_src_h[s] = 0;
  }
}

} // namespace studio
