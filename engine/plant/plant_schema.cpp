// Geekatplay TerraForge - the plant schema: kinds, the shared parameter
// blocks, and turning a table into a node's attributes and ports. The
// per-kind tables are in plant_schema_*.cpp (500-line module rule).
#include "plant/plant_schema.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "gpx/node_graph.hpp"
#include <cstring>
#include <sstream>

namespace gpx {
namespace plant {

namespace {

const char *TYPES[(int)Kind::COUNT] = {
    "PlantSpecies", "PlantSegment", "PlantLeaf",    "PlantCutoutLeaf", "PlantWarpboard",
    "PlantObject",  "PlantUrchin",  "PlantHydra",   "PlantBall",       "PlantFlower",
    "PlantGrowth",  "PlantRepeat",  "PlantChildSelect", "PlantBias",   "PlantMaterial",
    "PlantVariable", "PlantVector"};

const char *DESCRIPTIONS[(int)Kind::COUNT] = {
    "A plant species: its trunk grows into this root, which builds one individual per seed with an age, a season, a health and a wind",
    "A segment: a trunk, branch, stem, twig or root along an axis, with caps, root flares and blades, carrying children",
    "A leaf card: one plane, crossed planes or a diamond, with a mid-rib, curvature and a picture",
    "A leaf cut to the outline of its picture, bent along an axis with a mid-rib and a twist",
    "A curled rectangle: the simplest leaf or petal",
    "An imported mesh grown as a part of the plant: a fruit, a cone, a scanned leaf",
    "A sphere that carries children on its skin: a seed head, a thistle, a cactus body",
    "Children spread around a circle: a clump, a rosette, a tuft",
    "A sphere: a berry, an apple, a bud",
    "A flower: a lathe of a lobed profile - disc, cup, bell, trumpet, tube",
    "A branch system grown bud by bud from light, gravity and apical dominance, carrying leaves",
    "The subtree on its body input grown into itself, iteration after iteration; the tail grows on the last",
    "Which of its children grow: at random by presence, alternately, in a sequence, by a threshold, or by level of detail",
    "A global bias the root applies to every segment: a lean, a cone, an attractor, a swirl",
    "What a part wears: a colour, pictures, a cut-out, seasonal looks, or a picture made from rules",
    "A number about the primitive being grown - where on it, its age, season, health, depth - as a field",
    "A direction or position of the primitive being grown, as a field vector"};

Curve curve_of(const char *s) {
  Curve c = Curve::constant(1.f);
  if (s && *s) curve_from_string(s, c);
  return c;
}

std::vector<GradientStop> gradient_of(const char *s) {
  std::vector<GradientStop> out;
  if (!s) return out;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ';')) {
    GradientStop g;
    if (std::sscanf(part.c_str(), "%f,%f,%f,%f,%f", &g.t, &g.r, &g.g, &g.b, &g.a) >= 4) out.push_back(g);
  }
  return out;
}

} // namespace

const char *kind_type(Kind k) { return TYPES[(int)k]; }
const char *kind_description(Kind k) { return DESCRIPTIONS[(int)k]; }

Kind kind_of_type(const std::string &type, bool *ok) {
  for (int i = 0; i < (int)Kind::COUNT; ++i)
    if (type == TYPES[i]) {
      if (ok) *ok = true;
      return (Kind)i;
    }
  if (ok) *ok = false;
  return Kind::COUNT;
}

int child_slots(Kind k) {
  switch (k) {
    case Kind::Segment: case Kind::Urchin: case Kind::Hydra: case Kind::Growth:
    case Kind::ChildSelect: return 6;
    case Kind::Repeat: return 1; // "child 1" is the body; "tail" is its own port
    case Kind::Flower: case Kind::Ball: return 2;
    default: return 0;
  }
}

