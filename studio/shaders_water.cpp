// Geekatplay TerraForge — the sea's shaders: one surface for the whole world.
//
// The water used to be two things. The tile carried a plane of its own,
// blended over the tile's square, and the planet surround flattened its
// ground to the water level and tinted it - two programs, two looks, and
// the tile's square standing out in the middle of every lake. Now there is
// one surface: a camera-centred clipmap of rings (renderer_water.cpp) laid
// on the world's curve at the water level, displaced by the waves
// (gpx/water_waves.hpp, mirrored below), and shaded against what is behind
// it - the depth of the scene the water covers gives the thickness of water
// each ray crosses and how deep the bed is, so the shallows are clear, the
// deep is dark and the foam gathers where the water meets anything.
#include "renderer_shaders.hpp"

namespace studio {

// The line-for-line twin of gpx::water::evaluate. Positions are metres from
// the rebased origin, so a short wave keeps its phase however far the
// camera has gone; the phases carry the origin and the clock.
const char *const WATER_WAVES_GLSL = R"GLSL(
uniform int u_wv_n;
uniform vec4 u_wv_a[32]; // direction x, direction z, wavenumber (rad/m), amplitude (m)
uniform vec4 u_wv_b[32]; // phase at the origin now, q, wavelength (m), unused
float wv_weight(float lambda, float min_lambda){
  if (min_lambda <= 0.0) return 1.0;
  return clamp(lambda / min_lambda - 1.0, 0.0, 1.0);
}
void wv_eval(vec2 xm, float min_lambda, out vec3 disp, out vec3 dpdx, out vec3 dpdz,
             out float lost){
  disp = vec3(0.0); dpdx = vec3(1.0, 0.0, 0.0); dpdz = vec3(0.0, 0.0, 1.0); lost = 0.0;
  for (int i = 0; i < u_wv_n; ++i){
    float wt = wv_weight(u_wv_b[i].z, min_lambda);
    float k = u_wv_a[i].z;
    float ak0 = u_wv_a[i].w * k;
    lost += ak0 * ak0 * 0.5 * (1.0 - wt * wt);
    if (wt <= 0.0) continue;
    float a = u_wv_a[i].w * wt;
    float dx = u_wv_a[i].x, dz = u_wv_a[i].y;
    float th = k * (dx * xm.x + dz * xm.y) + u_wv_b[i].x;
    float S = sin(th), C = cos(th);
    float qa = u_wv_b[i].y * a;
    disp.x += qa * dx * C;
    disp.y += a * S;
    disp.z += qa * dz * C;
    float ak = a * k, qak = qa * k;
    dpdx.x -= qak * dx * dx * S;
    dpdx.y += ak * dx * C;
    dpdx.z -= qak * dz * dx * S;
    dpdz.x -= qak * dx * dz * S;
    dpdz.y += ak * dz * C;
    dpdz.z -= qak * dz * dz * S;
  }
}
)GLSL";

