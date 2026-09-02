#include "console.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
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

// The GLSL lives in planet_shaders.cpp; these are its exports.
extern const char *PL_FN;
extern const char *PL_FIELD_STUB;
extern const char *VS_PLANET;
extern const char *FS_PLANET;
extern const char *VS_INF;
extern const char *FS_INF;

// The generated program, the one currently spliced in, and whether a relink
// is owed. As in the terrain renderer, the *request* is what a change is
// measured against: a graph that fails to compile must not relink every frame.
static std::string g_field_want, g_field_glsl, g_field_error;
static float g_field_strength = 0.f;
static bool g_field_dirty = false;


// ---------------------------------------------------------------- GL state
static GLuint prog_planet = 0, prog_inf = 0;
// three sphere LODs shared by every planet in existence
static GLuint sph_vao[3] = {0}, sph_vbo[3] = {0}, sph_ebo[3] = {0};
static int sph_count[3] = {0};
static GLuint inf_vao = 0, inf_vbo = 0, inf_ebo = 0;
static int inf_count = 0;
// mesh-LOD hysteresis per planet: switching thresholds overlap so a planet
// hovering at a boundary never flickers between meshes
static std::vector<int> g_lod_state;

// ------------------------------------------------------------------- helpers
static void puni3(GLuint p, const char *n, const float *v) {
  glUniform3fv(glGetUniformLocation(p, n), 1, v);
}
static void puni1(GLuint p, const char *n, float v) {
  glUniform1f(glGetUniformLocation(p, n), v);
}
static void punii(GLuint p, const char *n, int v) {
  glUniform1i(glGetUniformLocation(p, n), v);
}

static GLuint pl_compile(GLenum type, const std::string &src) {
  GLuint sh = glCreateShader(type);
  const char *s = src.c_str();
  glShaderSource(sh, 1, &s, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(sh, sizeof log, nullptr, log);
    log_error("shader", std::string("planet: ") + log);
  }
  return sh;
}

// The placeholder expands to: the transpiler's prelude, then the generated
// function (or the stub), then the built-in layer maths - in that order,
// because pl_height calls gpx_surface_field and GLSL has no forward
// declarations to lean on. Each shader stage is its own translation unit, so
// each gets its own copy; duplicates only collide within a stage.
static std::string pl_inject(const char *src) {
  std::string body = gpx::field_glsl_prelude();
  body += g_field_glsl.empty() ? std::string(PL_FIELD_STUB)
                               : gpx::field_glsl_strip_prelude(g_field_glsl);
  body += PL_FN;
  std::string s(src);
  size_t p;
  while ((p = s.find("PL_FN_PLACEHOLDER")) != std::string::npos)
    s.replace(p, strlen("PL_FN_PLACEHOLDER"), body);
  return s;
}

// Link and say so when it fails. The built-in shaders are known good; a
// generated one is written by the user's graph and can genuinely fail, and an
// unlinked planet program draws nothing at all - the worst way to find out.
static GLuint pl_link_checked(const char *vs, const char *fs,
                              std::string &err) {
  GLuint p = glCreateProgram();
  GLuint v = pl_compile(GL_VERTEX_SHADER, pl_inject(vs));
  GLuint f = pl_compile(GL_FRAGMENT_SHADER, pl_inject(fs));
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, sizeof log, nullptr, log);
    err = log;
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

static GLuint pl_link(const char *vs, const char *fs) {
  std::string err;
  GLuint p = pl_link_checked(vs, fs, err);
  if (!p) log_error("shader", "planet program: " + err);
  return p;
}

void planet_set_field_program(const std::string &glsl, float strength) {
  if (glsl == g_field_want && strength == g_field_strength) return;
  g_field_want = glsl;
  g_field_strength = strength;
  g_field_dirty = true;
}

const std::string &planet_field_error() { return g_field_error; }

