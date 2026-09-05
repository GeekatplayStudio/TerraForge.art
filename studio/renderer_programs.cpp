#include "uniform_cache.hpp"
// Geekatplay TerraForge - shader program management: compiling, linking,
// splicing the generated field/surface GLSL into the terrain programs, and
// the relink-on-change bookkeeping. Split from renderer.cpp for the 500-line
// module rule.
#include "renderer_internal.hpp"
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include "glsl_version.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stb_image_write.h"
#include "renderer_shaders.hpp"

namespace studio {

void renderer_set_surface_bump(float strength, float scale) {
  g_surf_bump_strength = strength;
  g_surf_bump_scale = scale;
}


std::string inject_sky(const char *src) {
  std::string s(src);
  auto sub = [&](const char *tag, const char *body) {
    size_t p = s.find(tag);
    if (p != std::string::npos) s.replace(p, strlen(tag), body);
  };
  sub("FRACTAL_FN_PLACEHOLDER", FRACTAL_FN);
  {
    extern const char *const MATERIAL_UNIFORMS_GLSL; // renderer_matparams.cpp
    extern const char *const MATERIAL_FN_GLSL;
    sub("MATERIAL_UNIFORMS_PLACEHOLDER", MATERIAL_UNIFORMS_GLSL);
    sub("MATERIAL_FN_PLACEHOLDER", MATERIAL_FN_GLSL);
  }
  {
    extern const char *PL_PALETTE;   // planet_shaders.cpp
    extern const char *PL_SPHERE_FN; // planet_shaders.cpp
    sub("PL_PALETTE_PLACEHOLDER", PL_PALETTE);
    sub("PL_SPHERE_PLACEHOLDER", PL_SPHERE_FN);
  }
  sub("SKY_FN_PLACEHOLDER", SKY_FN);
  sub("FOG_FN_PLACEHOLDER", FOG_FN);
  // The vertex and fragment stages are separate translation units, so each
  // gets its own copy of the prelude; duplicate definitions only collide
  // within one stage. But a stage may hold two generated functions, and then
  // the second must not bring its own copy — so the prelude is emitted once,
  // here, and stripped from everything spliced after it.
  std::string first = gpx::field_glsl_prelude();
  first += g_field_glsl.empty() ? GPX_FIELD_STUB
                                : gpx::field_glsl_strip_prelude(g_field_glsl);
  sub("GPX_FIELD_PLACEHOLDER", first.c_str());
  auto later = [](const std::string &code, const char *stub) {
    return code.empty() ? std::string(stub)
                        : gpx::field_glsl_strip_prelude(code);
  };
  std::string surf = later(g_surface_glsl, GPX_SURFACE_STUB);
  sub("GPX_SURFACE_PLACEHOLDER", surf.c_str());
  std::string rough = later(g_rough_glsl, GPX_ROUGH_STUB);
  sub("GPX_ROUGH_PLACEHOLDER", rough.c_str());
  std::string bump = later(g_bump_glsl, GPX_BUMP_STUB);
  sub("GPX_BUMP_PLACEHOLDER", bump.c_str());
  return s;
}


GLuint compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  // glsl_for_platform is the choke point for the #version line: a no-op
  // everywhere except macOS, which caps OpenGL at 4.1 core. See
  // studio/glsl_version.hpp for why the downgrade is safe.
  const std::string patched = glsl_for_platform(src);
  const char *s = patched.c_str();
  glShaderSource(sh, 1, &s, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof log, nullptr, log);
    log_error("shader", log);
  }
  return sh;
}


GLuint link_prog(const char *vs, const char *fs) {
  GLuint p = glCreateProgram();
  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}


// Link, but report rather than assume. The built-in shaders are known good, so
// nothing checked this before; generated code is written by the user's graph
// and can genuinely fail, and a silently unlinked program renders nothing at
// all — the worst way to find out.
GLuint link_checked(const std::string &vs, const std::string &fs,
                           std::string &err) {
  GLuint p = glCreateProgram();
  GLuint v = compile(GL_VERTEX_SHADER, vs.c_str());
  GLuint f = compile(GL_FRAGMENT_SHADER, fs.c_str());
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096] = {0};
    glGetProgramInfoLog(p, sizeof log, nullptr, log);
    err = log;
    delete_program(p);
    return 0;
  }
  return p;
}

std::string terrain_tes_source() {
  return std::string("#version 430 core\n") + TERRAIN_VERT_COMMON +
         TES_TERRAIN_TAIL;
}


// Link a four-stage program. Tessellation is the one place we need more than
// a vertex and a fragment shader.
GLuint link_checked_tess(const std::string &vs, const std::string &tcs,
                                const std::string &tes, const std::string &fs,
                                std::string &err) {
  GLuint p = glCreateProgram();
  GLuint v = compile(GL_VERTEX_SHADER, vs.c_str());
  GLuint c = compile(GL_TESS_CONTROL_SHADER, tcs.c_str());
  GLuint e = compile(GL_TESS_EVALUATION_SHADER, tes.c_str());
  GLuint f = compile(GL_FRAGMENT_SHADER, fs.c_str());
  for (GLuint s : {v, c, e, f}) glAttachShader(p, s);
  glLinkProgram(p);
  for (GLuint s : {v, c, e, f}) glDeleteShader(s);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096] = {0};
    glGetProgramInfoLog(p, sizeof log, nullptr, log);
    err = log;
    delete_program(p);
    return 0;
  }
  return p;
}