// What every program that shades water shares: the look's uniforms, the
// sun's glint off a rough sea, and the far sea - the limit the full shading
// arrives at once every wave is smaller than a pixel, which is what the
// planet's far shell draws (planet_shaders.cpp), so the two meet.
const char *const WATER_FN_GLSL = R"GLSL(
uniform float u_w_time, u_w_clarity, u_w_atmo, u_w_sun_half, u_w_slope_var;
uniform vec3 u_w_deep, u_w_shallow, u_w_foam_color;
uniform int u_w_foam_on;
uniform float u_w_foam_amount, u_w_foam_scale, u_w_foam_crests, u_w_foam_depth, u_w_foam_coverage;
uniform float u_w_whitecaps; // how much breaking the wind drives (gpx::water::whitecap_share)
uniform vec2 u_w_foam_off, u_w_wind;
// The sky the sea reflects: the sky pass's own picture of it, clouds and
// all, shot from the water under the eye into a latitude-longitude panorama
// (SKY_ENV_GLSL, renderer_clouds.cpp). It used to be the sky's gradient
// alone - no clouds in the water at all, whatever was overhead.
// The reflected sky along R, blurred to the cone a roughness of `alpha`
// radians spreads it over; the gradient when there is no sky to shoot.
vec3 wat_sky(vec3 R, vec3 L, vec3 zenith, vec3 horizon, vec3 sun_color, float alpha){
  if (u_sky_env_on == 0) return sky_color(R, zenith, horizon, L, sun_color, u_w_atmo);
  return sky_env(R, alpha);
}
// water reflects 2% head-on (index 1.33) and everything at grazing
float wat_fresnel(float ndv){ return 0.02 + 0.98 * pow(1.0 - clamp(ndv, 0.0, 1.0), 5.0); }
// The light deep water sends back up out of itself. The deep colour is read
// as what that water looks like under an open noon sky, which puts about two
// of this renderer's units of light on it - so the body answers the scene's
// light (it darkens at dusk, brightens under a stronger sun) and at noon is
// the colour that was picked.
vec3 wat_body(vec3 up, vec3 L, vec3 sun_c, float shadow, vec3 sky_amb){
  return u_w_deep * (sun_c * max(dot(up, L), 0.0) * shadow / 3.14159265 + sky_amb) * 0.5;
}
// GGX glint of the sun, Fresnel included; alpha is the sea's roughness
// with the sun's own width folded in, so a larger sun spreads its glitter
vec3 wat_sun_glint(vec3 N, vec3 V, vec3 L, float alpha, vec3 sun_c){
  float ndl = dot(N, L);
  if (ndl <= 0.0) return vec3(0.0);
  vec3 H = normalize(L + V);
  float ndv = max(dot(N, V), 1e-3), ndh = max(dot(N, H), 0.0);
  float a2 = alpha * alpha;
  float dn = ndh * ndh * (a2 - 1.0) + 1.0;
  float D = a2 / max(3.14159265 * dn * dn, 1e-7);
  float k = alpha * 0.5;
  float G = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));
  float F = wat_fresnel(dot(V, H));
  return sun_c * (D * G * F / (4.0 * ndv));
}
// The sea seen from so far that its waves are all below a pixel: a flat
// mirror whose roughness is every wave's slope, over water too deep to show
// a bed. up is the surface's up, sky_amb the skylight on it.
vec3 water_far_color(vec3 V, vec3 up, vec3 L, vec3 sun_c, vec3 sky_amb,
                     vec3 zenith, vec3 horizon, vec3 sun_color){
  float ndv = max(dot(up, V), 1e-3);
  float F = wat_fresnel(ndv);
  vec3 R = reflect(-V, up);
  float alpha = sqrt(0.0004 + 2.0 * u_w_slope_var + u_w_sun_half * u_w_sun_half);
  vec3 sky = wat_sky(R, L, zenith, horizon, sun_color, min(sqrt(0.0004 + 2.0 * u_w_slope_var), 1.0));
  return sky * F + wat_sun_glint(up, V, L, min(alpha, 1.0), sun_c) +
         (1.0 - F) * wat_body(up, L, sun_c, 1.0, sky_amb);
}
// Foam's texture: bubbles gathered into lace between cells, over broad
// patches. The lattice repeats every 16384 cells and the origin's place in
// it is folded in on the CPU in double (u_w_foam_off, modulo 65536 so that
// every octave's own scale lands on a whole period when it wraps), so the
// pattern stays put on the water however far the camera travels.
vec2 wat_h2(vec2 p){
  p = mod(p, 16384.0);
  vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
  q += dot(q, q.yzx + 33.33);
  return fract((q.xx + q.yz) * q.zy);
}
float wat_vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(wat_h2(i).x, wat_h2(i + vec2(1.0, 0.0)).x, f.x),
             mix(wat_h2(i + vec2(0.0, 1.0)).x, wat_h2(i + vec2(1.0, 1.0)).x, f.x), f.y);
}
float wat_lace(vec2 p){ // F2 - F1: zero on the borders between cells
  vec2 i = floor(p), f = fract(p);
  float d1 = 8.0, d2 = 8.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x){
      vec2 g = vec2(float(x), float(y));
      vec2 r = g + wat_h2(i + g) - f;
      float d = dot(r, r);
      if (d < d1){ d2 = d1; d1 = d; } else if (d < d2) d2 = d;
    }
  return sqrt(d2) - sqrt(d1);
}
// pm: metres from the origin; fw: the pixel's footprint in metres. Broad
// soft patches, and inside them the bubbles: cells whose borders hold the
// foam and whose middles are the holes in it. A caller thresholds it, so
// more foam wanted means the patches spread and join.
float foam_pattern(vec2 pm, float fw){
  float s = max(u_w_foam_scale * 0.4, 0.05); // one bubble cell, metres
  vec2 p = pm / s + u_w_foam_off;
  float patches = wat_vnoise(p * 0.25) * 0.6 + wat_vnoise(p + 5.0) * 0.3 +
                  wat_vnoise(p * 3.0 + 7.0) * 0.1;
  float holes = smoothstep(0.08, 0.45, wat_lace(p * 3.0 + 17.0));
  float v = clamp(patches * 1.15 - 0.1 * holes, 0.0, 1.0);
  // smaller than a pixel the bubbles average out to their mean
  return mix(v, 0.45, smoothstep(0.3, 1.5, fw / s));
}
)GLSL";

