// Geekatplay TerraForge — CPU/GPU agreement check for field graphs (P0.2).
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
#include "render_settings.hpp"
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
      // Compare all four components, not just the first. FieldValue maps
      // one-to-one onto the vec4 the shader returns, so this is the honest
      // comparison — and it is the only one that means anything for a vector
      // output, where .number() would be the length rather than a component.
      gpx::FieldValue cpu = node.eval_field(port, ctx);
      const float *gpu = &px[((size_t)y * GRID + x) * 4];
      for (int c = 0; c < 4; ++c) {
        float d = std::fabs(cpu.v[c] - gpu[c]);
        if (!std::isfinite(cpu.v[c]) || !std::isfinite(gpu[c])) d = 1e9f;
        r.max_abs_error = std::max(r.max_abs_error, d);
        sum += d;
        ++r.samples;
      }
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
  auto run_port = [&](const char *name, gpx::Graph &g, gpx::Node *tip,
                      const char *port) {
    FieldGpuResult r = field_gpu_verify(*tip, port);
    out += std::string(name) + ": " + r.message + "\n";
    (void)g;
  };
  auto run = [&](const char *name, gpx::Graph &g, gpx::Node *tip) {
    run_port(name, g, tip, "out");
  };

  {   // a bare noise field
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    run("noise", g, n);
  }
  {   // noise through a curve and into math with altitude — a real chain
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
  // ---- the displacement family (P1) -------------------------------------
  // These are the graphs where CPU and GPU could most easily drift, because
  // the shader has to re-emit a subtree under a second evaluation point. If
  // the scoped cache were wrong the GPU would quietly reuse the un-redirected
  // value and still produce a plausible-looking picture, so this is the check
  // that catches it.
  {   // noise redirected by noise — domain warping, the classic case
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *w = g.add_node("FieldNoise");
    w->attrs.find("frequency")->f = 1.5f;
    w->attrs.find("seed")->seed = 77;
    gpx::Node *r = g.add_node("FieldRedirect");
    r->attrs.find("strength")->f = 0.3f;
    g.add_link(n->id, "out", r->id, "input");
    g.add_link(w->id, "out", r->id, "redirect");
    run("redirect(noise by noise)", g, r);
  }
  {   // a redirected subtree that is ALSO used un-redirected: the two must
      // stay distinct in the shader
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *r = g.add_node("FieldRedirect");
    gpx::Node *m = g.add_node("FieldMath");
    g.add_link(n->id, "out", r->id, "input");
    g.add_link(n->id, "out", r->id, "redirect");
    g.add_link(r->id, "out", m->id, "a");
    g.add_link(n->id, "out", m->id, "b");
    run("redirect sharing its own source", g, m);
  }
  {   // displacement with a quality boost and smoothing both engaged
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *d = g.add_node("FieldDisplace");
    d->attrs.find("dir_mode")->i = 1; // straight up: no normal dependence
    d->attrs.find("depth")->f = 3.f;
    d->attrs.find("quality")->i = 2;
    d->attrs.find("smoothing")->f = 0.6f;
    d->attrs.find("smooth_radius")->f = 0.02f;
    g.add_link(n->id, "out", d->id, "amount");
    run("displace(quality+smoothing)", g, d);
  }
  {   // computed normals — five evaluations of one subtree at four points
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *cn = g.add_node("FieldComputeNormal");
    cn->attrs.find("epsilon")->f = 0.02f;
    g.add_link(n->id, "out", cn->id, "height");
    run_port("compute normal", g, cn, "normal");
    run_port("compute normal (slope)", g, cn, "slope");
  }
  {   // zone, and its mask output on its own
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *z = g.add_node("FieldZone");
    z->attrs.find("size")->f = 0.4f;
    z->attrs.find("fade")->f = 0.5f;
    g.add_link(n->id, "out", z->id, "inside");
    run("zone", g, z);
    run_port("zone (mask)", g, z, "mask");
  }
  // ---- cellular ----------------------------------------------------------
  // Worley noise is the one pattern where a CPU/GPU split is easy to miss by
  // eye: a wrong bit slice in the per-cell offset still produces a plausible
  // cell pattern, just a different one. So all four outputs and all three
  // metrics are checked, plus a jitterless grid where every cell point sits
  // exactly on a lattice centre and any rounding difference shows up as a
  // whole-cell jump.
  {
    const char *out_name[4] = {"F1", "F2", "walls", "cell value"};
    for (int o = 0; o < 4; ++o) {
      gpx::Graph g;
      gpx::Node *n = g.add_node("FieldVoronoi");
      n->attrs.find("output")->i = o;
      n->attrs.find("frequency")->f = 5.f;
      run((std::string("voronoi ") + out_name[o]).c_str(), g, n);
    }
    const char *metric_name[3] = {"round", "diamond", "square"};
    for (int m = 0; m < 3; ++m) {
      gpx::Graph g;
      gpx::Node *n = g.add_node("FieldVoronoi");
      n->attrs.find("metric")->i = m;
      n->attrs.find("output")->i = 2; // walls: the most sensitive output
      run((std::string("voronoi ") + metric_name[m]).c_str(), g, n);
    }
    {
      gpx::Graph g;
      gpx::Node *n = g.add_node("FieldVoronoi");
      n->attrs.find("jitter")->f = 0.f;
      run("voronoi (no jitter)", g, n);
    }
    {   // cells warped by noise, which is how it is actually used
      gpx::Graph g;
      gpx::Node *w = g.add_node("FieldNoise");
      w->attrs.find("frequency")->f = 2.f;
      gpx::Node *v = g.add_node("FieldVoronoi");
      gpx::Node *r = g.add_node("FieldRedirect");
      r->attrs.find("strength")->f = 0.25f;
      g.add_link(v->id, "out", r->id, "input");
      g.add_link(w->id, "out", r->id, "redirect");
      run("voronoi warped by noise", g, r);
    }
  }

  {   // texture coordinates with a rotation, where sin/cos signs can flip
    gpx::Graph g;
    gpx::Node *tc = g.add_node("FieldTexCoord");
    tc->attrs.find("angle")->f = 31.f;
    run("texcoord(rotated)", g, tc);
  }

  // ---- field materials (P2) ---------------------------------------------
  {   // distribution on all three criteria at once, with fades
    gpx::Graph g;
    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("altitude")->v2[0] = -1.f;
    d->attrs.find("altitude")->v2[1] = 1.f;
    d->attrs.find("altitude_fuzz")->f = 0.5f;
    d->attrs.find("use_slope")->b = true;
    d->attrs.find("use_orientation")->b = true;
    run("distribution(alt+slope+facing)", g, d);
  }
  {   // and with a hard edge, where smoothstep would be undefined
    gpx::Graph g;
    gpx::Node *n = g.add_node("FieldNoise");
    gpx::Node *d = g.add_node("FieldDistribution");
    d->attrs.find("altitude_fuzz")->f = 0.f;
    g.add_link(n->id, "out", d->id, "altitude");
    run("distribution(hard edge)", g, d);
  }
  {   // colour blending, where the alpha channel is easy to get wrong
    gpx::Graph g;
    gpx::Node *ga = g.add_node("FieldGradient");
    gpx::Node *cb = g.add_node("FieldColorConstant");
    gpx::Node *nz = g.add_node("FieldNoise");
    gpx::Node *mx = g.add_node("FieldColorMix");
    mx->attrs.find("mode")->i = 4; // overlay: the branchy one
    g.add_link(nz->id, "out", ga->id, "in");
    g.add_link(ga->id, "out", mx->id, "a");
    g.add_link(cb->id, "out", mx->id, "b");
    g.add_link(nz->id, "out", mx->id, "factor");
    run("colour mix(overlay)", g, mx);
  }

  // The live terrain program: whether a TerrainDisplacement graph is actually
  // driving the viewport, and whether its generated shader linked. Agreement
  // on a test grid means nothing if the real program failed to build.
  {
    const char *e = renderer_field_error();
    out += std::string("terrain displacement program: ") +
           (e && *e ? std::string("LINK FAILED - ") + e
                    : std::string("ok (no link error)")) +
           "\n";
    out += renderer_tess_status() + "\n";
    // Culling only runs inside the tessellation program, so a report that
    // shows one without the other cannot say whether it is live.
    out += renderer_cull_status() + "\n";
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


