#include "uniform_cache.hpp"
// Geekatplay TerraForge - material properties on the GPU: the GLSL that
// every lit shader shares, and the one call that uploads a MaterialParams.
//
// The terrain, the material preview and any future lit surface include the
// same two snippets through placeholders (MATERIAL_UNIFORMS_PLACEHOLDER and
// MATERIAL_FN_PLACEHOLDER, substituted by inject_sky), so a property means
// one thing everywhere and a new one is added in one place.
#include "renderer_internal.hpp"
#include "render_settings.hpp"

namespace studio {

extern const char *const MATERIAL_UNIFORMS_GLSL;
const char *const MATERIAL_UNIFORMS_GLSL = R"GLSL(
uniform float u_roughness, u_metallic, u_specular, u_reflection;
uniform float u_translucency, u_transparency, u_normal_strength;
uniform vec3 u_m_tint, u_m_hl_color, u_m_sss_color, u_m_luminous;
uniform float u_m_gain, u_m_sat, u_m_ior, u_m_refl_angle, u_m_refl_min;
uniform float u_m_diffuse, u_m_ambient, u_m_contrast, u_m_backlight;
uniform int u_m_color_reflected, u_m_phong, u_m_ignore_light, u_m_ignore_atmo;
uniform float u_m_alpha, u_m_map_scale, u_m_rotation, u_m_cycling;
uniform vec2 u_m_origin;
uniform int u_m_turb_on, u_m_turb_complexity;
uniform float u_m_turb_amp, u_m_turb_scale, u_m_turb_harm;
uniform vec3 u_m_blend_color, u_m_cc_tint;
uniform float u_m_blend_amount, u_m_blend_mask, u_m_cc, u_m_cc_rough, u_m_cc_ior;
)GLSL";

extern const char *const MATERIAL_FN_GLSL;
const char *const MATERIAL_FN_GLSL = R"GLSL(
// Global transformation: scale, origin, rotation, turbulence and cycling of
// where every map is read (Vue's Effects tab, Global Transformation group)
float mat_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float mat_vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mat_hash(i), mat_hash(i + vec2(1, 0)), f.x),
             mix(mat_hash(i + vec2(0, 1)), mat_hash(i + vec2(1, 1)), f.x), f.y);
}
vec2 mat_uv(vec2 uv) {
  uv = (uv - 0.5) * u_m_map_scale + 0.5 + u_m_origin;
  float r = radians(u_m_rotation);
  if (r != 0.0) {
    float c = cos(r), s = sin(r);
    uv = vec2(c * (uv.x - 0.5) - s * (uv.y - 0.5), s * (uv.x - 0.5) + c * (uv.y - 0.5)) + 0.5;
  }
  if (u_m_cycling > 0.0)
    uv += (vec2(mat_vnoise(uv * 0.7 + 11.0), mat_vnoise(uv * 0.7 + 57.0)) - 0.5) * u_m_cycling * 0.5;
  if (u_m_turb_on == 1) {
    float amp = u_m_turb_amp, sc = u_m_turb_scale;
    for (int i = 0; i < u_m_turb_complexity; ++i) {
      uv += (vec2(mat_vnoise(uv * sc + 3.1), mat_vnoise(uv * sc + 91.7)) - 0.5) * amp;
      amp *= u_m_turb_harm;
      sc /= u_m_turb_harm;
    }
  }
  return uv;
}
// Color tab: overall colour, brightness, saturation, colour blend
vec3 mat_albedo(vec3 a) {
  a *= u_m_tint * u_m_gain;
  float l = dot(a, vec3(0.299, 0.587, 0.114));
  a = max(mix(vec3(l), a, u_m_sat), 0.0);
  if (u_m_blend_amount > 0.0) {
    vec3 product = a * mix(vec3(1.0), u_m_blend_color, u_m_blend_amount);
    a = mix(product, u_m_blend_color, u_m_blend_mask * u_m_blend_amount);
  }
  return a;
}
// Clearcoat (PBR): a second, smooth specular lobe with its own F0
vec3 mat_clearcoat(float ndh, float ndv, float ndl) {
  if (u_m_cc <= 0.0) return vec3(0.0);
  float a = u_m_cc_rough * u_m_cc_rough, a2 = a * a;
  float d = ndh * ndh * (a2 - 1.0) + 1.0;
  float D = a2 / max(3.14159265 * d * d, 1e-6);
  float f0 = pow((u_m_cc_ior - 1.0) / (u_m_cc_ior + 1.0), 2.0);
  float F = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);
  return u_m_cc_tint * (D * F * u_m_cc * 0.25 * ndl);
}
// Effects tab: contrast is how fast light turns to shadow
float mat_ndl(float ndl) { return pow(clamp(ndl, 0.0, 1.0), u_m_contrast); }
// Highlights + Transparency: reflectance at normal incidence. A transparent
// surface takes it from its refraction index (water 2%, glass 4%); a metal
// from its own colour; everything else from the highlight intensity.
vec3 mat_f0(vec3 albedo) {
  float f_ior = pow((u_m_ior - 1.0) / (u_m_ior + 1.0), 2.0);
  float f_diel = mix(0.08 * u_specular, f_ior, u_transparency);
  vec3 f0 = vec3(f_diel) * u_m_hl_color;
  return mix(f0, albedo, u_metallic);
}
// Reflection tab: minimal reflectivity, sensitivity to the incidence angle
float mat_fresnel(float ndv, float f0) {
  float e = mix(1.0, 5.0, u_m_refl_angle);
  float fr = f0 + (1.0 - f0) * pow(1.0 - ndv, e);
  return max(fr, u_m_refl_min);
}
// Translucency tab: light through thin material toward the viewer, and the
// backlight of a leaf against the sun
vec3 mat_translucent(vec3 albedo, vec3 V, vec3 L, vec3 sun_c) {
  float through = max(dot(V, -L), 0.0);
  vec3 tint = mix(albedo, u_m_sss_color, 0.5);
  vec3 t = tint * u_translucency * pow(through, 3.0) * sun_c * 0.6;
  t += albedo * u_m_backlight * pow(through, 2.0) * sun_c * 0.5;
  return t;
}
// Phong highlights as an option: size and intensity independent
float mat_phong(float ndh, float rough) {
  float shin = mix(600.0, 4.0, rough);
  return pow(ndh, shin) * (shin + 2.0) / (2.0 * 3.14159265) * u_specular;
}
)GLSL";

