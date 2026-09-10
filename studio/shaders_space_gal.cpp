// Geekatplay TerraForge — deep space, part 3 of 4: the milky band across the
// sky, and the galaxy discs a Nebula object can be.
//
// The band is our own galaxy seen edge-on from inside it: a latitude
// profile, a bulge toward the core, fractal structure along it, dark rifts
// of dust that redden what they dim, and - the thing that makes it read as
// a galaxy rather than a smear - a haze of stars too faint to resolve. That
// haze used to be a hash of the direction rounded to a lattice, which drew
// the sky as bright cubes; it is a very fine star grid now.
//
// The discs are drawn flat, because a galaxy is flat: a spiral's arms and
// dust lanes, an elliptical's smooth glow, a planetary nebula's ring.
// Spliced after shaders_space_stars.cpp, which sp_star_grid comes from.
#include "renderer_shaders.hpp"

namespace studio {

const char *const SPACE_GAL_FN = R"GLSL(
uniform int u_sp_gal, u_sp_gal_seed;
uniform float u_sp_gal_int, u_sp_gal_width, u_sp_gal_dust, u_sp_gal_grain;
uniform vec3 u_sp_gal_axis, u_sp_gal_zero, u_sp_gal_color;

vec3 sp_galaxy(vec3 d, float pix){
  if (u_sp_gal == 0 || u_sp_gal_int <= 0.0) return vec3(0.0);
  vec3 ax = u_sp_gal_axis, z0 = u_sp_gal_zero;
  float sb = clamp(dot(d, ax), -1.0, 1.0);
  float b = asin(sb);
  vec3 ip = d - ax * sb;
  float ipl = length(ip);
  ip = ipl > 1e-5 ? ip / ipl : z0;
  float l = atan(dot(ip, cross(ax, z0)), dot(ip, z0));
  float w = max(u_sp_gal_width, 0.5) * 0.017453293;
  float seed = float(u_sp_gal_seed);
  // A disc seen edge-on falls off across its plane with long wings, not
  // with a hard edge: a soft core that never quite stops.
  float x = abs(b) / w;
  float band = 1.0 / (1.0 + x * x * (1.0 + 0.30 * x));
  // the bulge: bright, but narrow enough to read as a core rather than
  // wash out a third of the sky
  float core = exp(-(l * l / 0.16 + b * b / (w * w * 2.0)));
  // The band's structure comes from the same filtered volume the nebulas
  // are marched through. Built by hand out of value noise it showed the
  // lattice it stood on - boxes of haze with straight edges, stretched
  // across degrees of sky - and it cost a fractal's worth of hashing at
  // every pixel of the band. Three texture reads do it better and faster.
  //
  // The seed slides the lookup rather than reseeding a hash, which is what
  // a tiling volume allows; the band must not be stretched much along
  // itself either, or the whole galaxy comes out as parallel stripes.
  vec3 sd = fract(vec3(seed * 0.0173, seed * 0.0431, seed * 0.0977));
  vec3 q = vec3(l * 0.36, b * 0.46, 0.27) + sd;
  float f = sp_vol(q).r;
  float mottle = sp_vol(q * 2.7 + 0.41).a;
  float ridge = sp_vol(q * 1.6 + 0.23).g;
  float bright = band * (0.24 + 0.72 * f + 0.34 * mottle + 0.34 * pow(ridge, 2.5))
                 + core * 1.1;
  // The rifts: long dark lanes of dust lying in the plane. Dust does not
  // simply darken what is behind it, it reddens it - blue is absorbed
  // most - which is why the band goes amber where it is thickest.
  float rift = sp_vol(vec3(l * 0.44, b * 0.72, 0.61) + sd.zxy).g;
  float lane = smoothstep(0.52, 0.86, rift) * exp(-pow(abs(b) / (w * 0.85), 2.0))
               * (0.45 + 0.55 * smoothstep(2.6, 0.4, abs(l)));
  vec3 ext = exp(-lane * clamp(u_sp_gal_dust, 0.0, 1.0) * 2.2 * vec3(1.0, 1.45, 2.15));
  vec3 tint = mix(vec3(0.80, 0.86, 1.00), vec3(1.00, 0.84, 0.62), clamp(core * 1.2, 0.0, 1.0));
  vec3 col = u_sp_gal_color * tint * bright * u_sp_gal_int * 0.115;
  // the stars the band is made of, too faint and too many to tell apart
  float grain_prob = clamp(band * (0.30 + 0.95 * f), 0.0, 1.0) * clamp(u_sp_gal_grain, 0.0, 1.0);
  if (grain_prob > 0.004)
    col += sp_star_grid(d, pix, 520.0, 0.55 * grain_prob, 0.40, seed + 313.0, 1.0, 0)
           * u_sp_gal_int * 0.45;
  return col * ext;
}

// ---- the discs a Nebula object can be -------------------------------------
// `uv` is the direction in the disc's own frame, in units of its angular
// radius, already turned and squashed by its tilt.

vec3 sp_disc_spiral(vec2 uv, float seed, float dust, float detail, float arms,
                    float tilt, vec3 c1, vec3 c2){
  vec2 q = vec2(uv.x, uv.y / max(cos(tilt), 0.10));
  float rr = length(q);
  if (rr > 2.6) return vec3(0.0);
  float th = atan(q.y, q.x);
  float lr = log(max(rr, 1.0e-3));
  float pitch = 3.0 + detail * 2.5;
  float arm = pow(0.5 + 0.5 * cos(arms * th - pitch * lr), 2.5 + detail * 3.0);
  float disc = exp(-rr * 2.1);
  float bulge = exp(-rr * rr * 16.0);
  float grain = sp_fbm(vec3(q * 5.5, seed), seed + 1.0, 4);
  float lum = (disc * (0.16 + arm * (0.5 + 1.05 * grain)) + bulge * 2.4)
              * smoothstep(2.5, 1.1, rr);
  // Dust rides the inner edge of each arm, a little ahead of the light,
  // and a ridged field breaks it into the flocculent lanes a real spiral
  // shows. It reddens as it dims.
  float lane = pow(0.5 + 0.5 * cos(arms * th - pitch * lr + 0.6), 5.0)
               * smoothstep(0.06, 0.32, rr) * (1.0 - bulge);
  float rift = smoothstep(0.50, 0.80, sp_ridge(vec3(q * 3.6 + 7.0, seed), seed + 5.0, 4));
  vec3 ext = exp(-(lane * 1.4 + rift * 0.7) * dust * disc * 2.0 * vec3(1.0, 1.35, 1.95));
  vec3 cc = mix(c2, c1, clamp(bulge / (bulge + disc * 0.5 + 1.0e-4), 0.0, 1.0));
  vec3 col = cc * lum;
  // the knots of star birth strung along the arms, blue and hot
  float knots = sp_points(q * 0.5 + 0.5, 34.0, 0.30, 0.55, seed + 8.0);
  col += vec3(0.58, 0.79, 1.00) * knots * arm * disc * 1.8;
  return col * ext;
}

vec3 sp_disc_elliptical(vec2 uv, float seed, float detail, float tilt, vec3 c1, vec3 c2){
  vec2 q = vec2(uv.x, uv.y / max(cos(tilt), 0.30));
  float rr = length(q);
  if (rr > 2.8) return vec3(0.0);
  // a de Vaucouleurs profile: very bright in the middle, and a halo that
  // fades for a long way out
  float lum = exp(-pow(max(rr, 1.0e-3) * 1.55, 0.55) * 2.9) * smoothstep(2.7, 0.9, rr);
  float grain = sp_fbm(vec3(q * 7.0, seed), seed + 1.0, 3);
  vec3 col = mix(c1, c2, clamp(rr * 0.55, 0.0, 1.0)) * lum * (0.88 + 0.24 * grain) * 1.5;
  // globular clusters round it, the old stars an elliptical is made of
  col += mix(c1, vec3(1.0), 0.5) * sp_points(q * 0.5 + 0.5, 22.0, 0.22, 0.35, seed + 12.0)
         * exp(-rr * 0.9) * 0.35 * detail;
  return col;
}

vec3 sp_disc_planetary(vec2 uv, float seed, float dens, float detail, vec3 c1, vec3 c2){
  float rr = length(uv);
  if (rr > 1.45) return vec3(0.0);
  float th = atan(uv.y, uv.x);
  // the shell is never a perfect circle: it is thicker where the star
  // threw off more, so the ring brightens and thins around it
  float wob = 0.55 + 0.85 * sp_fbm(vec3(cos(th) * 2.0, sin(th) * 2.0, seed), seed + 2.0, 4);
  float ring = exp(-pow((rr - 0.62) / (0.10 + 0.12 * dens), 2.0)) * wob;
  float inner = exp(-rr * rr * 5.5) * 0.75;
  float shell = smoothstep(1.35, 0.85, rr) * 0.20
                * sp_fbm(vec3(uv * 3.2, seed), seed + 3.0, 3 + int(detail * 4.0));
  // the outer gas is doubly ionised and teal, the inner rim of the ring is
  // hydrogen and red: the two colours a planetary nebula always shows
  vec3 col = c1 * ring + c2 * inner + mix(c1, c2, 0.5) * shell;
  col += vec3(0.90, 0.95, 1.00) * exp(-rr * rr * 320.0) * 2.4; // the dying star
  return col;
}
)GLSL";

} // namespace studio
