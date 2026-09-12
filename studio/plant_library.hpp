// Geekatplay TerraForge - the plant library: every plant the Plants
// workspace can put in a scene.
//
// Three shelves. The built-in plants are built from their kind
// (scene_plants.cpp) and need no file at all. The free plants are CC0 models
// fetched on request from Poly Haven by orchestrator/plant_fetch.py, never
// shipped with the application (docs/LICENSING.md). And a person's own
// models - an export from any plant tool they own - are recorded where they
// are, without being copied: the library holds a manifest pointing at them.
//
// Each downloaded or recorded plant is a folder holding plant.json, the
// manifest this file reads:
//
//   <data_dir>/library/plants/<source>/<id>/plant.json
//
// A file that holds a set of plants (Poly Haven's fern_02 is four ferns) lists
// them as variants, each its own glTF; the library shows the set once and the
// panel picks the variant.
//
// No GL and no App here, so the manifest reader and the folder scan are
// tested on their own (tests/cpp/test_plant_library.cpp). Putting a plant in
// the scene is plant_place.cpp; the download is plant_fetch.cpp.
#pragma once
#include <string>
#include <vector>

namespace studio {

struct PlantVariant {
  std::string id;     // "a", "dead_a"
  std::string name;   // "a", "dead a"
  std::string model;  // absolute path of its glTF
  float height_m = 0; // 0 when unknown
  float width_m = 0;
  int polycount = 0;
};

struct PlantEntry {
  std::string id;      // "builtin/pine", "polyhaven/fern_02", "user/my_oak"
  std::string name;    // "Fern 02"
  std::string source;  // builtin | polyhaven | user
  std::string group;   // trees | shrubs | ground cover | flowers | grass | deadwood | rocks
  std::string kind;    // a built-in's primitive kind ("pine"); empty for a model
  std::string model;   // a model's file (the whole set, when it has variants)
  std::string thumb;   // its picture; empty when none
  std::string folder;  // its folder in the library; empty for a built-in
  std::string license; // "CC0 1.0", "built in", "your own"
  std::string url;     // where it came from
  std::string note;    // one line on what it is
  std::vector<std::string> authors, categories, tags;
  std::vector<PlantVariant> variants; // sorted by name; empty for a single plant
  float height_m = 0;  // 0 when unknown
  float unit_m = 1;    // metres per unit of the model's file
  int polycount = 0;
};

// The shelves the panel filters by, in its order ("all" first).
const std::vector<std::string> &plant_groups();

// Where fetched and recorded plants live: <data_dir>/library/plants.
std::string plant_library_dir();

// The built-in plants, in the order the panel shows them.
std::vector<PlantEntry> plant_builtins();

// One plant.json read into an entry, its file names resolved against
// `folder`. False with `err` when the text is not a plant manifest.
bool plant_manifest_parse(const std::string &text, const std::string &folder,
                          PlantEntry &out, std::string &err);

// Every manifest under `root` (<root>/<source>/<id>/plant.json), by name.
// A folder whose manifest does not read, or whose model is missing, is left
// out rather than listed as a plant that cannot be placed.
std::vector<PlantEntry> plant_scan(const std::string &root);

// The whole library: the built-ins, then plant_scan(plant_library_dir()).
// Read once; `rescan` reads the folder again (after a download or an import).
const std::vector<PlantEntry> &plant_library(bool rescan = false);

// By id ("polyhaven/fern_02"), by bare id ("fern_02") or by name, any case.
const PlantEntry *plant_find(const std::string &id);

// The variant a request names - by id or name, any case - or the first; null
// for a plant without variants.
const PlantVariant *plant_variant(const PlantEntry &p, const std::string &want);

// Every word of `query` found in the name, id, group, tags or categories.
bool plant_matches(const PlantEntry &p, const std::string &query);

// Record a model the user owns in the library, where it lies: writes
// <root>/user/<id>/plant.json naming the file. `height_m` > 0 sets the size
// the model is placed at; 0 reads the model's units from how tall it is in
// its own file (over 150 units is taken as centimetres, over 1500 as
// millimetres). Returns the new id ("user/<id>"), or "" with `err`.
std::string plant_record_model(const std::string &root, const std::string &model_path,
                               const std::string &name, float height_m, std::string &err);

// ---- fetching the free plants (plant_fetch.cpp) ----------------------------
// orchestrator/plant_fetch.py in the background, into plant_library_dir().
// `ids` empty fetches its curated set. False with `err` when one is already
// running or Python's layer cannot be found.
bool plant_fetch_start(const std::vector<std::string> &ids, const std::string &res,
                       std::string &err);
struct PlantFetchState {
  bool running = false;
  bool finished = false; // the last run is over (read by plant_fetch_poll)
  int exit_code = 0;
  std::string line;      // its progress, or how it ended
};
PlantFetchState plant_fetch_state();
// Once a frame: reads the progress, and rescans the library when a run ends.
void plant_fetch_poll();
void plant_fetch_cancel();

} // namespace studio