// Relink both programs against the current request. Called at the top of a
// draw, where a GL context is certain.
static void pl_refresh_programs() {
  if (!g_field_dirty) return;
  g_field_dirty = false;
  g_field_glsl = g_field_want;
  std::string err;
  GLuint np = pl_link_checked(VS_PLANET, FS_PLANET, err);
  GLuint ni = np ? pl_link_checked(VS_INF, FS_INF, err) : 0;
  if (np && ni) {
    if (prog_planet) glDeleteProgram(prog_planet);
    if (prog_inf) glDeleteProgram(prog_inf);
    prog_planet = np;
    prog_inf = ni;
    g_field_error.clear();
    return;
  }
  // Failed: keep the programs that work, and remember the broken request so
  // this is not retried every frame until the graph actually changes.
  if (np) glDeleteProgram(np);
  g_field_error = err.empty() ? "generated surface shader failed to link" : err;
  log_error("shader", "planet surface program: " + g_field_error);
  g_field_glsl.clear();
  g_field_strength = 0.f;
  GLuint fp = pl_link_checked(VS_PLANET, FS_PLANET, err);
  GLuint fi = fp ? pl_link_checked(VS_INF, FS_INF, err) : 0;
  if (fp && fi) {
    if (prog_planet) glDeleteProgram(prog_planet);
    if (prog_inf) glDeleteProgram(prog_inf);
    prog_planet = fp;
    prog_inf = fi;
  } else {
    if (fp) glDeleteProgram(fp);
  }
}

static void build_sphere_lod(int lod, int sect, int rings) {
  std::vector<float> v;
  v.reserve((size_t)(rings + 1) * (sect + 1) * 3);
  for (int r = 0; r <= rings; ++r)
    for (int s = 0; s <= sect; ++s) {
      float phi = float(r) / rings * 3.14159265f;
      float th = float(s) / sect * 6.2831853f;
      v.push_back(std::sin(phi) * std::cos(th));
      v.push_back(std::cos(phi));
      v.push_back(std::sin(phi) * std::sin(th));
    }
  std::vector<unsigned> idx;
  idx.reserve((size_t)rings * sect * 6);
  for (int r = 0; r < rings; ++r)
    for (int s = 0; s < sect; ++s) {
      unsigned a = r * (sect + 1) + s, b = a + sect + 1;
      idx.insert(idx.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
  sph_count[lod] = (int)idx.size();
  glGenVertexArrays(1, &sph_vao[lod]);
  glBindVertexArray(sph_vao[lod]);
  glGenBuffers(1, &sph_vbo[lod]);
  glBindBuffer(GL_ARRAY_BUFFER, sph_vbo[lod]);
  glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &sph_ebo[lod]);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sph_ebo[lod]);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
}

