// Geekatplay TerraForge - the graph surgery behind the Material Editor's
// channel modes, hierarchy, randomize and snapshots. No window in here, so
// every operation is tested on a bare graph (tests/cpp/test_material_channel_ops.cpp).
//
// Vue's editor shows each channel with a mode - None/Constant, Mapped
// Picture, Procedural (and Natural Grain for colour) - and a hierarchy of
// the material's layers and sub-materials. In this graph those are nodes
// feeding ports; these functions are the translation, one way each.
#pragma once
#include "gpx/node_graph.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace studio {

enum ChannelMode {
  CH_NONE = 0,   // nothing feeds the port: the owner's constant applies
  CH_PICTURE,    // a TextureFile
  CH_PROCEDURAL, // any generating node (FractalColor by default)
  CH_GRAIN,      // NaturalGrain
  CH_OTHER       // fed by something that is not a leaf (a layer, a stack)
};
const char *channel_mode_name(int mode);

// What feeds `port` of `owner`, and which mode that reads as.
gpx::Node *channel_source(gpx::Graph &g, gpx::Node *owner, const char *port);
int channel_mode_of(gpx::Graph &g, gpx::Node *owner, const char *port);

// Give the channel a mode. NONE disconnects (the old node stays in the graph,
// bypassed); PICTURE adds a TextureFile with `path`; PROCEDURAL a
// FractalColor; GRAIN a NaturalGrain. A node of the right kind already there
// is kept. Returns the node feeding the port afterwards (null for NONE).
gpx::Node *channel_set_mode(gpx::Graph &g, gpx::Node *owner, const char *port,
                            int mode, const std::string &path = "");

// Every node upstream of the material, the material itself excluded.
std::vector<uint64_t> material_upstream(gpx::Graph &g, uint64_t mat_id);

// Vue's Randomize: a new seed on every fractal and noise node of the
// material. Returns how many changed.
int material_randomize(gpx::Graph &g, gpx::Node *mat, uint32_t salt);

// Restore a stored snapshot into an existing material: the MaterialOutput
// keeps its id (objects assigned to it stay assigned); its upstream is
// replaced by the snapshot's and the old, now orphaned, nodes are removed.
bool material_replace_from_json(gpx::Graph &g, uint64_t mat_id,
                                const std::string &json, std::string &err);

// The material hierarchy (Vue p690): the material, its layers top first,
// a mix and the two materials it mixes, a distribution or effector layer.
struct MatHierItem {
  uint64_t node = 0;
  int depth = 0;
  int kind = 0; // 0 material, 1 layer, 2 mix, 3 sub-material, 4 distribution, 5 effector
  std::string label;
};
std::vector<MatHierItem> material_hierarchy(gpx::Graph &g, gpx::Node *mat);

// Vue's three-state visibility switch on a hierarchy line: 0 normal,
// 1 invisible (bypassed), 2 highlighted (a MaterialLayer's solid colour).
int hier_visibility(gpx::Node *n);
void hier_set_visibility(gpx::Node *n, int state);

} // namespace studio
