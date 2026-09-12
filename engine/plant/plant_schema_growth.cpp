// Geekatplay TerraForge - the growth simulation, the loop, the child
// selector and the two field nodes: their parameter tables, and the
// params() dispatcher over every kind (plant_schema.hpp).
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace gpx {
namespace plant {

const std::vector<ParamDef> &species_params();
const std::vector<ParamDef> &bias_params();
const std::vector<ParamDef> &material_params();
const std::vector<ParamDef> &segment_params();
const std::vector<ParamDef> &leaf_params();
const std::vector<ParamDef> &cutout_leaf_params();
const std::vector<ParamDef> &warpboard_params();
const std::vector<ParamDef> &object_params();
const std::vector<ParamDef> &urchin_params();
const std::vector<ParamDef> &hydra_params();
const std::vector<ParamDef> &ball_params();
const std::vector<ParamDef> &flower_params();

const std::vector<ParamDef> &growth_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"iterations", PType::Random, "Iterations", "Growth", 12.f, 1.f, 60.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"growth_input", PType::Choice, "Input", "Growth", 0.f, 0.f, 0.f, 0.f, {"From the root", "Over the parent segment"}});
  v.push_back({"favor_up", PType::Random, "Bud placement: favor up", "Growth", 0.3f, -1.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"favor_sides", PType::Random, "Bud placement: favor sides", "Growth", 0.f, -1.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"internode", PType::Float, "Internode length (m)", "Geometry", 0.2f, 0.001f, 5.f, 0.f, {}, nullptr, nullptr, true});
  v.push_back({"bud_radius", PType::Float, "Bud radius (m)", "Geometry", 0.006f, 0.0001f, 1.f, 0.f, {}, nullptr, nullptr, true});
  v.push_back({"angle_with_parent", PType::Random, "Angle with parent (deg)", "Geometry", 40.f, 0.f, 180.f, 8.f, {}, nullptr, nullptr, false, true});
  v.push_back({"phyllotaxis", PType::Random, "Phyllotaxis angle (deg)", "Geometry", 137.5f, -180.f, 180.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"angular_noise", PType::Random, "Angular noise (deg)", "Geometry", 10.f, 0.f, 180.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"growth_speed", PType::Random, "Growth speed", "Geometry", 5.f, 1.f, 100.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"apical", PType::Random, "Apical", "Geometry", 0.03f, -0.1f, 0.1f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"decay", PType::Random, "Decay", "Geometry", 0.5f, 0.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"shadow_strength", PType::Float, "Shadowing: strength", "Shadowing", 0.5f, 0.f, 4.f});
  v.push_back({"shadow_size", PType::Float, "Shadowing: shadow size (m)", "Shadowing", 0.3f, 0.01f, 10.f});
  v.push_back({"light_influence", PType::Random, "Light influence", "Shadowing", 1.f, 0.f, 10.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"shedding", PType::Random, "Shedding threshold", "Shadowing", 0.1f, 0.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"gravitropism_influence", PType::Random, "Gravitropism influence", "Tropisms", 0.2f, 0.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"gravitropism_angle", PType::Random, "Gravitropism angle (deg)", "Tropisms", 40.f, 0.f, 180.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"phototropism", PType::Random, "Phototropism influence", "Tropisms", 0.3f, 0.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"vertical_trunk", PType::Bool, "Vertical trunk", "Tropisms", 1.f});
  v.push_back({"bottom_cut_mode", PType::Choice, "Bottom cut: parameter type", "Cuts", 1.f, 0.f, 0.f, 0.f, {"Absolute", "Relative to height"}});
  v.push_back({"bottom_cut_rank", PType::Int, "Bottom cut: rank", "Cuts", 0.f, 0.f, 6.f});
  v.push_back({"bottom_cut_height", PType::Random, "Bottom cut: height", "Cuts", 0.f, 0.f, 50.f});
  v.push_back({"profile_cut_enable", PType::Bool, "Profile cut", "Cuts", 0.f});
  v.push_back({"profile_cut_mode", PType::Choice, "Profile cut: parameter type", "Cuts", 1.f, 0.f, 0.f, 0.f, {"Absolute", "Relative to height"}});
  v.push_back({"profile_cut", PType::Curve, "Profile cut: profile", "Cuts", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0.3,0,0;0.5,1,0,0;1,0.2,0,0"});
  v.push_back({"profile_bottom", PType::Random, "Profile cut: bottom height", "Cuts", 0.2f, 0.f, 50.f});
  v.push_back({"profile_top", PType::Random, "Profile cut: top height", "Cuts", 1.f, 0.f, 50.f});
  v.push_back({"profile_radius", PType::Random, "Profile cut: radius", "Cuts", 0.6f, 0.f, 50.f});
  v.push_back({"axial_subdiv", PType::Float, "Axial subdivisions (per m)", "Meshing", 6.f, 0.5f, 200.f});
  v.push_back({"angular_subdiv", PType::Float, "Angular subdivisions (per m)", "Meshing", 12.f, 1.f, 400.f});
  v.push_back({"mesh_boost", PType::Float, "Boost", "Meshing", 0.f, -3.f, 3.f});
  v.push_back({"flexibility", PType::Random, "Wind and gust: flexibility", "Wind", 1.f, 0.f, 4.f});
  v.push_back({"gravity", PType::Random, "Wind and gust: gravity", "Wind", 1.f, -2.f, 4.f});
  v.push_back({"wind", PType::Random, "Wind and gust: wind", "Wind", 1.f, 0.f, 4.f});
  v.push_back({"bone_boost", PType::Float, "Bone boost", "Wind", 0.f, -3.f, 3.f});
  v.push_back({"breeze_strength", PType::Random, "Ambient motion: strength", "Wind", 1.f, 0.f, 4.f});
  v.push_back({"breeze_override", PType::Bool, "Override breeze response", "Wind", 0.f});
  v.push_back({"mat_distribution", PType::Choice, "Material distribution", "Material", 0.f, 0.f, 0.f, 0.f, {"Random", "First"}});
  lod_params(v, false);
  attachment_params(v);
  return v;
}

const std::vector<ParamDef> &repeat_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"iterations", PType::Random, "Iterations", "Loop", 3.f, 1.f, 12.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"tail_every", PType::Bool, "Tail on every iteration", "Loop", 0.f});
  attachment_params(v);
  return v;
}

