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
// The uniforms and the body that place a terrain vertex. Shared verbatim
// between the plain vertex shader and the tessellation evaluation shader, so
// the two paths cannot drift: whichever one runs, the surface is the same
// surface. Both call TERRAIN_PLACE with the uv they arrived at.
static const char *TERRAIN_VERT_COMMON = R"GLSL(
uniform sampler2D u_height;
uniform sampler2D u_disp;
uniform int u_has_disp;
uniform float u_disp_strength;
uniform mat4 u_mvp;
uniform float u_hscale;
uniform vec3 u_cam;
uniform float u_frac_amount;   // fractal detail height, world units
uniform float u_frac_scale;    // base frequency of the detail
uniform float u_planet_radius; // 0 = flat, else curve the world down
uniform float u_field_strength; // graph-authored displacement, 0 = none
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
out vec2 v_uv;
out vec3 v_world;
out float v_detail;
void terrain_place(vec2 uv){
  float h = texture(u_height, uv).r * u_hscale;
  if (u_has_disp == 1)
    h += (texture(u_disp, uv).r - 0.5) * 2.0 * u_disp_strength;
  vec3 p = vec3(uv.x, h, uv.y);
  // A displacement graph authored by the user, evaluated per vertex on the
  // GPU. The same function runs on the CPU for picking and baking, which is
  // what the CPU/GPU agreement check exists to keep true.
  if (u_field_strength != 0.0)
    p.y += gpx_terrain_field(p, vec3(0.0,1.0,0.0), h, 1.0, 0.0, 0.0,
                             gp_octavesf(length(u_cam - p), 9.0)).x *
           u_field_strength;
  // fractal micro-relief, refined by how close the camera is
  float d = length(u_cam - p);
  v_detail = 0.0;
  if (u_frac_amount > 0.0){
    int oct = gp_octaves(d, 9.0);
    if (oct > 0){
      float f = gp_detail(uv, u_frac_scale, oct, 0.5) - 0.5;
      v_detail = f;
      p.y += f * u_frac_amount;
    }
  }
  // planetary curvature: the ground falls away with distance
  if (u_planet_radius > 0.0){
    vec2 flat_d = p.xz - u_cam.xz;
    float r2 = dot(flat_d, flat_d);
    p.y -= r2 / (2.0 * u_planet_radius);
  }
  v_uv = uv; v_world = p;
  gl_Position = u_mvp * vec4(p,1.0);
}
)GLSL";

// The fixed-grid path: one vertex per grid point, as before.
static const char *VS_TERRAIN_TAIL = R"GLSL(
layout(location=0) in vec2 in_uv;
void main(){ terrain_place(in_uv); }
)GLSL";

// ---------------------------------------------------- adaptive subdivision
// A fixed grid spends the same triangles on a ridge filling the screen and on
// one at the horizon, and caps how fine a displacement can ever be. Vue calls
// the alternative Dynamic subdivision (p719) and Terragen's displacement is
// strong for exactly this reason: the surface is subdivided to whatever the
// camera needs, then displaced.
//
// The control shader chooses a level per *edge* from that edge's length in
// pixels. Both patches sharing an edge compute it from the same two endpoints,
// so they always agree and no crack can open between them.
static const char *TCS_TERRAIN = R"GLSL(#version 430 core
layout(vertices = 4) out;
in vec2 tc_uv[];
out vec2 te_uv[];
uniform sampler2D u_height;
uniform float u_hscale;
uniform mat4 u_mvp;
uniform vec2 u_viewport;
uniform float u_tess_px;   // target pixels per triangle edge
uniform float u_tess_min;  // floor, so this is never coarser than the old grid
uniform float u_tess_max;  // Vue's "limit automatic subdivision"
vec2 screen_of(vec2 uv){
  vec3 p = vec3(uv.x, texture(u_height, uv).r * u_hscale, uv.y);
  vec4 c = u_mvp * vec4(p, 1.0);
  // a point behind the camera has a tiny or negative w; clamp rather than
  // divide by it, or one such vertex tessellates the whole patch to death
  return c.xy / max(abs(c.w), 1e-4) * 0.5 * u_viewport;
}
// The floor matters as much as the ceiling. Displacement and fractal relief
// are evaluated per vertex, so a patch that subdivides to nothing loses them —
// and when the whole tile is small on screen, a purely screen-space metric
// asks for exactly that. The floor keeps this at least as fine as the fixed
// grid it replaces, so adaptive subdivision can only ever add detail.
float edge_tess(vec2 a, vec2 b){
  float px = distance(screen_of(a), screen_of(b));
  return clamp(px / max(u_tess_px, 1.0), u_tess_min, u_tess_max);
}
void main(){
  te_uv[gl_InvocationID] = tc_uv[gl_InvocationID];
  if (gl_InvocationID == 0){
    // outer[i] is the edge opposite corner i in GL's quad convention
    gl_TessLevelOuter[0] = edge_tess(tc_uv[3], tc_uv[0]);
    gl_TessLevelOuter[1] = edge_tess(tc_uv[0], tc_uv[1]);
    gl_TessLevelOuter[2] = edge_tess(tc_uv[1], tc_uv[2]);
    gl_TessLevelOuter[3] = edge_tess(tc_uv[2], tc_uv[3]);
    gl_TessLevelInner[0] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
    gl_TessLevelInner[1] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
  }
}
)GLSL";

