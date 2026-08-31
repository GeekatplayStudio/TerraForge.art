// Geekatplay TerraForge â€” OpenGL scene renderer.
// Terrain (PBR: roughness/metallic/reflection/translucency/displacement),
// volumetric raymarched clouds, height fog with absorption, water with foam,
// shadow mapping, scene meshes, sun gizmo, selection outlines, object picking.
#include "app.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <glad/gl.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "stb_image_write.h" // implementation lives in the engine lib

#include "renderer_shaders.hpp"

namespace studio {

RenderSettings &render_settings() {
  static RenderSettings rs;
  return rs;
}

void compute_sun_dir(const RenderSettings &rs, float out[3]) {
  float az, alt;
  if (rs.sun_mode == 0) {
    az = rs.sun_azimuth * 0.017453293f;
    alt = rs.sun_altitude * 0.017453293f;
  } else {
    static const int mdays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int m = std::clamp(rs.month, 1, 12);
    int doy = mdays[m - 1] + std::clamp(rs.day, 1, 31);
    float frac_year = 2.f * 3.14159265f / 365.f * (doy - 1 + (rs.hour - 12.f) / 24.f);
    float decl = 0.006918f - 0.399912f * std::cos(frac_year) +
                 0.070257f * std::sin(frac_year) - 0.006758f * std::cos(2 * frac_year) +
                 0.000907f * std::sin(2 * frac_year) - 0.002697f * std::cos(3 * frac_year) +
                 0.00148f * std::sin(3 * frac_year);
    float eqtime = 229.18f * (0.000075f + 0.001868f * std::cos(frac_year) -
                              0.032077f * std::sin(frac_year) -
                              0.014615f * std::cos(2 * frac_year) -
                              0.040849f * std::sin(2 * frac_year));
    float time_offset = eqtime + 4.f * rs.longitude - 60.f * rs.utc_offset;
    float tst = rs.hour * 60.f + time_offset;
    float ha = (tst / 4.f - 180.f) * 0.017453293f;
    float lat = rs.latitude * 0.017453293f;
    float cos_zen = std::sin(lat) * std::sin(decl) +
                    std::cos(lat) * std::cos(decl) * std::cos(ha);
    cos_zen = std::clamp(cos_zen, -1.f, 1.f);
    alt = 1.5707963f - std::acos(cos_zen);
    float sin_az = -std::cos(decl) * std::sin(ha) / std::max(std::cos(alt), 1e-4f);
    float cos_az = (std::sin(decl) - std::sin(lat) * cos_zen) /
                   std::max(std::cos(lat) * std::cos(alt), 1e-4f);
    az = std::atan2(sin_az, cos_az);
    az = 1.5707963f - az;
    alt = std::max(alt, 0.02f);
  }
  out[0] = std::cos(alt) * std::cos(az);
  out[1] = std::sin(alt);
  out[2] = std::cos(alt) * std::sin(az);
}

struct Camera {
  // cinematic default: low angle so terrain and sky both read
  float yaw = 0.7f, pitch = 0.26f, dist = 1.9f;
  float target[3] = {0.5f, 0.08f, 0.5f};
};
static Camera CAM;

static GLuint prog_terrain = 0, prog_water = 0, prog_sky = 0, prog_depth = 0;
// mean height of the uploaded tile, so the infinite surround can meet it
static float g_terrain_mean = 0.f;
static GLuint prog_lines = 0, prog_bg = 0, prog_mesh = 0, prog_gizmo = 0;
static GLuint prog_matprev = 0;
static GLuint matprev_fbo = 0, matprev_tex = 0, matprev_depth = 0;
static int matprev_size = 0;
// preview shapes: 0 sphere, 1 cube, 2 flat — pos(3)+nrm(3)+uv(2)
static GLuint prev_vao[3] = {0, 0, 0}, prev_vbo[3] = {0, 0, 0};
static int prev_verts[3] = {0, 0, 0};
static GLuint vao_grid = 0, vbo_grid = 0, ebo_grid = 0, vao_quad = 0;
static GLuint vao_lines = 0, vbo_lines = 0;
static GLuint vao_dyn = 0, vbo_dyn = 0;      // dynamic outline lines
static GLuint vao_sphere = 0, vbo_sphere = 0; // sun gizmo
static int sphere_verts = 0;
static int line_vert_count = 0;
static GLuint tex_height = 0, tex_albedo = 0;
static GLuint tex_normal = 0, tex_rough = 0, tex_disp = 0;
static GLuint tex_cloud_shape = 0, tex_cloud_detail = 0;
static bool has_normal_map = false, has_rough_map = false, has_disp_map = false;
static int grid_n = 512, index_count = 0;
// A coarse quad grid the tessellator subdivides. 64x64 patches at up to 64
// subdivisions an edge reaches an effective 4096 across where the camera is
// close, while a patch at the horizon costs two triangles.
static const int patch_n = 65; // vertices per side, so 64 patches
static GLuint vao_patch = 0, vbo_patch = 0, ibo_patch = 0;
static int patch_index_count = 0;
static GLuint prog_terrain_tess = 0;
static bool tess_ok = false;
static int hm_w = 0;
static bool has_albedo = false;
static gpx::Heightmap cpu_height; // normalized copy, for picking
// sculpt brush cursor: uv, radius (<=0 hidden), erase flag. Reset every frame
// by the viewport, so a hidden panel never leaves a stale ring behind.
static float g_brush[4] = {0, 0, -1.f, 0};
static GLuint fbo[6] = {0}, fbo_color[6] = {0}, fbo_depth[6] = {0};
static int fbo_w[6] = {0}, fbo_h[6] = {0};
static GLuint shadow_fbo = 0, shadow_tex = 0;
static const int SHADOW_RES = 2048;
static float cloud_time = 0.f;
static float g_last_fovy = 0.9f; // for the planet pass's pixel-size LOD

// ----------------------------------------------------------------- shaders

// ------------------------------------------------------------------ helpers
// The generated displacement function, or a stub. Always substituting
// something keeps every shader well-formed whether or not the user has
// authored a displacement graph, so the placeholder never needs a conditional
// and there is no second code path to get wrong.
static const char *GPX_FIELD_STUB =
    "vec4 gpx_terrain_field(vec3 P, vec3 N, float alt, float slope,\n"
    "                       float orient, float t, float lod){\n"
    "  return vec4(0.0);\n}\n";
static const char *GPX_SURFACE_STUB =
    "vec4 gpx_terrain_surface(vec3 P, vec3 N, float alt, float slope,\n"
    "                         float orient, float t, float lod){\n"
    "  return vec4(0.5, 0.5, 0.5, 1.0);\n}\n";
static const char *GPX_ROUGH_STUB =
    "vec4 gpx_terrain_rough(vec3 P, vec3 N, float alt, float slope,\n"
    "                       float orient, float t, float lod){\n"
    "  return vec4(0.5, 0.0, 0.0, 1.0);\n}\n";
static const char *GPX_BUMP_STUB =
    "vec4 gpx_terrain_bump(vec3 P, vec3 N, float alt, float slope,\n"
    "                      float orient, float t, float lod){\n"
    "  return vec4(0.0);\n}\n";
static std::string g_surface_want, g_surface_glsl;
static std::string g_rough_want, g_rough_glsl;
static std::string g_bump_want, g_bump_glsl;
// bump shaping, taken from the TerrainSurface node when the graph evaluates
static float g_surf_bump_strength = 1.f, g_surf_bump_scale = 0.004f;
void renderer_set_surface_bump(float strength, float scale) {
  g_surf_bump_strength = strength;
  g_surf_bump_scale = scale;
}
// The source the graph asked for, and the source actually spliced into the
// live program. They differ only while a relink is owed, or after one failed:
// on failure the program falls back to the stub but the request is remembered,
// so the same broken source is never retried. Comparing against the *request*
// rather than the live source is what stops a failing graph from relinking a
// shader every single frame.
static std::string g_field_want;      // what the graph asked for
static std::string g_field_glsl;      // what is spliced in now, empty = stub
static bool g_field_dirty = false;    // a relink is owed
static std::string g_field_error;     // why the last relink failed, if it did
// Buffers a generated program samples, uploaded as textures and bound to the
// uniform names the transpiler declared.
struct FieldTex {
  std::string name;
  GLuint tex = 0;
};
static std::vector<FieldTex> g_field_tex;

static std::string inject_sky(const char *src) {
  std::string s(src);
  auto sub = [&](const char *tag, const char *body) {
    size_t p = s.find(tag);
    if (p != std::string::npos) s.replace(p, strlen(tag), body);
  };
  sub("FRACTAL_FN_PLACEHOLDER", FRACTAL_FN);
  sub("SKY_FN_PLACEHOLDER", SKY_FN);
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

static GLuint compile(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader error: %s\n", log);
  }
  return sh;
}

static GLuint link_prog(const char *vs, const char *fs) {
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
static GLuint link_checked(const std::string &vs, const std::string &fs,
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
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

static void mat_mul(float *o, const float *a, const float *b) {
  float r[16];
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      r[i * 4 + j] = 0;
      for (int k = 0; k < 4; ++k) r[i * 4 + j] += a[k * 4 + j] * b[i * 4 + k];
    }
  for (int i = 0; i < 16; ++i) o[i] = r[i];
}

static bool mat_inverse(float *out, const float *m);

// active photographic grading (set per-frame from the active camera)
static float g_grade[3] = {1.f, 1.f, 1.f};
static float g_saturation = 1.f;
static float g_exposure_mult = 1.f;

void renderer_set_film(const float tint[3], float saturation, float exposure_mult) {
  g_grade[0] = tint[0];
  g_grade[1] = tint[1];
  g_grade[2] = tint[2];
  g_saturation = saturation;
  g_exposure_mult = exposure_mult;
}

static void uni3(GLuint prog, const char *name, const float *v) {
  glUniform3fv(glGetUniformLocation(prog, name), 1, v);
}
static void uni1(GLuint prog, const char *name, float v) {
  glUniform1f(glGetUniformLocation(prog, name), v);
}
static void unii(GLuint prog, const char *name, int v) {
  glUniform1i(glGetUniformLocation(prog, name), v);
}

static void make_sphere() {
  std::vector<float> v;
  const int RINGS = 12, SECT = 18;
  auto pt = [&](int r, int s, float *o) {
    float phi = float(r) / RINGS * 3.14159265f;
    float th = float(s) / SECT * 6.2831853f;
    o[0] = std::sin(phi) * std::cos(th);
    o[1] = std::cos(phi);
    o[2] = std::sin(phi) * std::sin(th);
  };
  for (int r = 0; r < RINGS; ++r)
    for (int s = 0; s < SECT; ++s) {
      float a[3], b[3], c[3], d[3];
      pt(r, s, a); pt(r + 1, s, b); pt(r + 1, s + 1, c); pt(r, s + 1, d);
      const float *tri[6] = {a, b, c, a, c, d};
      for (int i = 0; i < 6; ++i)
        v.insert(v.end(), {tri[i][0], tri[i][1], tri[i][2], tri[i][0], tri[i][1],
                           tri[i][2]});
    }
  sphere_verts = (int)(v.size() / 6);
  glGenVertexArrays(1, &vao_sphere);
  glBindVertexArray(vao_sphere);
  glGenBuffers(1, &vbo_sphere);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_sphere);
  glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
  glBindVertexArray(0);
}

static void upload_prev_mesh(int slot, const std::vector<float> &v) {
  prev_verts[slot] = (int)(v.size() / 8);
  glGenVertexArrays(1, &prev_vao[slot]);
  glBindVertexArray(prev_vao[slot]);
  glGenBuffers(1, &prev_vbo[slot]);
  glBindBuffer(GL_ARRAY_BUFFER, prev_vbo[slot]);
  glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);
  glBindVertexArray(0);
}

