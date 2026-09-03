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
// The sky function is shared by the sky pass, terrain reflections and water
// reflections, so the backdrop dome lives in it too: whatever looks at the sky
// sees the same picture. renderer_backdrop.cpp binds the sampler and uniforms
// in every program that carries this block.
const char *const SKY_FN = R"GLSL(
uniform sampler2D u_backdrop;
uniform int u_bd_on, u_bd_mode, u_bd_flip, u_bd_hide_sun;
uniform float u_bd_aspect, u_bd_yaw, u_bd_pitch, u_bd_tanhalf, u_bd_gain;
uniform float u_bd_blend, u_bd_haze;
uniform vec3 u_bd_tint;
// how much of the last sky_color() came from the dome (0 = none / no pixel)
float g_bd_weight = 0.0;
vec3 bd_rotate(vec3 d){
  float cy = cos(u_bd_yaw), sy = sin(u_bd_yaw);
  d = vec3(d.x*cy - d.z*sy, d.y, d.x*sy + d.z*cy);
  float cp = cos(u_bd_pitch), sp = sin(u_bd_pitch);
  return vec3(d.x, d.y*cp - d.z*sp, d.y*sp + d.z*cp);
}
// Direction to image coordinates for each mapping. Forward is -Z, so the
// middle of a panorama faces the default camera; v = 0 is the top row.
// Returns false where the mapping has no pixel (below a sky dome, outside a
// planar plate) so the procedural sky shows through there.
bool bd_uv(vec3 d, out vec2 uv){
  const float PI_ = 3.14159265, PI2 = 6.2831853;
  uv = vec2(0.5);
  if (u_bd_mode == 0) {
    uv = vec2(atan(d.x, -d.z) / PI2 + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI_);
  } else if (u_bd_mode == 1) {
    float r = acos(clamp(-d.z, -1.0, 1.0)) / PI_;
    float k = r / max(length(d.xy), 1e-6);
    uv = vec2(d.x * k, -d.y * k) * 0.5 + 0.5;
  } else if (u_bd_mode == 2) {
    float m = 2.0 * length(vec3(d.x, d.y, d.z + 1.0));
    if (m < 1e-6) return false;
    uv = vec2(d.x / m, -d.y / m) + 0.5;
  } else if (u_bd_mode == 3) {
    vec3 a = abs(d); int face; vec2 st;
    if (a.x >= a.y && a.x >= a.z) { face = d.x > 0.0 ? 0 : 1; st = vec2(d.x > 0.0 ? -d.z : d.z, -d.y) / a.x; }
    else if (a.y >= a.z) { face = d.y > 0.0 ? 2 : 3; st = vec2(d.x, d.y > 0.0 ? d.z : -d.z) / a.y; }
    else { face = d.z > 0.0 ? 4 : 5; st = vec2(d.z > 0.0 ? d.x : -d.x, -d.y) / a.z; }
    st = st * 0.5 + 0.5;
    vec2 cell, grid;
    if (u_bd_aspect > 1.0) { // horizontal cross, 4 x 3
      grid = vec2(4.0, 3.0);
      if (face == 0) cell = vec2(2.0, 1.0); else if (face == 1) cell = vec2(0.0, 1.0);
      else if (face == 2) cell = vec2(1.0, 0.0); else if (face == 3) cell = vec2(1.0, 2.0);
      else if (face == 4) cell = vec2(1.0, 1.0); else cell = vec2(3.0, 1.0);
    } else { // vertical cross, 3 x 4, the back face upside down at the bottom
      grid = vec2(3.0, 4.0);
      if (face == 0) cell = vec2(2.0, 1.0); else if (face == 1) cell = vec2(0.0, 1.0);
      else if (face == 2) cell = vec2(1.0, 0.0); else if (face == 3) cell = vec2(1.0, 2.0);
      else if (face == 4) cell = vec2(1.0, 1.0); else { cell = vec2(1.0, 3.0); st = 1.0 - st; }
    }
    uv = (cell + st) / grid;
  } else if (u_bd_mode == 4) {
    float t = d.y / max(length(d.xz), 1e-6);
    uv = vec2(atan(d.x, -d.z) / PI2 + 0.5, 0.5 - t / (2.0 * u_bd_tanhalf));
    if (uv.y < 0.0 || uv.y > 1.0) return false;
  } else if (u_bd_mode == 5) {
    if (d.y <= 0.0) return false;
    float r = acos(clamp(d.y, -1.0, 1.0)) / (PI_ * 0.5) * 0.5;
    vec2 dir = length(d.xz) > 1e-6 ? normalize(d.xz) : vec2(0.0);
    uv = vec2(0.5) + r * vec2(dir.x, -dir.y);
  } else {
    if (d.z >= -1e-6) return false;
    vec2 p = d.xy / (-d.z);
    uv = vec2(p.x / (2.0 * u_bd_tanhalf * u_bd_aspect), -p.y / (2.0 * u_bd_tanhalf)) + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return false;
  }
  if (u_bd_flip == 1) uv.x = 1.0 - uv.x;
  return true;
}
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
  // The dome is an absolute HDR picture: it is not dimmed with the procedural
  // sky, its own lighting is whatever the photograph holds.
  g_bd_weight = 0.0;
  if (u_bd_on == 1) {
    vec2 uv;
    if (bd_uv(bd_rotate(dir), uv)) {
      vec3 bd = texture(u_backdrop, uv).rgb * u_bd_gain * u_bd_tint;
      col = mix(col, bd, u_bd_blend);
      g_bd_weight = u_bd_blend;
    }
  }
  return col;
}
)GLSL";

