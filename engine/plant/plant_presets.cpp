// Geekatplay TerraForge - a fresh plant node, already dialled in.
//
// Every plant part starts life at the schema's defaults, and the schema's
// defaults are deliberately neutral: one metre, one radius, no bend, no
// taper. That is right for a table of parameters and wrong for a person
// who has just asked for a trunk - a trunk is six metres of tapering wood
// that flares at the foot, stands up against gravity and wears a ridged
// bark, and nobody wants to discover that by moving fourteen sliders.
//
// So a preset is the first dozen values a person would otherwise dial in,
// written from how the part actually grows: a branch inherits about half
// its parent's radius and leaves it near 45 degrees on a spiral of 137.5
// (the angle a shoot really puts its next leaf at); a twig is short, thin,
// uncapped and bends easily in wind; a frond curves down and carries two
// symmetrical blades; a growth node's internodes are twenty centimetres
// because that is roughly what a temperate shoot adds in a season.
//
// Only keys the schema declares are written, and a preset refuses a node
// of the wrong type rather than half-applying itself, so "add a trunk"
// either makes a trunk or says it could not.
#include "gpx/plant.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace gpx {
namespace {

// "Billboard leaf", "billboard_leaf" and "BILLBOARD LEAF" are one preset.
std::string normalise(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == ' ' || c == '-') c = '_';
    out += (char)std::tolower((unsigned char)c);
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  size_t b = 0;
  while (b < out.size() && out[b] == '_') ++b;
  return out.substr(b);
}

// ------------------------------------------------------- writing a value
// A missing key is silently skipped: the preset says what it knows about
// the node it is given, and a node of another kind simply has less of it.

void setf(Node &n, const char *key, float v, float spread = 0.f, int mode = 1) {
  Attribute *a = n.attrs.find(key);
  if (!a || (a->type != AttrType::Random && a->type != AttrType::Float)) return;
  a->f = std::clamp(v, a->fmin, a->fmax);
  if (a->type == AttrType::Random) {
    a->spread = spread;
    a->spread_mode = mode;
  }
}
void seti(Node &n, const char *key, int v) {
  Attribute *a = n.attrs.find(key);
  if (a && a->type == AttrType::Int) a->i = std::clamp(v, a->imin, a->imax);
}
void setb(Node &n, const char *key, bool v) {
  Attribute *a = n.attrs.find(key);
  if (a && a->type == AttrType::Bool) a->b = v;
}
void setc(Node &n, const char *key, const char *label) {
  Attribute *a = n.attrs.find(key);
  if (!a || a->type != AttrType::Choice) return;
  for (size_t i = 0; i < a->labels.size(); ++i)
    if (a->labels[i] == label) {
      a->i = (int)i;
      return;
    }
}
void sets(Node &n, const char *key, const char *v) {
  Attribute *a = n.attrs.find(key);
  if (a && (a->type == AttrType::Text || a->type == AttrType::Filename)) a->s = v;
}
void setcol(Node &n, const char *key, float r, float g, float b) {
  Attribute *a = n.attrs.find(key);
  if (!a || a->type != AttrType::Color) return;
  a->col[0] = r;
  a->col[1] = g;
  a->col[2] = b;
  a->col[3] = 1.f;
}
void setcurve(Node &n, const char *key, const Curve &c) {
  Attribute *a = n.attrs.find(key);
  if (!a || a->type != AttrType::Curve) return;
  a->curves.curves.assign(1, c);
  a->curves.weights.clear();
}

// ------------------------------------------------------------- the parts