int material_slots(Kind k) {
  switch (k) {
    case Kind::Segment: return 4; // body, then alternates for the distribution
    case Kind::Leaf: case Kind::CutoutLeaf: case Kind::Warpboard: case Kind::Urchin:
    case Kind::Ball: case Kind::Flower: case Kind::Growth: return 4;
    case Kind::Object: return 1;
    default: return 0;
  }
}

int schema_version() { return 1; }

// ------------------------------------------------------------ shared blocks

void transform_params(std::vector<ParamDef> &v, bool tropism, bool wind_strength) {
  if (wind_strength) {
    v.push_back({"wind_strength", PType::Random, "Wind strength", "Transform", 1.f, 0.f, 4.f, 0.f});
    v.push_back({"breeze_override", PType::Bool, "Override breeze response", "Transform", 0.f});
  }
  v.push_back({"scale", PType::Random, "Scale", "Transform", 1.f, 0.01f, 10.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"scale_x", PType::Random, "Scale X", "Transform", 1.f, 0.01f, 10.f});
  v.push_back({"scale_y", PType::Random, "Scale Y", "Transform", 1.f, 0.01f, 10.f});
  v.push_back({"scale_z", PType::Random, "Scale Z", "Transform", 1.f, 0.01f, 10.f});
  v.push_back({"scale_inherit", PType::Bool, "Inherit scale", "Transform", 1.f});
  v.push_back({"offset_x", PType::Random, "Offset X (m)", "Transform", 0.f, -10.f, 10.f});
  v.push_back({"offset_y", PType::Random, "Offset Y (m)", "Transform", 0.f, -10.f, 10.f});
  v.push_back({"offset_z", PType::Random, "Offset Z (m)", "Transform", 0.f, -10.f, 10.f});
  v.push_back({"rot_x", PType::Random, "Rotation X (deg)", "Transform", 0.f, -180.f, 180.f});
  v.push_back({"rot_y", PType::Random, "Rotation Y (deg)", "Transform", 0.f, -180.f, 180.f});
  v.push_back({"rot_z", PType::Random, "Rotation Z (deg)", "Transform", 0.f, -180.f, 180.f});
  if (tropism) {
    v.push_back({"orient_vertical", PType::Random, "Orientation tropism: vertical", "Transform", 0.f, -1.f, 1.f});
    v.push_back({"orient_horizontal", PType::Random, "Orientation tropism: horizontal", "Transform", 0.f, -1.f, 1.f});
  }
}

void lod_params(std::vector<ParamDef> &v, bool inherit) {
  v.push_back({"lod_min", PType::Int, "Min LOD", "Level of detail", 0.f, 0.f, 8.f});
  v.push_back({"lod_max", PType::Int, "Max LOD", "Level of detail", 8.f, 0.f, 8.f});
  if (inherit) v.push_back({"lod_inherit", PType::Bool, "Inherit LOD", "Level of detail", 1.f});
}

void breeze_params(std::vector<ParamDef> &v, bool flexibility) {
  v.push_back({"breeze_strength", PType::Random, "Breeze strength", "Ambient motion", 1.f, 0.f, 4.f});
  if (flexibility)
    v.push_back({"breeze_flexibility", PType::Random, "Breeze flexibility", "Ambient motion", 0.5f, 0.f, 1.f});
  v.push_back({"breeze_override", PType::Bool, "Override breeze response", "Ambient motion", 0.f});
}

