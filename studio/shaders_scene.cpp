// Geekatplay TerraForge — water, sky, mesh, gizmo and helper shaders
#include "renderer_shaders.hpp"

namespace studio {

// The shadow pass has to see the same surface the camera does. It always
// ignored the fractal micro-relief, which is fine — that detail is far below
// the shadow map's resolution. A graph displacement is not: it can move the
// surface by a large fraction of the terrain's whole height, and terrain that
// casts a shadow from where it used to be looks broken rather than subtle.
//
// The detail budget is fixed rather than camera-derived, because there is no
// camera here — the shadow map is rendered from the sun.
const char *const VS_DEPTH_SRC = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
uniform sampler2D u_height;
uniform mat4 u_light_mvp;
uniform float u_hscale;
uniform float u_field_strength;
FRACTAL_FN_PLACEHOLDER
GPX_FIELD_PLACEHOLDER
void main(){
  float h = texture(u_height, in_uv).r * u_hscale;
  vec3 p = vec3(in_uv.x, h, in_uv.y);
  if (u_field_strength != 0.0)
    p.y += gpx_terrain_field(p, vec3(0.0,1.0,0.0), h, 1.0, 0.0, 0.0, 7.0).x *
           u_field_strength;
  gl_Position = u_light_mvp * vec4(p, 1.0);
})GLSL";

const char *const FS_DEPTH = R"GLSL(#version 430 core
void main(){})GLSL";

const char *const VS_WATER = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
uniform mat4 u_mvp;
uniform float u_level;
out vec2 v_uv;
out vec3 v_world;
void main(){
  vec3 p = vec3(in_uv.x, u_level, in_uv.y);
  v_uv = in_uv; v_world = p;
  gl_Position = u_mvp * vec4(p,1.0);
})GLSL";

