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
#include <map>
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

// One linked pair of programs (sphere + surround) per surface graph. Entry 0
// is the built-in: layers only, no field. A planet names the
// SurfaceDisplacement node that shapes it (Terragen: every planet has its
// own terrain network), and the programs for that graph are linked once and
// shared by every planet that uses it. As in the terrain renderer, the
// *request* is what a change is measured against, so a graph that fails to
// compile does not relink every frame.
struct SurfProg {
  std::string want;   // requested generated GLSL ("" = none)
  std::string glsl;   // what is currently linked
  float strength = 0.f;
  GLuint planet = 0, inf = 0;
  bool dirty = true;
  std::string error;
};
static std::map<uint64_t, SurfProg> g_progs;
static std::string g_field_error;

// ---------------------------------------------------------------- GL state
// three sphere LODs shared by every planet in existence
static GLuint sph_vao[3] = {0}, sph_vbo[3] = {0}, sph_ebo[3] = {0};
static int sph_count[3] = {0};
static GLuint inf_vao = 0, inf_vbo = 0, inf_ebo = 0;
static int inf_count = 0;
// mesh-LOD hysteresis per planet: switching thresholds overlap so a planet
// hovering at a boundary never flickers between meshes
static std::vector<int> g_lod_state;

// ------------------------------------------------------------------- helpers
extern int g_aov; // renderer_aov.cpp: which render pass is being drawn

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
static std::string pl_inject(const char *src, const std::string &glsl) {
  std::string body = gpx::field_glsl_prelude();
  body += glsl.empty() ? std::string(PL_FIELD_STUB)
                       : gpx::field_glsl_strip_prelude(glsl);
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
                              const std::string &glsl, std::string &err) {
  GLuint p = glCreateProgram();
  GLuint v = pl_compile(GL_VERTEX_SHADER, pl_inject(vs, glsl));
  GLuint f = pl_compile(GL_FRAGMENT_SHADER, pl_inject(fs, glsl));
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

void planet_set_field_program(unsigned long long node, const std::string &glsl,
                              float strength) {
  SurfProg &sp = g_progs[node];
  if (glsl == sp.want && strength == sp.strength && !sp.dirty) return;
  if (glsl != sp.want) sp.dirty = true;
  sp.want = glsl;
  sp.strength = strength;
}

void planet_field_programs_keep(const std::vector<unsigned long long> &live) {
  for (auto it = g_progs.begin(); it != g_progs.end();) {
    bool keep = it->first == 0 ||
                std::find(live.begin(), live.end(), it->first) != live.end();
    if (keep) {
      ++it;
      continue;
    }
    if (it->second.planet) glDeleteProgram(it->second.planet);
    if (it->second.inf) glDeleteProgram(it->second.inf);
    it = g_progs.erase(it);
  }
}

const std::string &planet_field_error() { return g_field_error; }

// Relink whatever changed. Called at the top of a draw, where a GL context
// is certain. A graph that fails keeps the previous programs (or the built-in
// pair) and remembers the broken request, so it is not retried every frame.
static void pl_refresh_programs() {
  g_field_error.clear();
  for (auto &[id, sp] : g_progs) {
    if (!sp.dirty) {
      if (!sp.error.empty() && g_field_error.empty()) g_field_error = sp.error;
      continue;
    }
    sp.dirty = false;
    std::string err;
    GLuint np = pl_link_checked(VS_PLANET, FS_PLANET, sp.want, err);
    GLuint ni = np ? pl_link_checked(VS_INF, FS_INF, sp.want, err) : 0;
    if (np && ni) {
      if (sp.planet) glDeleteProgram(sp.planet);
      if (sp.inf) glDeleteProgram(sp.inf);
      sp.planet = np;
      sp.inf = ni;
      sp.glsl = sp.want;
      sp.error.clear();
      continue;
    }
    if (np) glDeleteProgram(np);
    sp.error = err.empty() ? "generated surface shader failed to link" : err;
    log_error("shader", "planet surface program: " + sp.error);
    if (g_field_error.empty()) g_field_error = sp.error;
    if (!sp.planet || !sp.inf) {
      // never linked anything: fall back to the built-in pair so the
      // planet is drawn at all
      sp.glsl.clear();
      sp.strength = 0.f;
      GLuint fp = pl_link_checked(VS_PLANET, FS_PLANET, "", err);
      GLuint fi = fp ? pl_link_checked(VS_INF, FS_INF, "", err) : 0;
      if (fp && fi) {
        sp.planet = fp;
        sp.inf = fi;
      } else if (fp) {
        glDeleteProgram(fp);
      }
    }
  }
}

// Which programs draw a surface that names `node`. 0 means "the graph", the
// single SurfaceDisplacement every planet used before per-planet graphs, so
// the first graph with code stands in for it; a name with no living program
// falls back to the built-in layers.
static const SurfProg &resolve_prog(unsigned long long node) {
  if (node) {
    auto it = g_progs.find(node);
    if (it != g_progs.end() && it->second.planet && !it->second.glsl.empty())
      return it->second;
  } else {
    for (auto &[id, sp] : g_progs)
      if (id && sp.planet && !sp.glsl.empty()) return sp;
  }
  return g_progs[0];
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
  SurfProg &base = g_progs[0];
  std::string err;
  base.planet = pl_link_checked(VS_PLANET, FS_PLANET, "", err);
  base.inf = base.planet ? pl_link_checked(VS_INF, FS_INF, "", err) : 0;
  base.dirty = false;
  if (!base.planet || !base.inf) log_error("shader", "planet program: " + err);
  build_sphere_lod(0, 48, 24);    //  ~1.2k verts: a dot in the sky
  build_sphere_lod(1, 176, 88);   //   ~16k verts: filling the view
  build_sphere_lod(2, 448, 224);  //  ~100k verts: surface approach
  build_infinite_grid();
  return base.planet != 0 && base.inf != 0;
}

// upload one object's stack of infinite layers as shader uniforms
static int upload_layers(GLuint prog, int planet_idx, float amp_scale,
                         float field_strength) {
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
  puni1(prog, "u_fstrength", field_strength);
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

  // the frame's lighting and grading, per program actually used
  GLuint bound = 0;
  auto use = [&](GLuint prog) {
    if (prog == bound) return;
    bound = prog;
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_mvp"), 1, GL_FALSE, f.mvp);
    puni3(prog, "u_cam", f.eye);
    puni3(prog, "u_sun", f.sun);
    puni1(prog, "u_sun_i", f.sun_intensity);
    puni1(prog, "u_exposure", f.exposure);
    punii(prog, "u_aov", g_aov);
    puni3(prog, "u_grade", f.grade);
    puni1(prog, "u_sat", f.saturation);
  };
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

    const SurfProg &sp = resolve_prog(P.surface_node);
    GLuint prog = sp.planet;
    if (!prog) continue;
    use(prog);
    puni3(prog, "u_center", o.pos);
    puni1(prog, "u_radius", P.radius);
    puni1(prog, "u_relief", P.relief);
    puni1(prog, "u_octf", octf);
    puni1(prog, "u_sea", P.sea_level);
    puni1(prog, "u_snow", P.snow_line);
    puni3(prog, "u_rock_lo", P.rock_low);
    puni3(prog, "u_rock_hi", P.rock_high);
    puni3(prog, "u_water_c", P.water_color);
    puni3(prog, "u_atmo_c", P.atmo_color);
    puni1(prog, "u_atmo", P.atmo_density);
    punii(prog, "u_aov", g_aov);
    punii(prog, "u_object_id", 3 + idx); // scene objects count from 3
    float spin = P.spin_deg * 0.017453293f;
    glUniform2f(glGetUniformLocation(prog, "u_spin"), std::cos(spin),
                std::sin(spin));
    upload_layers(prog, idx, 1.f, sp.glsl.empty() ? 0.f : sp.strength);
    glBindVertexArray(sph_vao[lod]);
    glDrawElements(GL_TRIANGLES, sph_count[lod], GL_UNSIGNED_INT, nullptr);
  }
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS); // back to the default the rest of the frame expects
}

void infinite_draw(const InfiniteFrame &f) {
  pl_refresh_programs();
  if (!infinite_layers_present()) return;
  // a planet small enough that the tile wraps it completely has no room
  // left for a surround: the tile *is* the surface
  if (f.planet_radius > 0.f && f.planet_radius < 1.f / 6.2831853f) return;
  // the home planet's graph is named on its first root layer
  SceneState &sc = scene();
  std::vector<int> roots = scene_surface_layers(-1);
  unsigned long long node = roots.empty() ? 0 : sc.objects[roots[0]].surf.surface_node;
  const SurfProg &sp = resolve_prog(node);
  GLuint prog_inf = sp.inf;
  if (!prog_inf) return;
  glUseProgram(prog_inf);
  glUniformMatrix4fv(glGetUniformLocation(prog_inf, "u_mvp"), 1, GL_FALSE, f.mvp);
  punii(prog_inf, "u_aov", g_aov);
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
  upload_layers(prog_inf, -1, amp / std::max(f.height_scale, 1e-4f),
                sp.glsl.empty() ? 0.f : sp.strength);
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