const std::vector<ParamDef> &child_select_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"mode", PType::Choice, "Mode", "Selection", 0.f, 0.f, 0.f, 0.f,
               {"Random children", "Spread children", "Sequence children", "Select 2 children", "LOD selector", "Random inputs"}});
  v.push_back({"sequence", PType::Text, "Sequence", "Selection", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "ABC"});
  v.push_back({"level", PType::Random, "Level", "Selection", 0.5f, -10.f, 10.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"value", PType::Random, "Value", "Selection", 0.f, -10.f, 10.f, 0.f, {}, nullptr, nullptr, false, true});
  attachment_params(v);
  return v;
}

const std::vector<ParamDef> &variable_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"which", PType::Choice, "Variable", "Plant", 0.f, 0.f, 0.f, 0.f,
               {"Primal", "Section angle", "Radial", "Age", "Maturity", "Health", "Season", "Time",
                "Hierarchy depth", "Distance to root", "Height fraction", "Iteration",
                "Position on parent", "Parent radius", "Parent length", "Remaining length",
                "Parent tilt", "Length", "Radius", "Horizontropic azimuth", "Random per primitive",
                "Random per plant", "LOD level", "LOD max", "Pruned", "Pruning ratio"}});
  v.push_back({"scale", PType::Float, "Scale", "Plant", 1.f, -100.f, 100.f});
  v.push_back({"offset", PType::Float, "Offset", "Plant", 0.f, -100.f, 100.f});
  return v;
}

const std::vector<ParamDef> &vector_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"which", PType::Choice, "Vector", "Plant", 0.f, 0.f, 0.f, 0.f,
               {"Primitive position", "Primitive direction", "Axis direction", "Radial direction", "Parent direction"}});
  return v;
}

const std::vector<ParamDef> &params(Kind k) {
  switch (k) {
    case Kind::Species: return species_params();
    case Kind::Segment: return segment_params();
    case Kind::Leaf: return leaf_params();
    case Kind::CutoutLeaf: return cutout_leaf_params();
    case Kind::Warpboard: return warpboard_params();
    case Kind::Object: return object_params();
    case Kind::Urchin: return urchin_params();
    case Kind::Hydra: return hydra_params();
    case Kind::Ball: return ball_params();
    case Kind::Flower: return flower_params();
    case Kind::Growth: return growth_params();
    case Kind::Repeat: return repeat_params();
    case Kind::ChildSelect: return child_select_params();
    case Kind::Bias: return bias_params();
    case Kind::Material: return material_params();
    case Kind::Variable: return variable_params();
    case Kind::Vector: return vector_params();
    default: break;
  }
  static const std::vector<ParamDef> none;
  return none;
}

} // namespace plant
} // namespace gpx