// fractional_odd_spacing so a patch's level changes continuously as the camera
// moves. Integer spacing would step, and a stepping subdivision pops — the
// same reason the octave count is a float (AGENTS.md, planets rule 3).
static const char *TES_TERRAIN_TAIL = R"GLSL(
layout(quads, fractional_odd_spacing, ccw) in;
in vec2 te_uv[];
void main(){
  vec2 lo = mix(te_uv[0], te_uv[1], gl_TessCoord.x);
  vec2 hi = mix(te_uv[3], te_uv[2], gl_TessCoord.x);
  terrain_place(mix(lo, hi, gl_TessCoord.y));
}
)GLSL";

static const char *VS_TERRAIN_PASS = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
out vec2 tc_uv;
void main(){ tc_uv = in_uv; }
)GLSL";

// Procedural fractal detail shared by the vertex and fragment stages: the
// baked heightmap carries the large forms, these octaves keep resolving as
// the camera closes in, so the terrain is fractal rather than a fixed grid.
static const char *FRACTAL_FN = R"GLSL(
float gp_hash(vec2 p){
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float gp_vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(gp_hash(i), gp_hash(i + vec2(1,0)), f.x),
             mix(gp_hash(i + vec2(0,1)), gp_hash(i + vec2(1,1)), f.x), f.y);
}
// ridged fBm; `octaves` is chosen from camera distance so cost scales with
// how much detail is actually visible
float gp_detail(vec2 uv, float base_freq, int octaves, float gain){
  float sum = 0.0, amp = 1.0, norm = 0.0, freq = base_freq;
  for (int i = 0; i < 12; ++i){
    if (i >= octaves) break;
    float n = gp_vnoise(uv * freq + float(i) * 17.3);
    n = 1.0 - abs(n * 2.0 - 1.0);      // ridged
    sum += n * amp;
    norm += amp;
    amp *= gain;
    freq *= 2.03;                       // slightly irrational: avoids banding
  }
  return norm > 0.0 ? sum / norm : 0.0;
}
// How much detail is worth evaluating at this distance, as a continuous
// value. A generated field graph must get this rather than the truncated
// form: stepping a detail budget with an int makes the surface pop as the
// camera moves (AGENTS.md, planets rule 3).
float gp_octavesf(float dist, float max_oct){
  return clamp(log2(1.0 / max(dist, 1e-4)) * 0.9 + 4.0, 0.0, max_oct);
}
// the integer form the fractal detail loop needs
int gp_octaves(float dist, float max_oct){
  return int(gp_octavesf(dist, max_oct));
}
)GLSL";

// shared sky helper injected into several shaders
static const char *SKY_FN = R"GLSL(
vec3 sky_color(vec3 dir, vec3 zenith_c, vec3 horizon_c, vec3 sun, vec3 sun_col,
               float atmo){
  float t = clamp(dir.y*0.5+0.5, 0.0, 1.0);
  vec3 col = mix(horizon_c, zenith_c, pow(t, 0.7/max(atmo,0.05)));
  float low = 1.0 - clamp(sun.y*3.0, 0.0, 1.0);
  col = mix(col, col * vec3(1.15,0.85,0.65), low*0.5*atmo);
  float s = max(dot(dir, sun), 0.0);
  col += sun_col * pow(s, 12.0) * 0.18 * atmo;
  return col;
}
)GLSL";

static const char *FS_TERRAIN_SRC = R"GLSL(#version 430 core
in vec2 v_uv;
in vec3 v_world;
out vec4 frag;
uniform sampler2D u_height;
uniform sampler2D u_albedo;
uniform sampler2D u_normal_map;
uniform sampler2D u_rough_map;
uniform sampler2DShadow u_shadowmap;
uniform sampler3D u_cl_shape;
uniform mat4 u_light_mvp;
uniform int u_has_albedo, u_has_normal, u_has_rough, u_shadows, u_quality;
uniform float u_shadow_soft, u_hscale, u_texel, u_exposure;
uniform vec3 u_sun, u_sun_color, u_cam, u_sky_zenith, u_sky_horizon;
uniform float u_sun_intensity, u_ambient, u_atmo;
// material
uniform float u_roughness, u_metallic, u_specular, u_reflection;
uniform float u_translucency, u_transparency, u_normal_strength;
// fog
uniform int u_fog_type;
uniform float u_fog_density, u_fog_level, u_fog_falloff, u_fog_scatter;
uniform vec3 u_fog_color, u_absorb;
// sculpt brush cursor: xy = terrain uv, z = radius (<=0 hides), w = erase flag
uniform vec4 u_brush;
// cloud shadows
uniform int u_cloud_shadows;
uniform vec3 u_cam_unused_marker;
uniform float u_frac_amount;
uniform float u_frac_scale;
in float v_detail;
uniform float u_cl_cov, u_cl_alt, u_cl_time;
uniform vec2 u_cl_wind;
const float PI = 3.14159265;
uniform float u_field_strength;
uniform int u_surface_on;
uniform int u_surf_rough_on;
uniform int u_surf_bump_on;
uniform float u_surf_bump_strength;
uniform float u_surf_bump_scale;
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
GPX_SURFACE_PLACEHOLDER
GPX_ROUGH_PLACEHOLDER
GPX_BUMP_PLACEHOLDER
SKY_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}