void trunk(Node &n) {
  setf(n, "length", 6.f, 0.2f);
  setc(n, "radius_mode", "User defined");
  setf(n, "radius", 0.22f, 0.15f);
  setcurve(n, "radius_profile", Curve::through({0.f, 1.f, 0.3f, 0.85f, 0.8f, 0.5f, 1.f, 0.08f}));
  setf(n, "tropism", 0.3f); // a trunk rights itself against whatever bent it
  setf(n, "perturb_strength", 0.15f);
  setf(n, "perturb_frequency", 1.5f);
  seti(n, "flare_number", 4);
  setf(n, "flare_height", 0.8f);
  setf(n, "flare_swell", 0.5f);
  setf(n, "flare_depth", 0.35f);
  setc(n, "rdisp_source", "Bark ridges");
  setf(n, "rdisp_amount", 0.05f);
  setb(n, "cap_enable", true);
  setf(n, "wind_flexibility", 0.6f); // wood this thick barely moves
  setc(n, "positioning", "Bottom");
}

void branch(Node &n) {
  setf(n, "length", 2.2f, 0.25f);
  setc(n, "radius_mode", "Inherit");
  setf(n, "inherit_ratio", 0.45f);
  setf(n, "tropism", 0.2f, 0.1f, 0);
  setf(n, "perturb_strength", 0.25f);
  setf(n, "count", 8.f, 0.2f);
  setc(n, "count_mode", "Fixed value");
  setf(n, "start", 0.35f);
  setf(n, "angle", 45.f, 8.f, 0); // 45 degrees off the trunk
  setc(n, "arrangement", "Spiral");
  setf(n, "coil", 137.5f);
  setc(n, "positioning", "Skin");
  setf(n, "inherit_scale", 0.62f);
  setf(n, "shrink_radius", 0.08f);
  setf(n, "wind_flexibility", 1.2f);
}

void stem(Node &n) {
  setf(n, "length", 0.5f, 0.25f);
  setc(n, "radius_mode", "User defined");
  setf(n, "radius", 0.005f, 0.2f);
  setcurve(n, "radius_profile", Curve::line(1.f, 0.5f));
  setf(n, "tropism", 0.5f, 0.15f, 0); // green stems stand up
  setf(n, "perturb_strength", 0.15f);
  setb(n, "cap_enable", false);
  setf(n, "count", 3.f, 0.3f);
  setc(n, "count_mode", "Fixed value");
  setf(n, "start", 0.2f);
  setf(n, "angle", 70.f, 10.f, 0);
  setc(n, "arrangement", "Spiral");
  setf(n, "coil", 137.5f);
  setc(n, "positioning", "Skin");
  setf(n, "wind_flexibility", 2.f);
}

void palm(Node &n) {
  setf(n, "length", 2.5f, 0.15f);
  setc(n, "radius_mode", "User defined");
  setf(n, "radius", 0.02f, 0.15f);
  setcurve(n, "radius_profile", Curve::line(1.f, 0.15f));
  setc(n, "axis_mode", "Curve down");
  setf(n, "axis_bend", 40.f, 10.f, 0);
  setf(n, "tropism", -0.6f, 0.15f, 0); // a frond hangs away from the crown
  setb(n, "cap_enable", false);
  seti(n, "blade_number", 2);
  setc(n, "blade_style", "Symmetrical");
  setf(n, "blade_width", 0.35f, 0.15f);
  setf(n, "blade_start", 0.15f);
  setf(n, "blade_spread", 160.f);
  setf(n, "blade_section_height", 0.35f);
  setcurve(n, "blade_profile", Curve::through({0.f, 0.2f, 0.35f, 1.f, 0.8f, 0.7f, 1.f, 0.f}));
  setf(n, "wind_flexibility", 2.f);
  setf(n, "wind_blade_influence", 1.5f);
  setf(n, "count", 1.f);
  setc(n, "count_mode", "Fixed value");
  setc(n, "positioning", "Tip");
  setf(n, "angle", 55.f, 10.f, 0);
}

void twig(Node &n) {
  setf(n, "length", 0.35f, 0.3f);
  setc(n, "radius_mode", "Inherit");
  setf(n, "inherit_ratio", 0.4f);
  setf(n, "tropism", 0.1f, 0.15f, 0);
  setf(n, "perturb_strength", 0.3f);
  setb(n, "cap_enable", false);
  setf(n, "count", 12.f, 0.25f);
  setc(n, "count_mode", "Fixed value");
  setf(n, "start", 0.2f);
  setf(n, "angle", 55.f, 10.f, 0);
  setc(n, "arrangement", "Spiral");
  setf(n, "coil", 137.5f);
  setc(n, "positioning", "Skin");
  setf(n, "inherit_scale", 0.55f);
  setf(n, "wind_flexibility", 2.2f);
}