static void build_infinite_grid() {
  const int N = 180;
  std::vector<float> v;
  v.reserve((size_t)(N + 1) * (N + 1) * 2);
  for (int y = 0; y <= N; ++y)
    for (int x = 0; x <= N; ++x) {
      v.push_back(x / float(N) * 2.f - 1.f);
      v.push_back(y / float(N) * 2.f - 1.f);
    }
  std::vector<unsigned> idx;
  idx.reserve((size_t)N * N * 6);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      unsigned a = y * (N + 1) + x, b = a + N + 1;
      idx.insert(idx.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
  inf_count = (int)idx.size();
  glGenVertexArrays(1, &inf_vao);
  glBindVertexArray(inf_vao);
  glGenBuffers(1, &inf_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, inf_vbo);
  glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &inf_ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, inf_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
}

bool planet_renderer_init() {
  prog_planet = pl_link(VS_PLANET, FS_PLANET);
  prog_inf = pl_link(VS_INF, FS_INF);
  build_sphere_lod(0, 48, 24);    //  ~1.2k verts: a dot in the sky
  build_sphere_lod(1, 176, 88);   //   ~16k verts: filling the view
  build_sphere_lod(2, 448, 224);  //  ~100k verts: surface approach
  build_infinite_grid();
  return prog_planet != 0 && prog_inf != 0;
}

// upload one object's stack of infinite layers as shader uniforms
static int upload_layers(GLuint prog, int planet_idx, float amp_scale) {
  SceneState &sc = scene();
  std::vector<int> layers = scene_surface_layers(planet_idx);
  float la[6][4], lb[6][4];
  int n = 0;
  for (int idx : layers) {
    if (n >= 6) break;
    const gpx::planet::Layer &L = sc.objects[idx].surf.layer;
    la[n][0] = L.frequency;
    la[n][1] = L.amplitude * amp_scale *
               (planet_idx < 0 ? sc.objects[idx].surf.height_scale : 1.f);
    la[n][2] = L.coverage;
    la[n][3] = L.mask_scale;
    lb[n][0] = (float)L.seed;
    lb[n][1] = (float)L.type;
    lb[n][2] = lb[n][3] = 0.f;
    ++n;
  }
  glUniform4fv(glGetUniformLocation(prog, "u_la"), 6, &la[0][0]);
  glUniform4fv(glGetUniformLocation(prog, "u_lb"), 6, &lb[0][0]);
  punii(prog, "u_lcount", n);
  // the field is part of the surface definition, so it is uploaded with the
  // layers rather than somewhere a caller could forget
  puni1(prog, "u_fstrength", g_field_glsl.empty() ? 0.f : g_field_strength);
  return n;
}

bool infinite_layers_present() {
  return !scene_surface_layers(-1).empty();
}

void planet_draw_all(const PlanetFrame &f) {
  pl_refresh_programs(); // a GL context is certain here, unlike in the setter
  SceneState &sc = scene();
  std::vector<int> planets = scene_planet_indices();
  if (planets.empty()) return;
  if (g_lod_state.size() < sc.objects.size())
    g_lod_state.assign(sc.objects.size(), 0);

  // painter sort far-to-near: planets render as a sky layer (no depth
  // writes), so terrain always occludes them correctly from the ground
  std::sort(planets.begin(), planets.end(), [&](int a, int b) {
    auto d2 = [&](int i) {
      const float *p = sc.objects[i].pos;
      float dx = p[0] - f.eye[0], dy = p[1] - f.eye[1], dz = p[2] - f.eye[2];
      return dx * dx + dy * dy + dz * dz;
    };
    return d2(a) > d2(b);
  });

  glUseProgram(prog_planet);
  glUniformMatrix4fv(glGetUniformLocation(prog_planet, "u_mvp"), 1, GL_FALSE,
                     f.mvp);
  puni3(prog_planet, "u_cam", f.eye);
  puni3(prog_planet, "u_sun", f.sun);
  puni1(prog_planet, "u_sun_i", f.sun_intensity);
  puni1(prog_planet, "u_exposure", f.exposure);
  puni3(prog_planet, "u_grade", f.grade);
  puni1(prog_planet, "u_sat", f.saturation);
  glDepthMask(GL_FALSE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  // restored before returning: GL_LEQUAL used to leak into every draw that
  // followed this pass, so whatever ran next silently got a different depth
  // comparison depending on whether a planet happened to be visible

  float half_tan = std::tan(f.fovy_rad * 0.5f);
  for (int idx : planets) {
    const SceneObject &o = sc.objects[idx];
    if (!sc.object_visible(o)) continue;
    const PlanetData &P = o.planet;
    float dx = o.pos[0] - f.eye[0], dy = o.pos[1] - f.eye[1],
          dz = o.pos[2] - f.eye[2];
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-5f) continue;
    // progressive quality: projected size in pixels decides everything
    float px = P.radius / dist * f.view_h / (2.f * half_tan);
    if (px < 0.6f) continue; // sub-pixel: costs nothing at all

    // geometry LOD with hysteresis so the mesh never flickers at a boundary
    int &lod = g_lod_state[idx];
    if (lod < 1 && px > 70.f) lod = 1;
    else if (lod >= 1 && px < 50.f) lod = 0;
    if (lod < 2 && px > 420.f) lod = 2;
    else if (lod >= 2 && px < 300.f) lod = 1;

    // continuous octave count: the shading LOD, pop-free by construction
    float octf = std::clamp(std::log2(std::max(px, 2.f)) - 1.5f, 1.f, 11.f);

    puni3(prog_planet, "u_center", o.pos);
    puni1(prog_planet, "u_radius", P.radius);
    puni1(prog_planet, "u_relief", P.relief);
    puni1(prog_planet, "u_octf", octf);
    puni1(prog_planet, "u_sea", P.sea_level);
    puni1(prog_planet, "u_snow", P.snow_line);
    puni3(prog_planet, "u_rock_lo", P.rock_low);
    puni3(prog_planet, "u_rock_hi", P.rock_high);
    puni3(prog_planet, "u_water_c", P.water_color);
    puni3(prog_planet, "u_atmo_c", P.atmo_color);
    puni1(prog_planet, "u_atmo", P.atmo_density);
    float spin = P.spin_deg * 0.017453293f;
    glUniform2f(glGetUniformLocation(prog_planet, "u_spin"), std::cos(spin),
                std::sin(spin));
    upload_layers(prog_planet, idx, 1.f);
    glBindVertexArray(sph_vao[lod]);
    glDrawElements(GL_TRIANGLES, sph_count[lod], GL_UNSIGNED_INT, nullptr);
  }
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS); // back to the default the rest of the frame expects
}