const char *const FS_WATER = R"GLSL(#version 430 core
in vec2 v_uv;
in vec3 v_world;
out vec4 frag;
uniform sampler2D u_height;
uniform float u_hscale, u_level, u_time, u_exposure;
uniform vec3 u_sun, u_sun_color, u_cam;
uniform vec3 u_deep, u_shallow;
uniform float u_wave_amp, u_wave_scale, u_wave_speed, u_clarity, u_opacity;
uniform vec3 u_sky_zenith, u_sky_horizon;
uniform float u_atmo;
uniform int u_foam_on;
uniform vec3 u_foam_color;
uniform float u_foam_amount, u_foam_scale, u_foam_crests;
uniform float u_roughness, u_reflection;
SKY_FN_PLACEHOLDER
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
float hash21(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f*f*(3.0-2.0*f);
  return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),
             mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),f.x), f.y);
}
void main(){
  float bed = texture(u_height, v_uv).r * u_hscale;
  float depth = u_level - bed;
  if (depth <= 0.0) discard;
  float t = u_time * u_wave_speed;
  float k = u_wave_scale;
  float w1 = sin(v_uv.x*140.0*k + t*1.3)*0.5 + sin(v_uv.y*120.0*k - t*1.7)*0.5;
  float w2 = sin((v_uv.x*90.0 - v_uv.y*70.0)*k + t*0.9);
  float w3 = sin((v_uv.x*47.0 + v_uv.y*61.0)*k - t*0.6);
  vec3 n = normalize(vec3((w1+w3*0.5)*0.02*u_wave_amp, 1.0, (w2+w3*0.5)*0.02*u_wave_amp));
  vec3 vdir = normalize(u_cam - v_world);
  float fresnel = pow(1.0 - max(dot(n, vdir),0.0), 5.0)*0.9 + 0.06;
  vec3 water = mix(u_shallow, u_deep, clamp(depth*u_clarity,0.0,1.0));
  vec3 R = reflect(-vdir, n);
  vec3 skyr = sky_color(R, u_sky_zenith, u_sky_horizon, u_sun, u_sun_color, u_atmo);
  vec3 col = mix(water, skyr, fresnel * (0.5 + 0.5*u_reflection));
  float spec = pow(max(dot(reflect(-u_sun, n), vdir),0.0), mix(900.0, 120.0, u_roughness));
  col += u_sun_color * spec * 2.0;
  float alpha = clamp(0.55 + depth*10.0, 0.0, u_opacity);
  if (u_foam_on == 1) {
    float fn = vnoise(v_uv*60.0*u_foam_scale + vec2(t*0.15, -t*0.1));
    fn = fn*0.6 + 0.4*vnoise(v_uv*140.0*u_foam_scale - vec2(t*0.22, t*0.13));
    float shore_w = 0.012 * u_foam_amount * u_hscale;
    float pulse = 0.6 + 0.4*sin(t*1.8 + v_uv.x*30.0 + v_uv.y*24.0);
    float shore = (1.0 - smoothstep(0.0, shore_w * (0.6+pulse), depth));
    shore *= smoothstep(0.35, 0.75, fn) * u_foam_amount * 1.6;
    float crest = smoothstep(1.05, 1.45, w1 + w2*0.5) * u_foam_crests;
    crest *= smoothstep(0.45, 0.8, fn);
    float foam = clamp(shore + crest, 0.0, 1.0);
    col = mix(col, u_foam_color, foam);
    alpha = max(alpha, foam * 0.95);
  }
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, alpha);
})GLSL";

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
  if (u_no_sun == 0) {
    float s = max(dot(dir, u_sun), 0.0);
    col += u_sun_color * pow(s, 700.0) * 8.0;   // sun disc
  }
  vec4 cl = march_clouds(u_cam, dir, col);
  col = cl.rgb;
  if (u_fog_type != 0) {
    float horizon_fog = pow(1.0 - clamp(dir.y, 0.0, 1.0), 8.0);
    col = mix(col, u_fog_color, clamp(horizon_fog * u_fog_density * 0.6, 0.0, 1.0));
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

// The model matrix is built on the CPU (scene_object_matrix) so that the
// renderer, picking and the selection outline all read one definition of
// where an object is. u_nrm is R*S^-1, so a squeezed object still shades
// correctly.
const char *const VS_MESH = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_nrm;
// scattering: when on, each copy replaces the model's translation with its
// own position and adds a yaw+scale of its own. 256 copies per draw call.
uniform int u_inst_on;
uniform vec3 u_inst_base;             // the model matrix's own translation
uniform vec4 u_inst[256];             // x,y,z,scale per copy
uniform vec4 u_inst_rot[256];         // cos(yaw), sin(yaw)
out vec3 v_nrm;
out float v_tint;
void main(){
  vec3 pos = in_pos;
  vec3 nrm = in_nrm;
  v_tint = 1.0;
  if (u_inst_on == 1) {
    vec4 I = u_inst[gl_InstanceID];
    vec4 R = u_inst_rot[gl_InstanceID];
    vec2 r = R.xy;
    v_tint = R.z;
    pos = vec3(pos.x*r.x - pos.z*r.y, pos.y, pos.x*r.y + pos.z*r.x) * I.w;
    nrm = vec3(nrm.x*r.x - nrm.z*r.y, nrm.y, nrm.x*r.y + nrm.z*r.x);
    vec4 p = u_model * vec4(pos, 1.0);
    p.xyz += I.xyz - u_inst_base;
    v_nrm = normalize(u_nrm * nrm);
    gl_Position = u_mvp * p;
    return;
  }
  vec4 p = u_model * vec4(pos, 1.0);
  v_nrm = normalize(u_nrm * nrm);
  gl_Position = u_mvp * p;
})GLSL";

const char *const FS_MESH = R"GLSL(#version 430 core
in vec3 v_nrm;
in float v_tint;
out vec4 frag;
uniform vec3 u_color, u_sun, u_sun_color;
uniform float u_exposure;
uniform int u_selected;
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  float ndl = max(dot(normalize(v_nrm), u_sun), 0.0);
  vec3 col = u_color * v_tint * (u_sun_color * 1.8 * ndl + vec3(0.35,0.38,0.45));
  if (u_selected == 1) col = mix(col, vec3(1.0,0.55,0.18), 0.25);
  col = aces(col * u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

const char *const VS_GIZMO = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
uniform mat4 u_mvp;
uniform vec4 u_xform; // center xyz, radius
out vec3 v_nrm;
void main(){
  vec3 p = in_pos * u_xform.w + u_xform.xyz;
  v_nrm = in_nrm;
  gl_Position = u_mvp * vec4(p, 1.0);
})GLSL";

const char *const FS_GIZMO = R"GLSL(#version 430 core
in vec3 v_nrm;
out vec4 frag;
uniform vec3 u_color;
uniform int u_selected;
void main(){
  float rim = pow(1.0 - abs(normalize(v_nrm).z), 1.5);
  vec3 col = u_color + rim * 0.5;
  if (u_selected == 1) col = mix(col, vec3(1.0, 0.62, 0.2), 0.6);
  frag = vec4(col, 1.0);
})GLSL";

