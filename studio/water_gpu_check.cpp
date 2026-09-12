// Geekatplay TerraForge - CPU/GPU agreement check for the sea's waves.
//
// gpx/water_waves.hpp and WATER_WAVES_GLSL (shaders_water.cpp) are written
// twice by hand: the viewport draws the sea from the GLSL, the offline
// render bakes its mesh from the C++, and a difference between them is a
// render whose waves are not the ones the camera showed. This runs the
// shader's wv_eval over a grid of points - near the origin and a few
// hundred metres out, filtered and whole - and compares with
// gpx::water::evaluate.
//
// Driven from the API ("verify_field_gpu") beside the planet check, because
// it needs a real GL context.
#include "glsl_version.hpp"
#include "renderer_shaders.hpp"
#include "gpx/water_waves.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace studio {

namespace {

const char *VS_WAVE_CHECK = R"GLSL(#version 430 core
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";

const char *FS_WAVE_CHECK = R"GLSL(#version 430 core
out vec4 frag;
uniform int u_grid, u_what;
uniform float u_span, u_min_lambda;
WATER_WAVES_PLACEHOLDER
void main(){
  vec2 xm = (gl_FragCoord.xy - 0.5) / float(u_grid - 1) * u_span - 0.5 * u_span;
  vec3 disp, dpdx, dpdz; float lost;
  wv_eval(xm, u_min_lambda, disp, dpdx, dpdz, lost);
  if (u_what == 0) frag = vec4(disp, lost);
  else frag = vec4(dpdx.y, dpdz.y, dpdx.x * dpdz.z - dpdx.z * dpdz.x, dpdx.z);
})GLSL";

GLuint compile_stage(GLenum type, const std::string &src, std::string &err) {
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

// One sea, one span and filter; appends a report line.
void wave_check_case(const char *name, const gpx::water::Params &p, float span_m,
                     float min_lambda, double t, std::string &out) {
  const int GRID = 48;
  gpx::water::Wave w[gpx::water::MAX_WAVES];
  const int n = gpx::water::build(p, w);
  float ph[gpx::water::MAX_WAVES];
  gpx::water::rebase(w, n, 1250.0, -875.0, t, ph);

  std::string fs = FS_WAVE_CHECK;
  const size_t at = fs.find("WATER_WAVES_PLACEHOLDER");
  fs.replace(at, std::strlen("WATER_WAVES_PLACEHOLDER"), WATER_WAVES_GLSL);
  std::string err;
  GLuint vs = compile_stage(GL_VERTEX_SHADER, VS_WAVE_CHECK, err);
  GLuint fsh = compile_stage(GL_FRAGMENT_SHADER, fs, err);
  if (!vs || !fsh) {
    out += std::string(name) + ": wave shader did not compile: " + err + "\n";
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
    out += std::string(name) + ": wave program did not link: " + log + "\n";
    glDeleteProgram(prg);
    return;
  }
  GLuint fbo = 0, tex = 0, vao = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, GRID, GRID, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  GLint prev_vp[4];
  glGetIntegerv(GL_VIEWPORT, prev_vp);
  glViewport(0, 0, GRID, GRID);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glUseProgram(prg);
  float a[gpx::water::MAX_WAVES * 4] = {}, b[gpx::water::MAX_WAVES * 4] = {};
  for (int i = 0; i < n; ++i) {
    a[i * 4] = w[i].dx; a[i * 4 + 1] = w[i].dz; a[i * 4 + 2] = w[i].k; a[i * 4 + 3] = w[i].amp;
    b[i * 4] = ph[i]; b[i * 4 + 1] = w[i].q; b[i * 4 + 2] = w[i].lambda;
  }
  glUniform4fv(glGetUniformLocation(prg, "u_wv_a"), gpx::water::MAX_WAVES, a);
  glUniform4fv(glGetUniformLocation(prg, "u_wv_b"), gpx::water::MAX_WAVES, b);
  glUniform1i(glGetUniformLocation(prg, "u_wv_n"), n);
  glUniform1i(glGetUniformLocation(prg, "u_grid"), GRID);
  glUniform1f(glGetUniformLocation(prg, "u_span"), span_m);
  glUniform1f(glGetUniformLocation(prg, "u_min_lambda"), min_lambda);
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  std::vector<float> px[2];
  for (int what = 0; what < 2; ++what) {
    glUniform1i(glGetUniformLocation(prg, "u_what"), what);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    px[what].resize((size_t)GRID * GRID * 4);
    glReadPixels(0, 0, GRID, GRID, GL_RGBA, GL_FLOAT, px[what].data());
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
  glDeleteVertexArrays(1, &vao);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  glDeleteProgram(prg);

  float worst_d = 0.f, worst_s = 0.f;
  for (int y = 0; y < GRID; ++y)
    for (int x = 0; x < GRID; ++x) {
      const float xm = float(x) / float(GRID - 1) * span_m - 0.5f * span_m;
      const float zm = float(y) / float(GRID - 1) * span_m - 0.5f * span_m;
      gpx::water::Sample s;
      gpx::water::evaluate(w, ph, n, xm, zm, min_lambda, s);
      const float *g0 = &px[0][((size_t)y * GRID + x) * 4];
      const float *g1 = &px[1][((size_t)y * GRID + x) * 4];
      for (int k = 0; k < 3; ++k) worst_d = std::max(worst_d, std::fabs(g0[k] - s.disp[k]));
      worst_s = std::max(worst_s, std::fabs(g0[3] - s.lost));
      worst_s = std::max(worst_s, std::fabs(g1[0] - s.dpdx[1]));
      worst_s = std::max(worst_s, std::fabs(g1[1] - s.dpdz[1]));
      worst_s = std::max(worst_s, std::fabs(g1[2] - gpx::water::jacobian(s)));
      worst_s = std::max(worst_s, std::fabs(g1[3] - s.dpdx[2]));
    }
  // Both sides run float32 sin and cos over the same sums in the same order;
  // the libraries' sines differ in their last bits. A millimetre of height
  // and a thousandth of slope are far below anything a pixel can show.
  const bool ok = worst_d < 1e-3f && worst_s < 1e-3f;
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "%s: %d samples, max |cpu-gpu| displacement = %.3e m, slope = %.3e -> %s\n",
                name, GRID * GRID, worst_d, worst_s, ok ? "AGREE" : "DIVERGE");
  out += buf;
}

} // namespace

std::string water_gpu_verify() {
  std::string out;
  gpx::water::Params calm;
  calm.wind_speed = 3.f;
  calm.choppiness = 0.2f;
  wave_check_case("water: breeze, every wave, 40 m", calm, 40.f, 0.f, 12.5, out);
  gpx::water::Params storm;
  storm.wind_speed = 14.f;
  storm.choppiness = 0.9f;
  storm.wind_dir_deg = 200.f;
  wave_check_case("water: gale, filtered at 3 m, 600 m", storm, 600.f, 3.f, 987.25, out);
  return out;
}

} // namespace studio