void infinite_draw(const InfiniteFrame &f) {
  pl_refresh_programs();
  if (!infinite_layers_present()) return;
  glUseProgram(prog_inf);
  glUniformMatrix4fv(glGetUniformLocation(prog_inf, "u_mvp"), 1, GL_FALSE, f.mvp);
  puni3(prog_inf, "u_cam", f.eye);
  puni3(prog_inf, "u_sun", f.sun);
  puni3(prog_inf, "u_sun_color", f.sun_color);
  puni1(prog_inf, "u_sun_i", f.sun_intensity);
  puni1(prog_inf, "u_ambient", f.ambient);
  puni3(prog_inf, "u_sky_zenith", f.sky_zenith);
  puni3(prog_inf, "u_sky_horizon", f.sky_horizon);
  puni1(prog_inf, "u_exposure", f.exposure);
  puni3(prog_inf, "u_grade", f.grade);
  puni1(prog_inf, "u_sat", f.saturation);
  puni1(prog_inf, "u_hscale", f.height_scale);
  puni1(prog_inf, "u_curve", f.planet_radius);
  punii(prog_inf, "u_fog_type", f.fog_type);
  puni1(prog_inf, "u_fogd", f.fog_density);
  puni3(prog_inf, "u_fog_color", f.fog_color);
  // the surround's relief budget follows the tile's own height scale
  float amp = f.height_scale * 1.2f;
  puni1(prog_inf, "u_amp", amp);
  puni1(prog_inf, "u_base", f.base_height);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, f.tex_height);
  punii(prog_inf, "u_height", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, f.tex_albedo);
  punii(prog_inf, "u_albedo", 1);
  punii(prog_inf, "u_has_albedo", f.tex_albedo ? 1 : 0);
  upload_layers(prog_inf, -1, amp / std::max(f.height_scale, 1e-4f));
  glBindVertexArray(inf_vao);
  glDrawElements(GL_TRIANGLES, inf_count, GL_UNSIGNED_INT, nullptr);
}

int planet_pick(const float ro[3], const float rd[3], float &t_out) {
  SceneState &sc = scene();
  int best = -1;
  float best_t = 1e30f;
  for (int idx : scene_planet_indices()) {
    const SceneObject &o = sc.objects[idx];
    if (!sc.object_visible(o)) continue;
    float r = o.planet.radius * (1.f + o.planet.relief * 0.5f);
    float oc[3] = {ro[0] - o.pos[0], ro[1] - o.pos[1], ro[2] - o.pos[2]};
    float b = oc[0] * rd[0] + oc[1] * rd[1] + oc[2] * rd[2];
    float c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - r * r;
    float disc = b * b - c;
    if (disc < 0) continue;
    float t = -b - std::sqrt(disc);
    if (t < 0) t = -b + std::sqrt(disc);
    if (t > 0 && t < best_t) {
      best_t = t;
      best = idx;
    }
  }
  t_out = best_t;
  return best;
}

} // namespace studio
