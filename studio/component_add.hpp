// Geekatplay TerraForge — every component the scene can take, by name.
//
// One factory behind the Add tile in the tool row, the Objects menu's Add
// submenu and the `add_component` op, so all three make the same thing the
// same way: the object, the node that drives it and (where it has one) a
// material - never a node without its object or an object the graph does
// not know about (component_new.cpp did this for primitives first).
//
// Kinds: "terrain" (a further heightfield tile with its own chain, standing
// beside the ones there are), "infinite_terrain", "planet", "atmosphere",
// "cloud_layer", "sun", "water", "light", "camera", "cube", "sphere",
// "plane", "cylinder", "cone", "scatter", "ecosystem", "material", and
// "import_mesh" with a path.
#pragma once
#include "component_new.hpp"
#include <string>
#include <vector>

namespace studio {
struct App;

struct ComponentKind {
  const char *kind, *label, *group, *tip;
};
// The catalogue, in the order the Add tile lists it.
const std::vector<ComponentKind> &component_kinds();

// Make one. `name` names the object when it takes one; `path` is the file
// for import_mesh. False with `err` for a kind it does not know or a step
// that failed. Takes the graph lock itself; call from outside a lease.
bool component_add(App &a, const std::string &kind, const std::string &name,
                   const std::string &path, NewComponent &out, std::string &err);

// A further terrain tile: the Terrain object, a Noise -> TerrainOutput chain
// driving it, a material, placed beside the tiles already there.
int scene_add_terrain_tile(App &a, const std::string &name);

} // namespace studio
