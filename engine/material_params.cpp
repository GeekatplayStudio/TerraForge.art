// Geekatplay TerraForge - material properties: declared on the node, read
// into the struct. See gpx/material_params.hpp.
//
// The groups are the Material Studio's tabs. A property is declared once,
// here, with its label, range and tooltip; the studio draws whatever a
// group holds, the renderer reads the struct, and the file carries the
// attribute. Adding one means this file and the GLSL, nothing else.
#include "gpx/material_params.hpp"
#include "gpx/attribute.hpp"
#include <algorithm>

namespace gpx {

namespace {
Attribute &f(AttrSet &a, const char *k, const char *l, float d, float lo, float hi,
             const char *g, bool log = false) {
  return add_float(a, k, l, d, lo, hi, g, log);
}
Attribute &b(AttrSet &a, const char *k, const char *l, bool d, const char *g) {
  return add_bool(a, k, l, d, g);
}
Attribute &c(AttrSet &a, const char *k, const char *l, float r, float gg, float bb,
             const char *g) {
  return add_color(a, k, l, r, gg, bb, 1.f, g);
}
} // namespace

void material_params_declare(AttrSet &a) {
  // Color
  c(a, "tint", "Overall color", 1.f, 1.f, 1.f, "Color").tooltip =
      "Multiplies every colour in the material. White leaves it alone.";
  f(a, "color_gain", "Brightness", 1.f, 0.f, 2.f, "Color")
      .tooltip = "Scales the whole colour up or down after everything\n"
                 "upstream. Use it to sit a material into a scene without\n"
                 "going back and editing the maps that made it.";
  f(a, "saturation", "Saturation", 1.f, 0.f, 2.f, "Color")
      .tooltip = "How strong the colour is. 0 leaves a greyscale surface\n"
                 "with all of its detail intact, which is often closer to\n"
                 "real weathered rock than the photograph it came from.";
  b(a, "color_blend", "Color blend", false, "Color").tooltip =
      "Blend the picture with a solid colour, in product mode.";
  c(a, "blend_color", "Blend color", 1.f, 1.f, 1.f, "Color")
      .tooltip = "A colour mixed into the whole surface. This is the\n"
                 "quickest way to tint a shared material differently per\n"
                 "object - a wash of ochre over the same rock.";
  f(a, "blend_amount", "Blend amount", 0.5f, 0.f, 1.f, "Color")
      .tooltip = "How much of the blend colour is mixed in.";
  f(a, "blend_mask", "Color mask", 0.f, 0.f, 1.f, "Color").tooltip =
      "0: the colour multiplies the picture. 1: it replaces it.";
  // Alpha
  f(a, "alpha", "Global alpha", 1.f, 0.f, 1.f, "Alpha").tooltip =
      "Where no alpha map is connected. Alpha does not bend light; "
      "transparency does.";
  f(a, "alpha_boost", "Alpha boost", 0.f, -1.f, 1.f, "Alpha").tooltip =
      "For a layer of a multi-layer material: its overall presence, within "
      "the limits the Presence tab sets.";
  // Bump
  f(a, "normal_strength", "Normal intensity", 1.f, 0.f, 4.f, "Bump").tooltip =
      "How much of the normal map's vector is applied.";
  f(a, "bump_depth", "Bump depth", 1.f, -4.f, 4.f, "Bump").tooltip =
      "The amount of bump. Negative turns bumps into holes.";
  f(a, "bump_slope", "Dependent on slope", 0.f, 0.f, 1.f, "Bump").tooltip =
      "Higher bumps on steep faces than on flat ground, as on eroded terrain.";
  b(a, "normal_invert", "Invert normal map", false, "Bump")
      .tooltip = "Flips the green channel of the normal map. There are two\n"
                 "conventions for which way is up and they are visually\n"
                 "identical until the light moves - if the bumps read as\n"
                 "dents, this is the switch.";
  f(a, "displacement", "Displacement depth", 0.f, 0.f, 0.1f, "Bump").tooltip =
      "Height map displacement applied to the surface, in world units. "
      "Moves geometry, not only normals.";
  f(a, "disp_smoothing", "Displacement smoothing", 0.f, 0.f, 1.f, "Bump")
      .tooltip = "Softens the displacement before it moves the surface,\n"
                 "without touching the colour. Use it when a height map is\n"
                 "noisier than the geometry can carry.";
  // Highlights
  add_choice(a, "highlight_model", "Lighting model", {"GGX", "Phong"}, 0,
             "Highlights")
      .tooltip = "GGX: the physically based microfacet model, size is "
                 "roughness. Phong: the legacy model, size and intensity "
                 "independent.";
  f(a, "specular", "Highlight intensity", 0.35f, 0.f, 1.f, "Highlights")
      .tooltip = "How bright the direct highlight is. This is the sheen a\n"
                 "light leaves on the surface, as against Reflectivity\n"
                 "below, which is how much of the surroundings it mirrors.";
  f(a, "roughness", "Roughness (highlight size)", 0.85f, 0.02f, 1.f, "Highlights")
      .tooltip = "Small: a polished surface with tight bright spots. Large: "
                 "dull. Also multiplies a connected roughness map.";
  c(a, "highlight_color", "Highlight color", 1.f, 1.f, 1.f, "Highlights").tooltip =
      "A uniform shade for the highlights - blue for pearl.";
  f(a, "anisotropy", "Anisotropy", 0.f, 0.f, 1.f, "Highlights").tooltip =
      "Stretched highlights along a direction, for brushed metal or hair.";
  // Transparency
  f(a, "transparency", "Global transparency", 0.f, 0.f, 1.f, "Transparency")
      .tooltip = "How much light passes straight through. 0 is opaque.";
  f(a, "ior", "Refraction index", 1.f, 1.f, 2.5f, "Transparency").tooltip =
      "1 air, 1.33 water, 1.52 glass. Bends light crossing the surface; also "
      "sets how reflective a transparent surface is.";
  f(a, "reflect_with_angle", "Turn reflective with angle", 0.f, 0.f, 1.f,
    "Transparency")
      .tooltip = "Glass and water mirror at a low angle. About 0.4 looks right.";
  f(a, "fade_out", "Fade out", 0.f, 0.f, 1.f, "Transparency")
      .tooltip = "How much the surface thins toward its silhouette, so an\n"
                 "edge dissolves rather than ending on a hard line.";
  b(a, "thin_surface", "Thin surface (no refraction)", false, "Transparency")
      .tooltip = "Treats the surface as having no thickness, so light passes\n"
                 "through without bending. Right for glass panes, leaves and\n"
                 "water films; wrong for a solid body of water, which does\n"
                 "refract.";
  b(a, "additive", "Additive", false, "Transparency").tooltip =
      "Adds the colour to the background: luminous, immaterial objects.";
  f(a, "flare_intensity", "Flare intensity", 0.f, 0.f, 1.f, "Transparency")
      .tooltip = "Brightening when light is seen through a partly transparent "
                 "surface. Strongest at 50% transparency.";
  f(a, "flare_span", "Flare span", 0.2f, 0.f, 1.f, "Transparency")
      .tooltip = "How far the highlight spreads. Narrow reads as polished,\n"
                 "wide as satin.";
  // Reflection
  f(a, "reflection", "Global reflectivity", 0.25f, 0.f, 1.f, "Reflection")
      .tooltip = "How much of the surroundings the surface mirrors. Distinct\n"
                 "from the highlight: this is the world reflected, that is\n"
                 "the light source.";
  f(a, "reflect_min", "Minimal reflectivity", 0.f, 0.f, 1.f, "Reflection").tooltip =
      "Reflectivity looking straight at the surface; the angle sensitivity "
      "raises it toward grazing.";
  f(a, "reflect_angle", "Sensitivity to incidence angle", 0.5f, 0.f, 1.f,
    "Reflection")
      .tooltip = "How much more reflective the surface becomes at a grazing\n"
                 "angle. Nearly every real material does this - it is why a\n"
                 "wet road mirrors the sky ahead but not underfoot - so 0\n"
                 "reads as wrong.";
  f(a, "reflect_blur", "Blurred reflections", 0.f, 0.f, 1.f, "Reflection")
      .tooltip = "How blurred the reflection is. 0 is a mirror; higher is\n"
                 "brushed metal or rippled water.";
  f(a, "metallic", "Metalness", 0.f, 0.f, 1.f, "Reflection").tooltip =
      "Metal reflects its own colour and has no diffuse. Also multiplies a "
      "connected metallic map.";
  f(a, "specular_level", "Specular level (PBR)", 0.5f, 0.f, 1.f, "Reflection")
      .tooltip = "F0 of the non-metal parts: 0.5 is the common 4 %, 1 is 8 %.";
  // Translucency
  f(a, "translucency", "Translucency", 0.f, 0.f, 1.f, "Translucency").tooltip =
      "Light bleeding through thin material toward the viewer.";
  b(a, "sss", "Subsurface scattering", false, "Translucency")
      .tooltip = "Lets light enter the surface, scatter inside and leave\n"
                 "elsewhere. This is what makes skin, wax, marble and snow\n"
                 "look lit from within rather than merely lit.";
  f(a, "sss_depth", "Average depth (m)", 0.01f, 0.0001f, 1.f, "Translucency", true)
      .tooltip = "How far light travels inside: a fraction of a millimetre for "
                 "skin, centimetres for wax.";
  f(a, "sss_balance", "Absorption / scattering balance", 0.5f, 0.f, 1.f,
    "Translucency")
      .tooltip = "Whether light inside the surface is mostly absorbed or\n"
                 "mostly scattered onward. Toward absorption gives dense,\n"
                 "waxy material; toward scattering gives translucent,\n"
                 "glowing material.";
  c(a, "sss_color", "Scattering color", 1.f, 0.6f, 0.5f, "Translucency").tooltip =
      "The colour light picks up inside - the red of a finger over a torch.";
  b(a, "backlight", "Backlight", false, "Translucency").tooltip =
      "Thin enough that light shows through from behind, like a leaf.";
  // Clearcoat
  f(a, "cc_intensity", "Coat intensity", 0.f, 0.f, 1.f, "Clearcoat").tooltip =
      "A thin reflective layer on top: the lacquer over car paint.";
  c(a, "cc_tint", "Coat tint", 1.f, 1.f, 1.f, "Clearcoat")
      .tooltip = "The colour of the clear coat over the surface - the\n"
                 "lacquer on car paint, the wet film on a stone. It reflects\n"
                 "and tints without changing the material underneath.";
  f(a, "cc_roughness", "Coat roughness", 0.1f, 0.02f, 1.f, "Clearcoat")
      .tooltip = "How polished the clear coat is, independently of the\n"
                 "surface under it. A rough stone under a smooth wet coat is\n"
                 "exactly what a rock in a stream is.";
  f(a, "cc_ior", "Coat refraction index", 1.5f, 1.f, 2.5f, "Clearcoat")
      .tooltip = "The coat's refractive index, which sets how strongly it\n"
                 "reflects at a glancing angle. 1.5 is a lacquer or a\n"
                 "varnish; 1.33 is water.";
  f(a, "cc_flatten", "Flatten", 1.f, 0.f, 1.f, "Clearcoat").tooltip =
      "1: the coat has its own smooth normal. 0: it follows the bumps below.";
  // Effects
  f(a, "diffuse", "Diffuse lighting", 0.6f, 0.f, 1.f, "Effects").tooltip =
      "How the material reacts to light from light sources. Diffuse + ambient "
      "should stay at 100%.";
  f(a, "ambient", "Ambient lighting", 0.4f, 0.f, 1.f, "Effects")
      .tooltip = "How much of the surrounding sky light the surface picks up\n"
                 "where nothing shines on it directly. Too low makes the\n"
                 "shadows read as black holes.";
  f(a, "luminous", "Luminous", 0.f, 0.f, 2.f, "Effects").tooltip =
      "Seems to emit light. Does not cast real light.";
  c(a, "luminous_color", "Luminous color", 1.f, 1.f, 1.f, "Effects")
      .tooltip = "Light the surface emits by itself. It lights nothing else\n"
                 "in the viewport - this is the surface glowing, not a lamp.";
  f(a, "contrast", "Contrast", 1.f, 0.2f, 4.f, "Effects").tooltip =
      "How fast the surface goes from light to shadow; low for fluffy things.";
  b(a, "color_reflected", "Color reflected light", false, "Effects").tooltip =
      "Highlights and reflections take the surface colour: metal.";
  b(a, "color_transmitted", "Color transmitted light", false, "Effects").tooltip =
      "Light crossing a transparent surface takes its colour: stained glass.";
  // Options
  b(a, "cast_shadows", "Casts shadows", true, "Options")
      .tooltip = "Whether the surface blocks light. Turning it off is a\n"
                 "lighting cheat, useful for glass and for foliage cards\n"
                 "that would otherwise shadow themselves into mud.";
  b(a, "receive_shadows", "Receives shadows", true, "Options")
      .tooltip = "Whether other things can cast shadows onto this surface.";
  b(a, "one_sided", "One sided", false, "Options").tooltip =
      "Traced for one intersection per ray; matters for transparent surfaces.";
  b(a, "hide_from_camera", "Hide from camera rays", false, "Options").tooltip =
      "Seen only in reflections and refractions.";
  b(a, "hide_from_reflections", "Hide from reflected / refracted rays", false,
    "Options")
      .tooltip = "Keeps the surface out of reflections and refractions while\n"
                 "leaving it visible to the camera. A compositing\n"
                 "convenience, not physics.";
  b(a, "ignore_lighting", "Ignore lighting", false, "Options").tooltip =
      "No sun, no lights: the surface shows its own colour.";
  b(a, "ignore_atmosphere", "Ignore atmosphere", false, "Options").tooltip =
      "No fog or haze between it and the camera.";
  b(a, "only_shadows", "Only shadows", false, "Options").tooltip =
      "Invisible, but still casts a shadow.";
  b(a, "disable_aa", "Disable anti-aliasing", false, "Options")
      .tooltip = "Turns off edge smoothing for this material. Only wanted\n"
                 "where a hard pixel boundary is the point, such as an index\n"
                 "or ID pass.";
  // Transform
  add_choice(a, "mapping", "Mapping",
             {"Automatic", "Flat", "Faces", "Cylindrical", "Spherical"}, 0, "Transform")
      .tooltip = "How the 2D maps wrap a 3D object. Terrain is always Flat "
                 "(projected from above); the others are for objects.";
  f(a, "map_scale", "Scale of the maps", 1.f, 0.05f, 20.f, "Transform", true)
      .tooltip = "Scales every texture map together.";
  add_vec2(a, "origin", "Origin", 0.f, 0.f, -10.f, 10.f, "Transform").tooltip =
      "Offsets the material in map space, for precise placement.";
  f(a, "rotation", "Rotation", 0.f, -180.f, 180.f, "Transform").tooltip =
      "Turns the maps about the surface normal, in degrees.";
  b(a, "turbulence", "Turbulence", false, "Transform").tooltip =
      "A noise repeatedly displaces where the maps are read, so a tiled "
      "picture stops looking tiled.";
  add_int(a, "turb_complexity", "Complexity", 3, 1, 8, "Transform")
      .tooltip = "How many scales of small-scale disturbance ripple the\n"
                 "surface normal. This is shading detail only - it does not\n"
                 "move the geometry.";
  f(a, "turb_amplitude", "Amplitude", 0.05f, 0.f, 0.5f, "Transform")
      .tooltip = "How strongly the disturbance tilts the surface normal. A\n"
                 "little breaks up a surface that reads as too clean; a lot\n"
                 "looks like hammered metal.";
  f(a, "turb_scale", "Scale", 4.f, 0.25f, 64.f, "Transform", true)
      .tooltip = "How fine the disturbance is, in repeats across the\n"
                 "surface.";
  f(a, "turb_harmonics", "Harmonics", 0.5f, 0.1f, 0.9f, "Transform").tooltip =
      "How scale and amplitude shrink with each repetition of the noise.";
  f(a, "cycling", "Cycling", 0.f, 0.f, 1.f, "Transform").tooltip =
      "A large, slow perturbation that keeps a material from repeating.";
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
  auto bb = [&](const char *k, bool def) { return a.get_b(k, def); };
  col3(a, "tint", p.tint);
  p.gain = f("color_gain", 1.f, 0.f, 2.f);
  p.saturation = f("saturation", 1.f, 0.f, 2.f);
  p.color_blend = bb("color_blend", false);
  col3(a, "blend_color", p.blend_color);
  p.blend_amount = f("blend_amount", 0.5f, 0.f, 1.f);
  p.blend_mask = f("blend_mask", 0.f, 0.f, 1.f);
  p.alpha = f("alpha", 1.f, 0.f, 1.f);
  p.alpha_boost = f("alpha_boost", 0.f, -1.f, 1.f);
  p.normal_strength = f("normal_strength", 1.f, 0.f, 4.f);
  p.bump_depth = f("bump_depth", 1.f, -4.f, 4.f);
  p.bump_slope = f("bump_slope", 0.f, 0.f, 1.f);
  p.normal_invert = bb("normal_invert", false);
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
  p.thin_surface = bb("thin_surface", false);
  p.additive = bb("additive", false);
  p.flare_intensity = f("flare_intensity", 0.f, 0.f, 1.f);
  p.flare_span = f("flare_span", 0.2f, 0.f, 1.f);
  p.reflection = f("reflection", 0.25f, 0.f, 1.f);
  p.reflect_min = f("reflect_min", 0.f, 0.f, 1.f);
  p.reflect_angle = f("reflect_angle", 0.5f, 0.f, 1.f);
  p.reflect_blur = f("reflect_blur", 0.f, 0.f, 1.f);
  p.metallic = f("metallic", 0.f, 0.f, 1.f);
  p.specular_level = f("specular_level", 0.5f, 0.f, 1.f);
  p.translucency = f("translucency", 0.f, 0.f, 1.f);
  p.sss = bb("sss", false);
  p.sss_depth = f("sss_depth", 0.01f, 0.0001f, 1.f);
  p.sss_balance = f("sss_balance", 0.5f, 0.f, 1.f);
  col3(a, "sss_color", p.sss_color);
  p.backlight = bb("backlight", false);
  p.cc_intensity = f("cc_intensity", 0.f, 0.f, 1.f);
  col3(a, "cc_tint", p.cc_tint);
  p.cc_roughness = f("cc_roughness", 0.1f, 0.02f, 1.f);
  p.cc_ior = f("cc_ior", 1.5f, 1.f, 2.5f);
  p.cc_flatten = f("cc_flatten", 1.f, 0.f, 1.f);
  p.diffuse = f("diffuse", 0.6f, 0.f, 1.f);
  p.ambient = f("ambient", 0.4f, 0.f, 1.f);
  p.luminous = f("luminous", 0.f, 0.f, 2.f);
  col3(a, "luminous_color", p.luminous_color);
  p.contrast = f("contrast", 1.f, 0.2f, 4.f);
  p.color_reflected = bb("color_reflected", false);
  p.color_transmitted = bb("color_transmitted", false);
  p.cast_shadows = bb("cast_shadows", true);
  p.receive_shadows = bb("receive_shadows", true);
  p.one_sided = bb("one_sided", false);
  p.hide_from_camera = bb("hide_from_camera", false);
  p.hide_from_reflections = bb("hide_from_reflections", false);
  p.ignore_lighting = bb("ignore_lighting", false);
  p.ignore_atmosphere = bb("ignore_atmosphere", false);
  p.only_shadows = bb("only_shadows", false);
  p.disable_aa = bb("disable_aa", false);
  p.map_scale = f("map_scale", 1.f, 0.05f, 20.f);
  a.get_vec2("origin", p.origin[0], p.origin[1]);
  p.rotation = f("rotation", 0.f, -180.f, 180.f);
  p.turbulence = bb("turbulence", false);
  p.turb_complexity = std::clamp(a.get_i("turb_complexity", 3), 1, 8);
  p.turb_amplitude = f("turb_amplitude", 0.05f, 0.f, 0.5f);
  p.turb_scale = f("turb_scale", 4.f, 0.25f, 64.f);
  p.turb_harmonics = f("turb_harmonics", 0.5f, 0.1f, 0.9f);
  p.cycling = f("cycling", 0.f, 0.f, 1.f);
  return p;
}

} // namespace gpx