void renderer_material_uniforms(unsigned prog, const gpx::MaterialParams &m) {
  uni1(prog, "u_roughness", m.roughness);
  uni1(prog, "u_metallic", m.metallic);
  uni1(prog, "u_specular", m.specular);
  uni1(prog, "u_reflection", m.reflection);
  uni1(prog, "u_translucency", m.translucency);
  uni1(prog, "u_transparency", m.transparency);
  // bump depth scales the normal map; a negative depth or the invert flag
  // turns bumps into holes
  float ns = m.normal_strength * m.bump_depth * (m.normal_invert ? -1.f : 1.f);
  uni1(prog, "u_normal_strength", ns);
  uni3(prog, "u_m_tint", m.tint);
  uni3(prog, "u_m_hl_color", m.highlight_color);
  uni3(prog, "u_m_sss_color", m.sss_color);
  float lum[3] = {m.luminous_color[0] * m.luminous, m.luminous_color[1] * m.luminous,
                  m.luminous_color[2] * m.luminous};
  uni3(prog, "u_m_luminous", lum);
  uni1(prog, "u_m_gain", m.gain);
  uni1(prog, "u_m_sat", m.saturation);
  uni1(prog, "u_m_ior", m.ior);
  // a transparent surface's "turn reflective with angle" is the same knob
  // as an opaque one's angle sensitivity, so the stronger of the two wins
  uni1(prog, "u_m_refl_angle", std::max(m.reflect_angle, m.reflect_with_angle));
  uni1(prog, "u_m_refl_min", m.reflect_min);
  uni1(prog, "u_m_diffuse", m.diffuse);
  uni1(prog, "u_m_ambient", m.ambient);
  uni1(prog, "u_m_contrast", m.contrast);
  uni1(prog, "u_m_backlight", m.backlight ? 1.f : 0.f);
  unii(prog, "u_m_color_reflected", m.color_reflected ? 1 : 0);
  unii(prog, "u_m_phong", m.highlight_model == 1 ? 1 : 0);
  unii(prog, "u_m_ignore_light", m.ignore_lighting ? 1 : 0);
  unii(prog, "u_m_ignore_atmo", m.ignore_atmosphere ? 1 : 0);
  uni1(prog, "u_m_alpha", m.alpha * (1.f - m.transparency));
  uni1(prog, "u_m_map_scale", 1.f / std::max(m.map_scale, 1e-3f));
  uni1(prog, "u_m_rotation", m.rotation);
  uni1(prog, "u_m_cycling", m.cycling);
  glUniform2f(uniform_location(prog, "u_m_origin"), m.origin[0], m.origin[1]);
  unii(prog, "u_m_turb_on", m.turbulence ? 1 : 0);
  unii(prog, "u_m_turb_complexity", m.turb_complexity);
  uni1(prog, "u_m_turb_amp", m.turb_amplitude);
  uni1(prog, "u_m_turb_scale", m.turb_scale);
  uni1(prog, "u_m_turb_harm", m.turb_harmonics);
  uni3(prog, "u_m_blend_color", m.blend_color);
  uni1(prog, "u_m_blend_amount", m.color_blend ? m.blend_amount : 0.f);
  uni1(prog, "u_m_blend_mask", m.blend_mask);
  uni3(prog, "u_m_cc_tint", m.cc_tint);
  uni1(prog, "u_m_cc", m.cc_intensity);
  uni1(prog, "u_m_cc_rough", m.cc_roughness);
  uni1(prog, "u_m_cc_ior", m.cc_ior);
}

} // namespace studio
