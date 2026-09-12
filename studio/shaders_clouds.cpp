// Geekatplay TerraForge — the cloud layers, shared by the sky, the pass that
// draws them over the ground, and the shadows they cast.
//
// The clouds used to exist only inside the sky shader, marched behind
// everything and then painted over by whatever came after: they could never
// stand in front of a mountain, never be seen from above, and never be seen
// from space - so from high up they were switched off and the far shell drew
// a flat stand-in of its own. The march now lives here, once, and runs in the
// sky for the rays that reach the sky and after the ground (FS_CLOUD_OVER) for
// the rays that end on it, cut where they end. One cloud layer is then the
// same clouds from the valley floor, from above the tops and from orbit, and
// the sea reflects that same sky (renderer_clouds.cpp).
#include "renderer_shaders.hpp"

namespace studio {

// The world the air lies on (world_shape.hpp) and the ray tests every layer on
// it needs. Needs PL_SPHERE_FN before it.
const char *const SKY_WORLD_GLSL = R"GLSL(
uniform float u_world_r;
uniform float u_world_w; // a ring's width (a flat world's size): its air ends there
uniform int u_world_outline;
uniform int u_sun_mode;
// The part of a ray that is over the world at all, as an interval. A ring
// reaches only so far along its axis and a flat world only so far across,
// and past either edge there is no ground to hold air or cloud - so the
// same clip belongs to the air, to every cloud layer, and to anything else
// that is a layer on the surface. Air that ignored it filled the whole sky
// inside a ring instead of running along the ring as a band.
bool world_slab(vec3 ro, vec3 rd, inout float t0, inout float t1){
  if (u_world_shape.y > 0.5 && u_world_w > 0.0 && !pl_world_flat()){
    float hw = u_world_w * 0.5;
    if (abs(rd.z) < 1e-6){ if (abs(ro.z - 0.5) > hw) return false; }
    else {
      float za = (0.5 - hw - ro.z) / rd.z, zb = (0.5 + hw - ro.z) / rd.z;
      t0 = max(t0, min(za, zb)); t1 = min(t1, max(za, zb));
    }
  }
  if (pl_world_flat() && u_world_w > 0.0){
    vec2 o = ro.xz - 0.5, dd = rd.xz;
    float hw = u_world_w * 0.5;
    if (u_world_outline == 1){
      for (int k = 0; k < 2; ++k){
        if (abs(dd[k]) < 1e-6){ if (abs(o[k]) > hw) return false; continue; }
        float ta = (-hw - o[k]) / dd[k], tb = (hw - o[k]) / dd[k];
        t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
      }
    } else {
      float a = dot(dd, dd);
      if (a < 1e-9){ if (dot(o, o) > hw * hw) return false; }
      else {
        float b = dot(o, dd), c = dot(o, o) - hw * hw;
        float disc = b * b - a * c;
        if (disc < 0.0) return false;
        float sq = sqrt(disc);
        t0 = max(t0, (-b - sq) / a); t1 = min(t1, (-b + sq) / a);
      }
    }
  }
  return t1 > t0;
}
float world_alt(vec3 p){ return pl_world_alt(p, u_world_r); }
vec3 world_up(vec3 p){ return pl_world_up(p, u_world_r); }
bool world_curved(){ return !(u_world_r <= 0.0 || u_world_r > 1.0e5 || pl_world_flat()); }
// where the sun shines from at a point: a body inside the world shines
// from the centre or the axis, anything else from u_sun
vec3 sun_at(vec3 p){ return u_sun_mode == 1 ? pl_world_up_at(p, u_world_r) : u_sun; }
// The ray against one shell of the world - a sphere about its centre, or a
// cylinder about a ring's axis - radius r: the interval of t the ray spends
// inside it, false when it misses.
bool shell_hits(vec3 ro, vec3 rd, float r, out float t0, out float t1){
  vec3 o = ro - pl_world_centre_at(ro, u_world_r);
  vec3 d = rd;
  if (u_world_shape.y > 0.5){ o.z = 0.0; d.z = 0.0; } // a ring: the axis is z
  float a = dot(d, d);
  if (a < 1e-12) return false;
  float b = dot(o, d), c = dot(o, o) - r * r;
  float disc = b * b - a * c;
  if (disc < 0.0) return false;
  float sq = sqrt(disc);
  t0 = (-b - sq) / a; t1 = (-b + sq) / a;
  return true;
}
)GLSL";

