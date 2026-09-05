// Geekatplay TerraForge - material properties: declared on the node, read
// into the struct. See gpx/material_params.hpp.
#include "gpx/material_params.hpp"
#include "gpx/attribute.hpp"
#include <algorithm>

namespace gpx {

void material_params_declare(AttrSet &a) {
  // Color
  add_color(a, "tint", "Overall color", 1.f, 1.f, 1.f, 1.f, "Color").tooltip =
      "Multiplies every colour in the material. White leaves it alone.";
  add_float(a, "color_gain", "Brightness", 1.f, 0.f, 2.f, "Color");
  add_float(a, "saturation", "Saturation", 1.f, 0.f, 2.f, "Color");
  add_float(a, "map_scale", "Scale of the maps", 1.f, 0.05f, 20.f, "Color", true)
      .tooltip = "Scales every texture map together.";
  add_vec2(a, "origin", "Origin", 0.f, 0.f, -10.f, 10.f, "Color").tooltip =
      "Offsets the material in map space, for precise placement.";
  // Bump
  add_float(a, "normal_strength", "Normal intensity", 1.f, 0.f, 4.f, "Bump")
      .tooltip = "How much of the normal map's vector is applied.";
  add_float(a, "bump_depth", "Bump depth", 1.f, -4.f, 4.f, "Bump").tooltip =
      "The amount of bump. Negative turns bumps into holes.";
  add_float(a, "bump_slope", "Dependent on slope", 0.f, 0.f, 1.f, "Bump")
      .tooltip = "Higher bumps on steep faces than on flat ground, as on "
                 "eroded terrain.";
  add_bool(a, "normal_invert", "Invert normal map", false, "Bump");
  add_float(a, "displacement", "Displacement depth", 0.f, 0.f, 0.1f, "Bump")
      .tooltip = "Height map displacement applied to the surface, in world "
                 "units. Moves geometry, not only normals.";
  add_float(a, "disp_smoothing", "Displacement smoothing", 0.f, 0.f, 1.f, "Bump");
  // Highlights
  add_choice(a, "highlight_model", "Lighting model", {"GGX", "Phong"}, 0,
             "Highlights")
      .tooltip = "GGX: the physically based microfacet model, size is "
                 "roughness. Phong: the legacy model, size and intensity "
                 "independent.";
  add_float(a, "specular", "Highlight intensity", 0.35f, 0.f, 1.f, "Highlights");
  add_float(a, "roughness", "Roughness (highlight size)", 0.85f, 0.02f, 1.f,
            "Highlights")
      .tooltip = "Small: a polished surface with tight bright spots. Large: "
                 "dull. Also multiplies a connected roughness map.";
  add_color(a, "highlight_color", "Highlight color", 1.f, 1.f, 1.f, 1.f,
            "Highlights")
      .tooltip = "A uniform shade for the highlights - blue for pearl.";
  add_float(a, "anisotropy", "Anisotropy", 0.f, 0.f, 1.f, "Highlights").tooltip =
      "Stretched highlights along a direction, for brushed metal or hair.";
  // Transparency
  add_float(a, "transparency", "Global transparency", 0.f, 0.f, 1.f,
            "Transparency");
  add_float(a, "ior", "Refraction index", 1.f, 1.f, 2.5f, "Transparency")
      .tooltip = "1 air, 1.33 water, 1.52 glass. Bends light crossing the "
                 "surface; also sets how reflective a transparent surface is.";
  add_float(a, "reflect_with_angle", "Turn reflective with angle", 0.f, 0.f,
            1.f, "Transparency")
      .tooltip = "Glass and water mirror at a low angle. About 0.4 looks right.";
  add_float(a, "fade_out", "Fade out", 0.f, 0.f, 1.f, "Transparency");
  add_bool(a, "thin_surface", "Thin surface (no refraction)", false,
           "Transparency");
  add_bool(a, "additive", "Additive", false, "Transparency").tooltip =
      "Adds the colour to the background: luminous, immaterial objects.";
  add_float(a, "flare_intensity", "Flare intensity", 0.f, 0.f, 1.f,
            "Transparency")
      .tooltip = "Brightening when light is seen through a partly "
                 "transparent surface. Strongest at 50% transparency.";
  add_float(a, "flare_span", "Flare span", 0.2f, 0.f, 1.f, "Transparency");
  // Reflection
  add_float(a, "reflection", "Global reflectivity", 0.25f, 0.f, 1.f,
            "Reflection");
  add_float(a, "reflect_min", "Minimal reflectivity", 0.f, 0.f, 1.f,
            "Reflection")
      .tooltip = "Reflectivity looking straight at the surface; the angle "
                 "sensitivity raises it toward grazing.";
  add_float(a, "reflect_angle", "Sensitivity to incidence angle", 0.5f, 0.f,
            1.f, "Reflection");
  add_float(a, "reflect_blur", "Blurred reflections", 0.f, 0.f, 1.f,
            "Reflection");
  add_float(a, "metallic", "Metalness", 0.f, 0.f, 1.f, "Reflection").tooltip =
      "Metal reflects its own colour and has no diffuse. Also multiplies a "
      "connected metallic map.";
  // Translucency
  add_float(a, "translucency", "Translucency", 0.f, 0.f, 1.f, "Translucency")
      .tooltip = "Light bleeding through thin material toward the viewer.";
  add_bool(a, "sss", "Subsurface scattering", false, "Translucency");
  add_float(a, "sss_depth", "Average depth (m)", 0.01f, 0.0001f, 1.f,
            "Translucency", true)
      .tooltip = "How far light travels inside: a fraction of a millimetre "
                 "for skin, centimetres for wax.";
  add_float(a, "sss_balance", "Absorption / scattering balance", 0.5f, 0.f,
            1.f, "Translucency");
  add_color(a, "sss_color", "Scattering color", 1.f, 0.6f, 0.5f, 1.f,
            "Translucency")
      .tooltip = "The colour light picks up inside - the red of a finger "
                 "over a torch.";
  add_bool(a, "backlight", "Backlight", false, "Translucency").tooltip =
      "Thin enough that light shows through from behind, like a leaf.";
  // Effects
  add_float(a, "diffuse", "Diffuse lighting", 0.6f, 0.f, 1.f, "Effects")
      .tooltip = "How the material reacts to light from light sources. "
                 "Diffuse + ambient should stay at 100%.";
  add_float(a, "ambient", "Ambient lighting", 0.4f, 0.f, 1.f, "Effects");
  add_float(a, "luminous", "Luminous", 0.f, 0.f, 2.f, "Effects").tooltip =
      "Seems to emit light. Does not cast real light.";
  add_color(a, "luminous_color", "Luminous color", 1.f, 1.f, 1.f, 1.f, "Effects");
  add_float(a, "contrast", "Contrast", 1.f, 0.2f, 4.f, "Effects").tooltip =
      "How fast the surface goes from light to shadow; low for fluffy things.";
  add_bool(a, "cast_shadows", "Casts shadows", true, "Effects");
  add_bool(a, "color_reflected", "Color reflected light", false, "Effects")
      .tooltip = "Highlights and reflections take the surface colour: metal.";
  add_bool(a, "color_transmitted", "Color transmitted light", false, "Effects")
      .tooltip = "Light crossing a transparent surface takes its colour: "
                 "stained glass.";
}

namespace {
void col3(const AttrSet &a, const char *key, float *out) {
  if (const Attribute *at = a.find(key))
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(at->col[i], 0.f, 1.f);
}
} // namespace

MaterialParams material_params_from(const AttrSet &a) {
  MaterialParams p;
  auto f = [&](const char *k, float def, float lo, float hi) {
    return std::clamp(a.get_f(k, def), lo, hi);
  };
  col3(a, "tint", p.tint);
  p.gain = f("color_gain", 1.f, 0.f, 2.f);
  p.saturation = f("saturation", 1.f, 0.f, 2.f);
  p.map_scale = f("map_scale", 1.f, 0.05f, 20.f);
  a.get_vec2("origin", p.origin[0], p.origin[1]);
  p.normal_strength = f("normal_strength", 1.f, 0.f, 4.f);
  p.bump_depth = f("bump_depth", 1.f, -4.f, 4.f);
  p.bump_slope = f("bump_slope", 0.f, 0.f, 1.f);
  p.normal_invert = a.get_b("normal_invert", false);
  p.displacement = f("displacement", 0.f, 0.f, 0.1f);
  p.disp_smoothing = f("disp_smoothing", 0.f, 0.f, 1.f);
  p.highlight_model = std::clamp(a.get_choice("highlight_model"), 0, 1);
  p.specular = f("specular", 0.35f, 0.f, 1.f);
  p.roughness = f("roughness", 0.85f, 0.02f, 1.f);
  col3(a, "highlight_color", p.highlight_color);
  p.anisotropy = f("anisotropy", 0.f, 0.f, 1.f);
  p.transparency = f("transparency", 0.f, 0.f, 1.f);
  p.ior = f("ior", 1.f, 1.f, 2.5f);
  p.reflect_with_angle = f("reflect_with_angle", 0.f, 0.f, 1.f);
  p.fade_out = f("fade_out", 0.f, 0.f, 1.f);
  p.thin_surface = a.get_b("thin_surface", false);
  p.additive = a.get_b("additive", false);
  p.flare_intensity = f("flare_intensity", 0.f, 0.f, 1.f);
  p.flare_span = f("flare_span", 0.2f, 0.f, 1.f);
  p.reflection = f("reflection", 0.25f, 0.f, 1.f);
  p.reflect_min = f("reflect_min", 0.f, 0.f, 1.f);
  p.reflect_angle = f("reflect_angle", 0.5f, 0.f, 1.f);
  p.reflect_blur = f("reflect_blur", 0.f, 0.f, 1.f);
  p.metallic = f("metallic", 0.f, 0.f, 1.f);
  p.translucency = f("translucency", 0.f, 0.f, 1.f);
  p.sss = a.get_b("sss", false);
  p.sss_depth = f("sss_depth", 0.01f, 0.0001f, 1.f);
  p.sss_balance = f("sss_balance", 0.5f, 0.f, 1.f);
  col3(a, "sss_color", p.sss_color);
  p.backlight = a.get_b("backlight", false);
  p.diffuse = f("diffuse", 0.6f, 0.f, 1.f);
  p.ambient = f("ambient", 0.4f, 0.f, 1.f);
  p.luminous = f("luminous", 0.f, 0.f, 2.f);
  col3(a, "luminous_color", p.luminous_color);
  p.contrast = f("contrast", 1.f, 0.2f, 4.f);
  p.cast_shadows = a.get_b("cast_shadows", true);
  p.color_reflected = a.get_b("color_reflected", false);
  p.color_transmitted = a.get_b("color_transmitted", false);
  return p;
}

} // namespace gpx
