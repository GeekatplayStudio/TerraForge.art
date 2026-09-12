// Geekatplay TerraForge - a plant from the library into the scene.
//
// A plant arrives the way every component does (component_new.hpp): the
// object, and the node in the graph that drives it - a Primitive node for a
// built-in plant, an ImportObject node for a model. It stands on the ground
// under the view's pivot, at its real size, and wears its own colours and
// pictures rather than a material. "Scatter" adds a Scatter points node and
// binds the plant to it, so copies of it cover the terrain.
//
// A model can be large (the jacaranda is two hundred megabytes of scan), so
// the panel reads it on a worker thread and the object appears when it is
// ready; a script's request reads it at once, so the next action of the same
// script can already find the object.
#pragma once
#include "plant_library.hpp"
#include <string>

namespace gpx {
class Node;
}

namespace studio {
struct App;

struct PlantPlace {
  bool at_view = true;          // under the view's pivot; otherwise at `pos`
  float pos[3] = {0.5f, 0.f, 0.5f}; // tile units; the height is the ground's
  float size = 1.f;             // times the plant's own size
  float heading_deg = 0.f;
  bool scatter = false;         // bind it to a new Scatter points node too
  int count = 300;              // the scattered copies
  std::string variant;          // which plant of a set; empty = the first
  std::string name;             // the object's name; empty = the plant's
};

// Now, on the calling thread. Returns the object's index, or -1 with `err`.
int plant_add(App &a, const PlantEntry &p, const PlantPlace &at, std::string &err);

// The same, with a model read on a worker thread; a built-in is added at
// once. The object appears on a later frame (plant_place_service).
bool plant_add_async(App &a, const PlantEntry &p, const PlantPlace &at, std::string &err);

// Once a frame: put the models that finished loading into the scene.
void plant_place_service(App &a);

// A model still loading, and which ("Jacaranda Tree").
bool plant_place_busy(std::string *what = nullptr);

// A grown species' plant into the scene: the object `root` drives, stood
// where `at` says, `height_m` tall until it grows. Caller holds the graph
// lock. Returns the object's index.
int plant_place_species(App &a, gpx::Node &root, const PlantPlace &at, float height_m);

} // namespace studio