bool rebuild_terrain_program(std::string &err) {
  std::string fs = inject_sky(FS_TERRAIN_SRC);
  GLuint p = link_checked(inject_sky(terrain_vs_source().c_str()), fs, err);
  if (!p) return false;
  if (prog_terrain) delete_program(prog_terrain);
  prog_terrain = p;

  // The shadow pass carries the same displacement, so the terrain does not
  // cast a shadow from where it used to be.
  std::string derr;
  if (GLuint d = link_checked(inject_sky(VS_DEPTH_SRC), FS_DEPTH, derr)) {
    if (prog_depth) delete_program(prog_depth);
    prog_depth = d;
  } else {
    log_error("shader", "shadow pass: " + derr);
  }

  // The tessellated program is an optimisation, not a requirement: if it does
  // not build we keep the fixed grid and say so, rather than losing the
  // terrain entirely. Everything below this point is allowed to fail.
  std::string terr;
  GLuint t = link_checked_tess(VS_TERRAIN_PASS, TCS_TERRAIN,
                               inject_sky(terrain_tes_source().c_str()), fs,
                               terr);
  if (t) {
    if (prog_terrain_tess) delete_program(prog_terrain_tess);
    prog_terrain_tess = t;
    tess_ok = true;
  } else {
    tess_ok = prog_terrain_tess != 0; // keep any program that already worked
    log_warn("shader",
             "terrain tessellation unavailable, using the fixed grid: " + terr);
  }
  return true;
}


// Hand the renderer a transpiled displacement graph. Same shape as
// renderer_set_material_maps: version-guarded, so an unchanged graph costs
// nothing. The relink itself is deferred to draw time because this is called
// from the evaluation path, which has no guarantee about the GL context.
void renderer_set_field_program(const std::string &glsl,
                                unsigned long long version) {
  (void)version; // the source itself is the identity; comparing it is cheap
  if (glsl == g_field_want) return;
  g_field_want = glsl;
  g_field_dirty = true;
}


// The surface channels. All of them live in one program, so a change to any
// one is a single relink.
void renderer_set_surface_program(const std::string &color,
                                  const std::string &roughness,
                                  const std::string &bump,
                                  unsigned long long version) {
  (void)version;
  if (color == g_surface_want && roughness == g_rough_want &&
      bump == g_bump_want)
    return;
  g_surface_want = color;
  g_rough_want = roughness;
  g_bump_want = bump;
  g_field_dirty = true;
}


// A field graph containing a Sample node reads a buffer, and the shader
// declares a sampler for it. Binding those is what lets an *eroded* heightfield
// drive displacement or colour — which is the whole reason both domains exist,
// so refusing such graphs left the bridge half-built.
void renderer_set_field_textures(
    const std::vector<std::pair<std::string, const gpx::Heightmap *>> &maps,
    unsigned long long version) {
  static unsigned long long last_version = ~0ull;
  if (version == last_version && maps.size() == g_field_tex.size()) return;
  last_version = version;
  ++g_shadow_revision;

  // The terrain pass already uses units 0-7, so these start at 8; more than a
  // handful of sampled buffers in one graph is not worth the units.
  const size_t MAX = 4;
  for (auto &t : g_field_tex)
    if (t.tex) glDeleteTextures(1, &t.tex);
  g_field_tex.clear();

  for (const auto &[name, hm] : maps) {
    if (g_field_tex.size() >= MAX) break;
    if (!hm || hm->empty()) continue;
    FieldTex ft;
    ft.name = name;
    glGenTextures(1, &ft.tex);
    glBindTexture(GL_TEXTURE_2D, ft.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, hm->w, hm->h, 0, GL_RED, GL_FLOAT,
                 hm->v.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_field_tex.push_back(ft);
  }
}


// Bind whatever the generated code asked for. A sampler the host never binds
// reads black, so an unbound one is not a crash — it is a silently wrong
// picture, which is worse.
void bind_field_textures(GLuint prog) {
  for (size_t i = 0; i < g_field_tex.size(); ++i) {
    glActiveTexture(GL_TEXTURE8 + (GLenum)i);
    glBindTexture(GL_TEXTURE_2D, g_field_tex[i].tex);
    unii(prog, g_field_tex[i].name.c_str(), 8 + (int)i);
  }
}


// Empty when the displacement graph is compiling cleanly. The Properties panel
// shows this on the node, so a broken graph says so instead of just not
// displacing.
const char *renderer_field_error() { return g_field_error.c_str(); }


// For the tests and the benchmark: how many of the patch grid survived the
// last camera pass, or -1 when culling was not applied.
int renderer_patches_visible() { return g_patches_visible; }

} // namespace studio
