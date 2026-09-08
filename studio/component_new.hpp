// Geekatplay TerraForge - a new component, complete.
//
// Adding a cube used to put a bare object in the scene: no node in the graph,
// no material, nothing to edit but the transform. Everything the application
// is built around - the graph is the truth, a material is a node, a node
// drives its object - only started existing for that cube once you went and
// made it by hand.
//
// So adding a component builds the whole thing: the object, the node in the
// editor that drives it, and a material assigned to it. The material is the
// last one you used, because working on a set of objects that share a look is
// the common case; failing that, a plain grey one, which is what a modelling
// application gives you and is a better starting point than "none" - an
// object with no material has nothing to open in the Material Studio.
#pragma once
#include <cstdint>
#include <string>

namespace studio {
struct App;

struct NewComponent {
  int object = -1;         // index into the scene, -1 if none was made
  uint64_t node = 0;       // the node driving it in the graph
  uint64_t material = 0;   // the MaterialOutput assigned to it
};

// The material a new component should wear: App::last_material if it still
// names a MaterialOutput, otherwise a plain grey one created on demand and
// remembered. Caller holds the graph lock.
uint64_t component_material(App &a);

// A built-in primitive (cube, sphere, plane, cylinder, cone), complete.
// Takes the graph lock itself; pushes one undo step for the whole thing.
NewComponent component_add_primitive(App &a, const std::string &kind,
                                     const std::string &name = "");

} // namespace studio