// The layer's shape at a point, before its height profile: what the march
// reads and what the shadow on the ground reads, so a shadow is the cloud
// that casts it.
const char *const CLOUD_SHAPE_GLSL = R"GLSL(
uniform sampler3D u_cl_shape;
// The picture that says where the cloud is, laid flat over the world and
// centred on the origin (cloud_shape_map.cpp). White is cloud, black is clear
// sky, mid grey leaves the coverage to decide - the same form the procedural
// weather below takes, which is why u_cl_map_amt can fade between them.
uniform sampler2D u_cl_map;
uniform int u_cl_map_on;
uniform float u_cl_map_amt, u_cl_map_size, u_cl_map_texels;
uniform vec2 u_cl_map_center;
uniform float u_cl_time, u_cl_weather, u_cl_weather_scale, u_cl_scale;
uniform vec2 u_cl_wind;
uniform vec2 u_cl_drift;
uniform float u_cl_drift_len;
// remap that allows a negative low bound (the standard cloud shaping form)
float cl_remap(float v, float lo, float hi, float nlo, float nhi){
  return nlo + (v - lo) / max(hi - lo, 1e-4) * (nhi - nlo);
}
// The level of a volume repeating `freq` times a world unit, `texels` across,
// for a sample that stands for `foot` world units. From orbit a pixel covers
// dozens of the volume's texels and a lookup at its finest level is noise
// that crawls as the camera moves.
float cl_lod(float foot, float freq, float texels){
  return max(log2(max(foot * freq * texels, 1e-6)), 0.0);
}
// The weather: how much cloud there is here at all, and where the shape
// volume is read from.
//
// That volume tiles, and a sky is seen thirty tiles deep, so read straight
// it draws the same few kilometres over and over - a plain grid across the
// lower sky, which is what an overcast looked like from the ground. One
// lookup far coarser than any cloud fixes both halves of that: its red
// channel opens and closes the cover the way a front does, and its other
// three push the shape lookup about, so which part of the volume you are
// standing under keeps changing. Warping is what actually breaks the
// repeat - modulating the cover alone only makes a modulated grid.
//
// The clouds travel with the wind and, slower, through the volume itself, so
// they build and thin as they go instead of sliding past as rigid shapes.
float cloud_shape_at(vec3 p, float cover, float foot, out float cov){
  vec3 wp = p;
  wp.xz += u_cl_drift;
  wp.y += u_cl_drift_len * 0.15;
  cov = cover;
  vec3 warp = vec3(0.0);
  float fs = max(u_cl_scale, 1e-4);
  float lod_s = cl_lod(foot, fs, 96.0);
  if (u_cl_weather > 0.0){
    float fw = max(u_cl_weather_scale, 1e-4);
    vec4 w = textureLod(u_cl_shape, wp * fw + vec3(0.37), cl_lod(foot, fw, 96.0));
    float k = clamp(u_cl_weather, 0.0, 1.0);
    cov = clamp(cover * mix(1.0, w.r * 2.0, k), 0.0, 1.0);
    // The push fades once a pixel covers several of the shape's texels: it
    // moves the lookup by most of a period, and where neighbouring pixels
    // are pushed that differently each reads an unrelated part of the
    // volume - grain. By then the repeat it hides is averaged away anyway.
    warp = (w.gba - 0.5) * k * 1.8 * (1.0 - smoothstep(0.5, 2.5, lod_s));
  }
  // The picture, where there is one. It is read at the sample's own place in
  // the world and NOT at the drifted one: a painted shape stays where it was
  // put while the cloud's own texture keeps moving through it, which is what
  // a wave cloud standing over a ridge in a gale actually does. Beyond its
  // edge the wrap mode returns 0 - clear sky - so one cloud can be one cloud.
  if (u_cl_map_on == 1){
    float ms = max(u_cl_map_size, 1e-4);
    vec2 uv = (p.xz - u_cl_map_center) / ms + 0.5;
    // by level, like every other lookup in the march: a sample far off covers
    // hundreds of the picture's pixels and its finest level there is grain
    // that crawls as the camera moves
    float lod_m = max(log2(max(foot / ms * u_cl_map_texels, 1e-6)), 0.0);
    float m = textureLod(u_cl_map, uv, lod_m).r;
    cov = mix(cov, clamp(cover * m * 2.0, 0.0, 1.0), clamp(u_cl_map_amt, 0.0, 1.0));
  }
  // And past a few texels a pixel the shape itself goes to its mean: its
  // coarse levels still hold the lowest octave, and the cover's threshold
  // turns that into a hard pattern repeating every few pixels - the volume's
  // tile, printed over the planet. From that far the cloud's shape is the
  // weather's.
  vec4 sn = textureLod(u_cl_shape, wp * fs + warp, lod_s + 5.0 * smoothstep(1.0, 3.0, lod_s));
  float fbm = sn.g * 0.625 + sn.b * 0.25 + sn.a * 0.125;
  // Perlin-Worley eroded by the Worley FBM (Schneider/Guerrilla)
  return clamp(cl_remap(sn.r, fbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
}
// Weather systems: a front or a clear spell hundreds of kilometres across,
// seven times coarser than the weather above, as a factor on the cover. It
// changes too slowly to vary along one ray, so the march reads it once where
// the ray meets the layer. Without it the layer seen from orbit lay over the
// whole world as one even fleece.
float cloud_systems(vec3 p, float foot){
  // Where a picture decides the cover, it decides it: a front hundreds of
  // kilometres across has no business thinning a cloud somebody drew, and
  // without this the painted shape came out at a strength that depended on
  // where in the procedural weather it happened to be standing.
  float sys_k = 1.0 - (u_cl_map_on == 1 ? clamp(u_cl_map_amt, 0.0, 1.0) : 0.0);
  if (u_cl_weather <= 0.0 || sys_k <= 0.0) return 1.0;
  vec3 wp = p;
  wp.xz += u_cl_drift;
  float f = max(u_cl_weather_scale, 1e-4) * 0.14;
  float s = textureLod(u_cl_shape, wp * f + vec3(0.71, 0.13, 0.29), cl_lod(foot, f, 96.0)).r;
  return mix(1.0, 0.45 + 1.1 * smoothstep(0.35, 0.65, s),
             clamp(u_cl_weather, 0.0, 1.0) * sys_k);
}
)GLSL";

// Every cloud layer, marched. Needs SKY_WORLD_GLSL, SKY_FN and
// CLOUD_SHAPE_GLSL before it, and u_sun, u_sun_color, u_sun_intensity,
// u_sky_zenith, u_sky_horizon, u_atmo and PI declared.
const char *const CLOUD_FN_GLSL = R"GLSL(
uniform int u_clouds, u_cl_steps, u_cl_type;
uniform int u_cl_volumetric; // 0 one flat sheet, 1 marched as a volume
uniform sampler3D u_cl_detail;
uniform sampler2D u_blue_noise;  // ray-march dither
uniform int u_cl_octaves;        // multiple-scattering octaves, 1 = single
uniform float u_cl_ms_depth;     // extinction attenuation per bounce
uniform float u_cl_cov, u_cl_den, u_cl_alt, u_cl_thick, u_cl_detail_amt;
uniform float u_cl_ambient, u_cl_anvil;
// a second layer: its own kind, height, coverage and density; same march
uniform int u_cl2, u_cl2_type;
uniform float u_cl2_cov, u_cl2_den, u_cl2_alt, u_cl2_thick;
uniform vec3 u_cl_color;
// the radians one pixel spans, for the march's level of detail (0: none)
uniform float u_cl_pixel_k;
// the layer being marched (set per march from the uniforms above)
int g_type; float g_alt, g_thick, g_cov, g_den;
float g_foot = 0.0;         // world units the current sample stands for
// the nearest distance any layer put light into this ray (for the air in
// front of it), and 1e30 while none has
float g_cl_front = 1.0e30;
float remap01(float v, float lo, float hi){ return clamp((v-lo)/max(hi-lo,1e-4), 0.0, 1.0); }
float hg(float c, float g){
  float g2 = g*g;
  return (1.0-g2) / (4.0*PI*pow(max(1.0+g2-2.0*g*c, 1e-4), 1.5));
}
float cloud_gradient(float hf){
  if (g_type == 0)               // stratus: low flat sheet
    return remap01(hf, 0.0, 0.08) * (1.0 - remap01(hf, 0.18, 0.36));
  if (g_type == 2){              // cumulonimbus: tall with anvil top
    float base = remap01(hf, 0.0, 0.08);
    float top = 1.0 - remap01(hf, 0.75 + u_cl_anvil*0.2, 1.0);
    return base * top;
  }
  return remap01(hf, 0.0, 0.16) * (1.0 - remap01(hf, 0.45, 0.85)); // cumulus
}
// The density is sampled six times a step (once forward, five toward the
// sun), which is why the weather and the shape share one fetch.
float cloud_density(vec3 p, out float hf){
  hf = clamp((world_alt(p) - g_alt) / max(g_thick, 1e-3), 0.0, 1.0);
  float cov;
  float shape = cloud_shape_at(p, g_cov, g_foot, cov) * cloud_gradient(hf);
  float d = clamp(cl_remap(shape, 1.0 - cov, 1.0, 0.0, 1.0), 0.0, 1.0);
  if (d <= 0.001) return 0.0;
  vec3 dp = p * 2.6; dp.xz += u_cl_drift * 2.0;
  vec3 dn = textureLod(u_cl_detail, dp, cl_lod(g_foot, 2.6, 32.0)).rgb;
  float dfbm = dn.r*0.625 + dn.g*0.25 + dn.b*0.125;
  float er = mix(dfbm, 1.0 - dfbm, clamp(hf*4.0, 0.0, 1.0));
  d = clamp(cl_remap(d, er * u_cl_detail_amt * 0.55, 1.0, 0.0, 1.0), 0.0, 1.0);
  return d * g_den;
}
// Where the ray crosses a cloud layer - the band of altitude [alt, alt +
// thick] over the world's surface - as up to two pieces, the nearer in `a`.
//
// A flat world's band is two planes: from under it only rays going up meet
// it, from over it only rays going down, from inside it every ray. A curved
// world's is the space between two shells, and a ray can cross it, pass
// under its base and cross it again. On an inside world that second piece is
// the cloud over the far side of the world, which is most of what its sky
// is; on a globe it is the far limb, unless the ground is in the way.
int layer_spans(vec3 ro, vec3 rd, float alt, float thick, out vec2 a, out vec2 b){
  a = vec2(0.0); b = vec2(0.0);
  if (!world_curved()){
    float y0 = alt, y1 = alt + thick;
    // within a degree of level the band is a sliver dozens of layers away,
    // which the air in front would hide: left out, as it always was
    if (ro.y < y0 && rd.y < 0.015) return 0;
    if (ro.y > y1 && rd.y > -0.015) return 0;
    float t0 = 0.0, t1 = thick * 40.0;
    if (abs(rd.y) > 1e-6){
      float ta = (y0 - ro.y) / rd.y, tb = (y1 - ro.y) / rd.y;
      t0 = max(min(ta, tb), 0.0);
      t1 = min(max(ta, tb), t0 + thick * 40.0);
    }
    if (t1 <= t0 || !world_slab(ro, rd, t0, t1)) return 0;
    a = vec2(t0, t1);
    return 1;
  }
  float R = u_world_r;
  bool ins = u_world_shape.z > 0.5;
  float r_lo = ins ? R - alt - thick : R + alt;   // the shell nearer the centre
  float r_hi = ins ? R - alt : R + alt + thick;   // the shell farther from it
  float b0, b1;
  if (r_hi <= 0.0 || !shell_hits(ro, rd, r_hi, b0, b1)) return 0;
  b0 = max(b0, 0.0);
  if (b1 <= b0) return 0;
  float s0, s1;
  if (!(r_lo > 0.0 && shell_hits(ro, rd, r_lo, s0, s1)) || s1 <= b0){
    if (!world_slab(ro, rd, b0, b1)) return 0;
    a = vec2(b0, b1);
    return 1;
  }
  int n = 0;
  float p0 = b0, p1 = min(s0, b1);
  if (p1 > p0 && world_slab(ro, rd, p0, p1)){ a = vec2(p0, p1); n = 1; }
  float q0 = max(s1, b0), q1 = b1;
  bool blocked = false;
  if (!ins){
    float g0, g1;
    blocked = shell_hits(ro, rd, R, g0, g1) && g0 > 0.0 && g0 < q0;
  }
  if (!blocked && q1 > q0 && world_slab(ro, rd, q0, q1)){
    if (n == 0) a = vec2(q0, q1); else b = vec2(q0, q1);
    ++n;
  }
  return n;
}
// One piece of one layer over what is behind it (`bg`).
vec4 march_span(vec3 ro, vec3 rd, vec3 bg, float t0, float t1, float thick,
                vec3 sun, float cosA, float phase, vec3 amb_col){
  if (t1 <= t0) return vec4(bg, 1.0);
  // A sheet instead of a volume, when that is what is wanted: one sample on
  // the middle of the layer rather than dozens along the ray. It costs a
  // fortieth of the march, and for an overcast seen from below it is most
  // of what the march arrives at anyway.
  if (u_cl_volumetric == 0){
    float hf;
    float tm = mix(t0, t1, 0.5);
    g_foot = tm * u_cl_pixel_k;
    float d = cloud_density(ro + rd * tm, hf);
    if (d <= 0.002) return vec4(bg, 1.0);
    // how much of the layer the ray crosses, so a shallow ray through it is
    // thicker than one straight up - the one thing a sheet must still get
    // right or the horizon goes clear
    float span = min(t1 - t0, thick * 12.0);
    float a = 1.0 - exp(-d * span * 2.4);
    float lit = exp(-d * thick * 3.0);
    vec3 col = (amb_col + u_sun_color * u_sun_intensity * phase * lit * 0.30) * u_cl_color;
    g_cl_front = min(g_cl_front, tm);
    return vec4(mix(bg, col, a), 1.0 - a);
  }
  // cap the march: a band seen edge-on from the ground runs for tens of
  // layers' width, and the steps would spread too thin to see a cloud
  t1 = min(t1, t0 + 30.0);
  // As many steps as the pixel can tell apart along the ray: the band seen
  // from orbit is a sliver of a pixel deep and needs a handful; seen from
  // under it, every step there is.
  int steps = u_cl_steps;
  if (u_cl_pixel_k > 0.0)
    steps = int(clamp((t1 - t0) / max(t0 * u_cl_pixel_k, 1e-9), 8.0, float(u_cl_steps)));
  float dt = (t1 - t0) / float(steps);
  // Offset every ray's start by a fraction of a step, or the whole screen
  // samples the volume at the same distances and a density boundary between
  // two steps draws a hard band across it. Blue noise, not a hash, so the
  // dither reads as grain rather than clumps; not animated per frame, since
  // with no temporal filter that would only make a still image crawl.
  float jitter = texture(u_blue_noise,
                         gl_FragCoord.xy / vec2(textureSize(u_blue_noise, 0))).r;
  float transmittance = 1.0;
  vec3 scatter = vec3(0.0);
  // Empty-space skipping: in clear air the ray takes three steps at once,
  // and drops back to one the moment it meets density, so the edge of a
  // cloud is integrated finely and the sky between clouds costs a third.
  // Not on a short march: with a handful of steps across the whole band a
  // stride of three leaps the thin cloud on one pixel and lands in it on the
  // next, and the layer seen from orbit turned to salt and pepper.
  float skip = steps >= 24 ? 3.0 : 1.0;
  float t = t0 + dt * jitter;
  float stride = skip;
  for (int i = 0; i < steps; ++i){
    if (t >= t1) break;
    g_foot = t * u_cl_pixel_k;
    vec3 p = ro + rd * t;
    float hf;
    float d = cloud_density(p, hf);
    if (d <= 0.002){
      stride = skip;
      t += dt * stride;
      continue;
    }
    if (stride > 1.0){
      // just entered: step back to where the edge is and resample finely
      stride = 1.0;
      t = max(t - dt * 2.0, t0);
      p = ro + rd * t;
      d = cloud_density(p, hf);
    }
    t += dt;
    if (d > 0.002){
      g_cl_front = min(g_cl_front, t);
      // light march toward the sun, over this layer's own depth
      float ldt = thick / 5.0;
      float sum = 0.0;
      for (int j = 0; j < 5; ++j){
        float hf2;
        vec3 lp = p + sun * (ldt * (float(j) + 0.5));
        sum += cloud_density(lp, hf2) * ldt;
      }
      // Multiple scattering, approximated: the shadow ray already taken is
      // evaluated again for each further bounce - light reaching deeper
      // (extinction falls by u_cl_ms_depth, exposed because the textbook
      // 0.5 belongs to another optical-depth scale and washed a dense cloud
      // out), scattering more evenly and carrying less energy. Normalised by
      // the octave weights, so the bounces redistribute the sun's energy
      // rather than invent more: summed raw, a storm went from a shaped grey
      // mass to a flat pale sheet. Normalised, an unshadowed sample lands
      // where single scattering left it and only the deep interior lifts.
      float powder = 1.0 - exp(-d * 4.0);
      float ext_a = 1.0, g_a = 1.0, e_a = 1.0;
      float lum_sum = 0.0, weight = 0.0;
      for (int o = 0; o < 4; ++o){
        if (o >= u_cl_octaves) break;
        float ph = (o == 0) ? phase
                            : mix(hg(cosA, 0.75*g_a), hg(cosA, -0.25*g_a), 0.4)
                              * 12.566;
        lum_sum += e_a * exp(-sum * 2.2 * ext_a) * ph;
        weight  += e_a;
        ext_a *= u_cl_ms_depth;
        g_a   *= 0.5;
        e_a   *= 0.4;
      }
      vec3 sun_c = u_sun_color * u_sun_intensity * 2.2 *
                   (lum_sum / max(weight, 1e-4)) * mix(1.0, powder, 0.55);
      vec3 lum = (sun_c + amb_col * (0.35 + 0.65*hf)) * u_cl_color;
      float ext = d * dt * 3.0;
      scatter += transmittance * lum * d * dt * 3.0;
      transmittance *= exp(-ext);
      if (transmittance < 0.012) break;
    }
  }
  return vec4(bg * transmittance + scatter, transmittance);
}
// One layer over what is behind it, the ray stopping at t_max - the ground,
// a mesh, the sea - or running on into the sky.
vec4 march_layer(vec3 ro, vec3 rd, vec3 bg, int type, float alt, float thick, float cov,
                 float den, float t_max){
  g_type = type; g_alt = alt; g_thick = thick; g_cov = cov; g_den = den;
  vec2 a, b;
  int n = layer_spans(ro, rd, alt, thick, a, b);
  if (n == 0) return vec4(bg, 1.0);
  g_cov = cov * cloud_systems(ro + rd * a.x, a.x * u_cl_pixel_k);
  vec3 sun = sun_at(ro);
  float cosA = dot(rd, sun);
  // dual-lobe HG, renormalised by 4*pi so the phase reads ~0.2..2 instead of
  // the tiny per-steradian value (otherwise clouds vanish against the sky)
  float phase = mix(hg(cosA, 0.75), hg(cosA, -0.25), 0.4) * 12.566;
  vec3 amb_col = sky_color(vec3(0,1,0), u_sky_zenith, u_sky_horizon, u_sun,
                           u_sun_color, u_atmo) * u_cl_ambient;
  vec4 c = vec4(bg, 1.0);
  if (n == 2) c = march_span(ro, rd, bg, b.x, min(b.y, t_max), thick, sun, cosA, phase, amb_col);
  vec4 near = march_span(ro, rd, c.rgb, a.x, min(a.y, t_max), thick, sun, cosA, phase, amb_col);
  return vec4(near.rgb, c.a * near.a);
}
// Every layer, far one first so each nearer one composes over the last.
// The list is the main layer, the second layer, and every extra CloudLayer
// node; "far" is by height difference from the eye, so from below them all
// the highest goes first and from above them all the lowest does.
uniform int u_clx_n;
uniform int u_clx_type[8];
uniform float u_clx_cov[8], u_clx_den[8], u_clx_alt[8], u_clx_thick[8];
vec4 march_clouds(vec3 ro, vec3 rd, vec3 bg, float t_max){
  g_cl_front = 1.0e30;
  if (u_clouds == 0) return vec4(bg, 1.0);
  int   ty[10];
  float al[10], th[10], cv[10], dn[10];
  int n = 0;
  ty[n] = u_cl_type; al[n] = u_cl_alt; th[n] = u_cl_thick; cv[n] = u_cl_cov; dn[n] = u_cl_den; ++n;
  if (u_cl2 == 1) { ty[n] = u_cl2_type; al[n] = u_cl2_alt; th[n] = u_cl2_thick; cv[n] = u_cl2_cov; dn[n] = u_cl2_den; ++n; }
  for (int i = 0; i < 8; ++i) {
    if (i >= u_clx_n) break;
    ty[n] = u_clx_type[i]; al[n] = u_clx_alt[i]; th[n] = u_clx_thick[i]; cv[n] = u_clx_cov[i]; dn[n] = u_clx_den[i]; ++n;
  }
  vec4 c = vec4(bg, 1.0);
  bool done[10];
  for (int i = 0; i < 10; ++i) done[i] = false;
  float ro_alt = world_alt(ro); // the eye's height over the world's surface
  for (int pass = 0; pass < 10; ++pass) {
    if (pass >= n) break;
    // the farthest layer not yet drawn
    int best = -1; float bestd = -1.0;
    for (int i = 0; i < 10; ++i) {
      if (i >= n || done[i]) continue;
      float d = abs(al[i] + th[i] * 0.5 - ro_alt);
      if (d > bestd) { bestd = d; best = i; }
    }
    if (best < 0) break;
    done[best] = true;
    vec4 l = march_layer(ro, rd, c.rgb, ty[best], al[best], th[best], cv[best], dn[best], t_max);
    c = vec4(l.rgb, c.a * l.a);
  }
  return c;
}
)GLSL";

} // namespace studio
