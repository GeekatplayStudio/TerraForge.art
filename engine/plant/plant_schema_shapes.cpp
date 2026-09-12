// Geekatplay TerraForge - the round parts' parameter tables
// (plant_schema.hpp): Urchin, Hydra, Ball, Flower.
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace gpx {
namespace plant {

const std::vector<ParamDef> &urchin_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Geometry";
  v.push_back({"radius", PType::Random, "Radius (m)", G, 0.1f, 0.001f, 20.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"skin", PType::Choice, "Skin", G, 0.f, 0.f, 0.f, 0.f, {"Standard", "None"}});
  v.push_back({"normal_mode", PType::Choice, "Normal computation", G, 0.f, 0.f, 0.f, 0.f, {"Parametric", "Geometric"}});
  v.push_back({"double_sided", PType::Bool, "Double sided", G, 0.f});
  v.push_back({"profile", PType::Curve, "Profile (radius by height)", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0,0,0;0.5,1,0,0;1,0,0,0"});
  v.push_back({"section", PType::Curve, "Section (radius by angle)", G, 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"pivot_offset", PType::Random, "Pivot offset", G, 0.f, 0.f, 1.f});
  v.push_back({"mesh_boost", PType::Float, "Boost", "Meshing", 0.f, -3.f, 3.f});
  v.push_back({"subdiv_mode", PType::Choice, "Subdivision mode", "Meshing", 0.f, 0.f, 0.f, 0.f, {"Parametric", "By unit length/radius"}});
  v.push_back({"axial_subdiv", PType::Float, "Axial subdivisions", "Meshing", 12.f, 2.f, 200.f});
  v.push_back({"axial_min", PType::Int, "Minimum axial subdivisions", "Meshing", 4.f, 2.f, 64.f});
  v.push_back({"axial_density", PType::Curve, "Axial subdivision density", "Meshing", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"angular_subdiv", PType::Float, "Angular subdivisions", "Meshing", 16.f, 3.f, 200.f});
  v.push_back({"angular_min", PType::Int, "Minimum angular subdivisions", "Meshing", 6.f, 3.f, 64.f});
  v.push_back({"adaptive", PType::Bool, "Adaptiveness", "Meshing", 1.f});
  v.push_back({"uv_parametric", PType::Bool, "Parametric UV", "Material", 1.f});
  v.push_back({"u_tile", PType::Float, "U tile", "Material", 1.f, 0.01f, 64.f});
  v.push_back({"v_tile", PType::Float, "V tile", "Material", 1.f, 0.01f, 64.f});
  v.push_back({"u_offset", PType::Float, "U offset", "Material", 0.f, -4.f, 4.f});
  v.push_back({"v_offset", PType::Float, "V offset", "Material", 0.f, -4.f, 4.f});
  v.push_back({"disp_source", PType::Choice, "Displacement source", "Material", 0.f, 0.f, 0.f, 0.f, {"None", "Noise", "Bumps", "Field"}});
  v.push_back({"disp_relative", PType::Bool, "Displacement relative", "Material", 1.f});
  v.push_back({"disp_amount", PType::Random, "Displacement amount", "Material", 0.f, 0.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"disp_offset", PType::Float, "Displacement offset", "Material", 0.f, -1.f, 1.f});
  v.push_back({"child_count", PType::Random, "Number", "Distribution", 30.f, 0.f, 2000.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"child_start", PType::Random, "Start", "Distribution", 0.f, 0.f, 1.f});
  v.push_back({"child_end", PType::Random, "End", "Distribution", 1.f, 0.f, 1.f});
  v.push_back({"child_density", PType::Curve, "Density", "Distribution", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"child_soft", PType::Choice, "Soft insert", "Distribution", 0.f, 0.f, 0.f, 0.f,
               {"None, use rounded number", "None, use rounded up number", "Scale down fractional part", "Random"}});
  v.push_back({"child_orthogonal", PType::Bool, "Orthogonal", "Distribution", 1.f});
  v.push_back({"child_angle", PType::Random, "Angle (deg)", "Distribution", 0.f, -90.f, 90.f});
  v.push_back({"child_scale", PType::Random, "Scale", "Distribution", 1.f, 0.01f, 10.f});
  v.push_back({"child_scale_shift", PType::Random, "Scale shift", "Distribution", 0.f, 0.f, 1.f});
  v.push_back({"child_phi", PType::Random, "Phi (deg)", "Distribution", 137.5f, -360.f, 360.f});
  v.push_back({"wind_flexibility", PType::Random, "Wind sensitivity: flexibility", "Transform", 1.f, 0.f, 4.f});
  v.push_back({"breeze_strength", PType::Random, "Ambient motion: strength", "Transform", 1.f, 0.f, 4.f});
  v.push_back({"breeze_override", PType::Bool, "Ambient motion: override breeze settings", "Transform", 0.f});
  v.push_back({"gust_gravity", PType::Random, "Wind and gust: gravity", "Transform", 1.f, -2.f, 4.f});
  v.push_back({"gust_wind", PType::Random, "Wind and gust: wind", "Transform", 1.f, 0.f, 4.f});
  transform_params(v, true, false);
  lod_params(v, false);
  attachment_params(v);
  season_params(v);
  return v;
}

const std::vector<ParamDef> &hydra_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Children";
  v.push_back({"child_count", PType::Random, "Number", G, 8.f, 0.f, 500.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"radius", PType::Random, "Radius (m)", G, 0.1f, 0.f, 50.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"child_scale", PType::Random, "Scale", G, 1.f, 0.01f, 10.f});
  v.push_back({"distribution", PType::Curve, "Distribution around the circle", G, 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"angle1", PType::Random, "Angle 1: orthogonal to the plane (deg)", G, 45.f, -180.f, 180.f});
  v.push_back({"angle2", PType::Random, "Angle 2: tangential (deg)", G, 0.f, -180.f, 180.f});
  v.push_back({"angle3", PType::Random, "Angle 3: around the axis (deg)", G, 0.f, -180.f, 180.f});
  v.push_back({"child_soft", PType::Choice, "Soft insert", G, 0.f, 0.f, 0.f, 0.f,
               {"None, use rounded number", "Scale down fractional part", "Random"}});
  transform_params(v, true, false);
  lod_params(v, false);
  attachment_params(v);
  return v;
}

const std::vector<ParamDef> &ball_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"radius", PType::Random, "Radius (m)", "Ball", 0.03f, 0.0005f, 20.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"pivot_offset", PType::Random, "Pivot offset", "Ball", 0.f, 0.f, 1.f});
  v.push_back({"squash", PType::Random, "Squash (height / width)", "Ball", 1.f, 0.2f, 3.f});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Material", 0.f});
  v.push_back({"mesh_boost", PType::Float, "Boost", "Mesh resolution", 0.f, -3.f, 3.f});
  v.push_back({"min_subdiv", PType::Int, "Minimum subdivisions", "Mesh resolution", 6.f, 3.f, 64.f});
  transform_params(v, true, true);
  lod_params(v, false);
  attachment_params(v);
  season_params(v);
  return v;
}

