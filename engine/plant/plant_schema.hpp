// Geekatplay TerraForge - the plant nodes' parameters, as one table.
//
// Every plant node declares its parameters from these tables rather than by
// hand, so the node registration (engine/nodes/nodes_plant.cpp), the
// builders (engine/plant/*.cpp) that read the keys, the tooltips
// (plant_schema_tips.cpp), the documentation generator and the assistant's
// catalogue all agree on the same keys, types, defaults and choice lists.
// A key is permanent once shipped (the regression lock forbids removing or
// retyping an attribute), so a parameter is added at the end of its group,
// never renamed.
//
// The tables follow the plant tool's parameter sheets group for group
// (docs/roadmaps/plants.md names the source); the groups become the
// collapsible sections of the Properties panel, in table order.
#pragma once
#include "gpx/attribute.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace gpx {
class Node;
namespace plant {

// The plant node types (permanent type names) and their roles.
enum class Kind {
  Species,      // PlantSpecies: the root; age, season, health, wind, meshing
  Segment,      // PlantSegment: trunks, branches, stems, twigs, roots
  Leaf,         // PlantLeaf: a flat leaf card, single/crossed/diamond planes
  CutoutLeaf,   // PlantCutoutLeaf: a leaf cut to its picture's outline
  Warpboard,    // PlantWarpboard: a curled rectangle (simple leaf, petal)
  Object,       // PlantObject: an imported mesh as a part
  Urchin,       // PlantUrchin: a sphere that carries children on its skin
  Hydra,        // PlantHydra: children spread around a circle
  Ball,         // PlantBall: a sphere (fruit)
  Flower,       // PlantFlower: a lathe of a profile, lobed
  Growth,       // PlantGrowth: a bud-by-bud growth simulation
  Repeat,       // PlantRepeat: a subtree grown into itself n times
  ChildSelect,  // PlantChildSelect: random / alternate / sequence / threshold / LOD
  Bias,         // PlantBias: a global bias fed to the root
  Material,     // PlantMaterial: colour, pictures, seasons, cut-out
  Variable,     // PlantVariable: a plant number as a field (primal, age, ...)
  Vector,       // PlantVector: a plant direction or position as a field
  COUNT
};
const char *kind_type(Kind k);        // "PlantSegment"
Kind kind_of_type(const std::string &type, bool *ok = nullptr);
const char *kind_description(Kind k); // one line, for the registry

// How many "child N" Plant inputs a node has (0 for a part that carries none)
int child_slots(Kind k);
// How many material inputs ("material", "material 2", ...) a node has
int material_slots(Kind k);

enum class PType { Float, Int, Bool, Choice, Random, Curve, Gradient, Seed, Text, Filename, Color, Vec2 };

struct ParamDef {
  const char *key;
  PType type;
  const char *label;
  const char *group;
  float def = 0.f, mn = 0.f, mx = 1.f;
  float spread = 0.f;                  // Random: the default spread
  std::vector<const char *> choices;   // Choice
  const char *curve = nullptr;         // Curve: default in curve_to_string form
  const char *text = nullptr;          // Text/Filename/Gradient default
  bool log_scale = false;
  bool field_input = false;            // also declared as a Number field input port
};

// The parameters of one kind, in declaration order.
const std::vector<ParamDef> &params(Kind k);
// The tooltip for (kind, key); "" when none is written yet. The tables live
// in plant_schema_tips_*.cpp and register themselves; a key under Kind::COUNT
// is a shared block's and applies to every kind.
struct Tip {
  Kind kind;
  const char *key;
  const char *text;
};
void tips_register(const Tip *table, size_t count);
const char *tooltip(Kind k, const char *key);
// Declare a kind's parameters (and field input ports) on a node.
void declare(Node &n, Kind k);
// The version of the whole schema, changed whenever a parameter is added;
// species files record it.
int schema_version();

// Shared groups: the transform block every part carries, the LOD block, the
// attachment block a child carries about how it sits on its parent, the
// season block a leaf-like part carries. Appended by the per-kind tables.
void transform_params(std::vector<ParamDef> &v, bool tropism, bool wind_strength);
void lod_params(std::vector<ParamDef> &v, bool inherit);
void attachment_params(std::vector<ParamDef> &v);
void season_params(std::vector<ParamDef> &v);
void breeze_params(std::vector<ParamDef> &v, bool flexibility);

} // namespace plant
} // namespace gpx