static void make_preview_shapes() {
  // sphere with proper spherical UVs
  {
    std::vector<float> v;
    const int RINGS = 24, SECT = 36;
    auto emit = [&](int r, int s) {
      float phi = float(r) / RINGS * 3.14159265f;
      float th = float(s) / SECT * 6.2831853f;
      float x = std::sin(phi) * std::cos(th);
      float y = std::cos(phi);
      float z = std::sin(phi) * std::sin(th);
      v.insert(v.end(), {x, y, z, x, y, z, float(s) / SECT * 3.f,
                         float(r) / RINGS * 1.5f});
    };
    for (int r = 0; r < RINGS; ++r)
      for (int s = 0; s < SECT; ++s) {
        emit(r, s); emit(r + 1, s); emit(r + 1, s + 1);
        emit(r, s); emit(r + 1, s + 1); emit(r, s + 1);
      }
    upload_prev_mesh(0, v);
  }
  // cube with planar per-face UVs
  {
    std::vector<float> v;
    const float N[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0},
                           {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
      const float *n = N[f];
      float t[3] = {n[1], n[2], n[0]}; // any perpendicular
      float b[3] = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2],
                    n[0] * t[1] - n[1] * t[0]};
      const float S = 0.72f;
      auto corner = [&](float u, float w, float *o) {
        for (int k = 0; k < 3; ++k)
          o[k] = (n[k] + t[k] * u + b[k] * w) * S;
      };
      float c[4][3];
      corner(-1, -1, c[0]); corner(1, -1, c[1]);
      corner(1, 1, c[2]); corner(-1, 1, c[3]);
      const int idx[6] = {0, 1, 2, 0, 2, 3};
      const float uv[4][2] = {{0, 0}, {1.2f, 0}, {1.2f, 1.2f}, {0, 1.2f}};
      for (int i : idx)
        v.insert(v.end(), {c[i][0], c[i][1], c[i][2], n[0], n[1], n[2],
                           uv[i][0], uv[i][1]});
    }
    upload_prev_mesh(1, v);
  }
  // flat: a full-frame quad facing the camera
  {
    std::vector<float> v;
    const float Q[4][2] = {{-1.05f, -1.05f}, {1.05f, -1.05f},
                           {1.05f, 1.05f}, {-1.05f, 1.05f}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i : idx)
      v.insert(v.end(), {Q[i][0], Q[i][1], 0.f, 0.f, 0.f, 1.f,
                         Q[i][0] * 0.5f + 0.5f, Q[i][1] * 0.5f + 0.5f});
    upload_prev_mesh(2, v);
  }
}

// Rebuild the terrain program around the current generated displacement.
// Called at init and again whenever the graph's displacement changes — the
// only shader in the codebase that is not built once and left alone.
//
// The old program is kept until the new one links. A graph that produces
// invalid GLSL therefore leaves the viewport exactly as it was rather than
// turning it black, which is the difference between a visible mistake and an
// apparently broken application.
// The plain vertex shader and the tessellation evaluation shader are the same
// body with a different way of arriving at a uv, so they are assembled from
// one string rather than kept in step by hand.
static std::string terrain_vs_source() {
  return std::string("#version 430 core\n") + TERRAIN_VERT_COMMON +
         VS_TERRAIN_TAIL;
}
static std::string terrain_tes_source() {
  return std::string("#version 430 core\n") + TERRAIN_VERT_COMMON +
         TES_TERRAIN_TAIL;
}