void leaf(Node &n) {
  setc(n, "orientation", "Fixed, user defined");
  setc(n, "mesh_kind", "Single plane");
  setf(n, "length", 0.09f, 0.2f);
  setf(n, "width", 0.05f, 0.2f);
  seti(n, "subdiv_w", 0);
  seti(n, "subdiv_h", 1);
  setf(n, "midrib_angle", 20.f, 8.f, 0);
  setf(n, "curvature_h", 18.f, 8.f, 0);
  setc(n, "normals", "Plant sphere"); // a crown reads as one mass, not as cards
  setf(n, "normal_hazard", 0.05f);
  setf(n, "count", 12.f, 0.25f);
  setc(n, "count_mode", "Fixed value");
  setf(n, "start", 0.1f);
  setf(n, "angle", 40.f, 12.f, 0);
  setc(n, "arrangement", "Spiral");
  setf(n, "coil", 137.5f); // the angle a real shoot sets its next leaf at
  setc(n, "positioning", "Skin");
  setf(n, "breeze_strength", 1.2f);
  setf(n, "breeze_flexibility", 0.7f);
}

void billboard_leaf(Node &n) {
  setc(n, "orientation", "Dynamic, facing camera");
  setc(n, "mesh_kind", "Single plane");
  setf(n, "length", 0.35f, 0.25f); // a card of many leaves, not one leaf
  setf(n, "width", 0.35f, 0.25f);
  seti(n, "subdiv_w", 0);
  seti(n, "subdiv_h", 0);
  setf(n, "midrib_angle", 0.f);
  setf(n, "curvature_h", 0.f);
  setf(n, "curvature_w", 0.f);
  setc(n, "normals", "Plant sphere");
  setf(n, "normal_hazard", 0.1f);
  setf(n, "count", 6.f, 0.3f);
  setc(n, "count_mode", "Fixed value");
  setf(n, "start", 0.2f);
  setf(n, "angle", 30.f, 15.f, 0);
  setc(n, "arrangement", "Spiral");
  setf(n, "coil", 137.5f);
  setc(n, "positioning", "Skin");
  setf(n, "breeze_strength", 0.8f);
}

void growth(Node &n) {
  setf(n, "iterations", 14.f);
  setf(n, "internode", 0.18f);   // about a season's shoot
  setf(n, "bud_radius", 0.005f); // the pipe model grows every radius from this
  setf(n, "growth_speed", 5.f);
  setf(n, "angle_with_parent", 42.f, 8.f, 0);
  setf(n, "phyllotaxis", 137.5f);
  setf(n, "angular_noise", 10.f);
  setf(n, "apical", 0.04f); // the leader outgrows its own laterals
  setf(n, "decay", 0.5f);
  setf(n, "light_influence", 1.f);
  setf(n, "shedding", 0.12f);
  setf(n, "shadow_strength", 0.5f);
  setf(n, "shadow_size", 0.3f);
  setf(n, "gravitropism_influence", 0.25f);
  setf(n, "gravitropism_angle", 40.f);
  setf(n, "phototropism", 0.35f);
  setb(n, "vertical_trunk", true);
  setf(n, "axial_subdiv", 6.f);
  setf(n, "angular_subdiv", 12.f);
  setc(n, "positioning", "Bottom");
}

void flower(Node &n) {
  setf(n, "radius", 0.025f, 0.2f);
  setf(n, "length", 0.03f, 0.2f);
  setc(n, "profile_mode", "Lobed (inner / outer)");
  seti(n, "lobes", 5);
  setf(n, "plug_radius", 0.003f);
  setf(n, "plug_influence", 0.008f);
  setf(n, "gravitropism", -0.2f); // the face turns up
  setf(n, "count", 1.f);
  setc(n, "count_mode", "Fixed value");
  setc(n, "positioning", "Tip");
  setf(n, "angle", 90.f);
  // open in spring, gone by autumn
  setcurve(n, "presence_season",
           Curve::through({0.f, 0.f, 0.18f, 0.f, 0.28f, 1.f, 0.62f, 1.f, 0.72f, 0.f, 1.f, 0.f}));
}