vec3 get_normal(vec2 uv){
  float e = u_texel;
  float hl = texture(u_height, uv - vec2(e,0)).r;
  float hr = texture(u_height, uv + vec2(e,0)).r;
  float hd = texture(u_height, uv - vec2(0,e)).r;
  float hu = texture(u_height, uv + vec2(0,e)).r;
  vec3 n = normalize(vec3((hl-hr)*u_hscale, 2.0*e, (hd-hu)*u_hscale));
  // The graph-authored displacement moved the geometry, so the normal has to
  // move with it or the lighting describes a surface that is not there.
  // Central differences, matching FieldComputeNormal on the CPU.
  if (u_field_strength != 0.0){
    float lodf = gp_octavesf(length(u_cam - v_world), 9.0);
    vec3 pc = v_world;
    float fxp = gpx_terrain_field(pc + vec3(e,0,0), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float fxm = gpx_terrain_field(pc - vec3(e,0,0), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float fzp = gpx_terrain_field(pc + vec3(0,0,e), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float fzm = gpx_terrain_field(pc - vec3(0,0,e), vec3(0,1,0), pc.y, 1.0, 0.0, 0.0, lodf).x;
    float k = u_field_strength / max(2.0*e, 1e-5);
    n = normalize(n + vec3(-(fxp-fxm)*k, 0.0, -(fzp-fzm)*k));
  }
  // fractal detail continues below the heightmap's resolution: perturb the
  // normal with the same octaves the vertex stage used, plus finer ones
  if (u_frac_amount > 0.0){
    float dist = length(u_cam - v_world);
    int oct = gp_octaves(dist, 11.0);
    if (oct > 0){
      float e2 = max(u_texel * 0.35, 1e-5);
      float c  = gp_detail(uv, u_frac_scale, oct, 0.5);
      float dx = gp_detail(uv + vec2(e2,0), u_frac_scale, oct, 0.5) - c;
      float dy = gp_detail(uv + vec2(0,e2), u_frac_scale, oct, 0.5) - c;
      float k = u_frac_amount / max(e2, 1e-5) * 0.25;
      n = normalize(n + vec3(-dx * k, 0.0, -dy * k));
    }
  }
  return n;
}

float shadow_factor(vec3 world){
  if (u_shadows == 0) return 1.0;
  vec4 lp = u_light_mvp * vec4(world, 1.0);
  vec3 pc = lp.xyz / lp.w * 0.5 + 0.5;
  if (pc.x < 0.0 || pc.x > 1.0 || pc.y < 0.0 || pc.y > 1.0 || pc.z > 1.0) return 1.0;
  float bias = 0.004;
  float s = 0.0;
  float texel = 1.0 / 2048.0 * u_shadow_soft;
  int R = (u_quality == 1) ? 2 : 1;
  float cnt = 0.0;
  for (int dy = -R; dy <= R; ++dy)
    for (int dx = -R; dx <= R; ++dx){
      s += texture(u_shadowmap, vec3(pc.xy + vec2(dx,dy)*texel, pc.z - bias));
      cnt += 1.0;
    }
  return s / cnt;
}

// horizon-based ambient occlusion marched against the heightfield
float terrain_ao(vec2 uv, float h){
  if (u_quality == 0) return 1.0;
  float occ = 0.0;
  const int DIRS = 6;
  for (int d = 0; d < DIRS; ++d){
    float a = float(d) * (6.2831853/float(DIRS));
    vec2 dir = vec2(cos(a), sin(a));
    float horiz = 0.0;
    for (int s = 1; s <= 4; ++s){
      float r = float(s) * u_texel * 5.0;
      float hs = texture(u_height, uv + dir*r).r;
      horiz = max(horiz, (hs - h) * u_hscale / r);
    }
    occ += horiz / (1.0 + horiz);
  }
  return clamp(1.0 - occ / float(DIRS) * 1.3, 0.15, 1.0);
}

float cloud_shadow(vec3 world){
  if (u_cloud_shadows == 0) return 1.0;
  // project the point up to the cloud slab along the sun direction
  float dy = max(u_cl_alt - world.y, 0.0);
  if (u_sun.y < 0.05) return 1.0;
  vec3 p = world + u_sun * (dy / max(u_sun.y, 0.05));
  vec3 wp = p; wp.xz += u_cl_wind * u_cl_time;
  vec4 sn = texture(u_cl_shape, wp * 0.35);
  float fbm = sn.g*0.625 + sn.b*0.25 + sn.a*0.125;
  float shape = clamp((sn.r - (fbm - 1.0)) / max(2.0 - fbm, 1e-3), 0.0, 1.0);
  float d = clamp((shape - (1.0 - u_cl_cov)) / max(u_cl_cov, 1e-3), 0.0, 1.0);
  return 1.0 - d * 0.65;
}

vec3 apply_fog(vec3 col, vec3 world, vec3 cam, float dist){
  if (u_fog_type == 0 || u_fog_density <= 0.0) return col;
  float level = u_fog_level * u_hscale * 4.0;
  float falloff = u_fog_falloff / max(u_hscale, 1e-3);
  float fy0 = cam.y - level, fy1 = world.y - level;
  float dY = fy1 - fy0;
  float a = exp(-falloff * max(fy0, 0.0));
  float b = exp(-falloff * max(fy1, 0.0));
  float od = (abs(falloff*dY) < 1e-3) ? dist * a
                                      : abs(dist * (a - b) / (falloff * dY));
  float dens = u_fog_density * (u_fog_type == 1 ? 0.35 : (u_fog_type == 2 ? 1.0 : 1.8));
  float f = clamp(1.0 - exp(-od * dens), 0.0, 1.0);
  float sunward = pow(max(dot(normalize(world - cam), u_sun), 0.0), 6.0);
  vec3 fogc = u_fog_color * mix(vec3(1.0), u_sun_color * 1.6, sunward * u_fog_scatter);
  if (u_fog_type == 3) fogc *= vec3(0.85, 0.75, 0.6);
  col *= mix(vec3(1.0), u_absorb, f);
  return mix(col, fogc, f);
}

void main(){
  vec3 N = get_normal(v_uv);
  if (u_has_normal == 1){
    vec3 nm = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;
    nm.xy *= u_normal_strength;
    // terrain tangent frame: +X tangent, +Z bitangent
    vec3 T = normalize(vec3(1.0, 0.0, 0.0) - N * N.x);
    vec3 B = cross(N, T);
    N = normalize(T * nm.x + B * nm.y + N * max(nm.z, 0.05));
  }
  float h = texture(u_height, v_uv).r;
  vec3 albedo;
  if (u_surface_on == 1) {
    // A colour field shades the terrain per pixel. Slope and facing come from
    // the shaded normal, so a distribution keyed on steepness follows detail
    // finer than the heightmap carrying it.
    float sl = N.y;
    float orient = atan(N.x, N.z) / PI;
    albedo = gpx_terrain_surface(v_world, N, v_world.y, sl, orient, 0.0,
                                 gp_octavesf(length(u_cam - v_world), 11.0)).rgb;
  } else if (u_has_albedo == 1) {
    albedo = pow(texture(u_albedo, v_uv).rgb, vec3(2.2));
  } else {
    float slope = 1.0 - N.y;
    vec3 rock = vec3(0.30,0.27,0.25), grass = vec3(0.13,0.20,0.08);
    vec3 snow = vec3(0.90,0.91,0.95), dirt = vec3(0.24,0.19,0.14);
    albedo = mix(grass, dirt, smoothstep(0.05,0.25,slope));
    albedo = mix(albedo, rock, smoothstep(0.18,0.45,slope));
    albedo = mix(albedo, snow, smoothstep(0.62,0.72,h)*(1.0-smoothstep(0.25,0.5,slope)));
  }
  float rough = clamp(u_roughness * (u_has_rough == 1 ?
                      texture(u_rough_map, v_uv).r * 2.0 : 1.0), 0.03, 1.0);
  if (u_surf_rough_on == 1){
    float lodr = gp_octavesf(length(u_cam - v_world), 11.0);
    rough = clamp(gpx_terrain_rough(v_world, N, v_world.y, N.y,
                                    atan(N.x, N.z) / PI, 0.0, lodr).x,
                  0.03, 1.0);
  }
  // A bump field tilts the normal without moving the geometry. Central
  // differences of the same function, so it describes the surface the colour
  // graph describes.
  if (u_surf_bump_on == 1){
    float e = u_surf_bump_scale;
    float lodb = gp_octavesf(length(u_cam - v_world), 11.0);
    float o = atan(N.x, N.z) / PI;
    float bx = gpx_terrain_bump(v_world + vec3(e,0,0), N, v_world.y, N.y, o, 0.0, lodb).x
             - gpx_terrain_bump(v_world - vec3(e,0,0), N, v_world.y, N.y, o, 0.0, lodb).x;
    float bz = gpx_terrain_bump(v_world + vec3(0,0,e), N, v_world.y, N.y, o, 0.0, lodb).x
             - gpx_terrain_bump(v_world - vec3(0,0,e), N, v_world.y, N.y, o, 0.0, lodb).x;
    float k = u_surf_bump_strength / max(2.0*e, 1e-5);
    N = normalize(N + vec3(-bx * k, 0.0, -bz * k));
  }
  vec3 V = normalize(u_cam - v_world);
  vec3 L = u_sun;
  vec3 H = normalize(L + V);
  float NdL = max(dot(N, L), 0.0);
  float NdV = max(dot(N, V), 1e-4);
  float NdH = max(dot(N, H), 0.0);
  float VdH = max(dot(V, H), 0.0);

  float shadow = shadow_factor(v_world + N * 0.002) * cloud_shadow(v_world);
  float ao = terrain_ao(v_uv, h);

  vec3 F0 = mix(vec3(0.08 * u_specular), albedo, u_metallic);
  float a = rough * rough;
  float a2 = a * a;
  float dnm = (NdH*NdH*(a2-1.0)+1.0);
  float D = a2 / max(PI * dnm * dnm, 1e-6);
  float k = (rough + 1.0); k = k*k/8.0;
  float G = (NdL/(NdL*(1.0-k)+k)) * (NdV/(NdV*(1.0-k)+k));
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
  vec3 spec = D * G * F / max(4.0 * NdL * NdV, 1e-4);

  vec3 sun_c = u_sun_color * u_sun_intensity;
  vec3 kd = (1.0 - F) * (1.0 - u_metallic);
  vec3 direct = (kd * albedo / PI + spec) * sun_c * NdL * shadow;

  vec3 sky_amb = mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient;
  vec3 ambient = albedo * sky_amb * (0.5 + 0.5*N.y) * ao;
  ambient += albedo * vec3(0.25,0.22,0.18) * 0.25 * u_ambient * (1.0 - N.y) * ao;

  // sky reflection
  vec3 R = reflect(-V, N);
  vec3 refl = sky_color(R, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  float fres = F0.g + (1.0 - F0.g) * pow(1.0 - NdV, 5.0);
  vec3 reflection = refl * fres * u_reflection * (1.0 - rough) * ao;

  // translucency (light bleeding through thin material toward the viewer)
  float trans_term = pow(max(dot(V, -L), 0.0), 3.0);
  vec3 translucent = albedo * u_translucency * trans_term * sun_c * 0.6;

  vec3 col = direct + ambient + reflection + translucent;
  float d = length(v_world - u_cam);
  col = apply_fog(col, v_world, u_cam, d);
  col = aces(col * u_exposure);
  col = pow(col, vec3(1.0/2.2));
  // sculpt brush ring, drawn on the surface after grading so it stays legible
  if (u_brush.z > 0.0) {
    float bd = distance(v_uv, u_brush.xy);
    float ring = abs(bd - u_brush.z);
    float px = fwidth(bd) * 1.5;
    float line = 1.0 - smoothstep(px, px * 2.5, ring);
    vec3 bcol = u_brush.w > 0.5 ? vec3(0.85, 0.30, 0.22) : vec3(0.90, 0.55, 0.20);
    col = mix(col, bcol, line * 0.85);
    // soft fill hinting at falloff
    float fill = 1.0 - smoothstep(0.0, u_brush.z, bd);
    col = mix(col, bcol, fill * fill * 0.10);
  }
  frag = vec4(col, 1.0 - u_transparency);
})GLSL";

// The shadow pass has to see the same surface the camera does. It always
// ignored the fractal micro-relief, which is fine — that detail is far below
// the shadow map's resolution. A graph displacement is not: it can move the
// surface by a large fraction of the terrain's whole height, and terrain that
// casts a shadow from where it used to be looks broken rather than subtle.
//
// The detail budget is fixed rather than camera-derived, because there is no
// camera here — the shadow map is rendered from the sun.
static const char *VS_DEPTH_SRC = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
uniform sampler2D u_height;
uniform mat4 u_light_mvp;
uniform float u_hscale;
uniform float u_field_strength;
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
void main(){
  float h = texture(u_height, in_uv).r * u_hscale;
  vec3 p = vec3(in_uv.x, h, in_uv.y);
  if (u_field_strength != 0.0)
    p.y += gpx_terrain_field(p, vec3(0.0,1.0,0.0), h, 1.0, 0.0, 0.0, 7.0).x *
           u_field_strength;
  gl_Position = u_light_mvp * vec4(p, 1.0);
})GLSL";

static const char *FS_DEPTH = R"GLSL(#version 430 core
void main(){})GLSL";

static const char *VS_WATER = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
uniform mat4 u_mvp;
uniform float u_level;
out vec2 v_uv;
out vec3 v_world;
void main(){
  vec3 p = vec3(in_uv.x, u_level, in_uv.y);
  v_uv = in_uv; v_world = p;
  gl_Position = u_mvp * vec4(p,1.0);
})GLSL";

