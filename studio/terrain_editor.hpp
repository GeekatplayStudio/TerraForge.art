// Geekatplay TerraForge — Vue's Terrain Editor, as operations on the graph.
//
// Vue edits a terrain in a modal window: styles down the left, brushes on
// the Paint tab, erosion and global effects on the Effects tab, a clipping
// slider with an end at each altitude, a toolbar of global commands
// (invert, zero edges, retopologize, resolution), and a Picture button that
// mixes an image into the heightfield (Reference Manual p519-546). We keep
// the same vocabulary and put every one of them on the Terrain object's
// Properties tab, but each one is a node dropped into the chain in front of
// the Terrain Output - so it is undoable, retunable, and there for a script
// to read. Nothing here is destructive.
//
// Every function takes the graph lock itself. Call from a panel that holds
// no lease, or from an op (ai_ops_terrain.cpp).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace studio {
struct App;

// A node between the chain and the Terrain Output (terrain_styles.cpp).
void terrain_insert_before_output(App &a, const char *type, const char *what,
                                  const std::vector<std::pair<const char *, float>> &floats,
                                  const std::vector<std::pair<const char *, int>> &ints);

// The styles down Vue's left edge, by name: "Mountain", "Ridged peaks",
// "Eroded mountain", "Canyon", "Mounds", "Dunes", "Iceberg", "Lunar",
// "Realistic mountain range". False with `err` for a name it does not know.
bool terrain_style_apply(App &a, const std::string &name, std::string &err);
const std::vector<std::string> &terrain_style_names();

// Clipping altitudes: the TerrainClip node in front of the output.
struct TerrainClipState {
  bool present = false; // a TerrainClip node is in the chain
  float low = 0.f, high = 1.f; // heightmap units (0..1 of the height range)
  int low_mode = 0;   // 0 hole, 1 flatten - what happens below the low mark
  int high_mode = 0;  // 0 flatten, 1 hole (the node's own order)
  float softness = 0.f;
};
TerrainClipState terrain_editor_clip_get(App &a);
// Sets the node's values, inserting the node when there is none. `err` on
// a chain with no Terrain Output.
bool terrain_editor_clip_set(App &a, const TerrainClipState &s, std::string &err);
// Removes the TerrainClip node, reconnecting the chain.
bool terrain_editor_clip_clear(App &a, std::string &err);

// The Effects tab. Erosion: "diffusive", "thermal", "glaciation", "wind",
// "dissolve", "alluvium", "fluvial", "river valley". Global: "grit",
// "gravel", "pebbles", "stones", "peaks", "fir trees", "plateaus",
// "terraces", "stairs", "craters", "sharpen", "cracks". Each call drops
// one node in front of the output (Vue's iteration count is calling it
// again); `hardness` 0..1 is Vue's Rock hardness, mapped to what each
// effect has. Names are matched case-insensitively.
bool terrain_editor_effect(App &a, const std::string &effect, float hardness, std::string &err);
const std::vector<std::string> &terrain_editor_effect_names(bool erosion);

// The toolbar commands: "invert", "zero_edges" (toggle the output's fade),
// "smooth_all" (Vue's Retopologize), "halve", "double" (the resolution),
// "reset_sculpt" (clear the sculpt layer), "remove_effects" (every editor
// node between the chain and the output).
bool terrain_editor_global(App &a, const std::string &action, std::string &err);

// Vue's Picture button: a HeightmapFile node into the output's first extra
// layer, mixed by `mode` ("blend", "add", "max", "min", "subtract",
// "multiply") at `proportion` 0..1.
bool terrain_editor_import_picture(App &a, const std::string &path, const std::string &mode,
                                   float proportion, std::string &err);

// How many editor nodes sit between the chain and the output (for the panel).
int terrain_editor_effect_count(App &a);

} // namespace studio
