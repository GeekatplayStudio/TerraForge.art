#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpx/planet_math.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace studio {

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

// ------------------------------------------------------------------ shaders
// GLSL mirror of gpx::planet (engine/gpx/planet_math.hpp). `octf` is a FLOAT
// octave count: the top octave fades in continuously with distance, so the
// level of detail changes without a single visible pop — the flicker-free
// requirement is solved here, not by blending frames.
static const char *PL_FN = R"GLSL(
float pl_hash(vec3 ip, uint seed){
  uvec3 q = uvec3(ivec3(ip));
  uint h = q.x*374761393u + q.y*668265263u + q.z*2147483647u + seed*3266489917u;
  h = (h ^ (h>>13u)) * 1274126177u;
  h ^= h>>16u;
  return float(h & 0xffffffu) / 16777215.0;
}
float pl_vnoise(vec3 p, uint seed){
  vec3 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  float c000=pl_hash(i,seed),               c100=pl_hash(i+vec3(1,0,0),seed);
  float c010=pl_hash(i+vec3(0,1,0),seed),   c110=pl_hash(i+vec3(1,1,0),seed);
  float c001=pl_hash(i+vec3(0,0,1),seed),   c101=pl_hash(i+vec3(1,0,1),seed);
  float c011=pl_hash(i+vec3(0,1,1),seed),   c111=pl_hash(i+vec3(1,1,1),seed);
  float x00=mix(c000,c100,f.x), x10=mix(c010,c110,f.x);
  float x01=mix(c001,c101,f.x), x11=mix(c011,c111,f.x);
  return mix(mix(x00,x10,f.y), mix(x01,x11,f.y), f.z);
}
float pl_fbm(vec3 p, uint seed, float octf, int type){
  float sum=0.0, amp=1.0, norm=0.0;
  vec3 q = p;
  for (int i = 0; i < 12; ++i){
    float w = clamp(octf - float(i), 0.0, 1.0);
    if (w <= 0.0) break;
    float n = pl_vnoise(q, seed + uint(i)*101u);
    if (type == 1) n = 1.0 - abs(n*2.0-1.0);
    else if (type == 2) n = abs(n*2.0-1.0);
    sum += n * amp * w;
    norm += amp * w;
    amp *= 0.5;
    q *= 2.03;
  }
  float v = norm > 0.0 ? sum/norm : 0.0;
  if (type == 1) v = v*v;
  return v - 0.5;
}
uniform vec4 u_la[6];  // freq, amp, coverage, mask_scale
uniform vec4 u_lb[6];  // seed, type, -, -
uniform int  u_lcount;
float pl_mask(vec3 d, int i){
  float cov = u_la[i].z;
  if (cov >= 0.999) return 1.0;
  if (cov <= 0.001) return 0.0;
  float m = pl_fbm(d * u_la[i].w, uint(u_lb[i].x) ^ 0x9e3779b9u, 3.0, 0) + 0.5;
  float edge = 1.0 - cov;
  return smoothstep(edge - 0.12, edge + 0.12, m);
}
float pl_height(vec3 d, float octf){
  float total = 0.0, wsum = 0.0;
  for (int i = 0; i < 6; ++i){
    if (i >= u_lcount) break;
    float amp = u_la[i].y;
    if (amp <= 0.0) continue;
    float m = pl_mask(d, i);
    wsum += amp;
    if (m <= 0.0) continue;
    total += pl_fbm(d * u_la[i].x, uint(u_lb[i].x), octf, int(u_lb[i].y)) * amp * m;
  }
  return wsum > 0.0 ? total / wsum : 0.0;
}
)GLSL";

static const char *VS_PLANET = R"GLSL(#version 430 core
layout(location=0) in vec3 in_dir;
uniform mat4 u_mvp;
uniform vec3 u_center;
uniform float u_radius, u_relief, u_octf, u_sea;
uniform vec2 u_spin; // cos, sin of the planet's static rotation
PL_FN_PLACEHOLDER
out vec3 v_dir;
out vec3 v_world;
void main(){
  vec3 d = normalize(in_dir);
  vec3 rd = vec3(d.x*u_spin.x - d.z*u_spin.y, d.y, d.x*u_spin.y + d.z*u_spin.x);
  float h = pl_height(rd, u_octf);
  // the ocean is a smooth sphere: submerged land never dents the water
  float hs = max(h, u_sea - 0.5);
  vec3 w = u_center + d * (u_radius * (1.0 + hs * u_relief));
  v_dir = d;
  v_world = w;
  gl_Position = u_mvp * vec4(w, 1.0);
  // planets are a sky layer: clamp depth just inside the far plane so no
  // planet is ever frustum-clipped, however far out the camera zooms
  gl_Position.z = min(gl_Position.z, gl_Position.w * 0.99999);
})GLSL";

