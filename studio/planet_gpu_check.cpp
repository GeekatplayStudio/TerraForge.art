// Geekatplay TerraForge - CPU/GPU agreement check for the planet maths.
//
// gpx/planet_math.hpp and the PL_FN GLSL in planet_shaders.cpp are written
// twice by hand, and everything rests on their agreeing: the tile is placed
// on the planet by the CPU version (planet_place.cpp) and the surround is
// drawn by the GPU version, so a difference between them is a step around
// the tile. This runs the shader's pl_height_w over a grid of the very
// points the surround samples and compares with gpx::planet::heightf.
//
// Driven from the API ("verify_field_gpu"), like the field check it lives
// beside, because it needs a real GL context.
#include "glsl_version.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/planet_math.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace studio {

extern const char *PL_FN;         // planet_shaders.cpp
extern const char *PL_FIELD_STUB; // planet_shaders.cpp

static const char *VS_PLANET_CHECK = R"GLSL(#version 430 core
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";

static const char *FS_PLANET_CHECK = R"GLSL(#version 430 core
out vec4 frag;
uniform int u_grid;
uniform float u_octf;
PL_FN_PLACEHOLDER
void main(){
  int x = int(gl_FragCoord.x);
  int y = int(gl_FragCoord.y);
  float u = float(x) / float(u_grid - 1);
  float v = float(y) / float(u_grid - 1);
  vec2 hw = pl_height_w(vec3(u, 0.37, v), u_octf);
  frag = vec4(hw.x, hw.y, 0.0, 1.0);
})GLSL";

