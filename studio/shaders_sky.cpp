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
uniform sampler3D u_cl_shape;
uniform sampler3D u_cl_detail;
uniform sampler2D u_blue_noise;  // ray-march dither
uniform int u_cl_octaves;        // multiple-scattering octaves, 1 = single
uniform float u_cl_ms_depth;     // extinction attenuation per bounce
uniform float u_cl_cov, u_cl_den, u_cl_alt, u_cl_thick, u_cl_detail_amt;
uniform float u_cl_time, u_cl_ambient, u_cl_anvil;
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
  if (u_cl_type == 0)            // stratus: low flat sheet
    return remap01(hf, 0.0, 0.08) * (1.0 - remap01(hf, 0.18, 0.36));
  if (u_cl_type == 2){           // cumulonimbus: tall with anvil top
    float base = remap01(hf, 0.0, 0.08);
    float top = 1.0 - remap01(hf, 0.75 + u_cl_anvil*0.2, 1.0);
    return base * top;
  }
  return remap01(hf, 0.0, 0.16) * (1.0 - remap01(hf, 0.45, 0.85)); // cumulus
}
float cloud_density(vec3 p, out float hf){
  hf = clamp((p.y - u_cl_alt) / max(u_cl_thick, 1e-3), 0.0, 1.0);
  vec3 wp = p; wp.xz += u_cl_wind * u_cl_time;
  vec4 sn = texture(u_cl_shape, wp * 0.18);
  float fbm = sn.g*0.625 + sn.b*0.25 + sn.a*0.125;
  // base shape: Perlin-Worley eroded by the Worley FBM (Schneider/Guerrilla)
  float shape = clamp(remapf(sn.r, fbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
  shape *= cloud_gradient(hf);
  float d = clamp(remapf(shape, 1.0 - u_cl_cov, 1.0, 0.0, 1.0), 0.0, 1.0);
  if (d <= 0.001) return 0.0;
  vec3 dp = p * 2.6; dp.xz += u_cl_wind * u_cl_time * 2.0;
  vec3 dn = texture(u_cl_detail, dp).rgb;
  float dfbm = dn.r*0.625 + dn.g*0.25 + dn.b*0.125;
  float er = mix(dfbm, 1.0 - dfbm, clamp(hf*4.0, 0.0, 1.0));
  d = clamp(remapf(d, er * u_cl_detail_amt * 0.55, 1.0, 0.0, 1.0), 0.0, 1.0);
  return d * u_cl_den;
}
vec4 march_clouds(vec3 ro, vec3 rd, vec3 bg){
  if (u_clouds == 0 || rd.y < 0.015) return vec4(bg, 1.0);
  float y0 = u_cl_alt, y1 = u_cl_alt + u_cl_thick;
  float t0 = (y0 - ro.y) / rd.y;
  float t1 = (y1 - ro.y) / rd.y;
  if (ro.y > y0 && ro.y < y1) t0 = 0.0;
  t0 = max(t0, 0.0); t1 = max(t1, 0.0);
  if (t1 <= t0) return vec4(bg, 1.0);
  t1 = min(t1, t0 + 30.0);
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
  float cosA = dot(rd, u_sun);
  // dual-lobe HG, renormalised by 4*pi so the phase reads ~0.2..2 instead of
  // the tiny per-steradian value (otherwise clouds vanish against the sky)
  float phase = mix(hg(cosA, 0.75), hg(cosA, -0.25), 0.4) * 12.566;
  vec3 amb_col = sky_color(vec3(0,1,0), u_sky_zenith, u_sky_horizon, u_sun,
                           u_sun_color, u_atmo) * u_cl_ambient;
  float transmittance = 1.0;
  vec3 scatter = vec3(0.0);
  for (int i = 0; i < steps; ++i){
    float t = t0 + dt * (float(i) + jitter);
    vec3 p = ro + rd * t;
    float hf;
    float d = cloud_density(p, hf);
    if (d > 0.002){
      // light march toward the sun
      float ldt = u_cl_thick / 5.0;
      float sum = 0.0;
      for (int j = 0; j < 5; ++j){
        float hf2;
        vec3 lp = p + u_sun * (ldt * (float(j) + 0.5));
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
void main(){
  vec3 dir;
  if (u_panorama == 1) {
    float az = v_ndc.x * PI;
    float el = v_ndc.y * PI * 0.5;
    dir = vec3(cos(el)*cos(az), sin(el), cos(el)*sin(az));
  } else {
    vec4 w = u_inv_vp * vec4(v_ndc, 1.0, 1.0);
    dir = normalize(w.xyz / w.w);
  }
  vec3 col = sky_color(dir, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  // an HDRI carries its own sun; drawing ours over it would light the scene
  // twice
  bool bd_sun_hidden = u_bd_on == 1 && u_bd_hide_sun == 1 && g_bd_weight > 0.0;
  if (u_no_sun == 0 && !bd_sun_hidden) {
    float s = max(dot(dir, u_sun), 0.0);
    col += u_sun_color * pow(s, 700.0) * 8.0;   // sun disc
  }
  // night: the same stable star field fades in as the sun sinks below the
  // horizon - added before the cloud march so clouds occlude the stars
  float night = smoothstep(0.03, -0.12, u_sun.y);
  if (night > 0.0 && dir.y > 0.0) {
    vec3 sd2 = dir * 380.0;
    vec3 si2 = floor(sd2);
    float hs2 = fract(sin(dot(si2, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    if (hs2 > 0.9965) {
      float tw2 = 0.55 + 0.45 * fract(hs2 * 91.7);
      col += vec3(tw2) * smoothstep(0.9965, 1.0, hs2) * night *
             clamp(dir.y * 4.0, 0.0, 1.0);
    }
  }
  vec4 cl = march_clouds(u_cam, dir, col);
  col = cl.rgb;
  float fogw = 0.0;
  vec3 fogcol = vec3(0.0);
  if (u_fog_type != 0) {
    float horizon_fog = pow(1.0 - clamp(dir.y, 0.0, 1.0), 8.0);
    // haze is lit air: it darkens with the same daylight factor as the sky,
    // or night would end at the horizon line
    float fog_day = clamp(u_sun.y * 4.0 + 0.35, 0.035, 1.0);
    fogcol = u_fog_color * fog_day;
    fogw = clamp(horizon_fog * u_fog_density * 0.6, 0.0, 1.0);
    // the dome is at infinity too, but a photograph may already hold its own
    // haze, so how much of ours it receives is a dial
    fogw *= mix(1.0, u_bd_haze, g_bd_weight);
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
  if (u_space > 0.0) {
    vec3 stars = vec3(0.0);
    // stable star field: hashed on the quantized view direction
    vec3 sd = dir * 380.0;
    vec3 si = floor(sd);
    float hs = fract(sin(dot(si, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    if (hs > 0.9965) {
      float tw = 0.55 + 0.45 * fract(hs * 91.7);
      stars = vec3(tw) * smoothstep(0.9965, 1.0, hs);
    }
    vec3 space = stars;
    if (u_no_sun == 0) {
      float s2 = max(dot(dir, u_sun), 0.0);
      space += u_sun_color * pow(s2, 900.0) * 9.0; // the sun stays, airless
    }
    col = mix(col, space, u_space);
  }
  if (u_hdr == 1) { frag = vec4(col, 1.0); return; } // linear for env maps
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

} // namespace studio