static const char *FS_PLANET = R"GLSL(#version 430 core
in vec3 v_dir;
in vec3 v_world;
out vec4 frag;
uniform vec3 u_center, u_cam, u_sun;
uniform float u_radius, u_relief, u_octf, u_sea, u_snow;
uniform vec2 u_spin;
uniform vec3 u_rock_lo, u_rock_hi, u_water_c, u_atmo_c;
uniform float u_atmo, u_sun_i, u_exposure, u_sat;
uniform vec3 u_grade;
PL_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  vec3 d = normalize(v_dir);
  vec3 rd = vec3(d.x*u_spin.x - d.z*u_spin.y, d.y, d.x*u_spin.y + d.z*u_spin.x);
  // shading re-evaluates the surface two octaves beyond the geometry (the
  // Vue manual's trick): detail too fine for the mesh still shows as shading
  float octf = min(u_octf + 2.0, 12.0);
  vec3 t1 = normalize(cross(d, abs(d.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0)));
  vec3 t2 = cross(d, t1);
  float e = max(0.35 / exp2(octf), 0.00012);
  float h0 = pl_height(rd, octf);
  vec3 o1 = normalize(d + t1*e), o2 = normalize(d + t2*e);
  vec3 r1 = vec3(o1.x*u_spin.x - o1.z*u_spin.y, o1.y, o1.x*u_spin.y + o1.z*u_spin.x);
  vec3 r2 = vec3(o2.x*u_spin.x - o2.z*u_spin.y, o2.y, o2.x*u_spin.y + o2.z*u_spin.x);
  float h1 = pl_height(r1, octf);
  float h2 = pl_height(r2, octf);
  float g = u_relief / e;
  vec3 N = normalize(d - t1*(h1-h0)*g - t2*(h2-h0)*g);

  float sea = u_sea - 0.5;
  bool water = u_sea > 0.001 && h0 < sea;
  vec3 albedo;
  if (water){
    N = d;
    float depth = clamp((sea - h0) * 6.0, 0.0, 1.0);
    albedo = mix(u_water_c * 1.6, u_water_c * 0.55, depth);
  } else {
    float t = clamp((h0 - sea) / max(0.5 - sea, 0.05), 0.0, 1.0);
    albedo = mix(u_rock_lo, u_rock_hi, t);
    // polar caps + high-altitude snow, both softened by fractal variation
    float lat = abs(rd.y);
    float snow = smoothstep(u_snow - 0.08, u_snow + 0.08, t)
               + smoothstep(0.82, 0.95, lat + h0*0.3);
    albedo = mix(albedo, vec3(0.92, 0.93, 0.95), clamp(snow, 0.0, 1.0));
  }

  float NdL = max(dot(N, u_sun), 0.0);
  float day = smoothstep(-0.15, 0.25, dot(d, u_sun));
  vec3 col = albedo * (NdL * u_sun_i * 0.9 + 0.035) * max(day, 0.06);
  if (water){
    vec3 V = normalize(u_cam - v_world);
    vec3 H = normalize(V + u_sun);
    col += vec3(1.0, 0.97, 0.9) * pow(max(dot(N, H), 0.0), 180.0) * u_sun_i * day;
  }
  // atmosphere: limb glow, stronger on the lit side
  vec3 V = normalize(u_cam - v_world);
  float rim = pow(1.0 - clamp(dot(V, d), 0.0, 1.0), 2.6) * u_atmo;
  col = mix(col, u_atmo_c * (0.25 + 0.85*day) * u_sun_i, clamp(rim, 0.0, 0.85));

  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

