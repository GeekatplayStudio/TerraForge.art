// Geekatplay TerraForge - the Segment's parameter table (plant_schema.hpp):
// the plant tool's Segment, Meshing, Influences, Materials and Transform
// sheets, then the attachment block every child carries.
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace gpx {
namespace plant {

const std::vector<ParamDef> &segment_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Segment";
  v.push_back({"simple", PType::Bool, "Simple segment", G, 0.f});
  v.push_back({"skin", PType::Choice, "Skin", G, 0.f, 0.f, 0.f, 0.f, {"Standard", "From object", "None"}});
  v.push_back({"length", PType::Random, "Length (m)", G, 5.f, 0.001f, 200.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"min_length", PType::Float, "Minimum length (m)", G, 0.f, 0.f, 50.f});
  v.push_back({"radius_mode", PType::Choice, "Radius mode", G, 1.f, 0.f, 0.f, 0.f, {"Inherit", "User defined", "Inherit, clamped to radius"}});
  v.push_back({"radius", PType::Random, "Radius (m)", G, 0.2f, 0.0001f, 20.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"inherit_ratio", PType::Random, "Inherit ratio", G, 0.45f, 0.01f, 1.f});
  v.push_back({"min_radius", PType::Float, "Minimum radius (m)", G, 0.f, 0.f, 5.f});
  v.push_back({"radius_profile", PType::Curve, "Radius along the segment", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,1,0,0;0.7,0.7,0,0;1,0.05,0,0"});
  v.push_back({"tropism", PType::Random, "Tropism", G, 0.f, -2.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"section", PType::Choice, "Section", G, 0.f, 0.f, 0.f, 0.f,
               {"Circle", "Ellipse", "Triangle", "Square", "Star", "Flat blade", "Custom"}});
  v.push_back({"section_custom", PType::Curve, "Custom section (radius by angle)", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"section_squash", PType::Random, "Section squash", G, 1.f, 0.05f, 4.f});
  v.push_back({"section_twist", PType::Random, "Section twist (turns)", G, 0.f, -4.f, 4.f});
  // Axis control
  v.push_back({"axis_mode", PType::Choice, "Axis", "Axis control", 0.f, 0.f, 0.f, 0.f,
               {"Straight", "Curve up", "Curve down", "S curve", "Spiral", "Custom"}});
  v.push_back({"axis_bend", PType::Random, "Axis bend (deg)", "Axis control", 0.f, -180.f, 180.f});
  v.push_back({"axis_curve", PType::Curve, "Custom axis (sideways by primal)", "Axis control", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,-1,1|1;0,0,0,0;1,0,0,0"});
  v.push_back({"sampling_boost", PType::Int, "Sampling boost", "Axis control", 0.f, 0.f, 4.f});
  v.push_back({"prevent_backfolds", PType::Bool, "Prevent back folds", "Axis control", 1.f});
  v.push_back({"shift_short_side", PType::Bool, "Shift to short side", "Axis control", 0.f});
  v.push_back({"smoothing", PType::Float, "Smoothing", "Axis control", 0.f, 0.f, 1.f});
  // Caps
  v.push_back({"cap_enable", PType::Bool, "Cap", "Cap", 1.f});
  v.push_back({"cap_mode", PType::Choice, "Mode", "Cap", 0.f, 0.f, 0.f, 0.f, {"Cut & Top", "Only cut", "Only top"}});
  v.push_back({"cap_profile", PType::Curve, "Cap profile", "Cap", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,-1,1|1;0,0,0,0;1,0,0,0"});
  v.push_back({"cap_offset", PType::Random, "Offset", "Cap", 0.f, -1.f, 1.f});
  v.push_back({"cap_smoothing", PType::Bool, "Smoothing", "Cap", 1.f});
  v.push_back({"cap2_profile", PType::Curve, "Secondary cap profile", "Secondary cap", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,-1,1|1;0,0,0,0;1,0,0,0"});
  v.push_back({"cap2_offset", PType::Random, "Offset", "Secondary cap", 0.f, -1.f, 1.f});
  v.push_back({"cap2_smoothing", PType::Bool, "Smoothing", "Secondary cap", 1.f});
  v.push_back({"bcap_enable", PType::Bool, "Bottom cap", "Bottom cap", 0.f});
  v.push_back({"bcap_profile", PType::Curve, "Bottom cap profile", "Bottom cap", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,-1,1|1;0,0,0,0;1,0,0,0"});
  v.push_back({"bcap_offset", PType::Random, "Offset", "Bottom cap", 0.f, -1.f, 1.f});
  v.push_back({"bcap_smoothing", PType::Bool, "Smoothing", "Bottom cap", 1.f});
  // Root flares
  v.push_back({"flare_number", PType::Int, "Number", "Root flares", 0.f, 0.f, 24.f});
  v.push_back({"flare_randomness", PType::Random, "Randomness", "Root flares", 0.3f, 0.f, 1.f});
  v.push_back({"flare_height", PType::Random, "Height (m)", "Root flares", 1.f, 0.f, 20.f});
  v.push_back({"flare_swell", PType::Random, "Base swell", "Root flares", 0.5f, 0.f, 4.f});
  v.push_back({"flare_depth", PType::Random, "Depth (m)", "Root flares", 0.8f, 0.f, 20.f});
  v.push_back({"flare_width", PType::Random, "Width", "Root flares", 0.5f, 0.05f, 2.f});
  v.push_back({"flare_shape", PType::Curve, "Shape", "Root flares", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,0,0,0"});
  // Blades
  v.push_back({"blade_number", PType::Int, "Number", "Blades", 0.f, 0.f, 12.f});
  v.push_back({"blade_start", PType::Random, "Start", "Blades", 0.f, 0.f, 1.f});
  v.push_back({"blade_end", PType::Random, "End", "Blades", 1.f, 0.f, 1.f});
  v.push_back({"blade_spread", PType::Random, "Spread (deg)", "Blades", 180.f, 0.f, 360.f});
  v.push_back({"blade_spread_offset", PType::Random, "Spread offset (deg)", "Blades", 0.f, -180.f, 180.f});
  v.push_back({"blade_width", PType::Random, "Width (m)", "Blades", 0.05f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"blade_style", PType::Choice, "Style", "Blades", 1.f, 0.f, 0.f, 0.f, {"Single", "Symmetrical", "Full width", "Simple flat"}});
  v.push_back({"blade_autosize", PType::Bool, "Auto-size", "Blades", 0.f});
  v.push_back({"blade_from_axis", PType::Bool, "From axis", "Blades", 1.f});
  v.push_back({"blade_section", PType::Curve, "Section", "Blades", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,-1,1|1;0,0,0,0;1,0,0,0"});
  v.push_back({"blade_section_height", PType::Random, "Section height", "Blades", 0.2f, 0.f, 2.f});
  v.push_back({"blade_profile", PType::Curve, "Profile", "Blades", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0.3,0,0;0.4,1,0,0;1,0,0,0"});
  v.push_back({"blade_pinching", PType::Random, "Pinching", "Blades", 0.f, 0.f, 1.f});
  v.push_back({"blade_follow_axis", PType::Bool, "Follow axis", "Blades", 1.f});
  // Meshing
  v.push_back({"mesh_boost", PType::Float, "Boost", "Meshing", 0.f, -3.f, 3.f});
  v.push_back({"normal_mode", PType::Choice, "Normal computation", "Meshing", 0.f, 0.f, 0.f, 0.f, {"Parametric", "Geometric"}});
  v.push_back({"double_sided", PType::Bool, "Double sided", "Meshing", 0.f});
  v.push_back({"axial_mode", PType::Choice, "Axial subdivision mode", "Meshing", 1.f, 0.f, 0.f, 0.f, {"Fixed value", "By length unit", "Curve threshold"}});
  v.push_back({"axial_number", PType::Float, "Axial subdivisions", "Meshing", 4.f, 1.f, 200.f});
  v.push_back({"axial_density", PType::Curve, "Axial density", "Meshing", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"axial_adaptive", PType::Bool, "Adaptiveness", "Meshing", 1.f});
  v.push_back({"axial_min", PType::Int, "Minimum axial", "Meshing", 3.f, 1.f, 64.f});
  v.push_back({"radial_mode", PType::Choice, "Radial subdivision mode", "Meshing", 1.f, 0.f, 0.f, 0.f, {"Fixed value", "By radius unit", "Curve threshold"}});
  v.push_back({"radial_number", PType::Float, "Radial subdivisions", "Meshing", 24.f, 3.f, 128.f});
  v.push_back({"radial_symmetry", PType::Int, "Symmetry factor", "Meshing", 1.f, 1.f, 12.f});
  v.push_back({"radial_min", PType::Int, "Minimum radial", "Meshing", 5.f, 3.f, 64.f});
  v.push_back({"blend_min_subdiv", PType::Int, "Blending: minimum subdivisions", "Meshing", 2.f, 0.f, 16.f});
  v.push_back({"cap_subdiv", PType::Int, "Cap radial subdivisions", "Meshing", 3.f, 1.f, 32.f});
  v.push_back({"cap_disp_transition", PType::Curve, "Cap body displacement transition", "Meshing", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,0,0,0"});
  v.push_back({"blade_subdiv", PType::Int, "Blade radial subdivisions", "Meshing", 3.f, 1.f, 32.f});
  // Influences
  v.push_back({"perturb_strength", PType::Random, "Axis perturbation: strength", "Influences", 0.f, 0.f, 2.f});
  v.push_back({"perturb_planar", PType::Float, "Axis perturbation: make planar", "Influences", 0.f, -1.f, 1.f});
  v.push_back({"perturb_frequency", PType::Random, "Axis perturbation: frequency", "Influences", 2.f, 0.1f, 40.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"perturb_keep_tip", PType::Bool, "Axis perturbation: keep tip", "Influences", 0.f});
  v.push_back({"perturb_smooth_start", PType::Bool, "Axis perturbation: smooth start", "Influences", 1.f});
  v.push_back({"perturb_apply", PType::Choice, "Axis perturbation: apply", "Influences", 1.f, 0.f, 0.f, 0.f, {"Before biases", "After biases"}});
  v.push_back({"wind_flexibility", PType::Random, "Wind sensitivity: flexibility", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"wind_blade_influence", PType::Random, "Wind sensitivity: influence of blades", "Influences", 0.5f, 0.f, 2.f});
  v.push_back({"breeze_strength", PType::Random, "Ambient motion: strength", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"breeze_override", PType::Bool, "Ambient motion: override breeze settings", "Influences", 0.f});
  v.push_back({"blade_breeze_amplitude", PType::Random, "Ambient motion of blades: amplitude", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"blade_breeze_frequency", PType::Random, "Ambient motion of blades: frequency", "Influences", 1.f, 0.f, 8.f});
  v.push_back({"gust_gravity", PType::Random, "Wind and gust: gravity", "Influences", 1.f, -2.f, 4.f});
  v.push_back({"gust_wind", PType::Random, "Wind and gust: wind", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"bone_boost", PType::Float, "Wind and gust: bone boost", "Influences", 0.f, -3.f, 3.f});
  v.push_back({"blade_flexibility", PType::Random, "Blades wind and gust: flexibility", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"blade_gravity", PType::Random, "Blades wind and gust: gravity", "Influences", 1.f, -2.f, 4.f});
  v.push_back({"blade_wind", PType::Random, "Blades wind and gust: wind", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"global_bias_strength", PType::Random, "Global biases: strength", "Influences", 1.f, 0.f, 4.f});
  v.push_back({"bias_type", PType::Choice, "Local bias: type", "Local bias", 0.f, 0.f, 0.f, 0.f,
               {"None", "Direction", "Conic", "Attractor", "Axis repeller", "Swirl", "Curl", "Twist"}});
  v.push_back({"bias_strength", PType::Random, "Strength", "Local bias", 0.5f, -4.f, 4.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"bias_dir_x", PType::Float, "Direction X", "Local bias", 0.f, -100.f, 100.f});
  v.push_back({"bias_dir_y", PType::Float, "Direction Y", "Local bias", 1.f, -100.f, 100.f});
  v.push_back({"bias_dir_z", PType::Float, "Direction Z", "Local bias", 0.f, -100.f, 100.f});
  v.push_back({"bias_local", PType::Bool, "Local coordinates", "Local bias", 0.f});
  v.push_back({"bias_length_agnostic", PType::Bool, "Length agnostic", "Local bias", 0.f});
  v.push_back({"bias_apply", PType::Choice, "Apply", "Local bias", 1.f, 0.f, 0.f, 0.f,
               {"Only to free floating parts", "To entire segment", "Only to parts growing on object"}});
  v.push_back({"bias_repeller", PType::Bool, "Conic: repeller", "Local bias", 0.f});
  v.push_back({"bias_cone_angle", PType::Float, "Conic: cone angle (deg)", "Local bias", 90.f, 0.f, 180.f});
  v.push_back({"bias_base_length", PType::Float, "Conic: base length (m)", "Local bias", 1.f, 0.01f, 100.f});
  v.push_back({"bias_origin_x", PType::Float, "Origin X (m)", "Local bias", 0.f, -1000.f, 1000.f});
  v.push_back({"bias_origin_y", PType::Float, "Origin Y (m)", "Local bias", 0.f, -1000.f, 1000.f});
  v.push_back({"bias_origin_z", PType::Float, "Origin Z (m)", "Local bias", 0.f, -1000.f, 1000.f});
  v.push_back({"twist_planar", PType::Bool, "Twist: planar", "Local bias", 0.f});
  v.push_back({"twist_symmetry", PType::Int, "Twist: symmetry order", "Local bias", 2.f, 1.f, 12.f});
  v.push_back({"twist_target", PType::Float, "Twist: target angle (deg)", "Local bias", 0.f, -180.f, 180.f});
  v.push_back({"interact_with", PType::Choice, "Interact with", "Interactions", 0.f, 0.f, 0.f, 0.f, {"None", "Ground", "Object", "Ground and Object"}});
  v.push_back({"prune_after", PType::Float, "Prune after (m)", "Interactions", 0.f, 0.f, 100.f});
  v.push_back({"interact_perturb_ratio", PType::Float, "Perturbation ratio", "Interactions", 1.f, 0.f, 4.f});
  // Materials
  v.push_back({"mat_distribution", PType::Choice, "Body material distribution", "Materials", 0.f, 0.f, 0.f, 0.f,
               {"Unique per segment", "Unique per section", "Sequential"}});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Materials", 0.f});
  v.push_back({"disable_blade_baking", PType::Bool, "Disable blade texture baking", "Materials", 0.f});
  v.push_back({"uv_mode", PType::Choice, "Body UV: mapping mode", "Materials", 0.f, 0.f, 0.f, 0.f, {"Standard", "Parametric U", "Parametric UV"}});
  v.push_back({"u_tile", PType::Float, "Body UV: U tile", "Materials", 1.f, 0.01f, 64.f});
  v.push_back({"v_tile", PType::Float, "Body UV: V tile", "Materials", 1.f, 0.01f, 64.f});
  v.push_back({"u_offset", PType::Float, "Body UV: U offset", "Materials", 0.f, -4.f, 4.f});
  v.push_back({"v_offset", PType::Float, "Body UV: V offset", "Materials", 0.f, -4.f, 4.f});
  v.push_back({"uv_twist", PType::Float, "Body UV: twist", "Materials", 0.f, -4.f, 4.f});
  v.push_back({"keep_aspect", PType::Bool, "Body UV: keep aspect ratio", "Materials", 1.f});
  v.push_back({"map_v_from_end", PType::Bool, "Body UV: map V from end", "Materials", 0.f});
  v.push_back({"extend_top_cap", PType::Bool, "Body UV: extend to top cap", "Materials", 0.f});
  v.push_back({"extend_bottom_cap", PType::Bool, "Body UV: extend to bottom cap", "Materials", 0.f});
  v.push_back({"detail_uv_mode", PType::Choice, "Detail UV: mapping mode", "Materials", 2.f, 0.f, 0.f, 0.f, {"Standard", "Parametric U", "Parametric UV"}});
  v.push_back({"detail_u_tile", PType::Float, "Detail UV: U tile", "Materials", 1.f, 0.01f, 64.f});
  v.push_back({"detail_v_tile", PType::Float, "Detail UV: V tile", "Materials", 1.f, 0.01f, 64.f});
  v.push_back({"rdisp_source", PType::Choice, "Radial displacement: source", "Displacement", 0.f, 0.f, 0.f, 0.f,
               {"None", "Noise", "Bark ridges", "Knots", "Field"}});
  v.push_back({"rdisp_relative", PType::Bool, "Radial displacement: relative", "Displacement", 1.f});
  v.push_back({"rdisp_amount", PType::Random, "Radial displacement: amount", "Displacement", 0.f, 0.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"rdisp_offset", PType::Float, "Radial displacement: offset", "Displacement", 0.f, -1.f, 1.f});
  v.push_back({"rdisp_scale", PType::Float, "Radial displacement: scale", "Displacement", 1.f, 0.05f, 20.f});
  v.push_back({"disp3_amount", PType::Random, "3D displacement: amount", "Displacement", 0.f, 0.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"disp3_offset", PType::Float, "3D displacement: offset", "Displacement", 0.f, -1.f, 1.f});
  v.push_back({"disp3_scale", PType::Float, "3D displacement: scale", "Displacement", 1.f, 0.05f, 20.f});
  v.push_back({"cap_mat_angle", PType::Float, "Cap: UV angle (deg)", "Cap material", 0.f, -180.f, 180.f});
  v.push_back({"cap_mat_scale", PType::Float, "Cap: UV scale", "Cap material", 1.f, 0.01f, 16.f});
  v.push_back({"cap_border", PType::Float, "Cap: border", "Cap material", 0.f, 0.f, 1.f});
  v.push_back({"cap_disp_amount", PType::Random, "Cap: displacement amount", "Cap material", 0.f, 0.f, 2.f});
  v.push_back({"cap_disp_offset", PType::Float, "Cap: displacement offset", "Cap material", 0.f, -1.f, 1.f});
  v.push_back({"blade_dist_mode", PType::Choice, "Blades: distribution mode", "Blade material", 1.f, 0.f, 0.f, 0.f,
               {"Unique per segment", "Unique per blade", "Sequential AAABBB", "Sequential ABCABC", "Sequential ABCA"}});
  v.push_back({"blade_uv_mode", PType::Choice, "Blades: mapping mode", "Blade material", 2.f, 0.f, 0.f, 0.f, {"Standard", "Parametric U", "Parametric UV"}});
  v.push_back({"blade_uv_extension", PType::Bool, "Blades: UV extension", "Blade material", 0.f});
  v.push_back({"blade_map_v_from_end", PType::Bool, "Blades: map V from end", "Blade material", 0.f});
  v.push_back({"blade_u_tile", PType::Float, "Blades: U tile", "Blade material", 1.f, 0.01f, 64.f});
  v.push_back({"blade_v_tile", PType::Float, "Blades: V tile", "Blade material", 1.f, 0.01f, 64.f});
  v.push_back({"blade_u_offset", PType::Float, "Blades: U offset", "Blade material", 0.f, -4.f, 4.f});
  v.push_back({"blade_v_offset", PType::Float, "Blades: V offset", "Blade material", 0.f, -4.f, 4.f});
  v.push_back({"blade_disp_amount", PType::Random, "Blades: displacement amount", "Blade material", 0.f, 0.f, 2.f});
  v.push_back({"blade_disp_radial", PType::Random, "Blades: radial amount", "Blade material", 0.f, 0.f, 2.f});
  v.push_back({"blade_disp_offset", PType::Float, "Blades: displacement offset", "Blade material", 0.f, -1.f, 1.f});
  // Transform, LOD, sap
  transform_params(v, true, false);
  lod_params(v, true);
  v.push_back({"sap_inherit", PType::Bool, "Inherit sap", "Level of detail", 1.f});
  attachment_params(v);
  season_params(v);
  return v;
}

} // namespace plant
} // namespace gpx