// ---------------------------------------------------------- the materials
// The four a plant actually wears, and the four the builders fall back to
// when a part has no material node of its own - so adding one and changing
// nothing looks the same as having none, and every dial is then in reach.

void bark_material(Node &n) {
  sets(n, "name", "Bark");
  setcol(n, "color", 0.35f, 0.28f, 0.2f);
  setf(n, "roughness", 0.85f);
  setc(n, "source", "Bark");
  setc(n, "bark_kind", "Fissured");
  setcol(n, "crack_color", 0.12f, 0.09f, 0.07f);
  setf(n, "v_tile", 3.f);
}

void leaf_material(Node &n) {
  sets(n, "name", "Leaf");
  setcol(n, "color", 0.25f, 0.45f, 0.15f);
  setf(n, "roughness", 0.55f);
  setf(n, "translucency", 0.35f);
  setf(n, "backlight", 0.4f);
  setb(n, "two_sided", true);
  setc(n, "source", "Leaf");
  setc(n, "leaf_shape", "Ovate");
  setf(n, "leaf_vein", 0.5f);
  setcol(n, "vein_color", 0.55f, 0.65f, 0.35f);
  // and the year it goes through: green, gold, gone, new green
  seti(n, "season_count", 4);
}

void petal_material(Node &n) {
  sets(n, "name", "Petal");
  setcol(n, "color", 0.95f, 0.85f, 0.3f);
  setf(n, "roughness", 0.4f);
  setf(n, "translucency", 0.5f);
  setf(n, "backlight", 0.55f);
  setb(n, "two_sided", true);
  setc(n, "source", "Petal");
}

void fruit_material(Node &n) {
  sets(n, "name", "Fruit");
  setcol(n, "color", 0.8f, 0.2f, 0.1f);
  setf(n, "roughness", 0.3f);
  setf(n, "translucency", 0.1f);
  setc(n, "source", "None (colour or pictures above)");
}

struct Preset {
  const char *name;
  const char *type;
  void (*apply)(Node &);
};

const Preset TABLE[] = {
    {"trunk", "PlantSegment", trunk},   {"branch", "PlantSegment", branch},
    {"stem", "PlantSegment", stem},     {"palm", "PlantSegment", palm},
    {"twig", "PlantSegment", twig},     {"leaf", "PlantLeaf", leaf},
    {"billboard_leaf", "PlantLeaf", billboard_leaf},
    {"growth", "PlantGrowth", growth},  {"flower", "PlantFlower", flower},
    {"bark_material", "PlantMaterial", bark_material},
    {"leaf_material", "PlantMaterial", leaf_material},
    {"petal_material", "PlantMaterial", petal_material},
    {"fruit_material", "PlantMaterial", fruit_material}};

const Preset *find_preset(const std::string &name) {
  const std::string k = normalise(name);
  for (const Preset &p : TABLE)
    if (k == p.name) return &p;
  return nullptr;
}

} // namespace

const std::vector<std::string> &plant_presets() {
  static const std::vector<std::string> v = [] {
    std::vector<std::string> out;
    for (const Preset &p : TABLE) out.push_back(p.name);
    return out;
  }();
  return v;
}

std::string plant_preset_type(const std::string &preset) {
  const Preset *p = find_preset(preset);
  return p ? p->type : std::string();
}

bool plant_preset_apply(Node &n, const std::string &preset) {
  const Preset *p = find_preset(preset);
  // A preset of the wrong kind is refused rather than half-applied: the
  // caller asked for a trunk and would otherwise get a leaf with a few of a
  // trunk's numbers on it.
  if (!p || n.type != p->type) return false;
  p->apply(n);
  return true;
}

} // namespace gpx