// One level of the clipmap (renderer_water.cpp): a grid of cells about a
// centre snapped to twice its cell, so it never slides over the waves.
// Toward its rim the odd vertices slide onto their even neighbours, and at
// the rim the level *is* the next one's lattice with the next one's waves -
// the seam between two levels has nothing to show.
const char *const VS_WATER = R"GLSL(#version 430 core
layout(location=0) in vec2 in_g; // grid index, whole numbers about the centre
uniform mat4 u_mvp;
uniform float u_level;            // the water level, world units
uniform float u_planet_radius;
uniform float u_size_m;           // metres in a tile unit
uniform vec2 u_lvl_origin;        // this level's centre, tile units
uniform float u_lvl_cell;         // its cell, tile units
uniform vec2 u_lvl_rel_m;         // its centre from the wave origin, metres
uniform vec2 u_eye_param;         // the eye over the flat world, tile units
uniform float u_morph_lo, u_morph_hi; // cells from the eye the morph starts and ends
uniform int u_displace;           // 0: flat, the waves in the shading only
PL_SPHERE_PLACEHOLDER
WATER_WAVES_PLACEHOLDER
out vec2 v_rel_m;  // the point at rest, metres from the wave origin
out vec3 v_world;
void main(){
  vec2 g = in_g;
  vec2 p = u_lvl_origin + g * u_lvl_cell;
  vec2 de = abs(p - u_eye_param) / u_lvl_cell;
  float m = clamp((max(de.x, de.y) - u_morph_lo) / (u_morph_hi - u_morph_lo), 0.0, 1.0);
  vec2 gm = g - fract(g * 0.5) * 2.0 * m;
  vec2 rel_m = u_lvl_rel_m + gm * (u_lvl_cell * u_size_m);
  vec2 xz = u_lvl_origin + gm * u_lvl_cell;
  vec3 disp = vec3(0.0);
  if (u_displace == 1){
    // a wave needs four cells of this level to be drawn by it
    vec3 dpdx, dpdz; float lost;
    wv_eval(rel_m, 4.0 * u_lvl_cell * u_size_m * (1.0 + m), disp, dpdx, dpdz, lost);
  }
  vec3 w = pl_sphere_place(xz + disp.xz / u_size_m, u_level + disp.y / u_size_m, u_planet_radius);
  v_rel_m = rel_m;
  v_world = w;
  gl_Position = u_mvp * vec4(w, 1.0);
})GLSL";