// Height fog, shared by terrain, water and meshes so that every surface at a
// given distance disappears into the same air, and the render passes: aov_out
// is what a shader writes instead of its colour while a pass is drawn (see
// renderer_aov.cpp; the numbers mirror RenderPass bit + 1).
const char *const FOG_FN = R"GLSL(
uniform int u_fog_type;
uniform float u_fog_density, u_fog_level, u_fog_falloff, u_fog_scatter;
uniform vec3 u_fog_color, u_absorb;
uniform int u_aov, u_object_id;
// f: fraction of the pixel that is fog; fogc: the colour of that fog
void fog_terms(vec3 world, vec3 cam, float dist, float hscale, vec3 sun, vec3 sun_col,
               out float f, out vec3 fogc){
  f = 0.0; fogc = vec3(0.0);
  if (u_fog_type == 0 || u_fog_density <= 0.0) return;
  float level = u_fog_level * hscale * 4.0;
  float falloff = u_fog_falloff / max(hscale, 1e-3);
  float fy0 = cam.y - level, fy1 = world.y - level;
  float dY = fy1 - fy0;
  float a = exp(-falloff * max(fy0, 0.0));
  float b = exp(-falloff * max(fy1, 0.0));
  float od = (abs(falloff*dY) < 1e-3) ? dist * a
                                      : abs(dist * (a - b) / (falloff * dY));
  float dens = u_fog_density * (u_fog_type == 1 ? 0.35 : (u_fog_type == 2 ? 1.0 : 1.8));
  f = clamp(1.0 - exp(-od * dens), 0.0, 1.0);
  float sunward = pow(max(dot(normalize(world - cam), sun), 0.0), 6.0);
  fogc = u_fog_color * mix(vec3(1.0), sun_col * 1.6, sunward * u_fog_scatter);
  if (u_fog_type == 3) fogc *= vec3(0.85, 0.75, 0.6);
}
vec3 apply_fog_terms(vec3 col, float f, vec3 fogc){
  col *= mix(vec3(1.0), u_absorb, f);
  return mix(col, fogc, f);
}
vec4 aov_out(int aov, float depth, vec3 N, vec3 albedo, vec3 world, float object_id,
             vec3 direct, float shadow, vec3 ambient, vec3 specular,
             float fog_f, vec3 fog_c, float water_mask, vec3 linear_col){
  if (aov == 1) return vec4(depth, 0.0, 0.0, 1.0);
  if (aov == 2) return vec4(N, 1.0);
  if (aov == 3) return vec4(world, 1.0);
  if (aov == 4) return vec4(object_id, 0.0, 0.0, 1.0);
  if (aov == 5) return vec4(water_mask, 0.0, 0.0, 1.0);
  if (aov == 6) return vec4(albedo, 1.0);
  if (aov == 7) return vec4(direct, 1.0);
  if (aov == 8) return vec4(shadow, 0.0, 0.0, 1.0);
  if (aov == 9) return vec4(ambient, 1.0);
  if (aov == 10) return vec4(specular, 1.0);
  if (aov == 11) return vec4(fog_c * fog_f, 1.0 - fog_f);
  if (aov == 12) return vec4(0.0, 0.0, 0.0, 1.0); // a surface hides the sky
  return vec4(apply_fog_terms(linear_col, fog_f, fog_c), 1.0); // 13: linear beauty
}
)GLSL";

} // namespace studio
