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
uniform float u_planet_radius; // 0 = flat, else the tile lies on a sphere
uniform float u_field_strength; // graph-authored displacement, 0 = none
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
out vec2 v_uv;
out vec3 v_world;
out float v_detail;
// The tile on a planet. The sphere's centre is R below the tile's centre;
// a tile point at (s, t) from that centre travels s and t along the surface,
// i.e. through angles s/R and t/R. When the tile is wider than the whole
// circumference the angles are clamped so the tile wraps the globe exactly
// once, equirectangular - which is how a 1 m planet is made from a 5 km
// heightmap. For a large R this reduces to the familiar r^2/2R drop.
vec2 gpx_sphere_angles(vec2 uv){
  float k = min(1.0 / u_planet_radius, 6.2831853);
  float kl = min(1.0 / u_planet_radius, 3.14159265);
  return vec2((uv.x - 0.5) * k, (uv.y - 0.5) * kl);
}
vec3 gpx_sphere_dir(vec2 a){
  float cl = cos(a.y);
  return vec3(sin(a.x) * cl, cos(a.x) * cl, sin(a.y));
}
vec3 gpx_sphere_place(vec2 uv, float h){
  if (u_planet_radius <= 0.0) return vec3(uv.x, h, uv.y);
  vec3 c = vec3(0.5, -u_planet_radius, 0.5);
  return c + gpx_sphere_dir(gpx_sphere_angles(uv)) * (u_planet_radius + h);
}
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
  // planetary curvature: the tile lies on the sphere
  p = gpx_sphere_place(uv, p.y);
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
  // a small planet wraps the tile round itself; no box bound holds there,
  // so every patch is drawn (the tile is a globe a few pixels across anyway)
  if (u_cull_radius > 0.0 && u_cull_radius < 4.0) return true;
  if (u_cull_radius > 0.0){
    // the surface falls away as r^2/(2R) from the tile's centre (the sphere
    // sits under it): nearest point lowers the box top, furthest corner
    // lowers its bottom
    vec2 ctr = vec2(0.5);
    vec2 d_near = max(max(lo_uv - ctr, vec2(0.0)), ctr - hi_uv);
    vec2 d_far = max(abs(lo_uv - ctr), abs(hi_uv - ctr));
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
  // the sky darkens as the sun sets: full brightness above the horizon,
  // deep blue-black once it is well below. Shared by sky, terrain ambient
  // and water reflections, so the whole scene agrees about nightfall.
  float day = clamp(sun.y * 4.0 + 0.35, 0.035, 1.0);
  col *= day;
  return col;
}
)GLSL";

} // namespace studio