// Link a four-stage program. Tessellation is the one place we need more than
// a vertex and a fragment shader.
static GLuint link_checked_tess(const std::string &vs, const std::string &tcs,
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
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

static bool rebuild_terrain_program(std::string &err) {
  std::string fs = inject_sky(FS_TERRAIN_SRC);
  GLuint p = link_checked(inject_sky(terrain_vs_source().c_str()), fs, err);
  if (!p) return false;
  if (prog_terrain) glDeleteProgram(prog_terrain);
  prog_terrain = p;

  // The shadow pass carries the same displacement, so the terrain does not
  // cast a shadow from where it used to be.
  std::string derr;
  if (GLuint d = link_checked(inject_sky(VS_DEPTH_SRC), FS_DEPTH, derr)) {
    if (prog_depth) glDeleteProgram(prog_depth);
    prog_depth = d;
  } else {
    std::fprintf(stderr, "shadow shader:\n%s\n", derr.c_str());
  }

  // The tessellated program is an optimisation, not a requirement: if it does
  // not build we keep the fixed grid and say so, rather than losing the
  // terrain entirely. Everything below this point is allowed to fail.
  std::string terr;
  GLuint t = link_checked_tess(VS_TERRAIN_PASS, TCS_TERRAIN,
                               inject_sky(terrain_tes_source().c_str()), fs,
                               terr);
  if (t) {
    if (prog_terrain_tess) glDeleteProgram(prog_terrain_tess);
    prog_terrain_tess = t;
    tess_ok = true;
  } else {
    tess_ok = prog_terrain_tess != 0; // keep any program that already worked
    std::fprintf(stderr, "terrain tessellation unavailable:\n%s\n", terr.c_str());
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
static void bind_field_textures(GLuint prog) {
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

// Whether the terrain is actually being subdivided adaptively, and at what
// settings. Worth being able to ask: the fixed grid is a silent fallback, and
// a silent fallback that nobody can see is indistinguishable from a feature
// that does not work.
std::string renderer_tess_status() {
  const RenderSettings &RS = render_settings();
  if (!prog_terrain_tess)
    return "adaptive subdivision: unavailable (tessellation program did not "
           "link; using the fixed grid)";
  if (!RS.tessellation)
    return "adaptive subdivision: available but switched off";
  char buf[224];
  std::snprintf(buf, sizeof buf,
                "adaptive subdivision: ON, %dx%d patches, %.0fx to %.0fx an "
                "edge (effective %d to %d across), target %.0f px/edge",
                patch_n - 1, patch_n - 1, RS.tess_min, RS.tess_max,
                (int)((patch_n - 1) * RS.tess_min),
                (int)((patch_n - 1) * RS.tess_max), RS.tess_pixels);
  return buf;
}

bool renderer_init() {
  std::string fs_terrain = inject_sky(FS_TERRAIN_SRC);
  std::string fs_sky = inject_sky(FS_SKY_SRC);
  std::string fs_water = inject_sky(FS_WATER);
  {
    // builds prog_terrain and, if the driver takes it, prog_terrain_tess
    std::string terr;
    rebuild_terrain_program(terr);
  }
  prog_water = link_prog(VS_WATER, fs_water.c_str());
  prog_sky = link_prog(VS_SKY, fs_sky.c_str());
  prog_lines = link_prog(VS_LINES, FS_LINES);
  prog_bg = link_prog(VS_BG, FS_BG);
  prog_mesh = link_prog(VS_MESH, FS_MESH);
  prog_gizmo = link_prog(VS_GIZMO, FS_GIZMO);
  prog_matprev = link_prog(VS_MATPREV, FS_MATPREV);
  make_preview_shapes();
  planet_renderer_init();

  // terrain grid
  std::vector<float> verts;
  verts.reserve((size_t)grid_n * grid_n * 2);
  for (int y = 0; y < grid_n; ++y)
    for (int x = 0; x < grid_n; ++x) {
      verts.push_back(x / float(grid_n - 1));
      verts.push_back(y / float(grid_n - 1));
    }
  std::vector<unsigned> idx;
  idx.reserve((size_t)(grid_n - 1) * (grid_n - 1) * 6);
  for (int y = 0; y < grid_n - 1; ++y)
    for (int x = 0; x < grid_n - 1; ++x) {
      unsigned i = y * grid_n + x;
      idx.insert(idx.end(), {i, i + (unsigned)grid_n, i + 1, i + 1,
                             i + (unsigned)grid_n, i + (unsigned)grid_n + 1});
    }
  index_count = (int)idx.size();
  glGenVertexArrays(1, &vao_grid);
  glBindVertexArray(vao_grid);
  glGenBuffers(1, &vbo_grid);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &ebo_grid);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_grid);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
  glBindVertexArray(0);

  // The coarse patch grid the tessellator subdivides. Four indices per quad,
  // corners counter-clockwise starting at the origin, which is the order the
  // evaluation shader's bilinear interpolation assumes.
  {
    std::vector<float> pv;
    pv.reserve((size_t)patch_n * patch_n * 2);
    for (int y = 0; y < patch_n; ++y)
      for (int x = 0; x < patch_n; ++x) {
        pv.push_back(x / float(patch_n - 1));
        pv.push_back(y / float(patch_n - 1));
      }
    std::vector<unsigned> pi;
    pi.reserve((size_t)(patch_n - 1) * (patch_n - 1) * 4);
    for (int y = 0; y < patch_n - 1; ++y)
      for (int x = 0; x < patch_n - 1; ++x) {
        unsigned i = y * patch_n + x;
        pi.insert(pi.end(), {i, i + 1, i + 1 + (unsigned)patch_n,
                             i + (unsigned)patch_n});
      }
    patch_index_count = (int)pi.size();
    glGenVertexArrays(1, &vao_patch);
    glBindVertexArray(vao_patch);
    glGenBuffers(1, &vbo_patch);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_patch);
    glBufferData(GL_ARRAY_BUFFER, pv.size() * 4, pv.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_patch);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_patch);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, pi.size() * 4, pi.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindVertexArray(0);
  }
  glGenVertexArrays(1, &vao_quad);

  // ground grid lines
  {
    std::vector<float> lv;
    const int DIV = 20;
    const float y = 0.0005f;
    for (int i = 0; i <= DIV; ++i) {
      float t = i / float(DIV);
      lv.insert(lv.end(), {t, y, 0.f, t, y, 1.f});
      lv.insert(lv.end(), {0.f, y, t, 1.f, y, t});
    }
    line_vert_count = (int)lv.size() / 3;
    glGenVertexArrays(1, &vao_lines);
    glBindVertexArray(vao_lines);
    glGenBuffers(1, &vbo_lines);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_lines);
    glBufferData(GL_ARRAY_BUFFER, lv.size() * 4, lv.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    glBindVertexArray(0);
  }
  // dynamic outline buffer
  glGenVertexArrays(1, &vao_dyn);
  glBindVertexArray(vao_dyn);
  glGenBuffers(1, &vbo_dyn);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
  glBufferData(GL_ARRAY_BUFFER, 256 * 3 * 4, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
  glBindVertexArray(0);
  make_sphere();

  auto mktex = [](GLuint &t, bool mips) {
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  mktex(tex_height, false);
  mktex(tex_albedo, true);
  mktex(tex_normal, true);
  mktex(tex_rough, true);
  mktex(tex_disp, false);

  cloud_noise_build(tex_cloud_shape, tex_cloud_detail);

  // shadow map
  glGenTextures(1, &shadow_tex);
  glBindTexture(GL_TEXTURE_2D, shadow_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float border[4] = {1, 1, 1, 1};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &shadow_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         shadow_tex, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void renderer_shutdown() {
  glDeleteProgram(prog_terrain);
  glDeleteProgram(prog_water);
  glDeleteProgram(prog_sky);
  glDeleteProgram(prog_depth);
  glDeleteProgram(prog_lines);
  glDeleteProgram(prog_bg);
  glDeleteProgram(prog_mesh);
  glDeleteProgram(prog_gizmo);
}

void renderer_set_terrain(const gpx::Heightmap &h, const gpx::TextureRGBA *albedo) {
  // Upload what the graph produced, at the height it produced it.
  //
  // This used to stretch every heightmap to fill 0..1, which quietly threw
  // away absolute altitude: a terrain spanning 0.4-0.5 displayed identically
  // to one spanning 0-1, so sea level, the snow line and every altitude-keyed
  // material meant nothing. Flat ground was worse still — min equals max, so
  // the whole tile collapsed to zero and sank under the sea, which made a flat
  // planet surface impossible to show at all.
  //
  // Normalising is the graph's job and it is already exposed there, on by
  // default: TerrainOutput's "Remap to range" and every node's own output
  // block. The renderer's business is to show what it is handed.
  gpx::Heightmap norm = h;
  hm_w = norm.w;
  // the level the surrounding ground should settle to, so the two meet
  double sum = 0;
  for (float v : norm.v) sum += v;
  g_terrain_mean = norm.v.empty() ? 0.f : (float)(sum / norm.v.size());
  glBindTexture(GL_TEXTURE_2D, tex_height);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, norm.w, norm.h, 0, GL_RED, GL_FLOAT,
               norm.v.data());
  // keep a CPU copy (downsampled) for picking
  cpu_height = norm.w > 256 ? norm.resampled(256, 256) : norm;
  has_albedo = albedo && !albedo->empty();
  if (has_albedo) {
    glBindTexture(GL_TEXTURE_2D, tex_albedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, albedo->w, albedo->h, 0, GL_RGBA,
                 GL_FLOAT, albedo->v.data());
    glGenerateMipmap(GL_TEXTURE_2D);
  }
}

// Uploading three textures every frame was costing far more than the whole
// rest of the frame; only re-upload when the source data actually changed.
void renderer_set_material_maps(const void *normal, const void *roughness,
                                const void *displacement, unsigned long long version) {
  static unsigned long long last_version = ~0ull;
  static const void *last_ptrs[3] = {nullptr, nullptr, nullptr};
  const void *ptrs[3] = {normal, roughness, displacement};
  if (version == last_version && ptrs[0] == last_ptrs[0] &&
      ptrs[1] == last_ptrs[1] && ptrs[2] == last_ptrs[2])
    return;
  last_version = version;
  for (int i = 0; i < 3; ++i) last_ptrs[i] = ptrs[i];

  auto up = [](GLuint tex, const gpx::TextureRGBA *t, bool mips, bool &flag) {
    flag = t && !t->empty();
    if (!flag) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA, GL_FLOAT,
                 t->v.data());
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
  };
  up(tex_normal, (const gpx::TextureRGBA *)normal, true, has_normal_map);
  up(tex_rough, (const gpx::TextureRGBA *)roughness, true, has_rough_map);
  up(tex_disp, (const gpx::TextureRGBA *)displacement, false, has_disp_map);
}

// Drives whichever camera is active. Scene cameras store an explicit
// eye/target, so orbit/pan/dolly operate on that pair directly.
static bool camera_object_input(float dx, float dy, float wheel, bool rotating,
                                bool panning, bool dolly) {
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (active < 0 || active >= (int)sc.objects.size() ||
      sc.objects[active].type != SceneObject::Camera)
    return false;
  CameraData &cd = sc.objects[active].cam;
  float d[3] = {cd.eye[0] - cd.target[0], cd.eye[1] - cd.target[1],
                cd.eye[2] - cd.target[2]};
  float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (dist < 1e-5f) dist = 1e-5f;
  float yaw = std::atan2(d[0], d[2]);
  float pitch = std::asin(std::clamp(d[1] / dist, -1.f, 1.f));
  if (rotating) {
    yaw += dx * 0.01f;
    pitch = std::clamp(pitch + dy * 0.01f, -1.55f, 1.55f);
  }
  if (wheel != 0.f) dist = std::clamp(dist * (1.f - wheel * 0.12f), 0.0004f, 400.f);
  if (dolly) dist = std::clamp(dist * (1.f + dy * 0.005f), 0.0004f, 400.f);
  if (panning) {
    // pan moves eye and target together, across the view plane
    float s = dist * 0.0015f;
    float cy = std::cos(yaw), sy = std::sin(yaw);
    float mx = (-dx * cy - dy * sy) * s, mz = (dx * sy - dy * cy) * s;
    cd.target[0] += mx;
    cd.target[2] += mz;
  }
  float cp = std::cos(pitch);
  cd.eye[0] = cd.target[0] + dist * cp * std::sin(yaw);
  cd.eye[1] = cd.target[1] + dist * std::sin(pitch);
  cd.eye[2] = cd.target[2] + dist * cp * std::cos(yaw);
  return true;
}

void renderer_camera_input(float dx, float dy, float wheel, bool rotating,
                           bool panning, bool dolly) {
  if (camera_object_input(dx, dy, wheel, rotating, panning, dolly)) return;
  if (dolly)
    CAM.dist = std::fmin(
        std::fmax(CAM.dist * (1.f + dy * 0.005f), 0.0004f), 100000.f);
  renderer_handle_input(dx, dy, wheel, rotating, panning);
}

// point the active camera at a world position, keeping its distance
void renderer_camera_look_at(const float target[3], float distance) {
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (active >= 0 && active < (int)sc.objects.size() &&
      sc.objects[active].type == SceneObject::Camera) {
    CameraData &cd = sc.objects[active].cam;
    float d[3] = {cd.eye[0] - cd.target[0], cd.eye[1] - cd.target[1],
                  cd.eye[2] - cd.target[2]};
    float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (distance > 0) dist = distance;
    if (dist < 1e-4f) dist = 1.f;
    float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-5f) { d[0] = 0; d[1] = 0.4f; d[2] = 1.f; len = 1.077f; }
    for (int i = 0; i < 3; ++i) {
      cd.target[i] = target[i];
      cd.eye[i] = target[i] + d[i] / len * dist;
    }
    return;
  }
  for (int i = 0; i < 3; ++i) CAM.target[i] = target[i];
  if (distance > 0) CAM.dist = distance;
}

void renderer_handle_input(float dx, float dy, float wheel, bool rotating,
                           bool panning) {
  if (rotating) {
    CAM.yaw += dx * 0.01f; // unbounded: full 360Â° orbit
    CAM.pitch = std::fmin(std::fmax(CAM.pitch + dy * 0.01f, -1.55f), 1.55f);
  }
  if (panning) {
    float s = CAM.dist * 0.0015f;
    float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
    CAM.target[0] += (-dx * cy - dy * sy) * s;
    CAM.target[2] += (dx * sy - dy * cy) * s;
  }
  // zoom range spans a grain of sand to a whole planetary neighbourhood
  if (wheel != 0)
    CAM.dist = std::fmin(
        std::fmax(CAM.dist * (1.f - wheel * 0.12f), 0.0004f), 100000.f);
}

static void build_light_mvp(const float *sun, float hscale, float *out) {
  float cx = 0.5f, cy = hscale * 0.5f, cz = 0.5f;
  float eye[3] = {cx + sun[0] * 2.f, cy + sun[1] * 2.f, cz + sun[2] * 2.f};
  float fz[3] = {cx - eye[0], cy - eye[1], cz - eye[2]};
  float fl = std::sqrt(fz[0] * fz[0] + fz[1] * fz[1] + fz[2] * fz[2]);
  for (float &v : fz) v /= fl;
  float upw[3] = {0, 1, 0};
  if (std::fabs(fz[1]) > 0.99f) { upw[0] = 1; upw[1] = 0; }
  float sx[3] = {fz[1] * upw[2] - fz[2] * upw[1], fz[2] * upw[0] - fz[0] * upw[2],
                 fz[0] * upw[1] - fz[1] * upw[0]};
  float sl = std::sqrt(sx[0] * sx[0] + sx[1] * sx[1] + sx[2] * sx[2]);
  for (float &v : sx) v /= sl;
  float uy[3] = {sx[1] * fz[2] - sx[2] * fz[1], sx[2] * fz[0] - sx[0] * fz[2],
                 sx[0] * fz[1] - sx[1] * fz[0]};
  float view[16] = {sx[0], uy[0], -fz[0], 0, sx[1], uy[1], -fz[1], 0,
                    sx[2], uy[2], -fz[2], 0,
                    -(sx[0] * eye[0] + sx[1] * eye[1] + sx[2] * eye[2]),
                    -(uy[0] * eye[0] + uy[1] * eye[1] + uy[2] * eye[2]),
                    fz[0] * eye[0] + fz[1] * eye[1] + fz[2] * eye[2], 1};
  float r = 0.95f, znear = 0.1f, zfar = 4.5f;
  float proj[16] = {1.f / r, 0, 0, 0, 0, 1.f / r, 0, 0,
                    0, 0, -2.f / (zfar - znear), 0,
                    0, 0, -(zfar + znear) / (zfar - znear), 1};
  mat_mul(out, proj, view);
}

static void draw_box_outline(const float *mvp, float x0, float y0, float z0,
                             float x1, float y1, float z1, const float *rgba) {
  float c[8][3] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},
                   {x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}};
  static const int E[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                               {0,4},{1,5},{2,6},{3,7}};
  std::vector<float> v;
  for (auto &e : E) {
    for (int k = 0; k < 3; ++k) v.push_back(c[e[0]][k]);
    for (int k = 0; k < 3; ++k) v.push_back(c[e[1]][k]);
  }
  glBindVertexArray(vao_dyn);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
  glBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * 4, v.data());
  glUseProgram(prog_lines);
  glUniformMatrix4fv(glGetUniformLocation(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
  glUniform4fv(glGetUniformLocation(prog_lines, "u_color"), 1, rgba);
  glLineWidth(2.f);
  glDrawArrays(GL_LINES, 0, (int)(v.size() / 3));
  glLineWidth(1.f);
}

static void draw_scene(int slot, const RenderSettings::ViewConfig &vc, int w,
                       int h, float time_acc, const float *view_eye,
                       const float *mvp, const float *inv_vp) {
  RenderSettings &RS = render_settings();
  float sun[3];
  compute_sun_dir(RS, sun);
  bool atmosphere = vc.atmosphere;
  bool textured = vc.display == 2;
  bool wireframe = vc.display == 0 || RS.wireframe;
  bool cinematic = RS.viewport_engine == 1 && vc.camera == 0;

  SceneState &sc = scene();
  bool show_terrain_obj = true, show_water_obj = true, sun_on = true;
  int sel_type = -1;
  for (size_t i = 0; i < sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    bool vis = sc.object_visible(o);
    if (o.type == SceneObject::Terrain) show_terrain_obj = vis;
    else if (o.type == SceneObject::Water) show_water_obj = vis;
    else if (o.type == SceneObject::Sun) sun_on = vis;
    else if (o.type == SceneObject::Atmosphere) atmosphere = atmosphere && vis;
    if ((int)i == sc.selected) sel_type = o.type;
  }
  float sun_intensity = sun_on ? RS.sun_intensity : 0.05f;

  // ---- progressive quality: features that only matter near the ground are
  // shut off as the camera pulls away, with hysteresis so nothing flickers
  // at the threshold. This is where the frame budget for planets comes from.
  float tile_dx = view_eye[0] - 0.5f, tile_dz = view_eye[2] - 0.5f;
  float tile_dist = std::sqrt(tile_dx * tile_dx + view_eye[1] * view_eye[1] +
                              tile_dz * tile_dz);
  static bool far_tier[6] = {false};
  if (!far_tier[slot] && tile_dist > 9.f) far_tier[slot] = true;
  else if (far_tier[slot] && tile_dist < 7.f) far_tier[slot] = false;
  bool near_ground = !far_tier[slot];
  // volumetric clouds are a ground-view effect; from high above they cost a
  // full raymarch for a few pixels
  bool clouds_ok = RS.clouds_on && view_eye[1] < 3.f && near_ground;
  bool shadows_ok = RS.shadows && near_ground; // shadow texels vanish out there
  bool heavy_maps = near_ground; // 4K material maps are wasted on a far tile
  // how far out of the atmosphere the camera is (0 ground .. 1 open space);
  // smooth, so the sky thins continuously as you pull back
  float space_t = std::clamp((tile_dist - 6.f) / 22.f, 0.f, 1.f);
  space_t = space_t * space_t * (3.f - 2.f * space_t);

  // shadow pass
  float light_mvp[16];
  build_light_mvp(sun, RS.height_scale, light_mvp);
  if (shadows_ok) {
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(prog_depth);
    glUniformMatrix4fv(glGetUniformLocation(prog_depth, "u_light_mvp"), 1, GL_FALSE,
                       light_mvp);
    uni1(prog_depth, "u_hscale", RS.height_scale);
    uni1(prog_depth, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    bind_field_textures(prog_depth);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_height);
    unii(prog_depth, "u_height", 0);
    glBindVertexArray(vao_grid);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, fbo[slot]);
  glViewport(0, 0, w, h);
  glClearColor(RS.bg_color[0], RS.bg_color[1], RS.bg_color[2], 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  float wind_rad = RS.cloud_wind_dir * 0.017453293f;
  float wind[2] = {std::cos(wind_rad) * RS.cloud_wind_speed,
                   std::sin(wind_rad) * RS.cloud_wind_speed};

  // background
  if (atmosphere && RS.background_mode == 0) {
    glUseProgram(prog_sky);
    glDepthMask(GL_FALSE);
    glUniformMatrix4fv(glGetUniformLocation(prog_sky, "u_inv_vp"), 1, GL_FALSE,
                       inv_vp);
    uni3(prog_sky, "u_cam", view_eye);
    uni3(prog_sky, "u_sun", sun);
    uni3(prog_sky, "u_sun_color", RS.sun_color);
    uni1(prog_sky, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_sky, "u_grade", g_grade);
    uni1(prog_sky, "u_sat", g_saturation);
    uni3(prog_sky, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_sky, "u_sky_horizon", RS.sky_horizon);
    uni1(prog_sky, "u_atmo", RS.atmosphere_density);
    unii(prog_sky, "u_fog_type", RS.fog_type);
    uni3(prog_sky, "u_fog_color", RS.fog_color);
    uni1(prog_sky, "u_fog_density", RS.fog_density);
    // clouds
    unii(prog_sky, "u_clouds", clouds_ok ? 1 : 0);
    int steps = RS.cloud_quality == 0 ? 24 : (RS.cloud_quality == 2 ? 72 : 44);
    if (cinematic) steps = (int)(steps * 1.5f);
    unii(prog_sky, "u_cl_steps", steps);
    unii(prog_sky, "u_cl_type", RS.cloud_type);
    uni1(prog_sky, "u_sun_intensity", sun_intensity);
    unii(prog_sky, "u_panorama", 0);
    unii(prog_sky, "u_hdr", 0);
    unii(prog_sky, "u_no_sun", 0);
    uni1(prog_sky, "u_space", vc.camera == 0 ? space_t : 0.f);
    uni1(prog_sky, "u_cl_cov", RS.cloud_coverage);
    uni1(prog_sky, "u_cl_den", RS.cloud_density);
    uni1(prog_sky, "u_cl_alt", RS.cloud_altitude);
    uni1(prog_sky, "u_cl_thick", RS.cloud_thickness);
    uni1(prog_sky, "u_cl_detail_amt", RS.cloud_detail);
    uni1(prog_sky, "u_cl_time", cloud_time);
    uni1(prog_sky, "u_cl_ambient", RS.cloud_ambient);
    uni1(prog_sky, "u_cl_anvil", RS.cloud_anvil);
    glUniform2fv(glGetUniformLocation(prog_sky, "u_cl_wind"), 1, wind);
    uni3(prog_sky, "u_cl_color", RS.cloud_color);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
    unii(prog_sky, "u_cl_shape", 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_detail);
    unii(prog_sky, "u_cl_detail", 4);
    glBindVertexArray(vao_quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
  } else if (RS.background_mode == 1) {
    glUseProgram(prog_bg);
    glDepthMask(GL_FALSE);
    uni3(prog_bg, "u_top", RS.bg_color);
    uni3(prog_bg, "u_bottom", RS.bg_color2);
    glBindVertexArray(vao_quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
  }

  // planets: drawn between the sky and the ground, so terrain in front of
  // them occludes correctly and they are never clipped however far you zoom
  if (vc.camera == 0) {
    PlanetFrame pf;
    pf.mvp = mvp;
    pf.eye = view_eye;
    pf.sun = sun;
    pf.sun_intensity = sun_intensity;
    pf.exposure = RS.exposure * g_exposure_mult;
    pf.grade = g_grade;
    pf.saturation = g_saturation;
    pf.view_h = h;
    pf.fovy_rad = g_last_fovy;
    planet_draw_all(pf);
  }

  // terrain
  if (show_terrain_obj) {
    // Adaptive subdivision when the driver took the tessellated program and
    // the user has not turned it off; the fixed grid is always there as the
    // fallback, and both run the same placement code.
    const bool use_tess = tess_ok && RS.tessellation && prog_terrain_tess;
    const GLuint PT = use_tess ? prog_terrain_tess : prog_terrain;
    glUseProgram(PT);
    glUniformMatrix4fv(glGetUniformLocation(PT, "u_mvp"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(glGetUniformLocation(PT, "u_light_mvp"), 1, GL_FALSE,
                       light_mvp);
    uni1(PT, "u_hscale", RS.height_scale);
    uni3(PT, "u_sun", sun);
    uni3(PT, "u_sun_color", RS.sun_color);
    uni1(PT, "u_sun_intensity", sun_intensity);
    uni3(PT, "u_sky_zenith", RS.sky_zenith);
    uni3(PT, "u_sky_horizon", RS.sky_horizon);
    uni1(PT, "u_ambient", RS.ambient_intensity);
    uni1(PT, "u_atmo", RS.atmosphere_density);
    uni3(PT, "u_cam", view_eye);
    uni1(PT, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(PT, "u_grade", g_grade);
    uni1(PT, "u_sat", g_saturation);
    uni1(PT, "u_texel", hm_w > 0 ? 1.f / hm_w : 1.f / 512.f);
    unii(PT, "u_has_albedo", (has_albedo && RS.use_albedo && textured) ? 1 : 0);
    unii(PT, "u_has_normal", (has_normal_map && textured && heavy_maps) ? 1 : 0);
    unii(PT, "u_has_rough", (has_rough_map && textured && heavy_maps) ? 1 : 0);
    unii(PT, "u_has_disp", (has_disp_map && RS.mat_displacement > 0) ? 1 : 0);
    uni1(PT, "u_disp_strength", RS.mat_displacement);
    uni1(PT, "u_frac_amount", RS.fractal_detail);
    uni1(PT, "u_frac_scale", RS.fractal_scale);
    // zero when no graph is driving displacement, which also short-circuits
    // the stub call in both stages
    uni1(PT, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    unii(PT, "u_surface_on", g_surface_glsl.empty() ? 0 : 1);
    unii(PT, "u_surf_rough_on", g_rough_glsl.empty() ? 0 : 1);
    unii(PT, "u_surf_bump_on", g_bump_glsl.empty() ? 0 : 1);
    uni1(PT, "u_surf_bump_strength", g_surf_bump_strength);
    uni1(PT, "u_surf_bump_scale", g_surf_bump_scale);
    glUniform4f(glGetUniformLocation(PT, "u_brush"), g_brush[0],
                g_brush[1], g_brush[2], g_brush[3]);
    uni1(PT, "u_planet_radius", RS.planet_radius);
    unii(PT, "u_shadows", (shadows_ok && vc.display != 0) ? 1 : 0);
    unii(PT, "u_quality", cinematic ? 1 : 0);
    uni1(PT, "u_shadow_soft", RS.shadow_softness);
    uni1(PT, "u_roughness", RS.mat_roughness);
    uni1(PT, "u_metallic", RS.mat_metallic);
    uni1(PT, "u_specular", RS.mat_specular);
    uni1(PT, "u_reflection", RS.mat_reflection);
    uni1(PT, "u_translucency", RS.mat_translucency);
    uni1(PT, "u_transparency", RS.mat_transparency);
    uni1(PT, "u_normal_strength", RS.mat_normal_strength);
    unii(PT, "u_fog_type", atmosphere ? RS.fog_type : 0);
    uni1(PT, "u_fog_density", RS.fog_density);
    uni1(PT, "u_fog_level", RS.fog_level);
    uni1(PT, "u_fog_falloff", RS.fog_falloff);
    uni3(PT, "u_fog_color", RS.fog_color);
    uni3(PT, "u_absorb", RS.absorption_color);
    uni1(PT, "u_fog_scatter", RS.fog_sun_scatter);
    unii(PT, "u_cloud_shadows",
         (clouds_ok && atmosphere && cinematic) ? 1 : 0);
    uni1(PT, "u_cl_cov", RS.cloud_coverage);
    uni1(PT, "u_cl_alt", RS.cloud_altitude);
    uni1(PT, "u_cl_time", cloud_time);
    glUniform2fv(glGetUniformLocation(PT, "u_cl_wind"), 1, wind);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_height);
    unii(PT, "u_height", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_albedo);
    unii(PT, "u_albedo", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    unii(PT, "u_shadowmap", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
    unii(PT, "u_cl_shape", 3);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, tex_normal);
    unii(PT, "u_normal_map", 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, tex_rough);
    unii(PT, "u_rough_map", 6);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, tex_disp);
    unii(PT, "u_disp", 7);
    if (RS.mat_transparency > 0.001f) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    bind_field_textures(PT);
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (use_tess) {
      float vp[2] = {(float)w, (float)h};
      glUniform2fv(glGetUniformLocation(PT, "u_viewport"), 1, vp);
      uni1(PT, "u_tess_px", RS.tess_pixels);
      uni1(PT, "u_tess_min", RS.tess_min);
      uni1(PT, "u_tess_max", RS.tess_max);
      glPatchParameteri(GL_PATCH_VERTICES, 4);
      glBindVertexArray(vao_patch);
      glDrawElements(GL_PATCHES, patch_index_count, GL_UNSIGNED_INT, nullptr);
    } else {
      glBindVertexArray(vao_grid);
      glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    }
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (RS.mat_transparency > 0.001f) glDisable(GL_BLEND);
  }

  // infinite ground: the tile continues procedurally to the horizon when
  // root-level InfiniteSurface layers exist (drawn after the tile so its
  // depth wins in the overlap band; the surround discards inside the tile)
  if (vc.camera == 0 && show_terrain_obj && infinite_layers_present()) {
    InfiniteFrame inf;
    inf.mvp = mvp;
    inf.eye = view_eye;
    inf.sun = sun;
    inf.sun_color = RS.sun_color;
    inf.sun_intensity = sun_intensity;
    inf.exposure = RS.exposure * g_exposure_mult;
    inf.grade = g_grade;
    inf.saturation = g_saturation;
    inf.ambient = RS.ambient_intensity;
    inf.sky_zenith = RS.sky_zenith;
    inf.sky_horizon = RS.sky_horizon;
    inf.tex_height = tex_height;
    inf.tex_albedo = (has_albedo && RS.use_albedo && textured) ? tex_albedo : 0;
    inf.height_scale = RS.height_scale;
    inf.base_height = g_terrain_mean * RS.height_scale;
    inf.planet_radius = RS.planet_radius;
    inf.fog_type = atmosphere ? RS.fog_type : 0;
    inf.fog_density = RS.fog_density;
    inf.fog_color = RS.fog_color;
    infinite_draw(inf);
  }

  // scene meshes
  for (SceneObject &o : sc.objects) {
    if (o.type != SceneObject::Mesh || !sc.object_visible(o)) continue;
    if (o.gpu_dirty) {
      if (!o.vao) {
        glGenVertexArrays(1, &o.vao);
        glGenBuffers(1, &o.vbo);
      }
      glBindVertexArray(o.vao);
      glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
      glBufferData(GL_ARRAY_BUFFER, o.verts.size() * 4, o.verts.data(),
                   GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
      glBindVertexArray(0);
      o.gpu_dirty = false;
    }
    bool is_sel = (&o - sc.objects.data()) == sc.selected;
    glUseProgram(prog_mesh);
    glUniformMatrix4fv(glGetUniformLocation(prog_mesh, "u_mvp"), 1, GL_FALSE, mvp);
    glUniform4f(glGetUniformLocation(prog_mesh, "u_xform"), o.pos[0], o.pos[2],
                o.scale, o.yaw * 0.017453293f);
    uni1(prog_mesh, "u_ybase", o.pos[1] * RS.height_scale);
    uni3(prog_mesh, "u_color", o.color);
    uni3(prog_mesh, "u_sun", sun);
    uni3(prog_mesh, "u_sun_color", RS.sun_color);
    uni1(prog_mesh, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_mesh, "u_grade", g_grade);
    uni1(prog_mesh, "u_sat", g_saturation);
    unii(prog_mesh, "u_selected", is_sel ? 1 : 0);
    glBindVertexArray(o.vao);
    glDrawArrays(GL_TRIANGLES, 0, o.vert_count);
  }

  // sun gizmo (a real, selectable scene object)
  if (sun_on) {
    float gd = 1.9f;
    float gpos[3] = {0.5f + sun[0] * gd, RS.height_scale + sun[1] * gd,
                     0.5f + sun[2] * gd};
    float radius = 0.055f;
    glUseProgram(prog_gizmo);
    glUniformMatrix4fv(glGetUniformLocation(prog_gizmo, "u_mvp"), 1, GL_FALSE, mvp);
    glUniform4f(glGetUniformLocation(prog_gizmo, "u_xform"), gpos[0], gpos[1],
                gpos[2], radius);
    float sun_col[3] = {RS.sun_color[0] * 1.4f, RS.sun_color[1] * 1.3f,
                        RS.sun_color[2] * 0.9f};
    uni3(prog_gizmo, "u_color", sun_col);
    unii(prog_gizmo, "u_selected", sel_type == SceneObject::Sun ? 1 : 0);
    glBindVertexArray(vao_sphere);
    glDrawArrays(GL_TRIANGLES, 0, sphere_verts);
  }

  // reference grid
  if (vc.grid) {
    glUseProgram(prog_lines);
    glUniformMatrix4fv(glGetUniformLocation(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
    glUniform4f(glGetUniformLocation(prog_lines, "u_color"), 0.7f, 0.7f, 0.72f, 0.35f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vao_lines);
    glDrawArrays(GL_LINES, 0, line_vert_count);
    glDisable(GL_BLEND);
  }

  // water
  if (RS.show_water && vc.show_water_view && show_water_obj) {
    glUseProgram(prog_water);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniformMatrix4fv(glGetUniformLocation(prog_water, "u_mvp"), 1, GL_FALSE, mvp);
    uni1(prog_water, "u_hscale", RS.height_scale);
    uni1(prog_water, "u_level", RS.water_level * RS.height_scale);
    uni3(prog_water, "u_sun", sun);
    uni3(prog_water, "u_sun_color", RS.sun_color);
    uni3(prog_water, "u_cam", view_eye);
    uni1(prog_water, "u_time", time_acc);
    uni1(prog_water, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_water, "u_grade", g_grade);
    uni1(prog_water, "u_sat", g_saturation);
    uni3(prog_water, "u_deep", RS.water_deep_color);
    uni3(prog_water, "u_shallow", RS.water_shallow_color);
    uni1(prog_water, "u_wave_amp", RS.water_wave_amp);
    uni1(prog_water, "u_wave_scale", RS.water_wave_scale);
    uni1(prog_water, "u_wave_speed", RS.water_wave_speed);
    uni1(prog_water, "u_clarity", RS.water_clarity);
    uni1(prog_water, "u_opacity", RS.water_opacity);
    uni3(prog_water, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_water, "u_sky_horizon", RS.sky_horizon);
    uni1(prog_water, "u_atmo", RS.atmosphere_density);
    unii(prog_water, "u_foam_on", RS.water_foam ? 1 : 0);
    uni3(prog_water, "u_foam_color", RS.foam_color);
    uni1(prog_water, "u_foam_amount", RS.foam_amount);
    uni1(prog_water, "u_foam_scale", RS.foam_scale);
    uni1(prog_water, "u_foam_crests", RS.foam_crests);
    uni1(prog_water, "u_roughness", RS.mat_roughness * 0.2f);
    uni1(prog_water, "u_reflection", RS.mat_reflection + 0.4f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_height);
    unii(prog_water, "u_height", 0);
    glBindVertexArray(vao_grid);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_BLEND);
  }

  // selection outlines
  if (sel_type >= 0 && vc.outlines) {
    float orange[4] = {1.f, 0.55f, 0.15f, 0.95f};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (sel_type == SceneObject::Terrain) {
      draw_box_outline(mvp, 0.f, 0.f, 0.f, 1.f, RS.height_scale, 1.f, orange);
    } else if (sel_type == SceneObject::Water) {
      float lv = RS.water_level * RS.height_scale;
      draw_box_outline(mvp, 0.f, lv - 0.001f, 0.f, 1.f, lv + 0.001f, 1.f, orange);
    } else if (sel_type == SceneObject::Mesh) {
      const SceneObject &o = sc.objects[sc.selected];
      float r = o.scale * 0.62f;
      draw_box_outline(mvp, o.pos[0] - r, o.pos[1] * RS.height_scale,
                       o.pos[2] - r, o.pos[0] + r,
                       o.pos[1] * RS.height_scale + o.scale, o.pos[2] + r, orange);
    } else if (sel_type == SceneObject::Sun && sun_on) {
      float gd = 1.9f, rr = 0.085f;
      float gx = 0.5f + sun[0] * gd, gy = RS.height_scale + sun[1] * gd,
            gz = 0.5f + sun[2] * gd;
      draw_box_outline(mvp, gx - rr, gy - rr, gz - rr, gx + rr, gy + rr, gz + rr,
                       orange);
    }
    glDisable(GL_BLEND);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void ortho_matrices(const RenderSettings::ViewConfig &vc, int w, int h,
                           float hscale, float *eye, float *mvp, float *inv_vp) {
  float cx = vc.ortho_cx, cy = vc.ortho_cy;
  float sx3[3], uy3[3], fz3[3];
  switch (vc.camera) {
    case 1:
      eye[0] = cx; eye[1] = 3.f; eye[2] = cy;
      fz3[0] = 0; fz3[1] = -1; fz3[2] = 0;
      sx3[0] = 1; sx3[1] = 0; sx3[2] = 0;
      uy3[0] = 0; uy3[1] = 0; uy3[2] = -1;
      break;
    case 2:
      eye[0] = cx; eye[1] = cy * hscale * 2.f; eye[2] = -3.f;
      fz3[0] = 0; fz3[1] = 0; fz3[2] = 1;
      sx3[0] = 1; sx3[1] = 0; sx3[2] = 0;
      uy3[0] = 0; uy3[1] = 1; uy3[2] = 0;
      break;
    default:
      eye[0] = 3.f; eye[1] = cy * hscale * 2.f; eye[2] = cx;
      fz3[0] = -1; fz3[1] = 0; fz3[2] = 0;
      sx3[0] = 0; sx3[1] = 0; sx3[2] = 1;
      uy3[0] = 0; uy3[1] = 1; uy3[2] = 0;
      break;
  }
  float view[16] = {sx3[0], uy3[0], -fz3[0], 0, sx3[1], uy3[1], -fz3[1], 0,
                    sx3[2], uy3[2], -fz3[2], 0,
                    -(sx3[0] * eye[0] + sx3[1] * eye[1] + sx3[2] * eye[2]),
                    -(uy3[0] * eye[0] + uy3[1] * eye[1] + uy3[2] * eye[2]),
                    fz3[0] * eye[0] + fz3[1] * eye[1] + fz3[2] * eye[2], 1};
  float aspect = w / float(h);
  float r = vc.ortho_zoom * 0.5f, znear = 0.01f, zfar = 10.f;
  float proj[16] = {1.f / (r * aspect), 0, 0, 0, 0, 1.f / r, 0, 0,
                    0, 0, -2.f / (zfar - znear), 0,
                    0, 0, -(zfar + znear) / (zfar - znear), 1};
  mat_mul(mvp, proj, view);
  mat_inverse(inv_vp, mvp);
}

// Builds the view/projection for either the free viewport camera or a scene
// camera object (which carries an explicit eye/target and a physical lens).
static void camera_matrices(int w, int h, float *eye, float *mvp, float *inv_vp) {
  float target[3];
  float fovy_rad = 0.9f;
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (active >= 0 && active < (int)sc.objects.size() &&
      sc.objects[active].type == SceneObject::Camera) {
    const CameraData &cd = sc.objects[active].cam;
    for (int i = 0; i < 3; ++i) {
      eye[i] = cd.eye[i];
      target[i] = cd.target[i];
    }
    int nf = 0;
    const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&nf);
    const gpx::cam::SensorFormat &f = F[std::clamp(cd.format, 0, nf - 1)];
    fovy_rad = gpx::cam::fov_y_deg(cd.focal_mm, f.height_mm) * 0.017453293f;
  } else {
    float cp = std::cos(CAM.pitch), sp = std::sin(CAM.pitch);
    float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
    eye[0] = CAM.target[0] + CAM.dist * cp * sy;
    eye[1] = CAM.target[1] + CAM.dist * sp;
    eye[2] = CAM.target[2] + CAM.dist * cp * cy;
    for (int i = 0; i < 3; ++i) target[i] = CAM.target[i];
  }
  float fz[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
  float fl = std::sqrt(fz[0] * fz[0] + fz[1] * fz[1] + fz[2] * fz[2]);
  for (float &v : fz) v /= fl;
  float up[3] = {0, 1, 0};
  if (std::fabs(fz[1]) > 0.999f) { up[0] = 1; up[1] = 0; }
  float sx[3] = {fz[1] * up[2] - fz[2] * up[1], fz[2] * up[0] - fz[0] * up[2],
                 fz[0] * up[1] - fz[1] * up[0]};
  float sl = std::sqrt(sx[0] * sx[0] + sx[1] * sx[1] + sx[2] * sx[2]);
  for (float &v : sx) v /= sl;
  float uy[3] = {sx[1] * fz[2] - sx[2] * fz[1], sx[2] * fz[0] - sx[0] * fz[2],
                 sx[0] * fz[1] - sx[1] * fz[0]};
  float view[16] = {sx[0], uy[0], -fz[0], 0, sx[1], uy[1], -fz[1], 0,
                    sx[2], uy[2], -fz[2], 0,
                    -(sx[0] * eye[0] + sx[1] * eye[1] + sx[2] * eye[2]),
                    -(uy[0] * eye[0] + uy[1] * eye[1] + uy[2] * eye[2]),
                    fz[0] * eye[0] + fz[1] * eye[1] + fz[2] * eye[2], 1};
  float aspect = w / float(h);
  g_last_fovy = fovy_rad;
  float cam_d = std::sqrt((eye[0]-target[0])*(eye[0]-target[0]) + (eye[1]-target[1])*(eye[1]-target[1]) + (eye[2]-target[2])*(eye[2]-target[2]));
  float znear = std::clamp(cam_d * 0.002f, 0.00002f, 0.5f);
  // the far plane follows the zoom so pulling out reveals the whole planetary
  // neighborhood; planets themselves render as a depth-write-free sky layer,
  // so they are never clipped by it regardless
  float zfar = std::max(cam_d * 40.f, 60.f);
  float f = 1.f / std::tan(fovy_rad * 0.5f);
  float proj[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0,
                    0, 0, (zfar + znear) / (znear - zfar), -1,
                    0, 0, 2 * zfar * znear / (znear - zfar), 0};
  mat_mul(mvp, proj, view);
  mat_inverse(inv_vp, mvp);
}

static void ensure_fbo(int slot, int w, int h) {
  if (w == fbo_w[slot] && h == fbo_h[slot] && fbo[slot]) return;
  if (fbo[slot]) {
    glDeleteFramebuffers(1, &fbo[slot]);
    glDeleteTextures(1, &fbo_color[slot]);
    glDeleteRenderbuffers(1, &fbo_depth[slot]);
  }
  glGenFramebuffers(1, &fbo[slot]);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[slot]);
  glGenTextures(1, &fbo_color[slot]);
  glBindTexture(GL_TEXTURE_2D, fbo_color[slot]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         fbo_color[slot], 0);
  glGenRenderbuffers(1, &fbo_depth[slot]);
  glBindRenderbuffer(GL_RENDERBUFFER, fbo_depth[slot]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            fbo_depth[slot]);
  fbo_w[slot] = w;
  fbo_h[slot] = h;
}

unsigned renderer_draw_view(int slot, RenderSettings::ViewConfig &vc, int w, int h,
                            float dt) {
  slot = std::clamp(slot, 0, 5);
  // Relink here rather than where the graph changed: this is the main thread
  // with the context current, and doing it once before the first view means
  // all six views draw the same program in the same frame.
  if (g_field_dirty) {
    g_field_dirty = false;
    g_field_glsl = g_field_want;
    g_surface_glsl = g_surface_want;
    g_rough_glsl = g_rough_want;
    g_bump_glsl = g_bump_want;
    std::string err;
    if (rebuild_terrain_program(err)) {
      g_field_error.clear();
    } else {
      g_field_error = err.empty() ? "generated terrain shader failed to link" : err;
      std::fprintf(stderr, "terrain shader:\n%s\n", g_field_error.c_str());
      // fall back to the stubs, which are known good, so the viewport keeps
      // drawing. The *requests* still hold the broken sources, so this is not
      // retried until the graph actually changes.
      g_field_glsl.clear();
      g_surface_glsl.clear();
      g_rough_glsl.clear();
      g_bump_glsl.clear();
      rebuild_terrain_program(err);
    }
  }
  if (slot == 0) cloud_time += dt;
  static float time_acc = 0;
  if (slot == 0) time_acc += dt;
  if (w < 8 || h < 8) return fbo_color[slot];
  ensure_fbo(slot, w, h);
  float eye[3], mvp[16], inv_vp[16];
  if (vc.camera == 0)
    camera_matrices(w, h, eye, mvp, inv_vp);
  else
    ortho_matrices(vc, w, h, render_settings().height_scale, eye, mvp, inv_vp);
  draw_scene(slot, vc, w, h, time_acc, eye, mvp, inv_vp);
  return fbo_color[slot];
}

unsigned renderer_draw(int w, int h, float dt) {
  return renderer_draw_view(0, render_settings().views[0], w, h, dt);
}

float renderer_view_width_m(const RenderSettings::ViewConfig &vc) {
  const RenderSettings &RS = render_settings();
  if (vc.camera == 0) return CAM.dist * RS.terrain_size_m;
  return vc.ortho_zoom * RS.terrain_size_m;
}

void renderer_view_input(RenderSettings::ViewConfig &vc, float dx, float dy,
                         float wheel, bool rotating, bool panning, int view_w) {
  if (vc.camera == 0) {
    renderer_handle_input(dx, dy, wheel, rotating, panning);
    return;
  }
  if (wheel != 0)
    vc.ortho_zoom = std::fmin(std::fmax(vc.ortho_zoom * (1.f - wheel * 0.12f), 0.0004f), 400.f);
  if (rotating || panning) {
    float s = vc.ortho_zoom / std::max(view_w, 1);
    if (vc.camera == 1) {
      vc.ortho_cx -= dx * s;
      vc.ortho_cy -= dy * s;
    } else {
      vc.ortho_cx -= dx * s;
      vc.ortho_cy += dy * s;
    }
  }
}

void renderer_get_camera(float eye[3], float target[3], float *fovy_deg) {
  float cp = std::cos(CAM.pitch), sp = std::sin(CAM.pitch);
  float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
  eye[0] = CAM.target[0] + CAM.dist * cp * sy;
  eye[1] = CAM.target[1] + CAM.dist * sp;
  eye[2] = CAM.target[2] + CAM.dist * cp * cy;
  for (int i = 0; i < 3; ++i) target[i] = CAM.target[i];
  *fovy_deg = 0.9f * 57.29578f;
}

void renderer_set_brush_cursor(float tx, float tz, float radius, bool erasing) {
  g_brush[0] = tx;
  g_brush[1] = tz;
  g_brush[2] = radius;
  g_brush[3] = erasing ? 1.f : 0.f;
}

// ------------------------------------------------------------------ picking
static bool ray_sphere(const float *ro, const float *rd, const float *c, float r,
                       float &t) {
  float oc[3] = {ro[0] - c[0], ro[1] - c[1], ro[2] - c[2]};
  float b = oc[0] * rd[0] + oc[1] * rd[1] + oc[2] * rd[2];
  float cc = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - r * r;
  float disc = b * b - cc;
  if (disc < 0) return false;
  float sq = std::sqrt(disc);
  t = -b - sq;
  if (t < 0) t = -b + sq;
  return t > 0;
}

// World-space ray through a view coordinate. Shared by object picking and by
// the terrain brushes, so both agree on where the cursor is pointing.
static bool view_ray(const RenderSettings::ViewConfig &vc, float u, float v, int w,
                     int h, float ro[3], float rd[3]) {
  RenderSettings &RS = render_settings();
  float eye[3], mvp[16], inv_vp[16];
  if (vc.camera == 0) camera_matrices(w, h, eye, mvp, inv_vp);
  else ortho_matrices(vc, w, h, RS.height_scale, eye, mvp, inv_vp);
  float ndc_x = u * 2.f - 1.f, ndc_y = 1.f - v * 2.f;
  auto unproject = [&](float z, float *out) {
    float p[4] = {ndc_x, ndc_y, z, 1.f};
    float r[4];
    for (int i = 0; i < 4; ++i) {
      r[i] = 0;
      for (int k = 0; k < 4; ++k) r[i] += inv_vp[k * 4 + i] * p[k];
    }
    float iw = std::fabs(r[3]) > 1e-9f ? 1.f / r[3] : 1.f;
    out[0] = r[0] * iw; out[1] = r[1] * iw; out[2] = r[2] * iw;
  };
  float pf[3];
  unproject(-1.f, ro);
  unproject(1.f, pf);
  rd[0] = pf[0] - ro[0];
  rd[1] = pf[1] - ro[1];
  rd[2] = pf[2] - ro[2];
  float rl = std::sqrt(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
  if (rl < 1e-9f) return false;
  for (int i = 0; i < 3; ++i) rd[i] /= rl;
  return true;
}

// March the height field and report where the cursor lands on it, in
// normalized terrain coordinates. This is what positions a sculpt brush.
bool renderer_pick_terrain(int slot, const RenderSettings::ViewConfig &vc, float u,
                           float v, int w, int h, float &tx, float &tz) {
  (void)slot;
  RenderSettings &RS = render_settings();
  if (cpu_height.empty()) return false;
  float pn[3], rd[3];
  if (!view_ray(vc, u, v, w, h, pn, rd)) return false;
  float prev_diff = 0;
  bool have_prev = false;
  float step = 0.003f;
  for (float tt = 0.f; tt < 12.f; tt += step) {
    float x = pn[0] + rd[0] * tt, y = pn[1] + rd[1] * tt, z = pn[2] + rd[2] * tt;
    if (x < 0.f || x > 1.f || z < 0.f || z > 1.f) {
      have_prev = false;
      if (y < -0.5f) break;
      continue;
    }
    float terr = cpu_height.sample(x, z) * RS.height_scale;
    float diff = y - terr;
    if (have_prev && prev_diff > 0 && diff <= 0) {
      float f = diff / (diff - prev_diff + 1e-9f);
      float hit = tt - step * f;
      tx = std::clamp(pn[0] + rd[0] * hit, 0.f, 1.f);
      tz = std::clamp(pn[2] + rd[2] * hit, 0.f, 1.f);
      return true;
    }
    prev_diff = diff;
    have_prev = true;
    step = std::min(step * 1.02f, 0.04f);
  }
  return false;
}

int renderer_pick(int slot, const RenderSettings::ViewConfig &vc, float u, float v,
                  int w, int h) {
  (void)slot;
  RenderSettings &RS = render_settings();
  float pn[3], rd[3];
  if (!view_ray(vc, u, v, w, h, pn, rd)) return -1;

  SceneState &sc = scene();
  int best_idx = -1;
  float best_t = 1e30f;
  float sun[3];
  compute_sun_dir(RS, sun);

  // planets first: they are behind everything else, so any closer hit below
  // simply replaces this one
  {
    float pt;
    int p = planet_pick(pn, rd, pt);
    if (p >= 0) {
      best_idx = p;
      best_t = pt;
    }
  }

  for (size_t i = 0; i < sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (!sc.object_visible(o)) continue;
    float t = 0;
    if (o.type == SceneObject::Sun) {
      float gd = 1.9f;
      float c[3] = {0.5f + sun[0] * gd, RS.height_scale + sun[1] * gd,
                    0.5f + sun[2] * gd};
      if (ray_sphere(pn, rd, c, 0.07f, t) && t < best_t) {
        best_t = t;
        best_idx = (int)i;
      }
    } else if (o.type == SceneObject::Mesh) {
      float c[3] = {o.pos[0], o.pos[1] * RS.height_scale + o.scale * 0.5f, o.pos[2]};
      if (ray_sphere(pn, rd, c, o.scale * 0.75f, t) && t < best_t) {
        best_t = t;
        best_idx = (int)i;
      }
    } else if (o.type == SceneObject::Water && RS.show_water) {
      float lv = RS.water_level * RS.height_scale;
      if (std::fabs(rd[1]) > 1e-6f) {
        t = (lv - pn[1]) / rd[1];
        if (t > 0) {
          float x = pn[0] + rd[0] * t, z = pn[2] + rd[2] * t;
          if (x >= 0 && x <= 1 && z >= 0 && z <= 1) {
            float bed = cpu_height.empty() ? 0.f
                                           : cpu_height.sample(x, z) * RS.height_scale;
            if (bed < lv && t < best_t) {
              best_t = t;
              best_idx = (int)i;
            }
          }
        }
      }
    } else if (o.type == SceneObject::Terrain && !cpu_height.empty()) {
      // march the heightfield
      float t0 = 0.f, t1 = 12.f;
      float prev_diff = 0;
      bool have_prev = false;
      float step = 0.004f;
      for (float tt = t0; tt < t1; tt += step) {
        float x = pn[0] + rd[0] * tt, y = pn[1] + rd[1] * tt, z = pn[2] + rd[2] * tt;
        if (x < -0.05f || x > 1.05f || z < -0.05f || z > 1.05f) {
          have_prev = false;
          if (y < -0.5f) break;
          continue;
        }
        float terr = cpu_height.sample(std::clamp(x, 0.f, 1.f),
                                       std::clamp(z, 0.f, 1.f)) * RS.height_scale;
        float diff = y - terr;
        if (have_prev && prev_diff > 0 && diff <= 0) {
          float hit_t = tt - step * (diff / (diff - prev_diff + 1e-9f));
          if (hit_t < best_t) {
            best_t = hit_t;
            best_idx = (int)i;
          }
          break;
        }
        prev_diff = diff;
        have_prev = true;
        step = std::min(step * 1.02f, 0.05f);
      }
    }
  }
  return best_idx;
}

unsigned renderer_material_preview(int size, int shape, float spin) {
  RenderSettings &RS = render_settings();
  if (size < 16) size = 16;
  shape = std::clamp(shape, 0, 2);
  if (size != matprev_size || !matprev_fbo) {
    if (matprev_fbo) {
      glDeleteFramebuffers(1, &matprev_fbo);
      glDeleteTextures(1, &matprev_tex);
      glDeleteRenderbuffers(1, &matprev_depth);
    }
    glGenFramebuffers(1, &matprev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, matprev_fbo);
    glGenTextures(1, &matprev_tex);
    glBindTexture(GL_TEXTURE_2D, matprev_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           matprev_tex, 0);
    glGenRenderbuffers(1, &matprev_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, matprev_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, matprev_depth);
    matprev_size = size;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, matprev_fbo);
  glViewport(0, 0, size, size);
  glClearColor(0.11f, 0.11f, 0.12f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  float sun[3];
  compute_sun_dir(RS, sun);
  glUseProgram(prog_matprev);
  uni3(prog_matprev, "u_sun", sun);
  uni3(prog_matprev, "u_sun_color", RS.sun_color);
  uni1(prog_matprev, "u_sun_intensity", RS.sun_intensity);
  uni3(prog_matprev, "u_sky_zenith", RS.sky_zenith);
  uni3(prog_matprev, "u_sky_horizon", RS.sky_horizon);
  uni1(prog_matprev, "u_ambient", RS.ambient_intensity);
  uni1(prog_matprev, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_matprev, "u_grade", g_grade);
    uni1(prog_matprev, "u_sat", g_saturation);
  uni1(prog_matprev, "u_roughness", RS.mat_roughness);
  uni1(prog_matprev, "u_metallic", RS.mat_metallic);
  uni1(prog_matprev, "u_specular", RS.mat_specular);
  uni1(prog_matprev, "u_reflection", RS.mat_reflection);
  unii(prog_matprev, "u_has_albedo", has_albedo ? 1 : 0);
  unii(prog_matprev, "u_has_normal", has_normal_map ? 1 : 0);
  unii(prog_matprev, "u_has_rough", has_rough_map ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_albedo);
  unii(prog_matprev, "u_albedo", 0);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, tex_normal);
  unii(prog_matprev, "u_normal_map", 5);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, tex_rough);
  unii(prog_matprev, "u_rough_map", 6);
  // gentle turntable for sphere/cube; flat stays facing the camera
  float ax = shape == 2 ? 0.f : spin;
  float tilt = shape == 1 ? 0.42f : (shape == 0 ? 0.18f : 0.f);
  float cy2 = std::cos(ax), sy2 = std::sin(ax);
  float cx2 = std::cos(tilt), sx2 = std::sin(tilt);
  // column-major rotY then rotX
  float rot[9] = {cy2, sy2 * sx2, -sy2 * cx2,
                  0,   cx2,        sx2,
                  sy2, -cy2 * sx2, cy2 * cx2};
  glUniformMatrix3fv(glGetUniformLocation(prog_matprev, "u_rot"), 1, GL_FALSE, rot);
  glBindVertexArray(prev_vao[shape]);
  glDrawArrays(GL_TRIANGLES, 0, prev_verts[shape]);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return matprev_tex;
}

// Renders the live sky (gradient + sun tint + volumetric clouds) into an
// equirectangular HDR so the offline path tracer lights the scene with the
// exact same environment the viewport shows.
bool renderer_export_sky_hdr(const std::string &path, int w, int h) {
  RenderSettings &RS = render_settings();
  float sun[3];
  compute_sun_dir(RS, sun);
  GLuint f = 0, t = 0;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
  bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (ok) {
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(prog_sky);
    float eye[3] = {0.5f, RS.cloud_altitude * 0.35f, 0.5f};
    uni3(prog_sky, "u_cam", eye);
    uni3(prog_sky, "u_sun", sun);
    uni3(prog_sky, "u_sun_color", RS.sun_color);
    uni1(prog_sky, "u_sun_intensity", RS.sun_intensity);
    uni1(prog_sky, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_sky, "u_grade", g_grade);
    uni1(prog_sky, "u_sat", g_saturation);
    uni3(prog_sky, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_sky, "u_sky_horizon", RS.sky_horizon);
    uni1(prog_sky, "u_atmo", RS.atmosphere_density);
    unii(prog_sky, "u_fog_type", RS.fog_type);
    uni3(prog_sky, "u_fog_color", RS.fog_color);
    uni1(prog_sky, "u_fog_density", RS.fog_density);
    unii(prog_sky, "u_clouds", RS.clouds_on ? 1 : 0);
    unii(prog_sky, "u_cl_steps", 72);
    unii(prog_sky, "u_cl_type", RS.cloud_type);
    uni1(prog_sky, "u_cl_cov", RS.cloud_coverage);
    uni1(prog_sky, "u_cl_den", RS.cloud_density);
    uni1(prog_sky, "u_cl_alt", RS.cloud_altitude);
    uni1(prog_sky, "u_cl_thick", RS.cloud_thickness);
    uni1(prog_sky, "u_cl_detail_amt", RS.cloud_detail);
    uni1(prog_sky, "u_cl_time", cloud_time);
    uni1(prog_sky, "u_cl_ambient", RS.cloud_ambient);
    uni1(prog_sky, "u_cl_anvil", RS.cloud_anvil);
    float wr = RS.cloud_wind_dir * 0.017453293f;
    float wind[2] = {std::cos(wr) * RS.cloud_wind_speed,
                     std::sin(wr) * RS.cloud_wind_speed};
    glUniform2fv(glGetUniformLocation(prog_sky, "u_cl_wind"), 1, wind);
    uni3(prog_sky, "u_cl_color", RS.cloud_color);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
    unii(prog_sky, "u_cl_shape", 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_detail);
    unii(prog_sky, "u_cl_detail", 4);
    unii(prog_sky, "u_panorama", 1);
    unii(prog_sky, "u_hdr", 1);
    unii(prog_sky, "u_no_sun", 1); // the sun is emitted separately
    uni1(prog_sky, "u_space", 0.f); // panoramas are always shot from the ground
    glBindVertexArray(vao_quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::vector<float> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, px.data());
    // flip vertically into RGB for the HDR writer (row 0 = top = +90 deg)
    std::vector<float> rgb((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const float *s = &px[(((size_t)y * w) + x) * 4];
        float *d = &rgb[(((size_t)(h - 1 - y) * w) + x) * 3];
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
      }
    ok = stbi_write_hdr(path.c_str(), w, h, 3, rgb.data()) != 0;
    glEnable(GL_DEPTH_TEST);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &f);
  glDeleteTextures(1, &t);
  return ok;
}

bool renderer_render_to_file(const std::string &path, int w, int h) {
  int rw = w * 2, rh = h * 2;
  ensure_fbo(5, rw, rh);
  RenderSettings::ViewConfig vc = render_settings().views[0];
  vc.camera = 0;
  vc.display = 2;
  vc.atmosphere = true;
  vc.grid = false;
  float eye[3], mvp[16], inv_vp[16];
  camera_matrices(rw, rh, eye, mvp, inv_vp);
  draw_scene(5, vc, rw, rh, 0.f, eye, mvp, inv_vp);
  std::vector<unsigned char> big((size_t)rw * rh * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[5]);
  glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, big.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  std::vector<unsigned char> out((size_t)w * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c) {
        int sum = 0;
        for (int sy = 0; sy < 2; ++sy)
          for (int sx2 = 0; sx2 < 2; ++sx2)
            sum += big[(((size_t)(y * 2 + sy) * rw) + x * 2 + sx2) * 4 + c];
        out[(((size_t)(h - 1 - y) * w) + x) * 4 + c] = (unsigned char)(sum / 4);
      }
  return stbi_write_png(path.c_str(), w, h, 4, out.data(), w * 4) != 0;
}

void renderer_settings_ui() {
  RenderSettings &RS = render_settings();
  ImGui::SetNextItemWidth(140);
  ImGui::SliderFloat("Height scale", &RS.height_scale, 0.02f, 0.8f);
  ImGui::SetNextItemWidth(140);
  ImGui::SliderFloat("Exposure", &RS.exposure, 0.3f, 3.f);
  studio::Checkbox("Wireframe", &RS.wireframe);
  ImGui::SameLine();
  studio::Checkbox("Graph albedo", &RS.use_albedo);
  studio::Checkbox("Shadows", &RS.shadows);
}

static bool mat_inverse(float *inv_out, const float *m) {
  float inv[16];
  inv[0] = m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
  float det = m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
  if (std::fabs(det) < 1e-20f) return false;
  det = 1.f / det;
  for (int i = 0; i < 16; ++i) inv_out[i] = inv[i] * det;
  return true;
}

} // namespace studio










