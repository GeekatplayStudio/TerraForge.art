// Geekatplay TerraForge â€” CPU/GPU agreement check for field graphs (P0.2).
//
// The whole dual-domain design rests on one claim: the field graph you author
// evaluates to the same numbers on the CPU (tests, picking, rasterizing) and on
// the GPU (displacement, shading). This verifies that claim against a real GL
// context by running the generated shader over a grid of points and comparing
// every sample to gpx::Node::eval_field.
//
// It is not a unit test because it needs a GPU; it is driven from the API so it
// can be run against the live app, and it prints a worst-case error so a
// regression shows up as a number rather than a vibe.
#include "app.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/node_graph.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace studio {

static const char *VS_FULL = R"GLSL(#version 430 core
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})GLSL";

// The grid of sample points must match the CPU side exactly, so it is derived
// from gl_FragCoord the same way the CPU loop derives it from x/y.
static const char *FS_TEMPLATE = R"GLSL(#version 430 core
out vec4 frag;
uniform int u_grid;
uniform float u_span;
GENERATED_FN
void main(){
  int x = int(gl_FragCoord.x);
  int y = int(gl_FragCoord.y);
  float fx = (float(x) / float(u_grid - 1) - 0.5) * u_span;
  float fz = (float(y) / float(u_grid - 1) - 0.5) * u_span;
  vec3 P = vec3(fx, 0.0, fz);
  vec3 N = vec3(0.0, 1.0, 0.0);
  frag = gpx_field(P, N, 0.0, 1.0, 0.0, 0.0, 12.0);
})GLSL";

struct FieldGpuResult {
  bool ok = false;
  float max_abs_error = 0.f;
  float mean_abs_error = 0.f;
  int samples = 0;
  std::string message;
};

static GLuint compile_or_report(GLenum type, const std::string &src,
                                std::string &err) {
  GLuint sh = glCreateShader(type);
  const char *s = src.c_str();
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

FieldGpuResult field_gpu_verify(const gpx::Node &node, const std::string &port) {
  FieldGpuResult r;
  const int GRID = 64;
  const float SPAN = 4.f; // covers negative and positive coordinates

  gpx::GlslProgram prog = gpx::field_to_glsl(node, port, "gpx_field");
  if (!prog.ok) {
    r.message = "transpile failed: " + prog.error;
    return r;
  }
  if (!prog.samplers.empty()) {
    // a graph reading a buffer would need those textures bound; out of scope
    // for the agreement check, which is about the arithmetic
    r.message = "skipped: graph samples a buffer";
    r.ok = true;
    return r;
  }

  std::string fs_src = FS_TEMPLATE;
  size_t p = fs_src.find("GENERATED_FN");
  fs_src.replace(p, strlen("GENERATED_FN"), prog.code);

  std::string err;
  GLuint vs = compile_or_report(GL_VERTEX_SHADER, VS_FULL, err);
  GLuint fs = compile_or_report(GL_FRAGMENT_SHADER, fs_src, err);
  if (!vs || !fs) {
    r.message = "generated shader did not compile: " + err;
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return r;
  }
  GLuint prg = glCreateProgram();
  glAttachShader(prg, vs);
  glAttachShader(prg, fs);
  glLinkProgram(prg);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint linked = 0;
  glGetProgramiv(prg, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[4096];
    glGetProgramInfoLog(prg, sizeof log, nullptr, log);
    r.message = std::string("generated program did not link: ") + log;
    glDeleteProgram(prg);
    return r;
  }

  // float target, so the comparison is not quantized by an 8-bit buffer
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
    r.message = "float framebuffer unavailable";
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteProgram(prg);
    return r;
  }

  GLint prev_vp[4];
  glGetIntegerv(GL_VIEWPORT, prev_vp);
  glViewport(0, 0, GRID, GRID);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(prg);
  glUniform1i(glGetUniformLocation(prg, "u_grid"), GRID);
  glUniform1f(glGetUniformLocation(prg, "u_span"), SPAN);
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

  // same points, on the CPU
  double sum = 0;
  for (int y = 0; y < GRID; ++y)
    for (int x = 0; x < GRID; ++x) {
      gpx::FieldContext ctx;
      ctx.pos[0] = (x / float(GRID - 1) - 0.5f) * SPAN;
      ctx.pos[1] = 0.f;
      ctx.pos[2] = (y / float(GRID - 1) - 0.5f) * SPAN;
      ctx.normal[0] = 0.f; ctx.normal[1] = 1.f; ctx.normal[2] = 0.f;
      ctx.altitude = 0.f;
      ctx.slope = 1.f;
      ctx.orientation = 0.f;
      ctx.time = 0.f;
      ctx.lod = 12.f;
      float cpu = node.eval_field(port, ctx).number();
      float gpu = px[((size_t)y * GRID + x) * 4];
      float d = std::fabs(cpu - gpu);
      if (!std::isfinite(cpu) || !std::isfinite(gpu)) d = 1e9f;
      r.max_abs_error = std::max(r.max_abs_error, d);
      sum += d;
      ++r.samples;
    }
  r.mean_abs_error = r.samples ? (float)(sum / r.samples) : 0.f;
  // float32 on two different execution units: exact equality is not the bar,
  // but anything above this means the two implementations really differ
  r.ok = r.max_abs_error < 2e-4f;
  char buf[256];
  std::snprintf(buf, sizeof buf,
                "%d samples, max |cpu-gpu| = %.3e, mean = %.3e -> %s",
                r.samples, r.max_abs_error, r.mean_abs_error,
                r.ok ? "AGREE" : "DIVERGE");
  r.message = buf;
  return r;
}

