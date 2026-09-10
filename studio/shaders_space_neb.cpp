// Geekatplay TerraForge — deep space, part 4 of 4: the nebulas.
//
// A nebula used to be a picture painted on the sky: one fractal in a disc,
// flat, with no way for one part of it to stand in front of another. This
// marches a ray through it instead, as a real cloud of gas and dust
// (space_noise.hpp holds the volume it samples), and integrates what comes
// back the way light actually arrives:
//
//   emission    the gas glows where a hot star inside has ionised it, and
//               the harder it is ionised the further the colour moves from
//               hydrogen's red toward doubly-ionised oxygen's teal. That
//               one rule is most of why a photograph of a nebula looks the
//               way it does: teal hearts, red outskirts.
//   reflection  dust scatters the starlight it does not absorb, and scatters
//               blue best, so the dust that is lit reads cold.
//   extinction  dust in front of gas absorbs blue hardest, so what shows
//               through it is not merely darker but redder.
//
// A dark nebula is the same march with the emission turned off. Galaxies
// and planetary nebulas stay flat - a disc is flat - and are drawn by
// shaders_space_gal.cpp. Spliced after it.
#include "renderer_shaders.hpp"

namespace studio {

const char *const SPACE_NEB_FN = R"GLSL(
uniform int u_neb_n, u_sp_steps;
// dir.xyz + angular radius; type, seed, brightness, density;
// tilt, rotation, detail, arms; dust, warp, glow, ionising sources
uniform vec4 u_neb_d[8], u_neb_a[8], u_neb_b[8], u_neb_e[8];
uniform vec3 u_neb_c1[8], u_neb_c2[8];

// The cloud at a point of the nebula's own unit sphere: how much gas glows
// there, and how much dust stands in the way. Warping the lookup with a
// coarse sample of the same volume is what turns a ball of noise into
// something with arms, hollows and a shape of its own.
void sp_neb_field(vec3 p, float warp, float dens, float detail, float dustk,
                  out float gas, out float dust){
  gas = 0.0;
  dust = 0.0;
  float rl = length(p);
  if (rl > 1.45) return; // nothing in the cloud reaches out this far
  vec3 w = p * 0.5 + 0.5;
  // One coarse sample is the largest scale in the body below. Without a
  // scale that big the cloud comes out an even haze from side to side,
  // which is the one thing a photograph of a nebula never is.
  float big = sp_vol(w * 0.29 + 0.61).r;
  vec3 q = w;
  if (warp > 0.0) q += (sp_vol(w * 0.41 + 0.17).rgb - 0.5) * (warp * 0.60);
  vec4 a = sp_vol(q * 0.87);
  // The edge is never a circle. Two scales push the boundary about - the
  // coarse one for how far the cloud reaches, the ridged mid one for the
  // wisps that trail off it - so it ends in filaments rather than on the
  // silhouette of the sphere the march happens to run through.
  float env = smoothstep(1.0, 0.22, rl * (0.50 + 0.62 * big + 0.52 * a.g));
  if (env <= 0.002) return;
  vec4 b = sp_vol(q * 2.13 + 0.37);
  // A fractal of value noise crowds its samples around the middle of its
  // range. Opening that out before the threshold is what gives the cloud
  // dense knots and empty voids instead of a haze at one density.
  float body = clamp((big * 0.42 + a.r * 0.38 + b.a * 0.20 - 0.5) * 3.0 + 0.5, 0.0, 1.0);
  float thr = 0.70 - dens * 0.42;
  float g = smoothstep(thr, thr + 0.18, body);
  // the filaments: the ridged field, thin and bright, and only where there
  // is gas for them to run through
  float fil = clamp(a.g * 0.55 + b.g * 0.45, 0.0, 1.0);
  g += pow(fil, 4.0) * detail * 1.4 * smoothstep(thr - 0.20, thr + 0.10, body);
  gas = g * env;
  // Dust gathers in the cellular field, which sits offset from the gas, so
  // it lies across the glow instead of inside it - the dark lanes and the
  // globules that a photograph of a nebula is mostly made of.
  float dn = clamp((a.b * 0.58 + b.r * 0.42 - 0.5) * 2.4 + 0.5, 0.0, 1.0);
  float dthr = 0.66 - dustk * 0.46;
  dust = smoothstep(dthr, dthr + 0.26, dn) * env;
}

// One nebula, marched. Adds its own light and multiplies `trans` by what it
// lets through, so the stars behind it are dimmed and reddened.
vec3 sp_neb_cloud(int i, vec3 d, float dither, inout vec3 trans){
  vec3 dir = u_neb_d[i].xyz;
  float r = clamp(u_neb_d[i].w, 1.0e-4, 1.30);
  float rho = sin(r); // a sphere at unit distance subtends asin(rho)
  float bq = dot(d, dir);
  int type = int(u_neb_a[i].x + 0.5);
  float seed = u_neb_a[i].y, bright = u_neb_a[i].z, dens = u_neb_a[i].w;
  float tilt = u_neb_b[i].x, rot = u_neb_b[i].y, detail = u_neb_b[i].z;
  float dustk = u_neb_e[i].x, warp = u_neb_e[i].y, glowk = u_neb_e[i].z;
  int nsrc = int(u_neb_e[i].w + 0.5);
  vec3 c_hot = u_neb_c1[i], c_gas = u_neb_c2[i];
  // The halo it throws around itself. A long exposure spreads the brightest
  // gas well past the cloud's own edge, so this is worked out from the
  // angle alone - inside the sphere the march would stop at, and outside
  // it. Worked out before the sphere is tested at all: when it was done
  // after, the glow ended dead on the silhouette and drew a hard arc
  // across the sky.
  vec3 halo_add = vec3(0.0);
  if (glowk > 0.0 && u_sp_glow > 0.0 && type != 1){
    float ang = acos(clamp(bq, -1.0, 1.0));
    float h = exp(-pow(ang / max(r, 1.0e-4), 1.2) * 2.6);
    halo_add = mix(c_gas, c_hot, 0.45) * h * glowk * u_sp_glow * bright * 0.10;
  }
  float disc = bq * bq - (1.0 - rho * rho);
  if (disc <= 0.0) return halo_add;
  float sq = sqrt(disc);
  float t0 = max(bq - sq, 0.0), t1 = bq + sq;
  if (t1 <= t0 + 1.0e-6) return halo_add;
  vec3 upv = abs(dir.y) < 0.98 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tx = normalize(cross(upv, dir));
  vec3 ty = cross(dir, tx);
  float cr = cos(rot), sr = sin(rot), ct = cos(tilt), st = sin(tilt);

  // The hot stars inside it. Their glare is what lights the gas at all, and
  // that it falls off as an inverse square is why the middle of a nebula is
  // ionised twice over and its edges barely once.
  vec3 src[4];
  for (int k = 0; k < 4; ++k)
    src[k] = (sp_hash3(vec3(float(k) * 13.0 + 1.0, seed, 3.0), seed) - 0.5) * 0.95;

  // dust scatters blue best; dust absorbs blue hardest. A photograph shows
  // both; the film palette softens the second so its colours stay saturated.
  float real = clamp(u_sp_realism, 0.0, 1.0);
  vec3 c_refl = mix(vec3(0.62, 0.36, 1.00), vec3(0.30, 0.47, 0.95), real);
  vec3 extc = mix(vec3(0.86, 1.00, 1.22), vec3(0.70, 1.00, 1.55), real);

  int steps = clamp(u_sp_steps, 6, 72);
  float dt = (t1 - t0) / float(steps);
  float dtn = dt / max(rho, 1.0e-4); // a step, in units of the nebula's own radius
  vec3 S = vec3(0.0), T = vec3(1.0);
  float t = t0 + dt * dither;
  for (int k = 0; k < 72; ++k){
    if (k >= steps) break;
    vec3 wp = d * t - dir;
    vec3 p = vec3(dot(wp, tx), dot(wp, ty), dot(wp, dir)) / rho;
    p.xy = vec2(p.x * cr - p.y * sr, p.x * sr + p.y * cr);
    p.yz = vec2(p.y * ct - p.z * st, p.y * st + p.z * ct);
    float gas, dust;
    sp_neb_field(p, warp, dens, detail, dustk, gas, dust);
    if (gas + dust > 1.0e-4){
      float ion = 0.0;
      for (int m = 0; m < 4; ++m){
        if (m >= nsrc) break;
        vec3 dv = p - src[m];
        ion += 1.0 / (0.020 + dot(dv, dv) * 4.0);
      }
      ion /= float(max(nsrc, 1));
      // How hard the gas is ionised decides its colour, so the brightness
      // must not run away with the same number - a core that climbs to
      // white loses the teal that says it is ionised at all.
      float hi = ion / (ion + 1.2);
      vec3 emis = type == 1 ? vec3(0.0) : mix(c_gas, c_hot, hi) * gas * (0.28 + 1.1 * hi);
      vec3 refl = c_refl * dust * (0.15 + 1.2 * hi) * 0.45;
      S += T * (emis + refl) * dtn * bright * 0.8;
      T *= exp(-(gas * 0.5 + dust * 3.0) * extc * 2.4 * dtn);
      if (max(T.r, max(T.g, T.b)) < 0.02) break;
    }
    t += dt;
  }
  trans *= T;
  return S + halo_add;
}

// A galaxy or a planetary nebula: flat, in its own tangent frame
// (shaders_space_gal.cpp draws each kind).
vec3 sp_neb_disc(int i, vec3 d){
  vec3 dir = u_neb_d[i].xyz;
  float r = max(u_neb_d[i].w, 1.0e-4);
  float c = dot(d, dir);
  if (c < cos(min(r * 2.4, 3.1))) return vec3(0.0);
  vec3 upv = abs(dir.y) < 0.98 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tx = normalize(cross(upv, dir)), ty = cross(dir, tx);
  vec2 uv = vec2(dot(d, tx), dot(d, ty)) / (max(c, 0.05) * r);
  float rot = u_neb_b[i].y;
  float cr = cos(rot), sr = sin(rot);
  uv = vec2(uv.x * cr - uv.y * sr, uv.x * sr + uv.y * cr);
  int type = int(u_neb_a[i].x + 0.5);
  float seed = u_neb_a[i].y, bright = u_neb_a[i].z, dens = u_neb_a[i].w;
  float tilt = u_neb_b[i].x, detail = u_neb_b[i].z, arms = u_neb_b[i].w;
  vec3 c1 = u_neb_c1[i], c2 = u_neb_c2[i];
  if (type == 2) return sp_disc_spiral(uv, seed, dens, detail, arms, tilt, c1, c2) * bright;
  if (type == 3) return sp_disc_elliptical(uv, seed, detail, tilt, c1, c2) * bright;
  return sp_disc_planetary(uv, seed, dens, detail, c1, c2) * bright;
}

vec3 sp_nebulas(vec3 d, float pix, inout vec3 trans){
  vec3 add = vec3(0.0);
  if (u_neb_n <= 0) return add;
  // A stable offset per pixel, so the march's steps do not line up into
  // shells across the picture: the same trick the cloud march uses.
  float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
  for (int i = 0; i < 8; ++i){
    if (i >= u_neb_n) break;
    if (int(u_neb_a[i].x + 0.5) >= 2) add += sp_neb_disc(i, d);
    else add += sp_neb_cloud(i, d, dither, trans);
  }
  return add;
}
)GLSL";

} // namespace studio