const std::vector<ParamDef> &flower_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  const char *G = "Geometry";
  v.push_back({"length", PType::Random, "Length (m)", G, 0.03f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"radius", PType::Random, "Radius (m)", G, 0.03f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true, true});
  v.push_back({"plug_radius", PType::Random, "Plug radius (m)", G, 0.003f, 0.f, 1.f});
  v.push_back({"plug_modify", PType::Bool, "Modify normals and displacement", G, 1.f});
  v.push_back({"plug_influence", PType::Random, "Plug influence (m)", G, 0.01f, 0.f, 1.f});
  v.push_back({"axis_bend", PType::Random, "Axis bend (deg)", G, 0.f, -180.f, 180.f});
  v.push_back({"twist", PType::Random, "Twist (turns)", G, 0.f, -4.f, 4.f});
  v.push_back({"axis_influence", PType::Float, "Axis influence on sections", G, 0.5f, 0.f, 1.f});
  v.push_back({"profile_mode", PType::Choice, "Profile", G, 0.f, 0.f, 0.f, 0.f,
               {"Lobed (inner / outer)", "Cylinder", "Disc", "Cup", "Bell", "Trumpet", "Custom"}});
  v.push_back({"inner_profile", PType::Curve, "Inner profile (radius by height)", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0.1,0,0;0.5,0.6,0,0;1,0.7,0,0"});
  v.push_back({"outer_profile", PType::Curve, "Outer profile (radius by height)", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0.1,0,0;0.5,0.8,0,0;1,1,0,0"});
  v.push_back({"inout_filter", PType::Curve, "In/Out filter", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0,0,0;0.5,1,0,0;1,0,0,0"});
  v.push_back({"lobes", PType::Int, "Lobes", G, 5.f, 1.f, 64.f});
  v.push_back({"custom_profile", PType::Curve, "Custom profile (radius by height)", G, 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0.2,0,0;1,1,0,0"});
  v.push_back({"disable_baking", PType::Bool, "Disable texture baking", "Material", 0.f});
  v.push_back({"disp_amount", PType::Random, "Displacement amount", "Material", 0.f, 0.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"gravitropism", PType::Random, "Gravitropism", "Influences", 0.f, -2.f, 2.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"mesh_mode", PType::Choice, "Mode", "Meshing", 0.f, 0.f, 0.f, 0.f, {"Parametric", "By unit length"}});
  v.push_back({"geometric_twist", PType::Bool, "Use geometric twist", "Meshing", 1.f});
  v.push_back({"normal_mode", PType::Choice, "Normal computation", "Meshing", 1.f, 0.f, 0.f, 0.f, {"Parametric", "Geometric"}});
  v.push_back({"radial_continuity", PType::Bool, "Radial continuity", "Meshing", 1.f});
  v.push_back({"invert_faces", PType::Bool, "Invert front/back", "Meshing", 0.f});
  v.push_back({"axial_subdiv", PType::Float, "Axial subdivisions", "Meshing", 8.f, 1.f, 200.f});
  v.push_back({"axial_min", PType::Int, "Minimum axial subdivisions", "Meshing", 3.f, 1.f, 64.f});
  v.push_back({"symmetry", PType::Int, "Symmetry factor", "Meshing", 1.f, 1.f, 64.f});
  v.push_back({"angular_subdiv", PType::Float, "Angular subdivisions", "Meshing", 32.f, 3.f, 400.f});
  v.push_back({"angular_min", PType::Int, "Minimum angular subdivisions", "Meshing", 8.f, 3.f, 128.f});
  v.push_back({"uv_mode", PType::Choice, "UV mode", "UV handling", 1.f, 0.f, 0.f, 0.f,
               {"Disc - Native", "Disc - Projected", "Cylinder - Native", "Cylinder - Projected"}});
  v.push_back({"uv_geometric_twist", PType::Bool, "Use geometric twist", "UV handling", 1.f});
  v.push_back({"uv_twist", PType::Random, "UV twist (turns)", "UV handling", 0.f, -4.f, 4.f});
  v.push_back({"angular_mapping", PType::Int, "Angular mapping", "UV handling", 1.f, 1.f, 64.f});
  v.push_back({"axial_mapping", PType::Choice, "Axial mapping", "UV handling", 0.f, 0.f, 0.f, 0.f, {"Linear", "Custom"}});
  v.push_back({"axial_map_curve", PType::Curve, "Custom axial mapping", "UV handling", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|0;0,0,0,0;1,1,0,0"});
  transform_params(v, true, true);
  lod_params(v, false);
  attachment_params(v);
  season_params(v);
  return v;
}

} // namespace plant
} // namespace gpx
