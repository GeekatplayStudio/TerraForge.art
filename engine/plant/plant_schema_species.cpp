// Geekatplay TerraForge - the species root, the global bias and the
// material: their parameter tables (plant_schema.hpp).
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace gpx {
namespace plant {

const std::vector<ParamDef> &species_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  // General parameters
  v.push_back({"name", PType::Text, "Species name", "General", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "Plant"});
  v.push_back({"seed", PType::Seed, "Seed", "General", 1.f});
  v.push_back({"max_age", PType::Float, "Max age (years)", "General", 80.f, 1.f, 1000.f});
  v.push_back({"age", PType::Float, "Age (years)", "General", 30.f, 0.f, 1000.f});
  v.push_back({"health", PType::Float, "Health", "General", 1.f, 0.f, 1.f});
  v.push_back({"season", PType::Float, "Season", "General", 0.5f, 0.f, 1.f});
  v.push_back({"time_from_scene", PType::Bool, "Time from the scene", "General", 1.f});
  v.push_back({"gravity", PType::Float, "Gravity strength", "General", 1.f, -2.f, 4.f});
  v.push_back({"scale", PType::Float, "Scale", "General", 1.f, 0.01f, 100.f, 0.f, {}, nullptr, nullptr, true});
  v.push_back({"true_size", PType::Bool, "True dimensions", "General", 1.f});
  // Placement (the driven object; metres, like Primitive)
  v.push_back({"object", PType::Text, "Object name", "Placement", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"x_m", PType::Float, "X (m)", "Placement", 2500.f, -1e7f, 1e7f});
  v.push_back({"y_m", PType::Float, "Y (m)", "Placement", 0.f, -1e6f, 1e6f});
  v.push_back({"z_m", PType::Float, "Z (m)", "Placement", 2500.f, -1e7f, 1e7f});
  v.push_back({"heading", PType::Float, "Heading (deg)", "Placement", 0.f, -180.f, 180.f});
  v.push_back({"visible", PType::Bool, "Visible", "Placement", 1.f});
  v.push_back({"grounded", PType::Bool, "Stand on the ground", "Placement", 1.f});
  // Wind settings
  v.push_back({"receive_wind", PType::Bool, "Receive wind effect", "Wind", 1.f});
  v.push_back({"wind_follow", PType::Bool, "Follow the world's wind", "Wind", 1.f});
  v.push_back({"wind_strength", PType::Float, "Constant wind: strength", "Wind", 0.3f, 0.f, 1.f});
  v.push_back({"wind_direction", PType::Float, "Constant wind: direction (deg)", "Wind", 0.f, -180.f, 180.f});
  v.push_back({"wind_breeze_influence", PType::Float, "Wind on breeze: influence", "Wind", 0.5f, 0.f, 1.f});
  v.push_back({"wind_breeze_speed", PType::Float, "Wind on breeze: speed", "Wind", 0.5f, 0.f, 1.f});
  v.push_back({"gust_amplitude", PType::Float, "Gusts: amplitude", "Wind", 0.3f, 0.f, 1.f});
  v.push_back({"gust_frequency", PType::Float, "Gusts: frequency (per s)", "Wind", 0.15f, 0.f, 2.f});
  v.push_back({"seg_breeze_influence", PType::Float, "Segments: breeze influence", "Wind on segments", 0.5f, 0.f, 2.f});
  v.push_back({"seg_breeze_speed", PType::Float, "Segments: breeze speed", "Wind on segments", 1.f, 0.f, 4.f});
  v.push_back({"seg_breeze_randomness", PType::Float, "Segments: breeze randomness", "Wind on segments", 0.5f, 0.f, 1.f});
  v.push_back({"seg_flutter_influence", PType::Float, "Segments: fluttering influence", "Wind on segments", 0.3f, 0.f, 2.f});
  v.push_back({"seg_flutter_speed", PType::Float, "Segments: fluttering speed", "Wind on segments", 2.f, 0.f, 8.f});
  v.push_back({"seg_wind_influence", PType::Float, "Segments: wind influence", "Wind on segments", 1.f, 0.f, 2.f});
  v.push_back({"boost_thin", PType::Float, "Flexibility: boost on thin segments", "Wind on segments", 0.5f, 0.f, 2.f});
  v.push_back({"boost_long", PType::Float, "Flexibility: boost on long segments", "Wind on segments", 0.5f, 0.f, 2.f});
  v.push_back({"blade_breeze_influence", PType::Float, "Blades: breeze influence", "Wind on blades", 0.5f, 0.f, 2.f});
  v.push_back({"blade_wind_influence", PType::Float, "Blades: wind influence", "Wind on blades", 1.f, 0.f, 2.f});
  v.push_back({"boost_large_blade", PType::Float, "Flexibility: boost on large blades", "Wind on blades", 0.5f, 0.f, 2.f});
  v.push_back({"leaf_breeze_influence", PType::Float, "Leaves: breeze influence", "Wind on leaves", 0.6f, 0.f, 2.f});
  v.push_back({"leaf_breeze_speed", PType::Float, "Leaves: breeze speed", "Wind on leaves", 1.5f, 0.f, 6.f});
  v.push_back({"leaf_breeze_randomness", PType::Float, "Leaves: breeze randomness", "Wind on leaves", 0.6f, 0.f, 1.f});
  v.push_back({"leaf_flutter_influence", PType::Float, "Leaves: fluttering influence", "Wind on leaves", 0.5f, 0.f, 2.f});
  v.push_back({"leaf_flutter_speed", PType::Float, "Leaves: fluttering speed", "Wind on leaves", 3.f, 0.f, 10.f});
  v.push_back({"boost_top", PType::Float, "Flexibility: boost on top of plant", "Wind on leaves", 0.5f, 0.f, 2.f});
  v.push_back({"boost_outer", PType::Float, "Flexibility: boost on outer parts", "Wind on leaves", 0.5f, 0.f, 2.f});
  // Meshing options
  v.push_back({"geometry_target", PType::Choice, "Geometry target", "Meshing", 0.f, 0.f, 0.f, 0.f, {"Offline", "Realtime", "Stylized"}});
  v.push_back({"meshing_type", PType::Choice, "Meshing type", "Meshing", 2.f, 0.f, 0.f, 0.f,
               {"Triangles", "Quads", "Automatic adaptative", "Automatic uniform", "Manual"}});
  v.push_back({"optimize_for", PType::Choice, "Optimization mode", "Meshing", 0.f, 0.f, 0.f, 0.f, {"Optimize for quality", "Optimize for animation"}});
  v.push_back({"mesh_boost", PType::Float, "Resolution boost", "Meshing", 0.f, -3.f, 3.f});
  v.push_back({"bone_boost", PType::Float, "Skinning: bone boost", "Meshing", 0.f, -3.f, 3.f});
  v.push_back({"bones_limit", PType::Int, "Skinning: structural bones limit", "Meshing", 512.f, 1.f, 8192.f});
  v.push_back({"facing", PType::Choice, "Camera-facing leaves bake", "Meshing", 0.f, 0.f, 0.f, 0.f,
               {"Fixed, user defined", "Fixed, facing current camera", "Fixed, outwards", "Fixed, facing X axis"}});
  // Level of detail
  v.push_back({"lod_levels", PType::Int, "Number of simplified LOD", "Level of detail", 2.f, 0.f, 6.f});
  v.push_back({"lod_boost", PType::Int, "Simplification boost", "Level of detail", 1.f, 1.f, 3.f});
  v.push_back({"lod_trigger", PType::Float, "Trigger for LOD 1 (px)", "Level of detail", 400.f, 8.f, 4000.f});
  v.push_back({"impostor_trigger", PType::Float, "Trigger for impostor (px)", "Level of detail", 40.f, 1.f, 1000.f});
  v.push_back({"use_lod_render", PType::Bool, "Use LOD for rendering", "Level of detail", 1.f});
  v.push_back({"lod_method", PType::Choice, "LOD selection method", "Level of detail", 0.f, 0.f, 0.f, 0.f, {"Bounding sphere", "Reference length"}});
  v.push_back({"ecosystem_quality", PType::Float, "Ecosystems quality", "Level of detail", 0.5f, 0.f, 1.f});
  // Envelope
  v.push_back({"envelope_resolution", PType::Int, "Envelope resolution", "Envelope", 200.f, 20.f, 5000.f});
  v.push_back({"envelope_smoothing", PType::Bool, "Envelope smoothing", "Envelope", 1.f});
  v.push_back({"envelope_convexity", PType::Bool, "Envelope convexity", "Envelope", 0.f});
  v.push_back({"residual_size", PType::Float, "Residual geometry size limit (m)", "Envelope", 0.3f, 0.f, 10.f});
  // Post processing (vertex colour ambient occlusion)
  v.push_back({"ao_min", PType::Float, "Ambient occlusion: min", "Post processing", 0.25f, 0.f, 1.f});
  v.push_back({"ao_max", PType::Float, "Ambient occlusion: max", "Post processing", 1.f, 0.f, 1.f});
  v.push_back({"ao_brightness", PType::Float, "Ambient occlusion: brightness", "Post processing", 0.f, -1.f, 1.f});
  v.push_back({"ao_falloff", PType::Float, "Ambient occlusion: falloff", "Post processing", 1.f, 0.1f, 4.f});
  v.push_back({"ao_ground_strength", PType::Float, "Ground influence: strength", "Post processing", 0.4f, 0.f, 1.f});
  v.push_back({"ao_ground_height", PType::Float, "Ground influence: height (%)", "Post processing", 15.f, 0.f, 100.f});
  v.push_back({"ao_ground_transition", PType::Float, "Ground influence: transition", "Post processing", 0.5f, 0.01f, 1.f});
  // Variations: the individuals worth keeping and the named presets
  v.push_back({"flagged", PType::Text, "Flagged seeds", "Variations", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"presets", PType::Text, "Presets (JSON)", "Variations", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "[]"});
  return v;
}

const std::vector<ParamDef> &bias_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"name", PType::Text, "Name", "Bias", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "Direction"});
  v.push_back({"bias_type", PType::Choice, "Bias type", "Bias", 0.f, 0.f, 0.f, 0.f,
               {"Direction", "Conic", "Attractor", "Axis repeller", "Swirl", "Curl", "Twist"}});
  v.push_back({"dir_x", PType::Float, "Direction X", "Bias", 0.f, -100.f, 100.f});
  v.push_back({"dir_y", PType::Float, "Direction Y", "Bias", 1.f, -100.f, 100.f});
  v.push_back({"dir_z", PType::Float, "Direction Z", "Bias", 0.f, -100.f, 100.f});
  v.push_back({"strength", PType::Random, "Strength", "Bias", 0.3f, -4.f, 4.f});
  v.push_back({"local", PType::Bool, "Local coordinates", "Bias", 0.f});
  v.push_back({"relative", PType::Bool, "Relative to segment length", "Bias", 1.f});
  v.push_back({"apply", PType::Choice, "Apply", "Bias", 1.f, 0.f, 0.f, 0.f,
               {"Only to free floating parts", "To entire segment", "Only to parts growing on object"}});
  v.push_back({"cone_angle", PType::Float, "Cone angle (deg)", "Bias", 90.f, 0.f, 180.f});
  v.push_back({"repeller", PType::Bool, "Repeller", "Bias", 0.f});
  v.push_back({"base_length", PType::Float, "Base length (m)", "Bias", 1.f, 0.01f, 100.f});
  v.push_back({"origin_x", PType::Float, "Origin X (m)", "Bias", 0.f, -1000.f, 1000.f});
  v.push_back({"origin_y", PType::Float, "Origin Y (m)", "Bias", 0.f, -1000.f, 1000.f});
  v.push_back({"origin_z", PType::Float, "Origin Z (m)", "Bias", 0.f, -1000.f, 1000.f});
  return v;
}