// material preview: a lit shape (sphere/cube/flat) textured with the channels
const char *const VS_MATPREV = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_nrm;
layout(location=2) in vec2 in_uv;
uniform mat3 u_rot;
out vec3 v_nrm;
out vec2 v_uv;
void main(){
  vec3 p = u_rot * in_pos;
  v_nrm = normalize(u_rot * in_nrm);
  v_uv = in_uv;
  gl_Position = vec4(p.xy * 0.82, p.z * 0.35 + 0.5, 1.0);
})GLSL";

const char *const FS_MATPREV = R"GLSL(#version 430 core
in vec3 v_nrm;
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_albedo;
uniform sampler2D u_normal_map;
uniform sampler2D u_rough_map;
uniform int u_has_albedo, u_has_normal, u_has_rough;
uniform float u_roughness, u_metallic, u_specular, u_reflection;
uniform vec3 u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_sun_intensity, u_ambient;
const float PI = 3.14159265;
uniform vec3 u_grade;
uniform float u_sat;
vec3 aces(vec3 x){
  x *= u_grade;
  float lum = dot(x, vec3(0.299, 0.587, 0.114));
  x = mix(vec3(lum), x, u_sat);
  return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);
}
void main(){
  vec3 N = normalize(v_nrm);
  vec3 albedo = (u_has_albedo == 1) ? pow(texture(u_albedo, v_uv).rgb, vec3(2.2))
                                    : vec3(0.55,0.53,0.5);
  if (u_has_normal == 1){
    vec3 nm = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;
    vec3 T = normalize(cross(vec3(0,1,0), N) + vec3(1e-4));
    vec3 B = cross(N, T);
    N = normalize(T*nm.x + B*nm.y + N*max(nm.z,0.05));
  }
  float rough = clamp(u_roughness * ((u_has_rough == 1) ?
                      texture(u_rough_map, v_uv).r*2.0 : 1.0), 0.03, 1.0);
  vec3 V = vec3(0,0,1);
  vec3 L = normalize(u_sun);
  vec3 H = normalize(L+V);
  float NdL = max(dot(N,L),0.0), NdV = max(dot(N,V),1e-4);
  float NdH = max(dot(N,H),0.0), VdH = max(dot(V,H),0.0);
  vec3 F0 = mix(vec3(0.08*u_specular), albedo, u_metallic);
  float a = rough*rough, a2 = a*a;
  float dnm = (NdH*NdH*(a2-1.0)+1.0);
  float D = a2 / max(PI*dnm*dnm, 1e-6);
  float k = (rough+1.0); k = k*k/8.0;
  float G = (NdL/(NdL*(1.0-k)+k)) * (NdV/(NdV*(1.0-k)+k));
  vec3 F = F0 + (1.0-F0)*pow(1.0-VdH,5.0);
  vec3 spec = D*G*F/max(4.0*NdL*NdV,1e-4);
  vec3 kd = (1.0-F)*(1.0-u_metallic);
  vec3 sky = mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient;
  vec3 col = (kd*albedo/PI + spec) * u_sun_color * u_sun_intensity * NdL
           + albedo * sky * (0.45 + 0.55*N.y);
  vec3 R = reflect(-V, N);
  vec3 refl = mix(u_sky_horizon, u_sky_zenith, clamp(R.y*0.5+0.5,0.0,1.0));
  col += refl * u_reflection * (1.0-rough) * (F0.g + (1.0-F0.g)*pow(1.0-NdV,5.0));
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, 1.0);
})GLSL";

const char *const VS_LINES = R"GLSL(#version 430 core
layout(location=0) in vec3 in_pos;
uniform mat4 u_mvp;
void main(){ gl_Position = u_mvp * vec4(in_pos, 1.0); })GLSL";

const char *const FS_LINES = R"GLSL(#version 430 core
out vec4 frag;
uniform vec4 u_color;
void main(){ frag = u_color; })GLSL";

const char *const VS_BG = R"GLSL(#version 430 core
out vec2 v_ndc;
void main(){
  vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0-1.0;
  v_ndc = p;
  gl_Position = vec4(p, 0.99999, 1.0);
})GLSL";

const char *const FS_BG = R"GLSL(#version 430 core
in vec2 v_ndc;
out vec4 frag;
uniform vec3 u_top, u_bottom;
void main(){
  float t = v_ndc.y * 0.5 + 0.5;
  frag = vec4(mix(u_bottom, u_top, t), 1.0);
})GLSL";


} // namespace studio
