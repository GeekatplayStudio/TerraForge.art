// Geekatplay TerraForge — the sky shader: the air, the sun, deep space, and the
// cloud layers marched through it (shaders_clouds.cpp). Split from
// shaders_scene.cpp for the 500-line module rule.
#include "renderer_shaders.hpp"

namespace studio {

// On the far plane itself: the sky pass is drawn after everything solid with
// GL_LEQUAL, so it lands only where the depth was never written
// (renderer_scene.cpp). At 0.99999 it would also have covered distant ground
// whose depth rounds above that.
const char *const VS_SKY = R"GLSL(#version 430 core
out vec2 v_ndc;
void main(){
  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0-1.0;
  v_ndc = p;
  gl_Position = vec4(p, 1.0, 1.0);
})GLSL";

// shared sky helper injected into several shaders
// The sky function is shared by the sky pass, terrain reflections and water
// reflections, so the backdrop dome lives in it too: whatever looks at the sky
// sees the same picture. renderer_backdrop.cpp binds the sampler and uniforms
// in every program that carries this block.
const char *const SKY_FN = R"GLSL(
// The light the sky casts on level ground, measured through the same sky and
// cloud march the viewport draws (studio/sky_light.cpp). Reflected radiance
// is albedo times this: the cosine-weighted integral over the sky and the
// division by pi cancel, which is why one number does the whole job.
//
// It replaced the average of the two sky *colours* - a number between 0 and
// 1 where the real one is several times larger. That substitution is why a
// path-traced frame, which integrates the real sky, came out far brighter
// than its own preview. A uniform never set reads as zero, so a shader that
// forgets to upload it goes black rather than quietly wrong.
uniform vec3 u_sky_light;
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
// The sky's up: the world's surface's up at the eye (world_shape.hpp). Every
// program starts with world y - dot(dir, (0,1,0)) is dir.y to the bit - and
// the sky pass sets it from the world's shape, so on a ring the gradient
// stands over the ring's ground and not over the tile's y axis.
vec3 g_sky_up = vec3(0.0, 1.0, 0.0);
vec3 sky_color(vec3 dir, vec3 zenith_c, vec3 horizon_c, vec3 sun, vec3 sun_col,
               float atmo){
  float t = clamp(dot(dir, g_sky_up)*0.5+0.5, 0.0, 1.0);
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

const char *const FS_SKY_SRC = R"GLSL(#version 430 core
in vec2 v_ndc;
layout(location = 0) out vec4 frag;
// the nebulas' transmittance, written only while u_neb_mode is 1
layout(location = 1) out vec4 frag_tr;
uniform mat4 u_inv_vp;
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_atmo;
uniform float u_sun_angle, u_sun_glow, u_sun_glow_size;
uniform int u_fog_type;
uniform vec3 u_fog_color;
uniform float u_fog_density;
uniform float u_sun_intensity;
// panorama export: equirectangular directions, linear HDR out, no sun disc
uniform int u_panorama;
uniform int u_hdr;
uniform int u_no_sun;
uniform float u_space; // 0 = inside the atmosphere, 1 = open space
uniform int u_aov;     // render pass being drawn, 0 = the picture
const float PI = 3.14159265;
// The world the air lies on (world_shape.hpp): its shape, the curvature
// this view draws the ground with, and whether the sun is a body inside it.
// Every height in here is an altitude over the world's surface - on a ring
// the clouds are a band round the inside of the ring, on a globe a shell
// over it - and the sky's up is the surface's up under the eye.
PL_SPHERE_PLACEHOLDER
SKY_WORLD_PLACEHOLDER
// The atmosphere is a layer of this height on the world's surface (0: the
// old rule, u_space by the camera's distance). How much of the ray lies in
// it, in vertical thicknesses, decides how much sky and how much space a
// pixel shows: straight up from the ground is one thickness (all sky), the
// limb of the world seen from space a short chord (a thin blue rim), a ray
// that misses the layer nothing but stars.
uniform float u_atm_h;
SPACE_FN_PLACEHOLDER
SKY_FN_PLACEHOLDER
CLOUD_SHAPE_PLACEHOLDER
CLOUD_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
// How much of the ray lies in the air, in vertical thicknesses of it: the
// band of altitude [0, u_atm_h] over the surface. Flat: a slab (a
// near-horizontal ray is capped, the way a real horizon's air runs out with
// distance). Curved: the space between two shells; on an inside world a
// ray that crosses the middle meets the far side's air too, on a globe the
// ground stops it at the inner shell.
uniform float u_atm_falloff;
// The airmass along one straight segment of an exponentially thinning
// atmosphere, exactly: the integral of exp(-h/Hs) with h running linearly
// from h0 to h1 over `len`. A handful of these follow the world's curve
// closely, and each one is arithmetic rather than a sample, so the answer
// does not shimmer where the samples happen to land.
float seg_air(float h0, float h1, float len, float Hs){
  h0 = max(h0, 0.0);
  h1 = max(h1, 0.0);
  float d = h1 - h0;
  if (abs(d) < 1.0e-5 * Hs) return len * exp(-h0 / Hs);
  return len * Hs / abs(d) * abs(exp(-h0 / Hs) - exp(-h1 / Hs));
}
float atm_path(vec3 ro, vec3 rd){
  float H = max(u_atm_h, 1e-5);
  // The scale height: how far up the air thins by a factor of e. Everything
  // below is measured in these, so straight up from the ground is one of
  // them however the dial is set - the sky over your head does not change
  // when you say how sharply the air thins above it.
  float Hs = max(H * clamp(u_atm_falloff, 0.02, 1.0), 1.0e-7);
  float t0, t1;
  if (!world_curved()){
    if (abs(rd.y) < 1e-6){
      if (ro.y < 0.0 || ro.y > H) return 0.0;
      t0 = 0.0; t1 = 40.0 * H;
    } else {
      float ta = -ro.y / rd.y, tb = (H - ro.y) / rd.y;
      t0 = max(min(ta, tb), 0.0); t1 = max(ta, tb);
    }
    if (t1 <= t0) return 0.0;
    t1 = min(t1, t0 + 60.0 * H);
  } else {
    float R = u_world_r;
    bool ins = u_world_shape.z > 0.5;
    float r_lo = ins ? R - H : R, r_hi = ins ? R : R + H;
    float b0, b1;
    if (r_hi <= 0.0 || !shell_hits(ro, rd, r_hi, b0, b1)) return 0.0;
    t0 = max(b0, 0.0);
    t1 = b1;
    if (t1 <= t0) return 0.0;
    // The ground stops the ray on a globe. On an inside world it does not:
    // the ray crosses the hollow middle and meets the far side's air, and
    // the profile below gives the empty middle nothing, because the middle
    // is many scale heights from any surface.
    float s0, s1;
    if (!ins && r_lo > 0.0 && shell_hits(ro, rd, r_lo, s0, s1)){
      if (s0 > t0) t1 = min(t1, s0);
      else if (s1 > t0) t0 = max(t0, s1);
    }
    if (t1 <= t0) return 0.0;
  }
  if (!world_slab(ro, rd, t0, t1)) return 0.0;
  // The air does not stop at a shell, it thins - which is the whole reason
  // a planet seen from space has a soft blue band round it rather than a
  // drawn line. Eight segments, each integrated exactly.
  const int N = 8;
  float dt = (t1 - t0) / float(N);
  float tau = 0.0;
  float h_prev = world_alt(ro + rd * t0);
  for (int i = 0; i < N; ++i){
    float h_next = world_alt(ro + rd * (t0 + float(i + 1) * dt));
    tau += seg_air(h_prev, h_next, dt, Hs);
    h_prev = h_next;
  }
  return tau / Hs;
}
void main(){
  vec3 dir;
  if (u_panorama == 1) {
    // Latitude-longitude, in the convention everything else here reads one
    // in: u through atan(d.x, -d.z), v down from +Y. That is bd_uv's mapping
    // for a lat-long backdrop, and it is the environment map convention the
    // offline engines use - written the other way round, the render's sky
    // came out as the viewport's sky turned a quarter turn, so the camera
    // looked at cloud and the render put clear blue there.
    float az = v_ndc.x * PI;
    float el = v_ndc.y * PI * 0.5;
    dir = vec3(cos(el)*sin(az), sin(el), -cos(el)*cos(az));
  } else if (u_panorama == 2) {
    // The irradiance probe. What a diffuse surface takes from the sky is
    // the integral of radiance against the cosine of the angle from its
    // normal, and with sin^2(elevation) spread evenly up the image that
    // integral is just the average of the pixels - no weights to get wrong
    // in the reduction, and only the upper half of the sky, which is the
    // half the ground can see.
    float az = v_ndc.x * PI;
    float el = asin(sqrt(clamp(v_ndc.y * 0.5 + 0.5, 0.0, 1.0)));
    dir = vec3(cos(el)*cos(az), sin(el), cos(el)*sin(az));
  } else {
    // Through the far plane, from the eye. The far point alone is not a
    // direction: taken as one, the whole sky sat up to a couple of degrees
    // off the ground drawn in front of it, and the sun with it.
    vec4 w = u_inv_vp * vec4(v_ndc, 1.0, 1.0);
    dir = normalize(w.xyz / w.w - u_cam);
  }
  // the sky stands over the world's surface under the eye: its up is the
  // surface's up there (world y on a flat world and, at the tile, on a globe)
  vec3 up = world_up(u_cam);
  g_sky_up = up;
  float dir_up = dot(dir, up);
  vec3 col = sky_color(dir, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  // an HDRI carries its own sun; drawing ours over it would light the scene
  // twice
  bool bd_sun_hidden = u_bd_on == 1 && u_bd_hide_sun == 1 && g_bd_weight > 0.0;
  vec3 sun_disc = vec3(0.0);
  vec3 sun_glow = vec3(0.0);
  float atm_scat = 1.0; // how much air the pixel looks through, 0..1
  {
    // The angle from the sun, which is what both the disc and the halo are
    // really functions of. acos is exact near zero where a dot product's
    // resolution runs out, and near zero is the whole sun.
    float ang = acos(clamp(dot(dir, u_sun), -1.0, 1.0));
    float half_w = max(u_sun_angle, 0.01) * 0.0087266; // degrees -> radians, halved
    if (u_no_sun == 0 && !bd_sun_hidden) {
      // A disc with a soft rim one part in forty of its width: the limb of
      // a star is not a step, and a hard edge aliases into a polygon at any
      // resolution. Its brightness is per solid angle, so making the sun
      // larger spreads the same light rather than adding more.
      float edge = half_w * 0.025;
      float d = 1.0 - smoothstep(half_w - edge, half_w + edge, ang);
      float ref = 0.0046;  // our own Sun's half-width, radians
      sun_disc = u_sun_color * u_sun_intensity * 6.0 * d *
                 (ref * ref) / (half_w * half_w);
    }
    // The aureole. Forward scattering piles light up around the sun, over
    // degrees rather than the arc-minutes of the disc, and it is air doing
    // it - so it fades with the air on the ray and dies in vacuum.
    if (u_sun_glow > 0.0 && !bd_sun_hidden) {
      float w = max(u_sun_glow_size, 0.5) * 0.0174533;
      float halo = exp(-ang / w) * 0.65 + exp(-ang / (w * 4.0)) * 0.35;
      sun_glow = u_sun_color * u_sun_intensity * u_sun_glow * 0.12 * halo * u_atmo;
    }
  }
  // Deep space: the stars, the galaxy and the nebulas, behind the air. How
  // much of it reaches the eye is worked out first, because the backdrop is
  // the most expensive thing in this shader - a nebula is marched as a
  // volume - and in daylight, under a full sky, none of it shows. A terrain
  // scene at noon must not pay for a sky it cannot see.
  float sp_w = 0.0;
  if (u_atm_h > 0.0) {
    float path0 = atm_path(u_cam, dir);
    float lum0 = dot(col, vec3(0.3, 0.59, 0.11)) * (1.0 - exp(-5.0 * u_atmo * path0));
    sp_w = exp(-0.35 * u_atmo * path0) * (1.0 - smoothstep(0.0, 0.2, lum0));
  } else {
    sp_w = smoothstep(0.03, -0.12, u_sun.y) * clamp(dir_up * 4.0, 0.0, 1.0);
    sp_w = max(sp_w, u_space);
  }
  // The pixel's angular size, taken here where every pixel runs it: inside
  // the branch below a derivative is undefined, and the stars' size and the
  // nebulas' detail read it right where the branch starts and stops (dusk).
  float sp_pix = max(length(fwidth(dir)), 4.0e-4);
  // The half-size picture of the nebulas alone (renderer_space_half.cpp):
  // what they add and what they let through, where space shows at all. Its
  // pixel is twice the view's, which is the filtering a half-size picture
  // needs - the finest billows fade rather than alias.
  if (u_neb_mode == 1){
    vec3 tr = vec3(1.0);
    vec3 add = (sp_w > 0.002 && u_sp_on == 1) ? sp_nebulas(dir, sp_pix, tr) : vec3(0.0);
    frag = vec4(add, 1.0);
    frag_tr = vec4(tr, 1.0);
    return;
  }
  // The irradiance probe passes a negative size: its pixel is a fifth of a
  // radian, a star's core is drawn a pixel wide, and every star in the sky
  // came out a tenth of a steradian of light - the night ground lit like dusk.
  vec3 space = sp_w > 0.002 ? space_color(dir, u_panorama == 2 ? -sp_pix : sp_pix) : vec3(0.0);
  if (u_atm_h > 0.0) {
    // The air is a layer on the world (atm_path): what it scatters is the
    // sky, what it lets through is space and the sun. Stars are lost in
    // daylight the way they are outside - not hidden by the air but
    // outshone by it - so their share falls with the sky's own brightness.
    float path = atm_path(u_cam, dir);
    float scat = 1.0 - exp(-5.0 * u_atmo * path);
    float T = exp(-0.35 * u_atmo * path);
    atm_scat = scat;
    // the disc is seen through the air, the halo is made by it
    col = space * sp_w + sun_disc * T + col * scat + sun_glow * scat;
  } else {
    // the old rule: sky everywhere, thinning to space with the camera's
    // distance from the tile (u_space), stars coming out at night
    float night = smoothstep(0.03, -0.12, u_sun.y);
    col += sun_disc + sun_glow;
    if (night > 0.0 && dir_up > 0.0) col += space * night * clamp(dir_up * 4.0, 0.0, 1.0);
  }
  // the clouds on every ray that reaches the sky; the rays that end on the
  // ground get theirs after the ground is drawn (FS_CLOUD_OVER)
  vec4 cl = march_clouds(u_cam, dir, col, 1.0e30);
  col = cl.rgb;
  float fogw = 0.0;
  vec3 fogcol = vec3(0.0);
  if (u_fog_type != 0) {
    float horizon_fog = pow(1.0 - clamp(dir_up, 0.0, 1.0), 8.0);
    // haze is lit air: it darkens with the same daylight factor as the sky,
    // or night would end at the horizon line
    float fog_day = clamp(u_sun.y * 4.0 + 0.35, 0.035, 1.0);
    fogcol = u_fog_color * fog_day;
    fogw = clamp(horizon_fog * u_fog_density * 0.6, 0.0, 1.0);
    // the dome is at infinity too, but a photograph may already hold its own
    // haze, so how much of ours it receives is a dial
    fogw *= mix(1.0, u_bd_haze, g_bd_weight);
    fogw *= atm_scat; // no haze where there is no air on the ray
    col = mix(col, fogcol, fogw);
  }
  if (u_aov != 0) {
    if (u_aov == 1) frag = vec4(1.0e9, 0.0, 0.0, 1.0);          // depth: infinitely far
    else if (u_aov == 8) frag = vec4(1.0, 0.0, 0.0, 1.0);        // shadow: lit
    else if (u_aov == 11) frag = vec4(fogcol * fogw, 1.0 - fogw); // atmosphere
    else if (u_aov == 12 || u_aov == 13) frag = vec4(col, 1.0);  // environment / linear
    else frag = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  // Leaving the atmosphere: the sky thins to space so that pulling the camera
  // back actually reveals the planets instead of drowning them in daylight
  // haze. u_space is a smooth 0..1 from the camera's altitude, so the
  // transition is continuous — no popping at any zoom level.
  if (u_atm_h <= 0.0 && u_space > 0.0) {
    vec3 sp = space;
    if (u_no_sun == 0) {
      float s2 = max(dot(dir, u_sun), 0.0);
      sp += u_sun_color * pow(s2, 900.0) * 9.0; // the sun stays, airless
    }
    col = mix(col, sp, u_space);
  }
  if (u_hdr == 1) { frag = vec4(col, 1.0); return; } // linear for env maps
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

} // namespace studio
