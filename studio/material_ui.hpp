// Geekatplay TerraForge - what the Material Studio, the Material Browser and
// the Properties tab share about materials, so a material means one thing in
// all three.
//
// A material is a MaterialOutput node plus everything upstream of it. Its
// *type* - Vue's seven, reduced to the six that mean something in a node
// graph - is read from what feeds the output, never stored: change the graph
// and the type follows.
#pragma once
#include "gpx/node_graph.hpp"
#include "render_settings.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace studio {
struct App;

enum MaterialType {
  MAT_SIMPLE = 0,   // channels fed directly (colour, noise, files)
  MAT_PBR,          // a PBRMaterial texture set drives the channels
  MAT_MIXED,        // two materials blended by a mask (MaterialStack)
  MAT_LAYERED,      // a MaterialLayer stack with presence per layer
  MAT_DISTRIBUTION, // presence also places objects (DistributionLayer)
  MAT_EFFECTOR,     // a typed influence field for other systems
  MAT_TYPE_COUNT
};
const char *material_type_name(int t);
const char *material_type_blurb(int t);

struct MatEntry {
  uint64_t id;
  std::string name;
};
// Every MaterialOutput in the graph. Caller holds the graph lock.
std::vector<MatEntry> collect_materials(App &a);

// The type a material currently is, from its graph.
int material_type_of(gpx::Graph &g, gpx::Node *mat);

// Give a material a type: inserts the nodes that type needs between the
// output and what already feeds it, keeping what it already had. Returns the
// node that was added (or the material itself when nothing was needed).
gpx::Node *material_set_type(App &a, gpx::Node *mat, int type);

// A hash of the material's whole subgraph, for "modified since opened".
uint64_t material_fingerprint(gpx::Graph &g, uint64_t mat_id);

// Everything the preview renderer needs, read from the node.
MaterialPreviewSpec material_preview_spec(App &a, gpx::Node *mat);

// One table row per channel: what feeds it, or "not connected".
void material_channel_row(App &a, gpx::Node *mat, const char *port,
                          const char *human);
// The surface sliders on the MaterialOutput. Returns true when one moved.
bool material_surface_ui(App &a, gpx::Node *mat, float label_w);

// The studio's editing state: which material is open, and whether it has
// changed since it was opened or last saved - so switching away can ask.
struct MaterialStudioState {
  uint64_t material = 0;
  uint64_t saved_fingerprint = 0;
  int shape = 0;          // 0 sphere, 1 cube, 2 flat
  int background = 0;     // 0 dark, 1 grey, 2 light
  bool turntable = true;
  float spin = 0.6f;
  float preview_size = 260.f;
  uint64_t pending_open = 0; // a material waiting on the save prompt
  std::string prompt_name;
};
MaterialStudioState &material_studio();
// Open a material in the studio. If the current one is modified, asks first
// and opens the new one after the answer; returns false while waiting.
bool material_studio_open(App &a, uint64_t mat_id);
bool material_studio_modified(App &a);
void material_studio_mark_saved(App &a);

} // namespace studio