const std::vector<ParamDef> &material_params() {
  static std::vector<ParamDef> v;
  if (!v.empty()) return v;
  v.push_back({"name", PType::Text, "Name", "Material", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "Bark"});
  v.push_back({"color", PType::Color, "Colour", "Material", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "0,0.35,0.28,0.2,1"});
  v.push_back({"roughness", PType::Float, "Roughness", "Material", 0.7f, 0.f, 1.f});
  v.push_back({"metallic", PType::Float, "Metallic", "Material", 0.f, 0.f, 1.f});
  v.push_back({"translucency", PType::Float, "Translucency", "Material", 0.f, 0.f, 1.f});
  v.push_back({"backlight", PType::Float, "Backlight", "Material", 0.f, 0.f, 1.f});
  v.push_back({"two_sided", PType::Bool, "Two sided", "Material", 0.f});
  v.push_back({"alpha_cutout", PType::Bool, "Alpha is a cut-out", "Material", 1.f});
  v.push_back({"color_map", PType::Filename, "Colour picture", "Pictures", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"alpha_map", PType::Filename, "Alpha picture", "Pictures", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"normal_map", PType::Filename, "Normal picture", "Pictures", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"roughness_map", PType::Filename, "Roughness picture", "Pictures", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"u_tile", PType::Float, "U tile", "Pictures", 1.f, 0.01f, 64.f});
  v.push_back({"v_tile", PType::Float, "V tile", "Pictures", 1.f, 0.01f, 64.f});
  v.push_back({"cutout", PType::Text, "Cut-out outline (u,v;...)", "Pictures", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  v.push_back({"auto_cutout", PType::Bool, "Trace cut-out from alpha", "Pictures", 1.f});
  // A picture made from rules, written beside the species when it is saved
  v.push_back({"source", PType::Choice, "Made from rules", "Procedural picture", 0.f, 0.f, 0.f, 0.f,
               {"None (colour or pictures above)", "Leaf", "Bark", "Petal"}});
  v.push_back({"leaf_shape", PType::Choice, "Leaf shape", "Procedural picture", 0.f, 0.f, 0.f, 0.f,
               {"Ovate", "Lanceolate", "Lobed", "Palmate", "Needle", "Scale", "Pinnate", "Heart", "Linear", "Round", "Elliptic", "Frond", "Blade", "Needle spray"}});
  v.push_back({"leaf_serration", PType::Float, "Serration", "Procedural picture", 0.f, 0.f, 1.f});
  v.push_back({"leaf_lobes", PType::Float, "Lobe depth", "Procedural picture", 0.5f, 0.f, 1.f});
  v.push_back({"leaf_lobe_count", PType::Int, "Lobe count", "Procedural picture", 5.f, 3.f, 11.f});
  v.push_back({"leaf_aspect", PType::Float, "Width / length", "Procedural picture", 0.55f, 0.05f, 1.5f});
  v.push_back({"leaf_vein", PType::Float, "Vein strength", "Procedural picture", 0.5f, 0.f, 1.f});
  v.push_back({"vein_color", PType::Color, "Vein colour", "Procedural picture", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "0,0.55,0.65,0.35,1"});
  v.push_back({"leaf_mottle", PType::Float, "Mottle", "Procedural picture", 0.3f, 0.f, 1.f});
  v.push_back({"bark_kind", PType::Choice, "Bark", "Procedural picture", 0.f, 0.f, 0.f, 0.f,
               {"Fissured", "Plated", "Smooth", "Birch", "Ringed", "Peeling", "Scaly", "Fibrous"}});
  v.push_back({"crack_color", PType::Color, "Crack colour", "Procedural picture", 0.f, 0.f, 0.f, 0.f, {}, nullptr, "0,0.12,0.09,0.07,1"});
  v.push_back({"bark_scale", PType::Float, "Bark scale", "Procedural picture", 1.f, 0.1f, 8.f});
  v.push_back({"tex_size", PType::Choice, "Picture size", "Procedural picture", 1.f, 0.f, 0.f, 0.f, {"256", "512", "1024", "2048"}});
  v.push_back({"tex_seed", PType::Seed, "Picture seed", "Procedural picture", 1.f});
  // Seasons: up to four looks, and the curve that picks one by the season
  v.push_back({"season_count", PType::Int, "Number of seasonal looks", "Seasons", 0.f, 0.f, 4.f});
  v.push_back({"season_curve", PType::Curve, "Look by season", "Seasons", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,4|0;0,3,0,0;0.15,3,0,0;0.25,0,0,0;0.7,0,0,0;0.8,1,0,0;0.9,2,0,0;1,3,0,0"});
  v.push_back({"shift_mask", PType::Filename, "Colour shift mask", "Seasons", 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
  static const char *names[4] = {"Summer", "Autumn", "Bare", "Spring"};
  static const char *keys[4][8] = {
      {"s1_name", "s1_color_map", "s1_alpha_map", "s1_normal_map", "s1_hue", "s1_lum", "s1_sat", "s1_presence"},
      {"s2_name", "s2_color_map", "s2_alpha_map", "s2_normal_map", "s2_hue", "s2_lum", "s2_sat", "s2_presence"},
      {"s3_name", "s3_color_map", "s3_alpha_map", "s3_normal_map", "s3_hue", "s3_lum", "s3_sat", "s3_presence"},
      {"s4_name", "s4_color_map", "s4_alpha_map", "s4_normal_map", "s4_hue", "s4_lum", "s4_sat", "s4_presence"}};
  static const char *labels[8] = {"name", "colour picture", "alpha picture", "normal picture",
                                  "hue shift (turns)", "luminosity shift", "saturation shift", "presence"};
  static std::string labs[4][8];
  for (int s = 0; s < 4; ++s) {
    const std::string G = std::string("Season ") + std::to_string(s + 1);
    static std::string groups[4];
    groups[s] = G;
    for (int i = 0; i < 8; ++i) labs[s][i] = std::string("Look ") + std::to_string(s + 1) + ": " + labels[i];
    v.push_back({keys[s][0], PType::Text, labs[s][0].c_str(), groups[s].c_str(), 0.f, 0.f, 0.f, 0.f, {}, nullptr, names[s]});
    v.push_back({keys[s][1], PType::Filename, labs[s][1].c_str(), groups[s].c_str(), 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
    v.push_back({keys[s][2], PType::Filename, labs[s][2].c_str(), groups[s].c_str(), 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
    v.push_back({keys[s][3], PType::Filename, labs[s][3].c_str(), groups[s].c_str(), 0.f, 0.f, 0.f, 0.f, {}, nullptr, ""});
    v.push_back({keys[s][4], PType::Float, labs[s][4].c_str(), groups[s].c_str(), 0.f, -0.5f, 0.5f});
    v.push_back({keys[s][5], PType::Float, labs[s][5].c_str(), groups[s].c_str(), 0.f, -1.f, 1.f});
    v.push_back({keys[s][6], PType::Float, labs[s][6].c_str(), groups[s].c_str(), 0.f, -1.f, 1.f});
    v.push_back({keys[s][7], PType::Float, labs[s][7].c_str(), groups[s].c_str(), s == 2 ? 0.f : 1.f, 0.f, 1.f});
  }
  return v;
}

} // namespace plant
} // namespace gpx
