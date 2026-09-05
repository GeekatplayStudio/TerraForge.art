// Geekatplay TerraForge - every surface property a material carries, in one
// struct the shaders, the preview and the tests all read.
//
// Vue's Material Editor lays these out as tabs - Color, Bump, Highlights,
// Transparency, Reflection, Translucency, Effects - and the MaterialOutput
// node declares them in the same groups, so the node's Properties view and
// the Material Studio show the same thing. This struct is what those
// attributes mean to a renderer: read once per material with
// material_params_from(), uploaded as uniforms by the studio side.
#pragma once

namespace gpx {

class AttrSet;

struct MaterialParams {
  // Color
  float tint[3] = {1.f, 1.f, 1.f}; // multiplies the base colour
  float gain = 1.f;                // brightness, 0..2
  float saturation = 1.f;          // 0 grey .. 2 vivid
  float map_scale = 1.f;           // every map scaled together
  float origin[2] = {0.f, 0.f};    // map offset
  // Bump
  float normal_strength = 1.f;     // normal-map intensity
  float bump_depth = 1.f;          // bump amount (signed: negative inverts)
  float bump_slope = 0.f;          // more bump on steep faces, 0..1
  bool normal_invert = false;
  float displacement = 0.f;        // world units
  float disp_smoothing = 0.f;
  // Highlights
  int highlight_model = 0;         // 0 GGX, 1 Phong
  float specular = 0.35f;          // highlight intensity
  float roughness = 0.85f;         // highlight size, inverse
  float highlight_color[3] = {1.f, 1.f, 1.f};
  float anisotropy = 0.f;
  // Transparency
  float transparency = 0.f;
  float ior = 1.f;                 // 1 air, 1.33 water, 1.52 glass
  float reflect_with_angle = 0.f;  // grazing reflectivity for transparent surfaces
  float fade_out = 0.f;
  bool thin_surface = false;
  bool additive = false;
  float flare_intensity = 0.f, flare_span = 0.2f;
  // Reflection
  float reflection = 0.25f;        // global reflectivity
  float reflect_min = 0.f;         // minimal reflectivity
  float reflect_angle = 0.5f;      // sensitivity to incidence angle
  float reflect_blur = 0.f;
  float metallic = 0.f;
  // Translucency
  float translucency = 0.f;
  bool sss = false;
  float sss_depth = 0.01f;         // metres
  float sss_balance = 0.5f;
  float sss_color[3] = {1.f, 0.6f, 0.5f};
  bool backlight = false;
  // Effects
  float diffuse = 0.6f, ambient = 0.4f;
  float luminous = 0.f;
  float luminous_color[3] = {1.f, 1.f, 1.f};
  float contrast = 1.f;
  bool cast_shadows = true;
  bool color_reflected = false;
  bool color_transmitted = false;
};

// Read the params from a MaterialOutput's attributes. Missing keys keep the
// defaults, so an old scene file reads as it did.
MaterialParams material_params_from(const AttrSet &attrs);

// Declare the attributes on a MaterialOutput node, grouped by tab.
void material_params_declare(AttrSet &attrs);

} // namespace gpx