const char *const FS_WATER = R"GLSL(#version 430 core
in vec2 v_rel_m;
in vec3 v_world;
out vec4 frag;
uniform mat4 u_mvp;
uniform float u_hscale, u_exposure, u_size_m, u_planet_radius;
uniform vec3 u_sun, u_sun_color, u_cam, u_sky_zenith, u_sky_horizon;
uniform float u_sun_intensity, u_ambient;
uniform vec2 u_wave_origin;       // tile units
// the level partition: a level draws only what is not a finer level's
uniform int u_lvl_inner, u_lvl_outer;
uniform vec2 u_inner_rel_m, u_outer_rel_m;
uniform float u_inner_r_m, u_outer_r_m;
// the world's extent: a small world's own angles, the reach of the ground,
// a ring's width, a flat world's outline, the horizon fade
uniform vec2 u_param_lim;
uniform float u_reach, u_shell_w;
uniform int u_world_outline, u_horizon, u_selected;
uniform int u_reach_square; // 1: stop on the far shell's square, not a circle
// The scene the water covers, as it stood before the water: its depth, and
// its colour - the finished picture, which the water un-develops back to
// light (u_linear_target 1: the target already holds light) so that what
// shows through is added to what the surface sends in light, not in the
// display's encoding, where every sum comes out too bright.
uniform sampler2D u_scene_depth, u_scene_color;
uniform int u_linear_target;
uniform vec2 u_viewport;
uniform int u_ortho;
uniform float u_depth_near, u_depth_far;
// one pixel: radians of view (perspective) or tile units (orthographic)
uniform float u_pixel_k;
uniform sampler2DShadow u_shadowmap;
uniform mat4 u_light_mvp;
uniform int u_shadows;
uniform float u_shadow_soft;
SKY_FN_PLACEHOLDER
FOG_FN_PLACEHOLDER
PL_SPHERE_PLACEHOLDER
WATER_WAVES_PLACEHOLDER
SKY_ENV_PLACEHOLDER
WATER_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
float water_shadow(vec3 world){
  if (u_shadows == 0) return 1.0;
  vec4 lp = u_light_mvp * vec4(world, 1.0);
  vec3 pc = lp.xyz / lp.w * 0.5 + 0.5;
  if (pc.x < 0.0 || pc.x > 1.0 || pc.y < 0.0 || pc.y > 1.0 || pc.z > 1.0) return 1.0;
  float texel = 1.0 / 2048.0 * u_shadow_soft;
  float s = 0.0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      s += texture(u_shadowmap, vec3(pc.xy + vec2(dx, dy) * texel, pc.z - 0.003));
  return s / 9.0;
}
// eye-space depth of a depth-buffer value
float lin_depth(float z){
  if (u_ortho == 1) return u_depth_near + z * (u_depth_far - u_depth_near);
  return 2.0 * u_depth_near * u_depth_far /
         (u_depth_far + u_depth_near - (2.0 * z - 1.0) * (u_depth_far - u_depth_near));
}
// The light behind a pixel of the scene: the display encoding, the ACES
// curve, the saturation and the grade undone, in that order, and the
// exposure divided out. What the curve clipped stays clipped.
vec3 scene_light(vec2 uv){
  vec3 c = texture(u_scene_color, uv).rgb;
  if (u_linear_target == 1) return c;
  vec3 y = pow(clamp(c, 0.0, 0.999), vec3(2.2));
  vec3 a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, k = 0.14 * y;
  vec3 x = (-b - sqrt(max(b * b - 4.0 * a * k, vec3(0.0)))) / (2.0 * a);
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = lum + (x - lum) / max(u_sat, 1e-3);
  return max(x / max(u_grade, vec3(1e-3)), vec3(0.0)) / max(u_exposure, 1e-6);
}
// Where a world point falls on screen: xy in 0..1, z the depth buffer's value.
vec3 to_screen(vec3 p){
  vec4 c = u_mvp * vec4(p, 1.0);
  return c.xyz / max(c.w, 1e-12) * 0.5 + 0.5;
}
// Is a point inside the ground the scene drew - behind its surface on screen?
bool in_ground(vec3 p){
  vec3 s = to_screen(p);
  if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) return false;
  return texture(u_scene_depth, s.xy).r < s.z;
}
// How far below a surface point the ground is, straight down, metres -
// found by halving, up to `reach`; 1e6 when it is deeper. Straight down
// and not along the view: at a grazing view the ray meets the bed far
// beyond the point, and a shore's foam measured that way grows with
// distance into a band.
float ground_below_m(vec3 surf, vec3 up, float reach){
  if (!in_ground(surf - up * (reach / u_size_m))) return 1.0e6;
  float lo = 0.0, hi = reach;
  for (int i = 0; i < 6; ++i){
    float mid = 0.5 * (lo + hi);
    if (in_ground(surf - up * (mid / u_size_m))) hi = mid; else lo = mid;
  }
  return hi;
}
void main(){
  if (u_lvl_inner == 1){
    vec2 d = abs(v_rel_m - u_inner_rel_m);
    if (max(d.x, d.y) < u_inner_r_m) discard;
  }
  if (u_lvl_outer == 1){
    vec2 d = abs(v_rel_m - u_outer_rel_m);
    if (max(d.x, d.y) >= u_outer_r_m) discard;
  }
  vec2 xz = u_wave_origin + v_rel_m / u_size_m;
  vec2 dc = xz - 0.5;
  if (abs(dc.x) > u_param_lim.x || abs(dc.y) > u_param_lim.y) discard;
  if (pl_world_flat()){
    float rw = u_world_outline == 1 ? max(abs(dc.x), abs(dc.y)) : length(dc);
    if (rw > u_shell_w * 0.5) discard;
  } else if (u_world_shape.y > 0.5 && abs(dc.y) > u_shell_w * 0.5) discard;
  float reach = length(dc);
  if ((u_reach_square == 1 ? max(abs(dc.x), abs(dc.y)) : reach) > u_reach) discard;

  vec3 V = u_cam - v_world;
  float dist = length(V);
  V /= max(dist, 1e-9);
  vec3 east, up, north;
  pl_sphere_frame(xz, u_planet_radius, east, up, north);
  // The waves at this pixel: every one the pixel can resolve, and what the
  // rest would have added to the slopes as roughness. The pixel's footprint
  // on the water is worked out from the distance and the angle it is seen
  // at, never from screen derivatives: where a clipmap level morphs, its
  // triangles thin to slivers whose derivatives are nonsense, and every
  // sliver drew a dot along the level's rim.
  float fw = (u_ortho == 1 ? u_pixel_k : dist * u_pixel_k) * u_size_m /
             max(abs(dot(V, up)), 0.1);
  fw = max(fw, 1e-4);
  vec3 disp, dpdx, dpdz; float lost;
  wv_eval(v_rel_m, 3.0 * fw, disp, dpdx, dpdz, lost);
  vec3 nf = normalize(cross(dpdz, dpdx));
  float jac = dpdx.x * dpdz.z - dpdx.z * dpdz.x;
  vec3 N = normalize(east * nf.x + up * nf.y + north * nf.z);

  // What the water covers - from the scene's own depth, so a tile, the ground
  // beyond it, a rock and a boat are all bed and all shore. Along this ray
  // the light crosses `thick` metres of water.
  vec2 suv = gl_FragCoord.xy / u_viewport;
  float zb = texture(u_scene_depth, suv).r;
  float ls = lin_depth(gl_FragCoord.z);
  // How coarse the depth buffer is out here, metres a step. A few metres of
  // lake far away sit inside one step: measured, they read as no water at
  // all, and every distant lake turned see-through and white with foam. So
  // the measurement is trusted only where the grain is fine against what it
  // measures, and where it is not the water counts as deep.
  float grain_m = u_ortho == 1 ? 0.0
                               : ls * ls / max(u_depth_near * 16777216.0, 1e-12) * u_size_m;
  float trust_thick = 1.0 - smoothstep(0.5, 6.0, grain_m);
  // Two tiles out the planet's own ground paints the lakes its coarse
  // triangles cover (FS_INF), always as deep water; the sea here fades to
  // deep over the same distances so one lake is not two colours.
  if (u_ortho == 0) trust_thick *= 1.0 - smoothstep(1.5, 2.5, dist);
  float thick_m = 1.0e6;
  if (zb < 1.0){
    float lb = lin_depth(zb);
    float t = u_ortho == 1 ? lb - ls : (lb / max(ls, 1e-12) - 1.0) * dist;
    thick_m = max(t, 0.0) * u_size_m / max(trust_thick, 1e-3);
  }
  // Light through water: red goes first, blue last - the shallow colour's
  // proportions - over `clarity` metres for the clearest channel.
  vec3 tint = u_w_shallow / max(max(u_w_shallow.r, u_w_shallow.g), max(u_w_shallow.b, 1e-4));
  vec3 T = exp(-thick_m / (max(u_w_clarity, 0.01) * max(tint, vec3(0.03))));
  // The bed seen through a tilted surface shifts, the more the deeper it is
  // (a quarter of the tilt times the water, water's index being 4/3) - but
  // never onto something standing in front of the water.
  vec2 ruv = suv;
  if (zb < 1.0){
    float k = min(thick_m, 4.0) * 0.25 / u_size_m;
    vec2 shifted = to_screen(v_world + (N - up) * k).xy - to_screen(v_world).xy + suv;
    float zr = texture(u_scene_depth, shifted).r;
    if (lin_depth(zr) > ls && zr < 1.0 && shifted == clamp(shifted, vec2(0.0), vec2(1.0)))
      ruv = shifted;
  }
  vec3 behind = scene_light(ruv);

  vec3 L = normalize(u_sun);
  float shadow = water_shadow(v_world);
  vec3 sun_c = u_sun_color * u_sun_intensity;
  vec3 sky_amb = u_sky_light * u_ambient;
  float ndv = max(dot(N, V), 1e-3);
  float F = wat_fresnel(ndv);
  vec3 R = reflect(-V, N);
  float rup = dot(R, up);
  if (rup < 0.0) R -= 2.0 * rup * up; // a ray into the sea meets the sky it was heading for
  // The sea's roughness blurs the sky it reflects; the sun's own width only
  // spreads the sun's glint. Folded into both, a large sun turned a calm
  // lake's mirror of the clouds into a smear.
  float alpha = sqrt(0.0004 + 2.0 * lost + u_w_sun_half * u_w_sun_half);
  vec3 sky = wat_sky(R, L, u_sky_zenith, u_sky_horizon, u_sun_color,
                     min(sqrt(0.0004 + 2.0 * lost), 1.0));
  vec3 glint = wat_sun_glint(N, V, L, min(alpha, 1.0), sun_c) * shadow;
  vec3 body = wat_body(up, L, sun_c, shadow, sky_amb);
  // what leaves the surface, and per channel what of the bed gets through
  vec3 add = sky * F + glint + (1.0 - F) * body * (1.0 - T);
  vec3 through = (1.0 - F) * T;

  if (u_w_foam_on == 1){
    // Vue's two foams. Along the coasts: where the ground comes within the
    // typical depth below the surface, in bands that run in toward it. Over
    // the waves: where a crest gathers the water in and starts to break
    // (the Jacobian drops), as much of it as the coverage allows.
    vec2 pm = v_rel_m - u_w_wind * (u_w_time * 0.3);
    // the pattern's cells are small and sharp: averaged over the pixel's
    // whole stretch along a grazing view, or they alias into a net
    float pat = foam_pattern(pm, fw * max(abs(dot(V, up)), 0.1) / max(abs(dot(V, up)), 0.02));
    float fd = max(u_w_foam_depth, 0.01);
    // Past about half the typical depth of grain the probe straight down is
    // noise, and the depth comes instead from the water the view ray crosses
    // (already counted deep where even that is past trusting) - leaning it
    // toward deep at a grazing view, where it would widen the shore's foam
    // into a band.
    float trust = 1.0 - smoothstep(0.15 * fd, 0.6 * fd, grain_m);
    float depth_m = trust > 0.0 ? ground_below_m(v_world, up, fd * 1.5) : 1.0e6;
    depth_m = mix(thick_m * max(dot(V, up), 0.25), depth_m, trust);
    float near = 1.0 - smoothstep(0.0, fd, depth_m);
    float surf = 0.5 + 0.5 * sin(depth_m / fd * 7.0 - u_w_time * 1.6 + pat * 2.5);
    float coast = clamp(near * mix(0.6, 1.0, surf) * u_w_foam_amount * 1.6, 0.0, 1.0);
    // a sea squeezed to 0.55 of its area is breaking whatever the coverage;
    // full coverage whitens every crest that gathers in at all
    float j0 = mix(0.55, 1.05, clamp(u_w_foam_coverage, 0.0, 1.0));
    float crest = clamp((j0 - jac) / 0.25, 0.0, 1.0) * clamp(u_w_foam_crests * 1.5, 0.0, 1.0) *
                  u_w_whitecaps;
    float want = clamp(max(coast, crest), 0.0, 1.0);
    float foam = want > 0.001 ? smoothstep(1.0 - want, 1.3 - want, pat) : 0.0;
    // and the waterline itself, a thin unbroken edge - never thinner than a
    // third of a pixel, so a coast seen from far off keeps its white line
    float edge = max(0.03 + fd * 0.04, 0.35 * fw);
    foam = max(foam, (1.0 - smoothstep(0.0, edge, depth_m)) * clamp(u_w_foam_amount, 0.0, 1.0));
    vec3 fn = normalize(mix(N, up, 0.6));
    vec3 foam_c = u_w_foam_color *
                  (sun_c * max(dot(fn, L), 0.0) * shadow / 3.14159265 + sky_amb);
    add = mix(add, foam_c, foam);
    through *= 1.0 - foam;
  }
  float tr = dot(through, vec3(1.0 / 3.0));

  // the same air as everything else; only the light the water itself sends
  // gets new air in front of it - what shows through carries its own
  float fog_f; vec3 fog_c;
  fog_terms(v_world, u_cam, dist, u_hscale, L, u_sun_color, fog_f, fog_c);
  vec3 own = apply_fog_terms(add, fog_f, fog_c) - fog_c * fog_f * tr;
  // where the ground beyond fades into the sky, so does the sea on it
  if (u_horizon == 1){
    float hz = 1.0 - smoothstep(23.0, 30.2, reach);
    if (hz < 1.0){
      if (u_planet_radius > 0.0) g_sky_up = pl_world_up_at(u_cam, u_planet_radius);
      vec3 far_sky = wat_sky(normalize(v_world - u_cam), L, u_sky_zenith, u_sky_horizon,
                             u_sun_color, 0.0);
      g_sky_up = vec3(0.0, 1.0, 0.0);
      own = mix(far_sky * (1.0 - tr), own, hz);
    }
  }
  vec3 col = own + through * behind;
  if (u_aov != 0){
    if (u_aov == 13){ frag = vec4(col, 1.0); return; }
    frag = aov_out(u_aov, dist, N, u_w_deep, v_world, float(u_object_id), glint, shadow,
                   body, glint + sky * F, fog_f, fog_c, 1.0, add);
    return;
  }
  if (u_selected == 1) col = mix(col, vec3(1.0, 0.55, 0.18) * 0.6, 0.12);
  frag = vec4(pow(aces(col * u_exposure), vec3(1.0 / 2.2)), 1.0);
})GLSL";

} // namespace studio