// Runs the check over a set of representative graphs and returns a report.
// Called from the API so it can be exercised against the live app.
std::string field_gpu_verify_all(App &a) {
  std::string out;
  auto run = [&](const char *name, gpx::Graph &g, gpx::Node *tip) {
    FieldGpuResult r = field_gpu_verify(*tip, "out");
    out += std::string(name) + ": " + r.message + "\n";
    (void)g;
  };

  {   // a bare noise field
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    run("noise", g, n);
  }
  {   // noise through a curve and into math with altitude â€” a real chain
    gpx::Graph g;
    gpx::Node *pos = g.add_node("FieldPosition");
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *c = g.add_node("FieldCurve");
    gpx::Node *m = g.add_node("FieldMath");
    gpx::Node *alt = g.add_node("FieldAltitude");
    g.add_link(pos->id, "out", n->id, "position");
    g.add_link(n->id, "out", c->id, "in");
    g.add_link(c->id, "out", m->id, "a");
    g.add_link(alt->id, "out", m->id, "b");
    run("noise>curve>math", g, m);
  }
  {   // ridged noise, the shape planets and mountains use
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    n->attrs.find("type")->i = 1;
    n->attrs.find("octaves")->i = 9;
    n->attrs.find("frequency")->f = 7.f;
    run("ridged x9", g, n);
  }
  {   // trigonometry and remapping, where degree/radian handling can drift
    gpx::Graph g;
    gpx::Node *p = g.add_node("FieldPosition");
    gpx::Node *v = g.add_node("FieldVectorOp");
    gpx::Node *t = g.add_node("FieldTrig");
    gpx::Node *rm = g.add_node("FieldRemap");
    g.add_link(p->id, "out", v->id, "a");
    g.add_link(v->id, "out", t->id, "in");
    g.add_link(t->id, "out", rm->id, "in");
    run("vector>trig>remap", g, rm);
  }
  {   // mix of two noises, exercising subexpression reuse
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *mx = g.add_node("FieldMix");
    g.add_link(n->id, "out", mx->id, "a");
    g.add_link(n->id, "out", mx->id, "factor");
    run("shared noise mix", g, mx);
  }
  // also drop it beside the API state so it can be read from a script — the
  // status bar cannot show a multi-line report
  if (const char *base = std::getenv("LOCALAPPDATA")) {
    std::string dir = std::string(base) + "\\GeekatplayTerraForge\\api";
    std::FILE *f = std::fopen((dir + "\\field_gpu_report.txt").c_str(), "w");
    if (f) {
      std::fwrite(out.data(), 1, out.size(), f);
      std::fclose(f);
    }
  }
  (void)a;
  return out;
}

} // namespace studio


