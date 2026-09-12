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
// Level of detail for the baked relief (docs/LOD.md): far from the camera
// the height is read from a calmer mip level, so a field of stones at the
// horizon is a texture, not a shimmer. The level is the one whose texel spans
// as many pixels as the tessellation puts between vertices, so it follows the
// lens and the screen; 0 = every vertex reads level 0.
uniform float u_height_lod_k;
uniform vec3 u_lod_cam;
uniform float u_lod_ground; // the tile's mean height, world units, for the distance
uniform float u_tri_k;      // a triangle's edge per unit of distance (0: unknown)
HEIGHT_SMOOTH_PLACEHOLDER
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
TILE_XFORM_PLACEHOLDER
out vec2 v_uv;
out vec3 v_world;
out float v_detail;
// where this point stands on the flat world, in tile units, before the
// curvature bends it onto the shape: the very number the surround indexes
// its palette grain by (planet_shaders.cpp), so the two agree across the
// tile's border instead of each mottling the ground its own way
out vec2 v_wxz;
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
PL_SPHERE_PLACEHOLDER
vec3 gpx_sphere_place(vec2 uv, float h){
  return pl_sphere_place(uv, h, u_planet_radius);
}
void terrain_place(vec2 uv){
  float h = height_smooth(uv, relief_lod(uv)) * u_hscale;
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
    // No octave finer than the triangles carrying it. A wave shorter than two
    // edges is drawn as a sawtooth along every ridge, and it crawls as the
    // tessellation moves; those octaves are in the shading's normal instead
    // (get_normal). The count is continuous, so an octave fades in rather
    // than popping as the camera closes. The CPU makes the same choice to
    // pick on this surface and settle the orbit pivot on it
    // (relief_view_octaves, terrain_relief.hpp): change one, change both.
    float of = gp_octavesf(d, 9.0);
    if (u_tri_k > 0.0){
      float tri = max(d * u_tri_k, 1e-7);
      of = min(of, log2(1.0 / (2.0 * tri * max(u_frac_scale, 1e-4))) / log2(2.03) + 1.0);
    }
    of = max(of, 0.0);
    int o0 = int(floor(of));
    float ft = of - float(o0);
    float f = o0 > 0 ? gp_detail(uv, u_frac_scale, o0, gp_gain()) - 0.5 : 0.0;
    if (ft > 0.001 && o0 < 9)
      f = mix(f, gp_detail(uv, u_frac_scale, o0 + 1, gp_gain()) - 0.5, ft);
    v_detail = f;
    p.y += f * u_frac_amount;
  }
  // the tile's own transform (terrain_xform.hpp): offset, heading, pitch,
  // bank, size per axis, deformers - then the planetary curvature, so a
  // moved tile lies on the sphere where it was moved to
  p = tile_xform(p);
  v_wxz = p.xz;
  p = gpx_sphere_place(p.xz, p.y);
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
TILE_XFORM_PLACEHOLDER
PL_SPHERE_PLACEHOLDER
vec2 screen_of(vec2 uv){
  vec3 p = tile_xform(vec3(uv.x, texture(u_height, uv).r * u_hscale, uv.y));
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
//
// But a flat floor charges for relief on edges too small to show any. Measured
// at orbital range, 2026-09-07: the metric asked for 8,192 triangles over the
// tile and the floor of 8 delivered 663,552 — eighty-one times the work, for a
// tile a few hundred pixels across. So the floor tapers with the edge: full
// strength on an edge long enough to hold detail, and giving way on one that
// is a handful of pixels long, where no subdivision can put relief anywhere
// the eye could find it. Still well above what the metric itself asks for,
// which is what keeps displacement from vanishing at middle distances.
//
// It depends on the two shared endpoints and nothing else, so both patches
// along an edge still compute the identical number and the crack invariant is
// untouched. That is not a detail — it is the reason this design works.
float edge_tess(vec2 a, vec2 b){
  float px = distance(screen_of(a), screen_of(b));
  float floor_here = min(u_tess_min, max(px * 0.5, 1.0));
  return clamp(px / max(u_tess_px, 1.0), floor_here, u_tess_max);
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
  // the other face of the shell: its heights go the other way, and it lies
  // the shell's thickness below the ground - flat worlds included
  if (u_world_shape.w > 0.5){ float t = ylo; ylo = -yhi - u_world_thick; yhi = -t - u_world_thick; }
  if (u_cull_radius > 0.0 && !pl_world_flat()){
    // the surface falls away as r^2/(2R) from the tile's centre (the sphere
    // sits under it): nearest point lowers the box top, furthest corner
    // lowers its bottom
    vec2 ctr = vec2(0.5);
    // a flat axis (a ring world's z) contributes no drop (world_shape.hpp)
    vec2 curved = vec2(u_world_shape.x > 0.5 ? 0.0 : 1.0, u_world_shape.y > 0.5 ? 0.0 : 1.0);
    vec2 d_near = max(max(lo_uv - ctr, vec2(0.0)), ctr - hi_uv) * curved;
    vec2 d_far = max(abs(lo_uv - ctr), abs(hi_uv - ctr)) * curved;
    if (u_world_shape.z > 0.5){
      // an inside face rises: the near distance lifts the bottom, the far
      // corner lifts the top
      ylo += dot(d_near, d_near) / (2.0 * u_cull_radius);
      yhi += dot(d_far, d_far) / (2.0 * u_cull_radius);
    } else {
      yhi -= dot(d_near, d_near) / (2.0 * u_cull_radius);
      ylo -= dot(d_far, d_far) / (2.0 * u_cull_radius);
    }
  }
  vec3 lo = vec3(lo_uv.x, ylo, lo_uv.y);
  vec3 hi = vec3(hi_uv.x, yhi, hi_uv.y);
  if (u_tx_on == 1){
    // the box's eight corners through the transform, and the box of those
    vec3 nlo = vec3(1e30), nhi = vec3(-1e30);
    for (int k = 0; k < 8; ++k){
      vec3 q = tile_xform(vec3((k & 1) != 0 ? hi.x : lo.x, (k & 2) != 0 ? hi.y : lo.y,
                               (k & 4) != 0 ? hi.z : lo.z));
      nlo = min(nlo, q); nhi = max(nhi, q);
    }
    lo = nlo; hi = nhi;
  }
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
// How much each finer octave of the micro-relief keeps (RenderSettings::
// fractal_gain). A program that never uploads it reads 0 and gets the 0.5 the
// relief always had, so the tile and the ground round it cannot disagree.
uniform float u_frac_gain;
float gp_gain(){ return u_frac_gain > 0.0 ? clamp(u_frac_gain, 0.05, 0.95) : 0.5; }
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

// Height fog, shared by terrain, water and meshes so that every surface at a
// given distance disappears into the same air, and the render passes: aov_out
// is what a shader writes instead of its colour while a pass is drawn (see
// renderer_aov.cpp; the numbers mirror RenderPass bit + 1).
const char *const FOG_FN = R"GLSL(
uniform int u_fog_type;
// The bands a FogLayer node adds, beyond the first (studio/scene_nodes.cpp).
#define MAX_FOG_LAYERS 8
uniform int u_fogl_count;
uniform int u_fogl_type[MAX_FOG_LAYERS];
uniform float u_fogl_density[MAX_FOG_LAYERS];
uniform float u_fogl_level[MAX_FOG_LAYERS];
uniform float u_fogl_falloff[MAX_FOG_LAYERS];
uniform vec3 u_fogl_color[MAX_FOG_LAYERS];
uniform float u_fogl_scatter[MAX_FOG_LAYERS];
uniform float u_fogl_albedo[MAX_FOG_LAYERS];
uniform float u_fogl_g[MAX_FOG_LAYERS];
uniform float u_fog_density, u_fog_level, u_fog_falloff, u_fog_scatter;
uniform vec3 u_fog_color, u_absorb;
uniform float u_fog_albedo, u_fog_g, u_fog_hetero;
uniform int u_fog_steps;
// The world the air lies on (world_shape.hpp). On an inside world (a ring,
// a Dyson sphere) the profile's height is measured from the world's
// surface - the air is a layer on the shell - rather than from world y,
// which is R at the far side of a ring. A globe keeps world y, the profile
// every scene was lit with (a giant radius is past float precision anyway).
uniform float u_fog_world_r;
uniform vec4 u_fog_world_shape;
float fog_alt(vec3 p){
  float R = u_fog_world_r;
  if (R <= 0.0 || R > 1.0e5 || u_fog_world_shape.z < 0.5 ||
      (u_fog_world_shape.x > 0.5 && u_fog_world_shape.y > 0.5)) return p.y;
  vec3 c = vec3(0.5, u_fog_world_shape.z > 0.5 ? R : -R, u_fog_world_shape.y > 0.5 ? p.z : 0.5);
  return abs(length(c - p) - R);
}
uniform int u_aov, u_object_id;
// ID colours: a view's shading mode that paints every object (mode 1) or
// every material (mode 2) in one flat bright colour, so a layer or an object
// is found by eye rather than by name. The colour is the golden-ratio hue of
// the key, which spreads any set of keys evenly round the wheel.
uniform int u_id_mode;
uniform float u_id_key;
vec3 id_colour(float key){
  float h = fract(key * 0.61803398875 + 0.13);
  vec3 k = vec3(h, h + 0.3333, h + 0.6667);
  vec3 rgb = clamp(abs(fract(k) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
  return mix(vec3(1.0), rgb, 0.85); // bright, fully saturated
}
// Fog as a participating medium, not a colour blend.
//
// Light along the view ray is extinguished by exp(-optical depth) (Beer-
// Lambert), where the extinction coefficient falls off exponentially with
// height above the fog level. What reaches the eye from the fog itself is
// in-scattered light: the sun, through a Henyey-Greenstein phase function
// with anisotropy u_fog_g, and the sky, both times the medium's single-
// scattering albedo. With a uniform-in-height medium the integral has a
// closed form, which is the default and costs nothing. With heterogeneity
// the density is broken up by noise and the ray is marched - u_fog_steps
// samples, each with a short march toward the sun for self-shadowing - and
// the march stops early once 99% of the light is extinguished, which is the
// cap on how far the iterations go.
float fog_hg(float c, float g){
  float g2 = g * g;
  return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}
float fog_hash3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float fog_noise3(vec3 p){
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = fog_hash3(i), n100 = fog_hash3(i + vec3(1,0,0));
  float n010 = fog_hash3(i + vec3(0,1,0)), n110 = fog_hash3(i + vec3(1,1,0));
  float n001 = fog_hash3(i + vec3(0,0,1)), n101 = fog_hash3(i + vec3(1,0,1));
  float n011 = fog_hash3(i + vec3(0,1,1)), n111 = fog_hash3(i + vec3(1,1,1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
// f: 1 - transmittance along the ray; fogc: the in-scattered radiance, per
// unit of f, so that col*T + fogc*f is the radiative transfer result.
// One band of air along the ray: how much of the surface it swallows, and
// what it puts back. Split out of fog_terms so that the layers above the
// first are the same arithmetic and not a second implementation of it.
void fog_band(vec3 world, vec3 cam, float dist, float hscale, vec3 sun, vec3 sun_col,
              int type, float density, float lvl, float fall, vec3 fcol,
              float scatter, float albedo, float g, out float od, out vec3 fogc){
  od = 0.0; fogc = vec3(0.0);
  if (type == 0 || density <= 0.0) return;
  float level = lvl * hscale * 4.0;
  float falloff = fall / max(hscale, 1e-3);
  float dens = density * (type == 1 ? 0.35 : (type == 2 ? 1.0 : 1.8));
  vec3 dir = (world - cam) / max(dist, 1e-5);
  float fog_day = clamp(sun.y * 4.0 + 0.35, 0.035, 1.0);
  float phase = fog_hg(dot(dir, sun), g) * 12.566;
  vec3 sun_in = sun_col * fog_day * phase * scatter;
  vec3 sky_in = fcol * fog_day;
  // optical depth through an exponential height profile, in closed form
  float fy0 = fog_alt(cam) - level, fy1 = fog_alt(world) - level;
  float dY = fy1 - fy0;
  float a = exp(-falloff * max(fy0, 0.0));
  float b = exp(-falloff * max(fy1, 0.0));
  od = ((abs(falloff * dY) < 1e-3) ? dist * a
                                   : abs(dist * (a - b) / (falloff * dY))) * dens;
  fogc = albedo * (sky_in + sun_in);
  if (type == 3) fogc *= vec3(0.85, 0.75, 0.6);
}

// Every band of air at once.
//
// Air is never one band - haze to the horizon, a fog lying in the valley
// bottom, a brown layer over a town, a clear gap, a sheet of mist above - and
// a single set of numbers can be any one of those and never two together.
// Each FogLayer node is a band; the settings below the loop are the first,
// which every scene already has.
//
// They compose the way air does. Absorption through mixed media adds, so the
// optical depths sum and the total is exact however many there are and
// whatever order they lie in. The colour is each band's weighted by how much
// of the total depth it accounts for, so a thin haze in front of a thick fog
// reads as the fog and neither hides the other.
void fog_terms(vec3 world, vec3 cam, float dist, float hscale, vec3 sun, vec3 sun_col,
               out float f, out vec3 fogc){
  f = 0.0; fogc = vec3(0.0);
  float od_total = 0.0;
  vec3 col_sum = vec3(0.0);
  // the first band, which may march rather than close if it is broken up
  if (u_fog_type != 0 && u_fog_density > 0.0) {
    if (u_fog_steps <= 1 || u_fog_hetero <= 0.0) {
      float od; vec3 c;
      fog_band(world, cam, dist, hscale, sun, sun_col, u_fog_type, u_fog_density,
               u_fog_level, u_fog_falloff, u_fog_color, u_fog_scatter, u_fog_albedo,
               u_fog_g, od, c);
      od_total += od; col_sum += c * od;
    } else {
      float level = u_fog_level * hscale * 4.0;
      float falloff = u_fog_falloff / max(hscale, 1e-3);
      float dens = u_fog_density * (u_fog_type == 1 ? 0.35 : (u_fog_type == 2 ? 1.0 : 1.8));
      vec3 dir = (world - cam) / max(dist, 1e-5);
      float fog_day = clamp(sun.y * 4.0 + 0.35, 0.035, 1.0);
      float phase = fog_hg(dot(dir, sun), u_fog_g) * 12.566;
      vec3 sun_in = sun_col * fog_day * phase * u_fog_scatter;
      vec3 sky_in = u_fog_color * fog_day;
      int steps = clamp(u_fog_steps, 2, 64);
      float dt = dist / float(steps);
      float T = 1.0;
      vec3 S = vec3(0.0);
      float nscale = 9.0 / max(hscale, 1e-3);
      float ls = (0.35 / falloff) / 3.0;
      for (int i = 0; i < steps; ++i){
        vec3 p = cam + dir * ((float(i) + 0.5) * dt);
        float sig = dens * exp(-falloff * max(fog_alt(p) - level, 0.0))
                  * mix(1.0, fog_noise3(p * nscale) * 1.6, u_fog_hetero);
        float ext = sig * dt;
        float od_sun = 0.0;
        for (int j = 0; j < 3; ++j){
          vec3 q = p + sun * ((float(j) + 0.5) * ls);
          od_sun += dens * exp(-falloff * max(fog_alt(q) - level, 0.0))
                  * mix(1.0, fog_noise3(q * nscale) * 1.6, u_fog_hetero) * ls;
        }
        vec3 Li = u_fog_albedo * (sky_in + sun_in * exp(-od_sun));
        float absorbed = 1.0 - exp(-ext);
        S += T * Li * absorbed;
        T *= 1.0 - absorbed;
        if (T < 0.01) { T = 0.0; break; }
      }
      float od = -log(max(T, 1e-4));
      vec3 c = S / max(1.0 - T, 1e-4);
      if (u_fog_type == 3) c *= vec3(0.85, 0.75, 0.6);
      od_total += od; col_sum += c * od;
    }
  }
  // and every band a FogLayer node added
  for (int i = 0; i < u_fogl_count; ++i) {
    float od; vec3 c;
    fog_band(world, cam, dist, hscale, sun, sun_col, u_fogl_type[i], u_fogl_density[i],
             u_fogl_level[i], u_fogl_falloff[i], u_fogl_color[i], u_fogl_scatter[i],
             u_fogl_albedo[i], u_fogl_g[i], od, c);
    od_total += od; col_sum += c * od;
  }
  f = clamp(1.0 - exp(-od_total), 0.0, 1.0);
  fogc = od_total > 1e-5 ? col_sum / od_total : vec3(0.0);
}
// What survives of the surface: transmittance, tinted by a wavelength-
// dependent absorber (u_absorb per channel, raised to the optical depth), plus
// what the fog scattered toward the eye.
vec3 apply_fog_terms(vec3 col, float f, vec3 fogc){
  float od = -log(max(1.0 - f, 1e-4));
  vec3 T = (1.0 - f) * pow(max(u_absorb, vec3(1e-3)), vec3(od));
  return col * T + fogc * f;
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
