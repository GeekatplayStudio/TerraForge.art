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
uniform int u_m_color_reflected, u_m_phong;
)GLSL";

extern const char *const MATERIAL_FN_GLSL;
const char *const MATERIAL_FN_GLSL = R"GLSL(
// Color tab: tint, brightness, saturation
vec3 mat_albedo(vec3 a) {
  a *= u_m_tint * u_m_gain;
  float l = dot(a, vec3(0.299, 0.587, 0.114));
  return max(mix(vec3(l), a, u_m_sat), 0.0);
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
}

} // namespace studio
