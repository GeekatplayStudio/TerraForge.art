// Geekatplay TerraForge - the lens flare, as the optical pass draws it
// (renderer_post.cpp splices this in).
//
// A real flare is several different things the glass does to one bright
// light, and the old one was only the last of them - five soft discs and a
// glow. Photographs of the sun through a lens show, from the inside out:
//
//  - the core, white-hot where the sensor saturates and tinted by the light
//    at its skirt;
//  - the starburst, thin rays of different lengths diffracted by the
//    aperture's blades, dispersed cool and warm toward their tips;
//  - through an anamorphic lens, a streak across the whole frame, with faint
//    parallel lines from the cylinder's other surfaces;
//  - a thin chromatic ring, where the three colours focus at slightly
//    different radii;
//  - the ghosts, reflections between the elements in the aperture's own
//    shape, strung along the line from the light through the frame's centre
//    and tinted by the coatings.
//
// Everything is analytic in the pixel's offset from the sun, so the flare is
// sharp at any resolution and costs a handful of arithmetic per pixel.
#include "renderer_internal.hpp"

namespace studio {

extern const char *const POST_FLARE_GLSL; // a namespace const is local without it
const char *const POST_FLARE_GLSL = R"(
uniform int u_flare_style;   // 0 classic ghosts, 1 cinematic, 2 anamorphic
uniform float u_flare_rays, u_flare_streak, u_flare_ghosts, u_flare_halo;
uniform vec3 u_sun_rgb;      // the sun's colour, brightest channel 1
uniform float u_sun_radius;  // its disc, frame heights
// the parts' shapes (CameraData): at their defaults, the flare as it was
uniform float u_flare_core, u_flare_ray_length, u_flare_streak_length;
uniform vec3 u_flare_streak_tint;
uniform float u_flare_halo_radius, u_flare_chroma, u_flare_seed;
uniform int u_flare_ray_count, u_flare_ghost_count, u_flare_blades;
float fl_hash(float n){ return fract(sin(n * 12.9898 + 4.1414) * 43758.5453); }
// The light the lens adds at this pixel. c: the pixel from the frame's
// middle, s: the sun from it, both in uv.
vec3 flare_light(vec2 c, vec2 s){
  vec2 k = vec2(u_aspect, 1.0);   // uv to frame heights
  vec2 p = (c - s) * k;           // from the sun
  float r = length(p);
  float px = u_texel.y;           // one pixel, frame heights
  vec3 tint = u_sun_rgb;
  vec3 hot = mix(tint, vec3(1.0), 0.7);
  vec3 L = vec3(0.0);
  // The core: white where it saturates, a bloom round that, and a wide soft
  // glow tinted by the light. Sized by the lens, not by the sun's disc - a
  // half-degree sun still floods the middle of the frame.
  float core_r = max(u_sun_radius, px * 2.5);
  L += hot * exp(-r / max(core_r * 1.2, 0.004)) * 2.2 * u_flare_core;
  L += hot * exp(-r / 0.035) * 0.85 * u_flare_core;
  L += tint * 0.12 / (1.0 + (r * r) / (0.1 * 0.1)) * u_flare_core;
  // the starburst: two sets of rays, long and few, short and many, each ray
  // at an irregular angle and length; only the nearest three of a set can
  // reach this pixel
  if (u_flare_rays > 0.0 && u_flare_style > 0){
    float ang = atan(p.y, p.x);
    float long_n = float(u_flare_ray_count);
    for (int set = 0; set < 2; ++set){
      float N = set == 0 ? long_n : floor(long_n * 2.75 + 0.5);
      float kc = floor(ang / 6.2831853 * N + 0.5);
      for (int m = -1; m <= 1; ++m){
        float kk = kc + float(m);
        float id = mod(kk, N) + float(set) * 101.0 + u_flare_seed * 7.13;
        // not every blade throws a ray a camera records
        if (fl_hash(id + 17.0) < (set == 0 ? 0.25 : 0.4)) continue;
        float a = (kk + (fl_hash(id) - 0.5) * 0.8) / N * 6.2831853;
        float da = abs(mod(ang - a + 3.14159265, 6.2831853) - 3.14159265);
        float perp = r * sin(min(da, 1.5));
        // lengths on a steep law: most rays short, a few reaching far
        float hl = fl_hash(id + 7.0);
        float len = set == 0 ? 0.07 + pow(hl, 1.8) * 0.5 : 0.03 + pow(hl, 1.6) * 0.16;
        len *= u_flare_rays * u_flare_ray_length;
        float width = px * (set == 0 ? 1.0 : 0.7) + r * 0.0025;
        float along = exp(-r / max(len * 0.6, 1e-4)) * (1.0 - smoothstep(len, len * 2.4, r));
        float lat = exp(-perp * perp / (width * width));
        float bright = (set == 0 ? 1.5 : 0.6) * (0.3 + 0.7 * pow(fl_hash(id + 3.0), 1.4));
        vec3 tip = fl_hash(id + 11.0) > 0.5 ? vec3(0.55, 0.75, 1.0) : vec3(1.0, 0.55, 0.75);
        // the dispersion decides how far the tips' colours part from the core's
        tip = max(mix(hot, tip, u_flare_chroma), vec3(0.0));
        vec3 rc = mix(hot, tip, smoothstep(0.0, len * 0.9, r));
        L += rc * along * lat * bright * min(u_flare_rays, 1.5);
      }
    }
  }
  // the streak: short in the cinematic style, across the frame through an
  // anamorphic lens, cool at one end and warm at the other
  if (u_flare_streak > 0.0 && u_flare_style > 0){
    float reach = (u_flare_style == 2 ? 1.5 : 0.3) * u_flare_streak * u_flare_streak_length;
    float h = px * 1.3 + abs(p.x) * 0.0015;
    float line = exp(-p.y * p.y / (h * h)) * exp(-abs(p.x) / max(reach, 1e-3));
    float soft = exp(-p.y * p.y / (h * h * 60.0)) * exp(-abs(p.x) / max(reach * 0.5, 1e-3)) * 0.35;
    vec3 end = p.x < 0.0 ? u_flare_streak_tint : vec3(1.0, 0.5, 0.65);
    vec3 sc = mix(hot, end, smoothstep(0.0, 0.3, abs(p.x)));
    L += sc * (line * (u_flare_style == 2 ? 1.1 : 0.55) + soft) * min(u_flare_streak, 1.5);
    if (u_flare_style == 2){
      for (int i = -1; i <= 1; i += 2){
        float y = p.y - float(i) * 0.011;
        L += u_flare_streak_tint * exp(-y * y / (h * h)) *
             exp(-abs(p.x) / max(reach * 0.6, 1e-3)) * 0.22 * min(u_flare_streak, 1.5);
      }
    }
  }
  // the ring, each colour at its own radius
  if (u_flare_halo > 0.0 && u_flare_style > 0){
    float R = u_flare_halo_radius + u_sun_radius;
    float w = 0.004 + px;
    float part = 0.015 * u_flare_chroma; // how far apart the colours focus
    vec3 ring = vec3(exp(-pow((r - R) / w, 2.0)),
                     exp(-pow((r - R * (1.0 - part)) / w, 2.0)),
                     exp(-pow((r - R * (1.0 - 2.0 * part)) / w, 2.0)));
    L += ring * vec3(1.0, 0.55, 0.5) * 0.3 * u_flare_halo;
    L += tint * exp(-pow((r - R * 0.9) / 0.06, 2.0)) * 0.025 * u_flare_halo;
  }
  // the ghosts: the aperture's blades, filled faintly and rimmed, along
  // the line from the sun (1) through the middle (0) and past it
  if (u_flare_ghosts > 0.0 && u_flare_style > 0){
    // the polygon's edge normals, a blade apart, one of them at 0.35 rad
    float seg = 6.2831853 / float(max(u_flare_blades, 3));
    for (int i = 0; i < 12; ++i){
      if (i >= u_flare_ghost_count) break;
      float fi = float(i) + u_flare_seed * 3.71;
      float t = mix(0.75, -1.35, fl_hash(fi + 21.0));
      float size = mix(0.012, 0.085, pow(fl_hash(fi + 31.0), 1.6));
      vec2 q = (c - s * t) * k;
      if (dot(q, q) > size * size * 1.7) continue;
      // the distance across the polygon: the point's reach along the nearest
      // edge normal
      float qa = atan(q.y, q.x) - 0.35 + seg * 0.5;
      float hx = length(q) * cos(mod(qa, seg) - seg * 0.5);
      float fill = 1.0 - smoothstep(size * 0.9, size, hx);
      float rim = exp(-pow((hx - size * 0.94) / (size * 0.05 + px), 2.0));
      float pick = fl_hash(fi + 41.0);
      vec3 gt = pick < 0.33 ? vec3(0.35, 0.95, 0.85)
              : (pick < 0.66 ? vec3(1.0, 0.45, 0.7) : vec3(1.0, 0.75, 0.35));
      // the small ones are bright and the large ones are washes
      float e = 0.14 + 0.5 * (1.0 - smoothstep(0.012, 0.085, size));
      L += gt * (fill * 0.35 + rim * 0.5) * e * u_flare_ghosts;
      // and the smallest focus to a hot point of their own
      if (size < 0.03)
        L += mix(gt, vec3(1.0), 0.5) * exp(-length(q) / (size * 0.18 + px)) * 0.9 * u_flare_ghosts;
    }
  }
  return L;
}
)";

} // namespace studio