void season_params(std::vector<ParamDef> &v) {
  v.push_back({"presence_season", PType::Curve, "Presence over the year", "Seasons", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"tint_season", PType::Gradient, "Tint over the year", "Seasons", 0.f, 0.f, 1.f, 0.f, {}, nullptr,
               "0,1,1,1,1;1,1,1,1,1"});
  v.push_back({"presence_health", PType::Curve, "Presence by health", "Seasons", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,0.2,0,0;1,1,0,0"});
  v.push_back({"tint_health", PType::Gradient, "Tint by health", "Seasons", 0.f, 0.f, 1.f, 0.f, {}, nullptr,
               "0,0.55,0.4,0.2,1;0.5,0.85,0.8,0.55,1;1,1,1,1,1"});
  v.push_back({"presence_age", PType::Curve, "Presence by maturity", "Seasons", 0.f, 0.f, 1.f, 0.f, {},
               "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"droop_health", PType::Random, "Droop when dry (deg)", "Seasons", 25.f, 0.f, 90.f});
  v.push_back({"shrink_health", PType::Random, "Shrink when dry", "Seasons", 0.3f, 0.f, 1.f});
}

void attachment_params(std::vector<ParamDef> &v) {
  const char *G = "Attachment";
  v.push_back({"presence", PType::Random, "Presence", G, 1.f, 0.f, 1.f});
  v.push_back({"count", PType::Random, "Number", G, 8.f, 0.f, 500.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"count_mode", PType::Choice, "Count mode", G, 0.f, 0.f, 0.f, 0.f, {"Fixed value", "Per metre", "Per length (legacy)"}});
  v.push_back({"soft_insert", PType::Choice, "Soft insert", G, 0.f, 0.f, 0.f, 0.f,
               {"None, use rounded number", "Offset fractional part", "Scale down fractional part", "Random"}});
  v.push_back({"start_mode", PType::Choice, "Start mode", G, 0.f, 0.f, 0.f, 0.f, {"Relative", "Absolute from start", "Absolute from end"}});
  v.push_back({"start", PType::Random, "Start", G, 0.2f, -2.f, 3.f});
  v.push_back({"end_mode", PType::Choice, "End mode", G, 0.f, 0.f, 0.f, 0.f, {"Relative", "Absolute from start", "Absolute from end"}});
  v.push_back({"end", PType::Random, "End", G, 1.f, -2.f, 3.f});
  v.push_back({"pair_offset_mode", PType::Choice, "Pair offset mode", G, 0.f, 0.f, 0.f, 0.f, {"Relative", "Absolute"}});
  v.push_back({"pair_offset", PType::Random, "Pair offset", G, 0.f, -1.f, 1.f});
  v.push_back({"margin_before_cut", PType::Float, "Margin before cut (m)", G, 0.f, 0.f, 5.f});
  v.push_back({"density", PType::Curve, "Density along the parent", G, 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,1,0,0;1,1,0,0"});
  v.push_back({"pruning", PType::Random, "Pruning", G, 0.f, 0.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"randomness", PType::Random, "Randomness", G, 0.f, 0.f, 1.f});
  v.push_back({"arrangement", PType::Choice, "Arrangement", G, 0.f, 0.f, 0.f, 0.f,
               {"Spiral", "Alternate", "Opposite", "Pairs (decussate)", "Simple"}});
  v.push_back({"positioning", PType::Choice, "Positioning method", G, 2.f, 0.f, 0.f, 0.f,
               {"Tip", "Axis", "Skin", "Dummy", "Orthogonal to surface", "Automatic transition", "Bottom"}});
  v.push_back({"move_out", PType::Random, "Move out", G, 0.f, -1.f, 2.f});
  v.push_back({"roll", PType::Random, "Roll (deg)", G, 0.f, -180.f, 180.f});
  v.push_back({"coil", PType::Random, "Coil (deg)", G, 137.5f, -360.f, 360.f});
  v.push_back({"avoid_mesh", PType::Bool, "Avoid mesh", G, 0.f});
  v.push_back({"influenced_by_twist", PType::Bool, "Influenced by twist", G, 1.f});
  v.push_back({"blend_subdiv", PType::Bool, "Use subdivision surfaces", "Blending", 0.f});
  v.push_back({"blend_upper", PType::Random, "Upper width", "Blending", 0.3f, 0.f, 3.f});
  v.push_back({"blend_lower", PType::Random, "Lower width", "Blending", 0.3f, 0.f, 3.f});
  v.push_back({"blend_child", PType::Random, "Child width", "Blending", 0.5f, 0.f, 3.f});
  v.push_back({"blend_move_away", PType::Random, "Move away", "Blending", 0.f, 0.f, 2.f});
  v.push_back({"blend_materials", PType::Random, "Blend materials", "Blending", 0.2f, 0.f, 2.f});
  v.push_back({"blend_normal", PType::Curve, "Normal blend", "Blending", 0.f, 0.f, 1.f, 0.f, {}, "d:0,1,0,1|1;0,0,0,0;1,1,0,0"});
  v.push_back({"blend_post_offset", PType::Random, "Post blending offset", "Blending", 0.f, 0.f, 1.f});
  v.push_back({"blend_bending", PType::Random, "Bending force", "Blending", 0.f, 0.f, 2.f});
  v.push_back({"blend_ignore_disp", PType::Random, "Ignore displacement", "Blending", 0.f, 0.f, 1.f});
  v.push_back({"shrink_radius", PType::Random, "Shrink parent radius", "Influence on parent", 0.f, 0.f, 1.f});
  v.push_back({"bending", PType::Random, "Bending (zig-zag)", "Influence on parent", 0.f, 0.f, 1.f});
  v.push_back({"bending_smoothness", PType::Random, "Smoothness", "Influence on parent", 0.5f, 0.f, 1.f});
  v.push_back({"angle", PType::Random, "Angle (deg)", "Orientation", 30.f, -90.f, 90.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"rotation", PType::Random, "Rotation (deg)", "Orientation", 0.f, -180.f, 180.f});
  v.push_back({"tropism_x", PType::Float, "Tropism direction X", "Orientation", 0.f, -1.f, 1.f});
  v.push_back({"tropism_y", PType::Float, "Tropism direction Y", "Orientation", 1.f, -1.f, 1.f});
  v.push_back({"tropism_z", PType::Float, "Tropism direction Z", "Orientation", 0.f, -1.f, 1.f});
  v.push_back({"tropism_cone", PType::Random, "Cone angle (deg)", "Orientation", 90.f, 0.f, 180.f});
  v.push_back({"tropism_local", PType::Bool, "Local coordinates", "Orientation", 0.f});
  v.push_back({"tropism_angle", PType::Random, "Angle strength", "Orientation", 0.f, 0.f, 1.f});
  v.push_back({"tropism_rotation", PType::Random, "Rotation strength", "Orientation", 0.f, 0.f, 1.f});
  v.push_back({"tropism_roll", PType::Random, "Roll strength", "Orientation", 0.f, 0.f, 1.f});
  v.push_back({"per_whorl", PType::Random, "Per whorl", "Whorl", 1.f, 1.f, 24.f});
  v.push_back({"whorl_soft", PType::Choice, "Soft insert", "Whorl", 0.f, 0.f, 0.f, 0.f,
               {"None, use rounded number", "Scale down fractional part", "Random"}});
  v.push_back({"whorl_spread", PType::Random, "Spread (deg)", "Whorl", 360.f, 0.f, 360.f});
  v.push_back({"whorl_randomness", PType::Random, "Randomness", "Whorl", 0.f, 0.f, 1.f});
  v.push_back({"whorl_angle_randomness", PType::Random, "Orientation angle randomness (deg)", "Whorl", 0.f, 0.f, 90.f});
  v.push_back({"whorl_position_randomness", PType::Random, "Position randomness", "Whorl", 0.f, 0.f, 1.f});
  v.push_back({"inherit_density", PType::Random, "Density", "Inherited properties", 1.f, 0.f, 1.f});
  v.push_back({"inherit_scale", PType::Random, "Scale", "Inherited properties", 1.f, 0.01f, 3.f});
  v.push_back({"inherit_sap", PType::Random, "Sap", "Inherited properties", 1.f, 0.f, 1.f});
  v.push_back({"cut_probability", PType::Random, "Probability of cut", "Pruning", 0.f, 0.f, 1.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"cut_length", PType::Random, "Cut length (m)", "Pruning", 0.5f, 0.f, 50.f, 0.f, {}, nullptr, nullptr, false, true});
  v.push_back({"cut_radius_reduction", PType::Random, "Radius reduction", "Pruning", 1.f, 0.f, 1.f});
}

// ---------------------------------------------------------------- declare

// The name a part answers to before anybody renames it.
const char *default_part_name(Kind k) {
  switch (k) {
    case Kind::Segment: return "Segment";
    case Kind::Leaf: return "Leaf";
    case Kind::CutoutLeaf: return "Cut-out leaf";
    case Kind::Warpboard: return "Board";
    case Kind::Object: return "Object";
    case Kind::Urchin: return "Urchin";
    case Kind::Hydra: return "Hydra";
    case Kind::Ball: return "Ball";
    case Kind::Flower: return "Flower";
    case Kind::Growth: return "Growth";
    case Kind::Repeat: return "Repeat";
    case Kind::ChildSelect: return "Choice";
    default: return "Part";
  }
}

void declare(Node &n, Kind k) {
  // Every part is named, and the name it is given is the name its geometry
  // carries: the parts tree, the group in an OBJ and the mesh in a glTF all
  // read it, so "lower branches" stays "lower branches" all the way out to
  // whatever opens the file. The kinds that are not parts (the root, a
  // material, a bias) declare their own name where it belongs among their
  // own settings.
  switch (k) {
    case Kind::Species:
    case Kind::Material:
    case Kind::Bias:
    case Kind::Variable:
    case Kind::Vector:
    case Kind::COUNT: break;
    default: {
      Attribute &nm = add_text(n.attrs, "name", "Name", default_part_name(k), "General");
      nm.tooltip = "What this part is called: shown in the species' parts tree, and written as the "
                   "group name when the plant is exported.";
    } break;
  }
  for (const ParamDef &p : params(k)) {
    Attribute *a = nullptr;
    switch (p.type) {
      case PType::Float: a = &add_float(n.attrs, p.key, p.label, p.def, p.mn, p.mx, p.group, p.log_scale); break;
      case PType::Int: a = &add_int(n.attrs, p.key, p.label, (int)p.def, (int)p.mn, (int)p.mx, p.group); break;
      case PType::Bool: a = &add_bool(n.attrs, p.key, p.label, p.def != 0.f, p.group); break;
      case PType::Choice: {
        std::vector<std::string> labels(p.choices.begin(), p.choices.end());
        a = &add_choice(n.attrs, p.key, p.label, labels, (int)p.def, p.group);
      } break;
      case PType::Random: a = &add_random(n.attrs, p.key, p.label, p.def, p.mn, p.mx, p.spread, p.group, p.log_scale); break;
      case PType::Curve: a = &add_curve(n.attrs, p.key, p.label, curve_of(p.curve), p.group); break;
      case PType::Gradient: a = &add_gradient(n.attrs, p.key, p.label, gradient_of(p.text), p.group); break;
      case PType::Seed: a = &add_seed(n.attrs, p.key, p.label, (uint32_t)p.def, p.group); break;
      case PType::Text: a = &add_text(n.attrs, p.key, p.label, p.text ? p.text : "", p.group); break;
      case PType::Filename: a = &add_filename(n.attrs, p.key, p.label, p.text ? p.text : "", p.group); break;
      case PType::Color: {
        std::vector<GradientStop> c = gradient_of(p.text);
        GradientStop g = c.empty() ? GradientStop{0, 0.5f, 0.5f, 0.5f, 1} : c[0];
        a = &add_color(n.attrs, p.key, p.label, g.r, g.g, g.b, g.a, p.group);
      } break;
      case PType::Vec2: a = &add_vec2(n.attrs, p.key, p.label, p.def, p.spread, p.mn, p.mx, p.group); break;
    }
    if (a) a->tooltip = tooltip(k, p.key);
    if (p.field_input) n.add_field_in(p.key, FieldType::Number, true);
  }
}

} // namespace plant
} // namespace gpx