static GLuint compile_stage(GLenum type, const std::string &src, std::string &err) {
  GLuint sh = glCreateShader(type);
  const std::string patched = glsl_for_platform(src.c_str());
  const char *s = patched.c_str();
  glShaderSource(sh, 1, &s, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof log, nullptr, log);
    err = log;
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

// One layer stack, one octave budget; appends a report line.
static void planet_check_stack(const char *name,
                               const std::vector<gpx::planet::Layer> &layers,
                               float octf, std::string &out) {
  const int GRID = 64;
  std::string fs = FS_PLANET_CHECK;
  std::string body = gpx::field_glsl_prelude();
  body += PL_FIELD_STUB;
  body += PL_FN;
  size_t p = fs.find("PL_FN_PLACEHOLDER");
  fs.replace(p, strlen("PL_FN_PLACEHOLDER"), body);
  std::string err;
  GLuint vs = compile_stage(GL_VERTEX_SHADER, VS_PLANET_CHECK, err);
  GLuint fsh = compile_stage(GL_FRAGMENT_SHADER, fs, err);
  if (!vs || !fsh) {
    out += std::string(name) + ": planet shader did not compile: " + err + "\n";
    if (vs) glDeleteShader(vs);
    if (fsh) glDeleteShader(fsh);
    return;
  }
  GLuint prg = glCreateProgram();
  glAttachShader(prg, vs);
  glAttachShader(prg, fsh);
  glLinkProgram(prg);
  glDeleteShader(vs);
  glDeleteShader(fsh);
  GLint linked = 0;
  glGetProgramiv(prg, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[4096];
    glGetProgramInfoLog(prg, sizeof log, nullptr, log);
    out += std::string(name) + ": planet program did not link: " + log + "\n";
    glDeleteProgram(prg);
    return;
  }
  GLuint fbo = 0, tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, GRID, GRID, 0, GL_RGBA, GL_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    out += std::string(name) + ": float framebuffer unavailable\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteProgram(prg);
    return;
  }
  GLint prev_vp[4];
  glGetIntegerv(GL_VIEWPORT, prev_vp);
  glViewport(0, 0, GRID, GRID);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(prg);
  glUniform1i(glGetUniformLocation(prg, "u_grid"), GRID);
  glUniform1f(glGetUniformLocation(prg, "u_octf"), octf);
  glUniform1f(glGetUniformLocation(prg, "u_fstrength"), 0.f);
  // the layers exactly as planet_renderer.cpp uploads them
  float la[6][4] = {}, lb[6][4] = {};
  int n = 0;
  for (const gpx::planet::Layer &L : layers) {
    if (n >= 6) break;
    la[n][0] = L.frequency;
    la[n][1] = L.amplitude;
    la[n][2] = L.coverage;
    la[n][3] = L.mask_scale;
    lb[n][0] = (float)L.seed;
    lb[n][1] = (float)L.type;
    lb[n][2] = (float)std::clamp(L.octaves, 1, 12);
    ++n;
  }
  glUniform4fv(glGetUniformLocation(prg, "u_la"), 6, &la[0][0]);
  glUniform4fv(glGetUniformLocation(prg, "u_lb"), 6, &lb[0][0]);
  glUniform1i(glGetUniformLocation(prg, "u_lcount"), n);
  GLuint vao = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::vector<float> px((size_t)GRID * GRID * 4);
  glReadPixels(0, 0, GRID, GRID, GL_RGBA, GL_FLOAT, px.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
  glDeleteVertexArrays(1, &vao);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  glDeleteProgram(prg);

  float worst_h = 0.f, worst_w = 0.f;
  double sum = 0;
  int samples = 0;
  for (int y = 0; y < GRID; ++y)
    for (int x = 0; x < GRID; ++x) {
      float d[3] = {x / float(GRID - 1), 0.37f, y / float(GRID - 1)};
      float wet = 0.f;
      float h = gpx::planet::heightf(d, layers.data(), (int)layers.size(), octf, &wet);
      const float *g = &px[((size_t)y * GRID + x) * 4];
      float dh = std::fabs(h - g[0]), dw = std::fabs(wet - g[1]);
      if (!std::isfinite(g[0]) || !std::isfinite(h)) dh = 1e9f;
      worst_h = std::max(worst_h, dh);
      worst_w = std::max(worst_w, dw);
      sum += dh;
      ++samples;
    }
  // Both sides run the same float32 arithmetic in the same order; the
  // realistic layer goes through smoothsteps and a terrace, which amplify a
  // last-bit difference, so the bar is looser than the field check's 2e-4.
  bool ok = worst_h < 2e-3f && worst_w < 2e-2f;
  char buf[320];
  std::snprintf(buf, sizeof buf,
                "%s: %d samples, max |cpu-gpu| height = %.3e (wet %.3e), mean = %.3e -> %s\n",
                name, samples, worst_h, worst_w, (float)(sum / samples),
                ok ? "AGREE" : "DIVERGE");
  out += buf;
}

// The planet maths against the GPU: the three classic styles stacked with
// partial coverage, and the realistic landscape at a fractional octave
// budget (where the top octave's fade weight must match too).
std::string planet_gpu_verify() {
  std::string out;
  {
    std::vector<gpx::planet::Layer> L(3);
    L[0].type = 1; L[0].seed = 42; L[0].frequency = 4.f;
    L[1].type = 0; L[1].seed = 7; L[1].frequency = 11.f; L[1].amplitude = 0.5f;
    L[1].coverage = 0.6f;
    L[2].type = 2; L[2].seed = 9; L[2].frequency = 6.f; L[2].amplitude = 0.3f;
    L[2].coverage = 0.4f; L[2].mask_scale = 2.5f;
    planet_check_stack("planet: hills+ridged+billow", L, 6.f, out);
  }
  {
    std::vector<gpx::planet::Layer> L(1);
    L[0].type = 3; L[0].seed = 5; L[0].frequency = 1.5f; L[0].octaves = 12;
    planet_check_stack("planet: realistic terrain x9", L, 9.f, out);
    planet_check_stack("planet: realistic terrain x6.5", L, 6.5f, out);
  }
  return out;
}

} // namespace studio
