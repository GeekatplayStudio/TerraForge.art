// Geekatplay TerraForge - plant species in the studio.
//
// The engine grows a species from its nodes (gpx/plant.hpp). This is
// everything around that which a person does with one: make it from a
// plant's name, a description, an archetype or a species file; stand its
// plant in the scene; try another individual, flag the good ones, keep
// presets; save it to the plant library; export its mesh. The Plant Editor
// window, the species operations (plant_species_ops.cpp) and the assistant
// all come through here, so a species made by typing "old oak" into the
// editor and one made by a script are made the same way.
//
// Locking: every function that changes the graph takes the graph lock itself
// with a bounded wait (the component_add_primitive pattern) and pushes one
// undo step, so callers must not hold it - a panel drawn under a GraphLease
// defers these calls until after it lets go. species_find is the exception:
// it reads a graph the caller already holds.
#pragma once
#include "gpx/plant.hpp"
#include "plant_place.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace studio {
struct App;
struct AiJob;

// ---- finding ---------------------------------------------------------------
struct SpeciesRef {
  uint64_t root = 0;
  std::string name;
};
// Every PlantSpecies root in the graph, in graph order. Caller holds the lock.
std::vector<SpeciesRef> species_in_graph(const gpx::Graph &g);
// A species by root id (decimal), species name or the name of the object it
// drives, any case; empty means the Plant Editor's current one, else the
// first. Null when there is none. Caller holds the lock.
gpx::Node *species_find(gpx::Graph &g, const std::string &which);
// The same, taking the lock briefly; 0 with `err` when none or busy.
uint64_t species_resolve(App &a, const std::string &which, std::string &err);
// The species the Plant Editor is showing; 0 when none.
uint64_t &species_current();
// The window being drawn right now animates its plants, or does not
// (RenderSettings::ViewConfig::animate_plants). Set by renderer_draw_view
// before each window; the mesh passes read it through
// a_plant_wind_preview().
void plant_view_animates_set(bool on);
// The scene object a species root drives, or -1.
int species_object(uint64_t root);

// ---- making ----------------------------------------------------------------
struct SpeciesMake {
  PlantPlace at;          // where its plant stands, its size, scatter
  bool place = true;      // stand the plant in the scene at all
  int individuals = 1;    // roots sharing the parts, each its own seed and object
};
uint64_t species_create(App &a, const gpx::PlantDescription &d, const SpeciesMake &mk,
                        std::string &err);
// A plant's name with modifiers ("old weeping willow in autumn") through the
// engine's knowledge of plants. `matched` is false when the words named no
// plant it knows and it grew its best guess.
uint64_t species_create_from_words(App &a, const std::string &words, const SpeciesMake &mk,
                                   bool &matched, std::string &err);
// A species file (its folder or its species.json) into the graph.
uint64_t species_load(App &a, const std::string &path, const SpeciesMake &mk, std::string &err);
// More individuals of a species: new roots on the same trunk, each with its
// own seed and, when placed, its own object beside the first. Returns how
// many were made.
int species_add_individuals(App &a, uint64_t root, int count, const SpeciesMake &mk,
                            std::string &err);

// ---- a wood ----------------------------------------------------------------
// A forest is a handful of individuals of one species and a great many
// instanced copies of them, on a population layer carrying the rules that
// make a wood a wood (plant_forest.cpp).
struct ForestPlan {
  std::string words;          // what to grow, when `species` is 0
  uint64_t species = 0;       // an existing species root to make a wood of
  int individuals = 5;        // different trees the wood is made of, 1..8
  float area_m = 1200.f;      // how far the wood reaches around the camera
  bool unbounded = true;      // follow the camera over the whole ground
  float density_ha = 0.f;     // trees per hectare; 0 works it out from spacing
  float spacing_m = 0.f;      // 0 takes it from the tree's own height
  float clumping = 0.55f;     // stands with clearings, not an orchard
  float clump_size_m = 0.f;   // 0 takes it from the spacing
  float altitude_lo = 0.02f;  // a share of the terrain's height range: out of
  float altitude_hi = 0.75f;  // the water, below the bare summits
  float max_slope_deg = 34.f; // not on cliffs
  float size_variation = 0.35f;
};
struct ForestResult {
  uint64_t layer = 0;
  std::vector<uint64_t> individuals;
  int kinds = 0;
  bool known = true;
};
int plant_forest(App &a, const ForestPlan &plan, ForestResult &out, std::string &err);

// ---- the text model --------------------------------------------------------
// Ask the configured text model to describe the plant `prompt` names; the
// species is built when the answer lands (ai_jobs_service -> species_ai_apply).
uint64_t species_ai_submit(const std::string &prompt, const std::string &image,
                           const SpeciesMake &mk);
bool species_ai_apply(App &a, AiJob &job, std::string &err);
std::string species_ai_system_prompt();
// The JSON object inside a model's reply, fences and chatter stripped.
std::string species_ai_extract_json(const std::string &reply);

// ---- editing ---------------------------------------------------------------
// Each field applies only when it is set (floats >= 0 unless noted).
struct SpeciesSettings {
  float age = -1.f, max_age = -1.f, health = -1.f, season = -1.f;
  float wind_strength = -1.f;
  float wind_direction = -1000.f; // degrees; -1000 = unchanged
  float detail = -1000.f;         // meshing boost; -1000 = unchanged
  long long seed = -1;
  int receive_wind = -1;          // 0 off, 1 on
};
bool species_set(App &a, uint64_t root, const SpeciesSettings &s, std::string &err);
// Another individual: `seed` < 0 picks a new one. Returns the seed in use,
// or -1 with `err`.
long long species_variation(App &a, uint64_t root, long long seed, std::string &err);
// Keep the current seed in the root's flagged list.
bool species_flag(App &a, uint64_t root, std::string &err);
std::vector<long long> species_flagged(const gpx::Node &root);
// A part from a node preset (gpx::plant_presets) onto `parent`: a child slot
// for a part, a material port for a material, the trunk for the root. `slot`
// 0 takes the first free one. Returns the new node's id.
uint64_t species_add_part(App &a, uint64_t parent, const std::string &preset, int slot,
                          std::string &err);
// "trunk", "root", a decimal node id or a plant node type ("PlantLeaf"): the
// part of a species that names. Caller holds the lock.
gpx::Node *species_part_find(gpx::Graph &g, gpx::Node &root, const std::string &which);
// Presets: "store" | "apply" | "delete" | "list" by name. `out` gets the
// list's names, one per line.
bool species_preset(App &a, uint64_t root, const std::string &action, const std::string &name,
                    std::string &out, std::string &err);
std::vector<std::string> species_preset_names(const gpx::Node &root);

// ---- keeping ---------------------------------------------------------------
// <plant library>/species: a folder per species, with species.json, the
// plant.json manifest the library reads, and a thumbnail.
std::string species_library_dir();
std::string species_save_to_library(App &a, uint64_t root, const std::string &name,
                                    const std::string &group, const std::string &note,
                                    std::string &err);
// The grown mesh as .glb, .gltf or .obj.
bool species_export(App &a, uint64_t root, const std::string &path, std::string &err);

// The viewport's wind preview: plants sway only while it is on.
void plant_wind_preview_set(bool on);

} // namespace studio