static const char *FS_WATER = R"GLSL(#version 430 core
in vec2 v_uv;
in vec3 v_world;
out vec4 frag;
uniform sampler2D u_height;
uniform float u_hscale, u_level, u_time, u_exposure;
uniform vec3 u_sun, u_sun_color, u_cam;
uniform vec3 u_deep, u_shallow;
uniform float u_wave_amp, u_wave_scale, u_wave_speed, u_clarity, u_opacity;
uniform vec3 u_sky_zenith, u_sky_horizon;
uniform float u_atmo;
uniform int u_foam_on;
uniform vec3 u_foam_color;
uniform float u_foam_amount, u_foam_scale, u_foam_crests;
uniform float u_roughness, u_reflection;
SKY_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
float hash21(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),
             mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),f.x), f.y);
}
void main(){
  float bed = texture(u_height, v_uv).r * u_hscale;
  float depth = u_level - bed;
  if (depth <= 0.0) discard;
  float t = u_time * u_wave_speed;
  float k = u_wave_scale;
  float w1 = sin(v_uv.x*140.0*k + t*1.3)*0.5 + sin(v_uv.y*120.0*k - t*1.7)*0.5;
  float w2 = sin((v_uv.x*90.0 - v_uv.y*70.0)*k + t*0.9);
  float w3 = sin((v_uv.x*47.0 + v_uv.y*61.0)*k - t*0.6);
  vec3 n = normalize(vec3((w1+w3*0.5)*0.02*u_wave_amp, 1.0, (w2+w3*0.5)*0.02*u_wave_amp));
  vec3 vdir = normalize(u_cam - v_world);
  float fresnel = pow(1.0 - max(dot(n, vdir),0.0), 5.0)*0.9 + 0.06;
  vec3 water = mix(u_shallow, u_deep, clamp(depth*u_clarity,0.0,1.0));
  vec3 R = reflect(-vdir, n);
  vec3 skyr = sky_color(R, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  vec3 col = mix(water, skyr, fresnel * (0.5 + 0.5*u_reflection));
  float spec = pow(max(dot(reflect(-u_sun, n), vdir),0.0), mix(900.0, 120.0, u_roughness));
  col += u_sun_color * spec * 2.0;
  float alpha = clamp(0.55 + depth*10.0, 0.0, u_opacity);
  if (u_foam_on == 1) {
    float fn = vnoise(v_uv*60.0*u_foam_scale + vec2(t*0.15, -t*0.1));
    fn = fn*0.6 + 0.4*vnoise(v_uv*140.0*u_foam_scale - vec2(t*0.22, t*0.13));
    float shore_w = 0.012 * u_foam_amount * u_hscale;
    float pulse = 0.6 + 0.4*sin(t*1.8 + v_uv.x*30.0 + v_uv.y*24.0);
    float shore = (1.0 - smoothstep(0.0, shore_w * (0.6+pulse), depth));
    shore *= smoothstep(0.35, 0.75, fn) * u_foam_amount * 1.6;
    float crest = smoothstep(1.05, 1.45, w1 + w2*0.5) * u_foam_crests;
    crest *= smoothstep(0.45, 0.8, fn);
    float foam = clamp(shore + crest, 0.0, 1.0);
    col = mix(col, u_foam_color, foam);
    alpha = max(alpha, foam * 0.95);
  }
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, alpha);
})GLSL";