// -------- home ground plane extended to the horizon -------------------------
static const char *VS_INF = R"GLSL(#version 430 core
layout(location=0) in vec2 in_p; // -1..1 param, concentrated near the tile
uniform mat4 u_mvp;
uniform sampler2D u_height;
uniform vec3 u_cam;
uniform float u_hscale, u_curve, u_amp, u_base;
PL_FN_PLACEHOLDER
out vec3 v_world;
out vec2 v_uv;
out float v_out;   // distance outside the tile, tile widths
void main(){
  // cubic concentration: vertex density is spent near the tile where the
  // blend must be exact, the far ring reaches ~30 tiles = the horizon
  vec2 off = in_p * (0.5 + 30.0 * in_p*in_p*in_p*in_p);
  vec2 uv = vec2(0.5) + off;
  vec2 uvc = clamp(uv, 0.0, 1.0);
  float dout = length(uv - uvc);
  float cam_d = max(length(u_cam.xz - uv) * 0.15, 0.02);
  float octf = clamp(9.0 - log2(cam_d) * 1.2, 2.0, 10.0);
  // The surround has to meet the tile at the tile's own level, or a step
  // appears all the way round it. This used to be a hardcoded fraction of the
  // height scale, which only looked right while every terrain was normalised
  // to fill 0..1; u_base is the tile's actual mean height.
  float proc = pl_height(vec3(uv.x, 0.37, uv.y), octf) * u_amp + u_base;
  float tile = texture(u_height, uvc).r * u_hscale;
  float s = smoothstep(0.0, 0.35, dout);
  float h = mix(tile, proc, s);
  vec3 p = vec3(uv.x, h, uv.y);
  if (u_curve > 0.0){
    vec2 fd = p.xz - u_cam.xz;
    p.y -= dot(fd, fd) / (2.0 * u_curve);
  }
  v_world = p; v_uv = uv; v_out = dout;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

static const char *FS_INF = R"GLSL(#version 430 core
in vec3 v_world;
in vec2 v_uv;
in float v_out;
out vec4 frag;
uniform sampler2D u_height, u_albedo;
uniform int u_has_albedo, u_fog_type;
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon, u_fog_color;
uniform float u_hscale, u_amp, u_sun_i, u_ambient, u_exposure, u_sat, u_fogd;
uniform vec3 u_grade;
PL_FN_PLACEHOLDER
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  // the tile itself is drawn by the terrain shader — never fight it
  if (v_out <= 0.0 &&
      all(greaterThan(v_uv, vec2(0.001))) && all(lessThan(v_uv, vec2(0.999))))
    discard;
  float cam_d = max(length(u_cam - v_world), 0.02);
  float octf = clamp(10.0 - log2(cam_d * 7.0) * 1.3, 2.0, 11.0);
  float e = max(0.5 / exp2(octf), 0.0004);
  float h0 = pl_height(vec3(v_uv.x, 0.37, v_uv.y), octf);
  float hx = pl_height(vec3(v_uv.x + e, 0.37, v_uv.y), octf);
  float hz = pl_height(vec3(v_uv.x, 0.37, v_uv.y + e), octf);
  vec3 N = normalize(vec3((h0-hx)*u_amp, e, (h0-hz)*u_amp));

  // palette: valley grass-rock into high rock into snow, steered by the
  // procedural height and slope; near the tile edge, borrow the tile's own
  // albedo so the seam is invisible
  float t = clamp(h0 + 0.5, 0.0, 1.0);
  float slope = 1.0 - N.y;
  vec3 col_lo = vec3(0.30, 0.31, 0.24);
  vec3 col_hi = vec3(0.46, 0.43, 0.40);
  vec3 alb = mix(col_lo, col_hi, clamp(t*1.3 + slope*2.0 - 0.35, 0.0, 1.0));
  alb = mix(alb, vec3(0.90, 0.91, 0.93),
            smoothstep(0.72, 0.85, t) * smoothstep(0.35, 0.15, slope));
  if (u_has_albedo == 1){
    vec3 edge = texture(u_albedo, clamp(v_uv, 0.0, 1.0)).rgb;
    alb = mix(edge, alb, smoothstep(0.0, 1.2, v_out));
  }

  float NdL = max(dot(N, u_sun), 0.0);
  vec3 col = alb * (u_sun_color * u_sun_i * NdL * 0.8 / 3.14159
                    + mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient
                          * (0.45 + 0.55*N.y));
  if (u_fog_type > 0){
    float f = 1.0 - exp(-cam_d * u_fogd * 0.35);
    col = mix(col, u_fog_color, clamp(f, 0.0, 1.0));
  }
  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

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
    std::fprintf(stderr, "planet shader error: %s\n", log);
  }
  return sh;
}

static GLuint pl_link(const char *vs, const char *fs) {
  auto inject = [](const char *src) {
    std::string s(src);
    size_t p;
    while ((p = s.find("PL_FN_PLACEHOLDER")) != std::string::npos)
      s.replace(p, strlen("PL_FN_PLACEHOLDER"), PL_FN);
    return s;
  };
  GLuint p = glCreateProgram();
  GLuint v = pl_compile(GL_VERTEX_SHADER, inject(vs));
  GLuint f = pl_compile(GL_FRAGMENT_SHADER, inject(fs));
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
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
  return n;
}

bool infinite_layers_present() {
  return !scene_surface_layers(-1).empty();
}

void planet_draw_all(const PlanetFrame &f) {
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
}

void infinite_draw(const InfiniteFrame &f) {
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
