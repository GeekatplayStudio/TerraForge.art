// Geekatplay TerraForge - which material node an object is wearing.
//
// The half of the MaterialSource feature that is pure: a scene, a name, and
// the id that comes out. Split from the frame service (scene_nodes_material.
// cpp) for the same reason imprint_footprint.cpp is split from imprint.cpp -
// what can be tested without a window should be, and this is the part that
// decides what the user sees.
#pragma once
#include "gpx/node_graph.hpp"
#include <string>

namespace studio {
struct SceneState;

// The MaterialOutput node driving the object called `name`, or 0.
//
// `terrain_fallback` is the render settings' terrain material, used only for
// a terrain object that has none of its own - which is how a project made
// before objects carried a material still resolves. The renderer prefers the
// object's in exactly the same order, so what the importer hands out is what
// the viewport is showing.
unsigned long long material_of_object(const SceneState &sc,
                                      const std::string &name,
                                      unsigned long long terrain_fallback);

// Point every MaterialSource in the graph at the material its named object is
// wearing. Marks each node it changes dirty and returns how many changed, so
// the caller knows whether to ask for an evaluation.
//
// A scene with no objects in it changes nothing: every name would resolve to
// nothing and every node would be cleared, which is how a project would lose
// its material sources between opening the file and the scene arriving.
int material_sources_resolve(gpx::Graph &g, const SceneState &sc,
                             unsigned long long terrain_fallback);

} // namespace studio