static const char *VS_SKY = R"GLSL(#version 430 core
out vec2 v_ndc;
void main(){
  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0-1.0;
  v_ndc = p;
  gl_Position = vec4(p, 0.99999, 1.0);
})GLSL";

static const char *FS_SKY_SRC = R"GLSL(#version 430 core
in vec2 v_ndc;
out vec4 frag;
uniform mat4 u_inv_vp;
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_atmo;
uniform int u_fog_type;
uniform vec3 u_fog_color;
uniform float u_fog_density;
// volumetric clouds
uniform int u_clouds, u_cl_steps, u_cl_type;
uniform sampler3D u_cl_shape;
uniform sampler3D u_cl_detail;
uniform float u_cl_cov, u_cl_den, u_cl_alt, u_cl_thick, u_cl_detail_amt;
uniform float u_cl_time, u_cl_ambient, u_cl_anvil;
uniform float u_sun_intensity;
uniform vec2 u_cl_wind;
uniform vec3 u_cl_color;
// panorama export: equirectangular directions, linear HDR out, no sun disc
uniform int u_panorama;
uniform int u_hdr;
uniform int u_no_sun;
uniform float u_space; // 0 = inside the atmosphere, 1 = open space
const float PI = 3.14159265;
SKY_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
float remap01(float v, float lo, float hi){ return clamp((v-lo)/max(hi-lo,1e-4), 0.0, 1.0); }
// remap that allows a negative low bound (the standard cloud shaping form)
float remapf(float v, float lo, float hi, float nlo, float nhi){
  return nlo + (v - lo) / max(hi - lo, 1e-4) * (nhi - nlo);
}
float hg(float c, float g){
  float g2 = g*g;
  return (1.0-g2) / (4.0*PI*pow(max(1.0+g2-2.0*g*c, 1e-4), 1.5));
}
float cloud_gradient(float hf){
  if (u_cl_type == 0)            // stratus: low flat sheet
    return remap01(hf, 0.0, 0.08) * (1.0 - remap01(hf, 0.18, 0.36));
  if (u_cl_type == 2){           // cumulonimbus: tall with anvil top
    float base = remap01(hf, 0.0, 0.08);
    float top = 1.0 - remap01(hf, 0.75 + u_cl_anvil*0.2, 1.0);
    return base * top;
  }
  return remap01(hf, 0.0, 0.16) * (1.0 - remap01(hf, 0.45, 0.85)); // cumulus
}
float cloud_density(vec3 p, out float hf){
  hf = clamp((p.y - u_cl_alt) / max(u_cl_thick, 1e-3), 0.0, 1.0);
  vec3 wp = p; wp.xz += u_cl_wind * u_cl_time;
  vec4 sn = texture(u_cl_shape, wp * 0.18);
  float fbm = sn.g*0.625 + sn.b*0.25 + sn.a*0.125;
  // base shape: Perlin-Worley eroded by the Worley FBM (Schneider/Guerrilla)
  float shape = clamp(remapf(sn.r, fbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
  shape *= cloud_gradient(hf);
  float d = clamp(remapf(shape, 1.0 - u_cl_cov, 1.0, 0.0, 1.0), 0.0, 1.0);
  if (d <= 0.001) return 0.0;
  vec3 dp = p * 2.6; dp.xz += u_cl_wind * u_cl_time * 2.0;
  vec3 dn = texture(u_cl_detail, dp).rgb;
  float dfbm = dn.r*0.625 + dn.g*0.25 + dn.b*0.125;
  float er = mix(dfbm, 1.0 - dfbm, clamp(hf*4.0, 0.0, 1.0));
  d = clamp(remapf(d, er * u_cl_detail_amt * 0.55, 1.0, 0.0, 1.0), 0.0, 1.0);
  return d * u_cl_den;
}
vec4 march_clouds(vec3 ro, vec3 rd, vec3 bg){
  if (u_clouds == 0 || rd.y < 0.015) return vec4(bg, 1.0);
  float y0 = u_cl_alt, y1 = u_cl_alt + u_cl_thick;
  float t0 = (y0 - ro.y) / rd.y;
  float t1 = (y1 - ro.y) / rd.y;
  if (ro.y > y0 && ro.y < y1) t0 = 0.0;
  t0 = max(t0, 0.0); t1 = max(t1, 0.0);
  if (t1 <= t0) return vec4(bg, 1.0);
  t1 = min(t1, t0 + 30.0);
  int steps = u_cl_steps;
  float dt = (t1 - t0) / float(steps);
  float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898,78.233))) * 43758.5453);
  float cosA = dot(rd, u_sun);
  // dual-lobe HG, renormalised by 4*pi so the phase reads ~0.2..2 instead of
  // the tiny per-steradian value (otherwise clouds vanish against the sky)
  float phase = mix(hg(cosA, 0.75), hg(cosA, -0.25), 0.4) * 12.566;
  vec3 amb_col = sky_color(vec3(0,1,0), u_sky_zenith, u_sky_horizon, u_sun,
                           u_sun_color, u_atmo) * u_cl_ambient;
  float transmittance = 1.0;
  vec3 scatter = vec3(0.0);
  for (int i = 0; i < steps; ++i){
    float t = t0 + dt * (float(i) + jitter);
    vec3 p = ro + rd * t;
    float hf;
    float d = cloud_density(p, hf);
    if (d > 0.002){
      // light march toward the sun
      float ldt = u_cl_thick / 5.0;
      float sum = 0.0;
      for (int j = 0; j < 5; ++j){
        float hf2;
        vec3 lp = p + u_sun * (ldt * (float(j) + 0.5));
        sum += cloud_density(lp, hf2) * ldt;
      }
      float light_trans = exp(-sum * 2.2);
      float powder = 1.0 - exp(-d * 4.0);
      vec3 sun_c = u_sun_color * u_sun_intensity * 2.2 * light_trans * phase *
                   mix(1.0, powder, 0.55);
      vec3 lum = (sun_c + amb_col * (0.35 + 0.65*hf)) * u_cl_color;
      float ext = d * dt * 3.0;
      scatter += transmittance * lum * d * dt * 3.0;
      transmittance *= exp(-ext);
      if (transmittance < 0.012) break;
    }
  }
  return vec4(bg * transmittance + scatter, transmittance);
}
void main(){
  vec3 dir;
  if (u_panorama == 1) {
    float az = v_ndc.x * PI;
    float el = v_ndc.y * PI * 0.5;
    dir = vec3(cos(el)*cos(az), sin(el), cos(el)*sin(az));
  } else {
    vec4 w = u_inv_vp * vec4(v_ndc, 1.0, 1.0);
    dir = normalize(w.xyz / w.w);
  }
  vec3 col = sky_color(dir, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  if (u_no_sun == 0) {
    float s = max(dot(dir, u_sun), 0.0);
    col += u_sun_color * pow(s, 700.0) * 8.0;   // sun disc
  }
  vec4 cl = march_clouds(u_cam, dir, col);
  col = cl.rgb;
  if (u_fog_type != 0) {
    float horizon_fog = pow(1.0 - clamp(dir.y, 0.0, 1.0), 8.0);
    col = mix(col, u_fog_color, clamp(horizon_fog * u_fog_density * 0.6, 0.0, 1.0));
  }
  // Leaving the atmosphere: the sky thins to space so that pulling the camera
  // back actually reveals the planets instead of drowning them in daylight
  // haze. u_space is a smooth 0..1 from the camera's altitude, so the
  // transition is continuous — no popping at any zoom level.
  if (u_space > 0.0) {
    vec3 stars = vec3(0.0);
    // stable star field: hashed on the quantized view direction
    vec3 sd = dir * 380.0;
    vec3 si = floor(sd);
    float hs = fract(sin(dot(si, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    if (hs > 0.9965) {
      float tw = 0.55 + 0.45 * fract(hs * 91.7);
      stars = vec3(tw) * smoothstep(0.9965, 1.0, hs);
    }
    vec3 space = stars;
    if (u_no_sun == 0) {
      float s2 = max(dot(dir, u_sun), 0.0);
      space += u_sun_color * pow(s2, 900.0) * 9.0; // the sun stays, airless
    }
    col = mix(col, space, u_space);
  }
  if (u_hdr == 1) { frag = vec4(col, 1.0); return; } // linear for env maps
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

static const char *VS_MESH = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
uniform mat4 u_mvp;
uniform vec4 u_xform; // pos.xz, scale, yaw
uniform float u_ybase;
out vec3 v_nrm;
void main(){
  float c = cos(u_xform.w), s = sin(u_xform.w);
  vec3 p = in_pos * u_xform.z;
  p = vec3(p.x*c - p.z*s, p.y, p.x*s + p.z*c);
  p += vec3(u_xform.x, u_ybase, u_xform.y);
  v_nrm = vec3(in_nrm.x*c - in_nrm.z*s, in_nrm.y, in_nrm.x*s + in_nrm.z*c);
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

static const char *FS_MESH = R"GLSL(#version 430 core
in vec3 v_nrm;
out vec4 frag;
uniform vec3 u_color, u_sun, u_sun_color;
uniform float u_exposure;
uniform int u_selected;
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  float ndl = max(dot(normalize(v_nrm), u_sun), 0.0);
  vec3 col = u_color * (u_sun_color * 1.8 * ndl + vec3(0.35,0.38,0.45));
  if (u_selected == 1) col = mix(col, vec3(1.0,0.55,0.18), 0.25);
  col = aces(col * u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

static const char *VS_GIZMO = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
uniform mat4 u_mvp;
uniform vec4 u_xform; // center xyz, radius
out vec3 v_nrm;
void main(){
  vec3 p = in_pos * u_xform.w + u_xform.xyz;
  v_nrm = in_nrm;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

static const char *FS_GIZMO = R"GLSL(#version 430 core
in vec3 v_nrm;
out vec4 frag;
uniform vec3 u_color;
uniform int u_selected;
void main(){
  float rim = pow(1.0 - abs(normalize(v_nrm).z), 1.5);
  vec3 col = u_color + rim * 0.5;
  if (u_selected == 1) col = mix(col, vec3(1.0, 0.62, 0.2), 0.6);
  frag = vec4(col, 1.0);
})GLSL";

// material preview: a lit shape (sphere/cube/flat) textured with the channels
static const char *VS_MATPREV = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
layout(location=2) in vec2 in_uv;
uniform mat3 u_rot;
out vec3 v_nrm;
out vec2 v_uv;
void main(){
  vec3 p = u_rot * in_pos;
  v_nrm = normalize(u_rot * in_nrm);
  v_uv = in_uv;
  gl_Position = vec4(p.xy * 0.82, p.z * 0.35 + 0.5, 1.0);
})GLSL";

static const char *FS_MATPREV = R"GLSL(#version 430 core
in vec3 v_nrm;
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_albedo;
uniform sampler2D u_normal_map;
uniform sampler2D u_rough_map;
uniform int u_has_albedo, u_has_normal, u_has_rough;
uniform float u_roughness, u_metallic, u_specular, u_reflection;
uniform vec3 u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_sun_intensity, u_ambient;
const float PI = 3.14159265;
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  vec3 N = normalize(v_nrm);
  vec3 albedo = (u_has_albedo == 1) ? pow(texture(u_albedo, v_uv).rgb, vec3(2.2))
                                    : vec3(0.55,0.53,0.5);
  if (u_has_normal == 1){
    vec3 nm = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;
    vec3 T = normalize(cross(vec3(0,1,0), N) + vec3(1e-4));
    vec3 B = cross(N, T);
    N = normalize(T*nm.x + B*nm.y + N*max(nm.z,0.05));
  }
  float rough = clamp(u_roughness * ((u_has_rough == 1) ?
                      texture(u_rough_map, v_uv).r*2.0 : 1.0), 0.03, 1.0);
  vec3 V = vec3(0,0,1);
  vec3 L = normalize(u_sun);
  vec3 H = normalize(L+V);
  float NdL = max(dot(N,L),0.0), NdV = max(dot(N,V),1e-4);
  float NdH = max(dot(N,H),0.0), VdH = max(dot(V,H),0.0);
  vec3 F0 = mix(vec3(0.08*u_specular), albedo, u_metallic);
  float a = rough*rough, a2 = a*a;
  float dnm = (NdH*NdH*(a2-1.0)+1.0);
  float D = a2 / max(PI*dnm*dnm, 1e-6);
  float k = (rough+1.0); k = k*k/8.0;
  float G = (NdL/(NdL*(1.0-k)+k)) * (NdV/(NdV*(1.0-k)+k));
  vec3 F = F0 + (1.0-F0)*pow(1.0-VdH,5.0);
  vec3 spec = D*G*F/max(4.0*NdL*NdV,1e-4);
  vec3 kd = (1.0-F)*(1.0-u_metallic);
  vec3 sky = mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient;
  vec3 col = (kd*albedo/PI + spec) * u_sun_color * u_sun_intensity * NdL
           + albedo * sky * (0.45 + 0.55*N.y);
  vec3 R = reflect(-V, N);
  vec3 refl = mix(u_sky_horizon, u_sky_zenith, clamp(R.y*0.5+0.5,0.0,1.0));
  col += refl * u_reflection * (1.0-rough) * (F0.g + (1.0-F0.g)*pow(1.0-NdV,5.0));
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

static const char *VS_LINES = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
uniform mat4 u_mvp;
void main(){ gl_Position = u_mvp * vec4(in_pos, 1.0); })GLSL";

static const char *FS_LINES = R"GLSL(#version 430 core
out vec4 frag;
uniform vec4 u_color;
void main(){ frag = u_color; })GLSL";

static const char *VS_BG = R"GLSL(#version 430 core
out vec2 v_ndc;
void main(){
  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0-1.0;
  v_ndc = p;
  gl_Position = vec4(p, 0.99999, 1.0);
})GLSL";

static const char *FS_BG = R"GLSL(#version 430 core
in vec2 v_ndc;
out vec4 frag;
uniform vec3 u_top, u_bottom;
void main(){
  float t = v_ndc.y * 0.5 + 0.5;
  frag = vec4(mix(u_bottom, u_top, t), 1.0);
})GLSL";

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
  gpx::Heightmap norm = h;
  norm.remap(0.f, 1.f);
  hm_w = norm.w;
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










