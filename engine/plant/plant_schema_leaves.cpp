// Geekatplay TerraForge - the leaf-like parts' parameter tables
// (plant_schema.hpp): Leaf, CutoutLeaf, Warpboard, Object.
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace gpx {
namespace plant {

const std::vector<ParamDef> &leaf_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Leaf";
  v.push_back({"orientation", PType::Choice, "Orientation", G, 0.f, 0.f, 0.f, 0.f,
               {"Fixed, user defined", "Fixed, facing current camera", "Fixed, outwards", "Fixed, facing X axis", "Dynamic, facing camera"}});
  v.push_back({"mesh_kind", PType::Choice, "Mesh", G, 0.f, 0.f, 0.f, 0.f,
               {"Single plane", "Two crossed planes", "Three crossed planes", "Diamond plane"}});
  v.push_back({"length", PType::Random, "Length (m)", G, 0.08f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"width", PType::Random, "Width (m)", G, 0.05f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"subdiv_w", PType::Int, "Subdivision boost W", G, 0.f, 0.f, 3.f});
  v.push_back({"subdiv_h", PType::Int, "Subdivision boost H", G, 1.f, 0.f, 3.f});
  v.push_back({"midrib_angle", PType::Random, "Mid rib angle (deg)", G, 0.f, -90.f, 90.f});
  v.push_back({"curvature_w", PType::Random, "Curvature W (deg)", G, 0.f, -90.f, 90.f});
  v.push_back({"curvature_h", PType::Random, "Curvature H (deg)", G, 15.f, -90.f, 90.f});
  v.push_back({"normals", PType::Choice, "Normals", G, 0.f, 0.f, 0.f, 0.f, {"Geometric", "Leaf dir", "Plant sphere", "Local sphere"}});
  v.push_back({"normal_hazard", PType::Float, "Normal hazard", G, 0.f, 0.f, 1.f});
  v.push_back({"rib_orientation", PType::Choice, "Basic rib orientation", G, 1.f, 0.f, 0.f, 0.f, {"Horizontal", "Vertical"}});
  v.push_back({"uv_mode", PType::Choice, "UV mode", G, 0.f, 0.f, 0.f, 0.f, {"Normal UV mode", "Diamond UV mode"}});
  v.push_back({"hook", PType::Vec2, "Hooking point (u,v)", G, 0.5f, 0.f, 1.f, 0.f});
  v.push_back({"mat_distribution", PType::Choice, "Material distribution mode", "Material", 1.f, 0.f, 0.f, 0.f,
               {"Per plane", "Per billboard", "Per material groups"}});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Material", 0.f});
  v.push_back({"shift_hue", PType::Random, "Colour shift: hue (turns)", "Colour shift", 0.f, -0.5f, 0.5f, 0.02f});
  v.push_back({"shift_lum", PType::Random, "Colour shift: luminosity", "Colour shift", 0.f, -1.f, 1.f, 0.08f});
  v.push_back({"shift_sat", PType::Random, "Colour shift: saturation", "Colour shift", 0.f, -1.f, 1.f, 0.05f});
  breeze_params(v, true);
  transform_params(v, false, false);
  v.push_back({"global_axial", PType::Random, "Global offset: axial", "Transform", 0.f, -1.f, 1.f});
  v.push_back({"global_radial", PType::Random, "Global offset: radial", "Transform", 0.f, -1.f, 1.f});
  lod_params(v, false);
  attachment_params(v);
  season_params(v);
  return v;
}

const std::vector<ParamDef> &cutout_leaf_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Geometry";
  v.push_back({"planes", PType::Choice, "Mode", G, 0.f, 0.f, 0.f, 0.f, {"Single plane", "Two crossed planes", "Three crossed planes"}});
  v.push_back({"length", PType::Random, "Length (as scale, m)", G, 0.1f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"width_tweak", PType::Random, "Width tweak", G, 1.f, 0.1f, 4.f});
  v.push_back({"axis_bend", PType::Random, "Axis bend (deg)", G, 0.f, -180.f, 180.f});
  v.push_back({"axis_curve", PType::Curve, "Axis (sideways by primal)", G, 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,-1,1|1;0,0,0,0;1,0,0,0"});
  v.push_back({"twist", PType::Random, "Twist (turns)", G, 0.f, -2.f, 2.f});
  v.push_back({"midrib_angle", PType::Random, "Midrib angle (deg)", G, 0.f, -90.f, 90.f});
  v.push_back({"midrib_curve", PType::Curve, "Midrib angle along the leaf", G, 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"smooth_rib", PType::Bool, "Smooth rib normals", G, 1.f});
  v.push_back({"lateral_profile", PType::Curve, "Lateral profile", G, 0.f, 0.f, 1.f, 0.f, {}, "d:-0.5,0.5,-0.5,0.5|1;-0.5,0,0,0;0,0,0,0;0.5,0,0,0"});
  v.push_back({"gravitropism", PType::Random, "Gravitropism", "Influences", 0.f, -2.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"grid_boost_x", PType::Int, "Grid resolution boost X", "Meshing", 0.f, -8.f, 8.f});
  v.push_back({"grid_boost_y", PType::Int, "Grid resolution boost Y", "Meshing", 0.f, -8.f, 8.f});
  v.push_back({"lod_affects_grid", PType::Bool, "LOD affects grid boost", "Meshing", 1.f});
  v.push_back({"catmull", PType::Int, "Catmull-Clark resolution boost", "Meshing", 0.f, -8.f, 8.f});
  v.push_back({"lod_affects_catmull", PType::Bool, "LOD affects Catmull-Clark boost", "Meshing", 1.f});
  v.push_back({"mixed_quads", PType::Bool, "Mixed quad triangle", "Meshing", 1.f});
  v.push_back({"grid_x", PType::Float, "Subdivision scale X", "Meshing", 0.25f, 0.02f, 2.f});
  v.push_back({"grid_y", PType::Float, "Subdivision scale Y", "Meshing", 0.25f, 0.02f, 2.f});
  v.push_back({"cutout_index", PType::Int, "Cut-out", "Material", 0.f, 0.f, 15.f});
  v.push_back({"mat_distribution", PType::Choice, "Material distribution mode", "Material", 1.f, 0.f, 0.f, 0.f,
               {"Per plane", "Per billboard", "Per material groups"}});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Material", 0.f});
  v.push_back({"shift_hue", PType::Random, "Colour shift: hue (turns)", "Colour shift", 0.f, -0.5f, 0.5f, 0.02f});
  v.push_back({"shift_lum", PType::Random, "Colour shift: luminosity", "Colour shift", 0.f, -1.f, 1.f, 0.08f});
  v.push_back({"shift_sat", PType::Random, "Colour shift: saturation", "Colour shift", 0.f, -1.f, 1.f, 0.05f});
  v.push_back({"breeze_strength", PType::Random, "Breeze strength", "Ambient motion", 1.f, 0.f, 4.f});
  v.push_back({"breeze_flexibility", PType::Curve, "Flexibility distribution", "Ambient motion", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,0,0,0;1,1,0,0"});
  v.push_back({"breeze_override", PType::Bool, "Override breeze response", "Ambient motion", 0.f});
  transform_params(v, false, false);
  lod_params(v, true);
  attachment_params(v);
  season_params(v);
  return v;
}

const std::vector<ParamDef> &warpboard_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Shape";
  v.push_back({"curl", PType::Random, "Curl", G, 0.2f, -2.f, 2.f});
  v.push_back({"flexibility", PType::Random, "Flexibility", G, 0.3f, -2.f, 2.f});
  v.push_back({"shape_randomness", PType::Random, "Randomness", G, 0.2f, 0.f, 1.f});
  v.push_back({"width", PType::Random, "Width (m)", G, 0.05f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"length", PType::Random, "Length (m)", G, 0.1f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Material", 0.f});
  v.push_back({"mesh_boost", PType::Float, "Boost", "Mesh resolution", 0.f, -3.f, 3.f});
  v.push_back({"min_subdiv", PType::Int, "Minimum subdivisions", "Mesh resolution", 2.f, 1.f, 32.f});
  transform_params(v, true, true);
  lod_params(v, false);
  attachment_params(v);
  season_params(v);
  return v;
}

const std::vector<ParamDef> &object_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"file", PType::Filename, "Object", "Object", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"double_sided", PType::Bool, "Double sided", "Object", 0.f});
  v.push_back({"size_m", PType::Random, "Size (m)", "Object", 0.f, 0.f, 100.f});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Material", 0.f});
  transform_params(v, true, true);
  lod_params(v, false);
  attachment_params(v);
  season_params(v);
  return v;
}

} // namespace plant
} // namespace gpx
