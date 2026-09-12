#pragma once
// Geekatplay TerraForge - the spray brush: a painted population of several
// kinds at once (studio/spray.cpp).
//
// A spray is a MaskPaint node feeding an EcosystemLayer: the mask says where,
// painted in the viewport with the terrain brushes, and the layer says what
// and how thickly. Each of the layer's kinds is a component - a plant grown
// from its name, a rock, an imported model - with a share of the total, a
// size, how much that size varies, and how far it tips with the ground.
#include <cstdint>
#include <string>
#include <vector>

namespace studio {

struct App;

// What to make the spray out of, when it is first created.
struct SprayPlan {
  float area_m = 1200.f;     // how far it reaches, when it follows the camera
  // A spray is bounded by what is painted, not by where the camera is. An
  // unbounded population covers the whole world around the viewer and is
  // realised cell by cell in world space, which cannot see a mask painted on
  // the tile - so an unpainted brush placed thousands of instances anyway.
  // Turn it on for a population meant to cover everything (plant_forest
  // does); a brush says where by being painted.
  bool unbounded = false;
  float density_ha = 60.f;   // instances per hectare where the mask is full
  float spacing_m = 6.f;     // the least distance between two of them
  float clumping = 0.4f;     // 0 evenly spread, 1 tight groups
  float size_variation = 0.35f;
};

// One kind in the mix.
struct SprayComponent {
  int slot = -1;               // which of the layer's kinds it is, 0..7
  std::string plant_words;     // grow a plant from its name ("gorse"), or
  std::string object;          // an existing mesh in the scene (a rock, a model)
  std::string name;            // what to call the object it places
  float share = 1.f;           // its weight against the other kinds
  float percent = 0.f;         // that weight as a share of the total (read back)
  float scale = 1.f;           // its size, against the layer's own
  float size_variation = 0.f;  // how much it varies; 0 takes the layer's
  float lean = -1.f;           // how far it tips with the slope; < 0 the layer's
};

// ---- a biome ---------------------------------------------------------------
// One layer of a laid-out biome: which group it places and what it was placed
// against, so a person (or a panel) can see the reasoning.
struct BiomeLayer {
  uint64_t node = 0;
  std::string group;     // "moss"
  std::string label;     // "moss", as a population is called
  std::string below;     // the group it was placed in relation to
  float share = 1.f;
};
// Lay a biome out as a stack of populations in the order the ground
// assembles, wired so each is placed against the one before it, all painted
// by one brush (studio/spray_biome.cpp). Returns the first layer's node id.
uint64_t spray_biome(App &a, const std::string &biome_id, const SprayPlan &plan,
                     std::vector<BiomeLayer> &made, std::string &err);

// A new spray: the painted mask and the population it feeds. Returns the
// layer's node id, which names the spray from then on.
uint64_t spray_new(App &a, const SprayPlan &plan, std::string &err);
// The layer standing for a group in this scene, or 0. This is how a thing
// added later finds where it belongs: it says what group it is, and the rules
// about that group have already decided where that is.
uint64_t spray_layer_for_group(const App &a, const std::string &group);
// Add a kind to it. Returns which slot it took, or -1.
int spray_add(App &a, uint64_t layer_id, const SprayComponent &c, std::string &err);
// What is in it, with each kind's share as a percentage of the whole.
std::vector<SprayComponent> spray_components(const App &a, uint64_t layer_id);
// Change one kind. Fields left at their defaults are not touched.
bool spray_set(App &a, uint64_t layer_id, int slot, const SprayComponent &c, std::string &err);

} // namespace studio
