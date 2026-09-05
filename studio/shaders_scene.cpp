// Geekatplay TerraForge — water, mesh, gizmo and helper shaders (sky lives in shaders_sky.cpp)
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

// The water lies on the same sphere as the tile (PL_SPHERE_PLACEHOLDER is
// spliced by inject_sky): on an Earth-size planet it curves with the
// terrain, and on a globe smaller than the tile it is a shell at sea level
// rather than a flat plane cutting through the marble.
const char *const VS_WATER = R"GLSL(#version 430 core
layout(location=0) in vec2 in_uv;
uniform mat4 u_mvp;
uniform float u_level;
uniform float u_planet_radius;
PL_SPHERE_PLACEHOLDER
out vec2 v_uv;
out vec3 v_world;
void main(){
  vec3 p = pl_sphere_place(in_uv, u_level, u_planet_radius);
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
FOG_FN_PLACEHOLDER
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
  // the same air the terrain disappears into: distant water was never fogged
  float dist = length(v_world - u_cam);
  float fog_f; vec3 fog_c;
  fog_terms(v_world, u_cam, dist, u_hscale, u_sun, u_sun_color, fog_f, fog_c);
  if (u_aov != 0) {
    vec3 refl_c = skyr * fresnel * (0.5 + 0.5*u_reflection);
    vec3 spec_c = u_sun_color * spec * 2.0;
    frag = aov_out(u_aov, dist, n, water, v_world, float(u_object_id), spec_c, 1.0,
                   refl_c, spec_c + refl_c, fog_f, fog_c, 1.0, col);
    if (u_aov == 13) frag.a = alpha;
    return;
  }
  col = apply_fog_terms(col, fog_f, fog_c);
  col = aces(col*u_exposure); col = pow(col, vec3(1.0/2.2));
  frag = vec4(col, alpha);
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
uniform float u_inst_sway;            // wind lean at the mesh's top
uniform float u_inst_time;
uniform vec3 u_inst_base;             // the model matrix's own translation
uniform vec4 u_inst[256];             // x,y,z,scale per copy
uniform vec4 u_inst_rot[256];         // cos(yaw), sin(yaw)
out vec3 v_nrm;
out float v_tint;
out vec3 v_world;
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
    if (u_inst_sway > 0.0) {
      // each copy leans on its own phase; the lean grows with height above
      // the copy's foot so trunks stay planted and crowns ride the wind
      float ph = u_inst_time * 1.7 + I.x * 37.0 + I.z * 53.0;
      float lean = sin(ph) * u_inst_sway * max(p.y - I.y, 0.0);
      p.x += lean;
      p.z += lean * 0.35;
    }
    v_nrm = normalize(u_nrm * nrm);
    v_world = p.xyz;
    gl_Position = u_mvp * p;
    return;
  }
  vec4 p = u_model * vec4(pos, 1.0);
  v_nrm = normalize(u_nrm * nrm);
  v_world = p.xyz;
  gl_Position = u_mvp * p;
})GLSL";

const char *const FS_MESH = R"GLSL(#version 430 core
in vec3 v_nrm;
in float v_tint;
in vec3 v_world;
out vec4 frag;
uniform vec3 u_color, u_sun, u_sun_color;
uniform float u_exposure;
uniform int u_light_count;
uniform vec4 u_lights[8];
uniform vec3 u_light_col[8];
uniform vec4 u_light_dir[8];
uniform int u_selected;
uniform vec3 u_cam;
uniform float u_hscale;
FOG_FN_PLACEHOLDER
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
  vec3 lit = u_sun_color * 1.8 * ndl + vec3(0.35,0.38,0.45);
  for (int li = 0; li < u_light_count; ++li) {
    vec3 ld = u_lights[li].xyz - v_world;
    float dist = length(ld);
    float att = clamp(1.0 - dist / max(u_lights[li].w, 1e-3), 0.0, 1.0);
    att *= att;
    vec3 l = ld / max(dist, 1e-5);
    float cone = 1.0;
    if (u_light_dir[li].w > -1.0) {
      float cd2 = dot(-l, u_light_dir[li].xyz);
      cone = smoothstep(u_light_dir[li].w,
                        mix(u_light_dir[li].w, 1.0, 0.35), cd2);
    }
    lit += u_light_col[li] * max(dot(normalize(v_nrm), l), 0.0) * att * cone;
  }
  vec3 col = u_color * v_tint * lit;
  // objects sit in the same air as the ground
  float dist = length(v_world - u_cam);
  float fog_f; vec3 fog_c;
  fog_terms(v_world, u_cam, dist, u_hscale, u_sun, u_sun_color, fog_f, fog_c);
  if (u_aov != 0) {
    vec3 alb = u_color * v_tint;
    frag = aov_out(u_aov, dist, normalize(v_nrm), alb, v_world, float(u_object_id),
                   alb * u_sun_color * 1.8 * ndl, 1.0, alb * vec3(0.35,0.38,0.45),
                   vec3(0.0), fog_f, fog_c, 0.0, col);
    return;
  }
  col = apply_fog_terms(col, fog_f, fog_c);
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
MATERIAL_UNIFORMS_PLACEHOLDER
uniform vec3 u_sun, u_sun_color, u_sky_zenith, u_sky_horizon;
uniform float u_exposure, u_sun_intensity, u_ambient;
const float PI = 3.14159265;
MATERIAL_FN_PLACEHOLDER
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
    nm.xy *= u_normal_strength;
    N = normalize(T*nm.x + B*nm.y + N*max(nm.z,0.05));
  }
  albedo = mat_albedo(albedo);
  float rough = clamp(u_roughness * ((u_has_rough == 1) ?
                      texture(u_rough_map, v_uv).r*2.0 : 1.0), 0.03, 1.0);
  vec3 V = vec3(0,0,1);
  vec3 L = normalize(u_sun);
  vec3 H = normalize(L+V);
  float NdL = mat_ndl(dot(N,L)), NdV = max(dot(N,V),1e-4);
  float NdH = max(dot(N,H),0.0), VdH = max(dot(V,H),0.0);
  vec3 F0 = mat_f0(albedo);
  float a = rough*rough, a2 = a*a;
  float dnm = (NdH*NdH*(a2-1.0)+1.0);
  float D = a2 / max(PI*dnm*dnm, 1e-6);
  float k = (rough+1.0); k = k*k/8.0;
  float G = (NdL/(NdL*(1.0-k)+k)) * (NdV/(NdV*(1.0-k)+k));
  vec3 F = F0 + (1.0-F0)*pow(1.0-VdH,5.0);
  vec3 spec = D*G*F/max(4.0*NdL*NdV,1e-4);
  if (u_m_phong == 1) spec = F * mat_phong(NdH, rough);
  vec3 kd = (1.0-F)*(1.0-u_metallic);
  vec3 sky = mix(u_sky_horizon, u_sky_zenith, 0.5) * u_ambient;
  vec3 sun_c = u_sun_color * u_sun_intensity;
  vec3 col = (kd*albedo/PI + spec) * sun_c * NdL * (u_m_diffuse / 0.6)
           + albedo * sky * (0.45 + 0.55*N.y) * (u_m_ambient / 0.4);
  vec3 R = reflect(-V, N);
  vec3 refl = mix(u_sky_horizon, u_sky_zenith, clamp(R.y*0.5+0.5,0.0,1.0));
  vec3 reflection = refl * u_reflection * (1.0-rough) * mat_fresnel(NdV, F0.g);
  if (u_m_color_reflected == 1) reflection *= albedo;
  col += reflection + mat_translucent(albedo, V, L, sun_c) + u_m_luminous;
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
