// Geekatplay TerraForge — sky and volumetric-cloud shaders. Split from shaders_scene.cpp for the 500-line module rule.
#include "renderer_shaders.hpp"

namespace studio {

const char *const VS_SKY = R"GLSL(#version 430 core
out vec2 v_ndc;
void main(){
  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0-1.0;
  v_ndc = p;
  gl_Position = vec4(p, 0.99999, 1.0);
})GLSL";

const char *const FS_SKY_SRC = R"GLSL(#version 430 core
in vec2 v_ndc;
out vec4 frag;
uniform mat4 u_inv_vp;
uniform vec3 u_cam, u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_atmo;
uniform int u_fog_type;
uniform vec3 u_fog_color;
uniform float u_fog_density;
// volumetric clouds
uniform int u_clouds, u_cl_steps, u_cl_type;
uniform int u_cl_volumetric; // 0 one flat sheet, 1 marched as a volume
uniform sampler3D u_cl_shape;
uniform sampler3D u_cl_detail;
uniform sampler2D u_blue_noise;  // ray-march dither
uniform int u_cl_octaves;        // multiple-scattering octaves, 1 = single
uniform float u_cl_ms_depth;     // extinction attenuation per bounce
uniform float u_cl_cov, u_cl_den, u_cl_alt, u_cl_thick, u_cl_detail_amt;
uniform float u_cl_time, u_cl_ambient, u_cl_anvil;
// a second layer: its own kind, height, coverage and density; same march
uniform int u_cl2, u_cl2_type;
uniform float u_cl2_cov, u_cl2_den, u_cl2_alt, u_cl2_thick;
// the layer being marched (set per march from the uniforms above)
int g_type; float g_alt, g_thick, g_cov, g_den;
uniform float u_sun_intensity;
uniform vec2 u_cl_wind;
uniform vec3 u_cl_color;
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
uniform float u_world_r;
uniform float u_world_w; // a ring's width (a flat world's size): its air ends there
uniform int u_world_outline;
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
uniform int u_sun_mode;
float world_alt(vec3 p){ return pl_world_alt(p, u_world_r); }
vec3 world_up(vec3 p){ return pl_world_up(p, u_world_r); }
bool world_curved(){ return !(u_world_r <= 0.0 || u_world_r > 1.0e5 || pl_world_flat()); }
// where the sun shines from at a point: a body inside the world shines
// from the centre or the axis, anything else from u_sun
vec3 sun_at(vec3 p){ return u_sun_mode == 1 ? pl_world_up_at(p, u_world_r) : u_sun; }
// The atmosphere is a layer of this height on the world's surface (0: the
// old rule, u_space by the camera's distance). How much of the ray lies in
// it, in vertical thicknesses, decides how much sky and how much space a
// pixel shows: straight up from the ground is one thickness (all sky), the
// limb of the world seen from space a short chord (a thin blue rim), a ray
// that misses the layer nothing but stars.
uniform float u_atm_h;
SPACE_FN_PLACEHOLDER
SKY_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
float remap01(float v, float lo, float hi){ return clamp((v-lo)/max(hi-lo,1e-4), 0.0, 1.0); }
// remap that allows a negative low bound (the standard cloud shaping form)
float remapf(float v, float lo, float hi, float nlo, float nhi){
  return nlo + (v - lo) / max(hi - lo, 1e-4) * (nhi - nlo);
}
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
uniform float u_cl_weather, u_cl_weather_scale, u_cl_scale;
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
// One fetch does both jobs, which is what makes it affordable: the density
// is sampled six times a step here (once forward, five toward the sun).
float cloud_density(vec3 p, out float hf){
  hf = clamp((world_alt(p) - g_alt) / max(g_thick, 1e-3), 0.0, 1.0);
  vec3 wp = p; wp.xz += u_cl_wind * u_cl_time;
  float cov = g_cov;
  vec3 warp = vec3(0.0);
  if (u_cl_weather > 0.0){
    vec4 w = texture(u_cl_shape, wp * max(u_cl_weather_scale, 1e-4) + vec3(0.37));
    cov = clamp(g_cov * mix(1.0, w.r * 2.0, clamp(u_cl_weather, 0.0, 1.0)), 0.0, 1.0);
    warp = (w.gba - 0.5) * clamp(u_cl_weather, 0.0, 1.0) * 1.8;
  }
  vec4 sn = texture(u_cl_shape, wp * max(u_cl_scale, 1e-4) + warp);
  float fbm = sn.g*0.625 + sn.b*0.25 + sn.a*0.125;
  // base shape: Perlin-Worley eroded by the Worley FBM (Schneider/Guerrilla)
  float shape = clamp(remapf(sn.r, fbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
  shape *= cloud_gradient(hf);
  float d = clamp(remapf(shape, 1.0 - cov, 1.0, 0.0, 1.0), 0.0, 1.0);
  if (d <= 0.001) return 0.0;
  vec3 dp = p * 2.6; dp.xz += u_cl_wind * u_cl_time * 2.0;
  vec3 dn = texture(u_cl_detail, dp).rgb;
  float dfbm = dn.r*0.625 + dn.g*0.25 + dn.b*0.125;
  float er = mix(dfbm, 1.0 - dfbm, clamp(hf*4.0, 0.0, 1.0));
  d = clamp(remapf(d, er * u_cl_detail_amt * 0.55, 1.0, 0.0, 1.0), 0.0, 1.0);
  return d * g_den;
}
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
// Where the ray crosses a cloud layer: the band of altitude [alt, alt +
// thick] over the world's surface. A flat world's band is two planes, as
// it always was. A curved world's is the space between two shells: the
// ray is inside the outer one over one interval and inside the inner one
// over another, and the band is the first interval less the second - the
// part before the inner shell, or, from inside it, the part after.
bool layer_span(vec3 ro, vec3 rd, float alt, float thick, out float t0, out float t1){
  if (!world_curved()){
    if (rd.y < 0.015) return false;
    float y0 = alt, y1 = alt + thick;
    t0 = (y0 - ro.y) / rd.y;
    t1 = (y1 - ro.y) / rd.y;
    if (ro.y > y0 && ro.y < y1) t0 = 0.0;
    t0 = max(t0, 0.0); t1 = max(t1, 0.0);
    return world_slab(ro, rd, t0, t1);
  }
  float R = u_world_r;
  bool ins = u_world_shape.z > 0.5;
  float r_lo = ins ? R - alt - thick : R + alt;   // the shell nearer the centre
  float r_hi = ins ? R - alt : R + alt + thick;   // the shell farther from it
  // (clipped to the world's own extent at the end - see world_slab)
  float b0, b1;
  if (r_hi <= 0.0 || !shell_hits(ro, rd, r_hi, b0, b1)) return false;
  b0 = max(b0, 0.0);
  if (b1 <= b0) return false;
  float s0, s1;
  if (!(r_lo > 0.0 && shell_hits(ro, rd, r_lo, s0, s1))){ t0 = b0; t1 = b1; }
  else if (s0 > b0){ t0 = b0; t1 = min(s0, b1); }
  else { t0 = max(s1, b0); t1 = b1; }
  if (t1 <= t0) return false;
  // a ring's air ends at its rim: the band is cut to the ring's width
  if (u_world_shape.y > 0.5 && u_world_w > 0.0){
    float hw = u_world_w * 0.5;
    if (abs(rd.z) < 1e-6){
      if (abs(ro.z - 0.5) > hw) return false;
    } else {
      float za = (0.5 - hw - ro.z) / rd.z, zb = (0.5 + hw - ro.z) / rd.z;
      t0 = max(t0, min(za, zb));
      t1 = min(t1, max(za, zb));
    }
  }
  return world_slab(ro, rd, t0, t1);
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
vec4 march_layer(vec3 ro, vec3 rd, vec3 bg, int type, float alt, float thick, float cov, float den){
  g_type = type; g_alt = alt; g_thick = thick; g_cov = cov; g_den = den;
  float t0, t1;
  if (!layer_span(ro, rd, alt, thick, t0, t1)) return vec4(bg, 1.0);
  t1 = min(t1, t0 + 30.0);
  vec3 sun = sun_at(ro);
  int steps = u_cl_steps;
  float dt = (t1 - t0) / float(steps);
  // Offset every ray's start by a fraction of a step, or the whole screen
  // samples the volume at the same distances and a density boundary between
  // two steps draws a hard band across it.
  //
  // Blue noise rather than a hash: with white noise, neighbouring pixels can
  // land on similar offsets, so the dither clumps into visible blobs. Blue
  // noise has no low-frequency content by construction, so the pattern is
  // even and reads as film grain instead of static. Same cost, and it is a
  // 64x64 texture lookup against a sin() and a multiply.
  //
  // It is deliberately *not* animated per frame. The usual trick is to advance
  // the value by the golden ratio each frame so temporal AA resolves it to a
  // smooth gradient — but we have no TAA yet, so animating it would only make
  // a still image crawl. Left static until that lands.
  float jitter = texture(u_blue_noise,
                         gl_FragCoord.xy / vec2(textureSize(u_blue_noise, 0))).r;
  float cosA = dot(rd, sun);
  // A sheet instead of a volume, when that is what is wanted: one sample on
  // the middle of the layer rather than dozens along the ray. It costs a
  // fortieth of the march, and for an overcast seen from below it is most
  // of what the march arrives at anyway.
  if (u_cl_volumetric == 0){
    float hf;
    float d = cloud_density(ro + rd * mix(t0, t1, 0.5), hf);
    if (d <= 0.002) return vec4(bg, 1.0);
    // how much of the layer the ray crosses, so a shallow ray through it is
    // thicker than one straight up - the one thing a sheet must still get
    // right or the horizon goes clear
    float span = min(t1 - t0, thick * 12.0);
    float a = 1.0 - exp(-d * span * 2.4);
    vec3 amb = sky_color(vec3(0,1,0), u_sky_zenith, u_sky_horizon, u_sun,
                         u_sun_color, u_atmo) * u_cl_ambient;
    float phase_f = mix(hg(cosA, 0.75), hg(cosA, -0.25), 0.4) * 12.566;
    float lit = exp(-d * thick * 3.0);
    vec3 col = amb + u_sun_color * u_sun_intensity * phase_f * lit * 0.30;
    return vec4(mix(bg, col, a), 1.0 - a);
  }
  // dual-lobe HG, renormalised by 4*pi so the phase reads ~0.2..2 instead of
  // the tiny per-steradian value (otherwise clouds vanish against the sky)
  float phase = mix(hg(cosA, 0.75), hg(cosA, -0.25), 0.4) * 12.566;
  vec3 amb_col = sky_color(vec3(0,1,0), u_sky_zenith, u_sky_horizon, u_sun,
                           u_sun_color, u_atmo) * u_cl_ambient;
  float transmittance = 1.0;
  vec3 scatter = vec3(0.0);
  // Empty-space skipping: in clear air the ray takes three steps at once,
  // and drops back to one the moment it meets density, so the edge of a
  // cloud is integrated finely and the sky between clouds costs a third.
  float t = t0 + dt * jitter;
  float stride = 3.0;
  for (int i = 0; i < steps; ++i){
    if (t >= t1) break;
    vec3 p = ro + rd * t;
    float hf;
    float d = cloud_density(p, hf);
    if (d <= 0.002){
      stride = 3.0;
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
      // light march toward the sun
      float ldt = u_cl_thick / 5.0;
      float sum = 0.0;
      for (int j = 0; j < 5; ++j){
        float hf2;
        vec3 lp = p + sun * (ldt * (float(j) + 0.5));
        sum += cloud_density(lp, hf2) * ldt;
      }
      // Multiple scattering, approximated. Single scattering stops light at
      // its first hit, which is why clouds computed that way read dense and
      // plastic: in reality light bounces inside the volume and diffuses.
      //
      // Rather than trace new rays, evaluate the shadow ray we already have
      // several times over, each pass standing for one more bounce: light
      // penetrates further (extinction falls), loses its forward bias (the
      // phase asymmetry falls toward isotropic) and carries less energy.
      // Two or three octaves cost almost nothing on top of the light march
      // that dominates this loop.
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
        // How much deeper each bounce reaches. The textbook value is 0.5 —
        // half the extinction, so twice the penetration — but that is written
        // for an optical-depth scale that is not ours: our `sum * 2.2` was
        // tuned against single scattering, and at 0.5 the interior of a dense
        // cloud comes out fifteen times brighter and the shape washes out.
        // Exposed rather than fixed so it can be set against the density it
        // is paired with.
        ext_a *= u_cl_ms_depth;
        g_a   *= 0.5;   // and scatters more evenly
        e_a   *= 0.4;   // carrying less energy
      }
      // Normalised by the octave weights, so the bounces redistribute the
      // sun's energy rather than invent more of it. Summing them raw (which
      // is how the technique is usually written down) makes the cloud's own
      // shadow far brighter than its lit side is dark: a dense storm went
      // from a shaped grey mass to a flat pale sheet, mean image brightness
      // 128 -> 198. Normalised, an unshadowed sample lands exactly where
      // single scattering left it, and only the deep interior lifts — which
      // is the part multiple scattering is actually about.
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
// Every layer, far one first so each nearer one composes over the last.
// The list is the main layer, the second layer, and every extra CloudLayer
// node; "far" is by height difference from the eye, so from below them all
// the highest goes first and from above them all the lowest does.
uniform int u_clx_n;
uniform int u_clx_type[8];
uniform float u_clx_cov[8], u_clx_den[8], u_clx_alt[8], u_clx_thick[8];
vec4 march_clouds(vec3 ro, vec3 rd, vec3 bg){
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
    vec4 l = march_layer(ro, rd, c.rgb, ty[best], al[best], th[best], cv[best], dn[best]);
    c = vec4(l.rgb, c.a * l.a);
  }
  return c;
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
    vec4 w = u_inv_vp * vec4(v_ndc, 1.0, 1.0);
    dir = normalize(w.xyz / w.w);
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
  float atm_scat = 1.0; // how much air the pixel looks through, 0..1
  if (u_no_sun == 0 && !bd_sun_hidden) {
    float s = max(dot(dir, u_sun), 0.0);
    sun_disc = u_sun_color * pow(s, 700.0) * 8.0;
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
  vec3 space = sp_w > 0.002 ? space_color(dir) : vec3(0.0);
  if (u_atm_h > 0.0) {
    // The air is a layer on the world (atm_path): what it scatters is the
    // sky, what it lets through is space and the sun. Stars are lost in
    // daylight the way they are outside - not hidden by the air but
    // outshone by it - so their share falls with the sky's own brightness.
    float path = atm_path(u_cam, dir);
    float scat = 1.0 - exp(-5.0 * u_atmo * path);
    float T = exp(-0.35 * u_atmo * path);
    atm_scat = scat;
    col = space * sp_w + sun_disc * T + col * scat;
  } else {
    // the old rule: sky everywhere, thinning to space with the camera's
    // distance from the tile (u_space), stars coming out at night
    float night = smoothstep(0.03, -0.12, u_sun.y);
    col += sun_disc;
    if (night > 0.0 && dir_up > 0.0) col += space * night * clamp(dir_up * 4.0, 0.0, 1.0);
  }
  vec4 cl = march_clouds(u_cam, dir, col);
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
