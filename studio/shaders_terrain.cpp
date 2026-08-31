// Geekatplay TerraForge — terrain, tessellation and shadow shaders
#include "renderer_shaders.hpp"

namespace studio {

// The uniforms and the body that place a terrain vertex. Shared verbatim
// between the plain vertex shader and the tessellation evaluation shader, so
// the two paths cannot drift: whichever one runs, the surface is the same
// surface. Both call TERRAIN_PLACE with the uv they arrived at.
const char *const TERRAIN_VERT_COMMON = R"GLSL(
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
const char *const VS_TERRAIN_TAIL = R"GLSL(
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
const char *const TCS_TERRAIN = R"GLSL(#version 430 core
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
// Per-patch visibility. A patch whose bounding box misses the frustum has its
// outer levels set to zero, which the spec discards before the tessellator
// generates anything at all.
uniform sampler2D u_patch_bounds; // RG = min,max height over this patch
uniform vec4 u_frustum[6];        // inward-facing planes, normalised
uniform float u_cull_pad;         // world units the bound may be wrong by
uniform vec3 u_cull_cam;          // camera, for the planetary curvature term
uniform float u_cull_radius;      // planet radius, 0 = flat
uniform int u_cull_on;
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
// The mirror of studio::aabb_visible / patches_visible. The plane extraction
// itself lives on the CPU and arrives in u_frustum, so there is only ever one
// implementation of the part that is easy to get wrong.
bool patch_visible(vec2 c0, vec2 c2){
  vec2 lo_uv = min(c0, c2), hi_uv = max(c0, c2);
  vec2 mm = texture(u_patch_bounds, (c0 + c2) * 0.5).rg;
  float ylo = mm.x * u_hscale - u_cull_pad;
  float yhi = mm.y * u_hscale + u_cull_pad;
  if (u_cull_radius > 0.0){
    // the surface falls away as r^2/(2R): nearest point lowers the box top,
    // furthest corner lowers its bottom
    vec2 d_near = max(max(lo_uv - u_cull_cam.xz, vec2(0.0)),
                      u_cull_cam.xz - hi_uv);
    vec2 d_far = max(abs(lo_uv - u_cull_cam.xz), abs(hi_uv - u_cull_cam.xz));
    yhi -= dot(d_near, d_near) / (2.0 * u_cull_radius);
    ylo -= dot(d_far, d_far) / (2.0 * u_cull_radius);
  }
  vec3 lo = vec3(lo_uv.x, ylo, lo_uv.y);
  vec3 hi = vec3(hi_uv.x, yhi, hi_uv.y);
  for (int i = 0; i < 6; ++i){
    vec3 n = u_frustum[i].xyz;
    vec3 p = mix(lo, hi, step(0.0, n)); // corner furthest along the normal
    if (dot(n, p) + u_frustum[i].w < 0.0) return false;
  }
  return true;
}
void main(){
  te_uv[gl_InvocationID] = tc_uv[gl_InvocationID];
  if (gl_InvocationID == 0){
    if (u_cull_on == 1 && !patch_visible(tc_uv[0], tc_uv[2])){
      gl_TessLevelOuter[0] = gl_TessLevelOuter[1] = 0.0;
      gl_TessLevelOuter[2] = gl_TessLevelOuter[3] = 0.0;
      gl_TessLevelInner[0] = gl_TessLevelInner[1] = 0.0;
      return;
    }
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
const char *const TES_TERRAIN_TAIL = R"GLSL(
layout(quads, fractional_odd_spacing, ccw) in;
in vec2 te_uv[];
void main(){
  vec2 lo = mix(te_uv[0], te_uv[1], gl_TessCoord.x);
  vec2 hi = mix(te_uv[3], te_uv[2], gl_TessCoord.x);
  terrain_place(mix(lo, hi, gl_TessCoord.y));
}
)GLSL";

const char *const VS_TERRAIN_PASS = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
out vec2 tc_uv;
void main(){ tc_uv = in_uv; }
)GLSL";

// Procedural fractal detail shared by the vertex and fragment stages: the
// baked heightmap carries the large forms, these octaves keep resolving as
// the camera closes in, so the terrain is fractal rather than a fixed grid.
const char *const FRACTAL_FN = R"GLSL(
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
const char *const SKY_FN = R"GLSL(
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

const char *const FS_TERRAIN_SRC = R"GLSL(#version 430 core
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
uniform float u_cl_cov, u_cl_alt, u_cl_thick, u_cl_time;
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
  // Project the point up to the *middle* of the cloud slab along the sun
  // direction, and sample the shape volume at the frequency the sky march
  // uses.
  //
  // Both were wrong. The projection landed on the slab's base, where the
  // height gradient puts density at zero, and the sample was taken at
  // wp * 0.35 against the march's wp * 0.18 - so the shadows on the ground
  // were a different pattern, at roughly half the scale, from the clouds
  // casting them. They now line up because they are the same lookup.
  float dy = max(u_cl_alt + u_cl_thick * 0.5 - world.y, 0.0);
  if (u_sun.y < 0.05) return 1.0;
  vec3 p = world + u_sun * (dy / max(u_sun.y, 0.05));
  vec3 wp = p; wp.xz += u_cl_wind * u_cl_time;
  vec4 sn = texture(u_cl_shape, wp * 0.18);
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


} // namespace studio
