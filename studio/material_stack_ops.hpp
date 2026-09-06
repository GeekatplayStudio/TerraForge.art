// Geekatplay TerraForge — the layer-stack surgery behind the Material Editor.
//
// Kept apart from the panel so it can be tested without a window: rewiring a
// chain of nodes is the part that can silently leave a material disconnected,
// and that is exactly the kind of bug a screenshot does not catch.
#pragma once
#include "gpx/node_graph.hpp"
#include <string>
#include <vector>

namespace studio {

// The link arriving at a port, or null. Ports take one link, so this is
// unambiguous.
gpx::Link *layer_incoming(gpx::Graph &g, uint64_t node, const char *port);

// A node that can stand in the stack: a MaterialLayer or an EcosystemLayer.
bool is_stack_layer(const gpx::Node *n);
// A layer that places instances (EcosystemLayer, DistributionLayer).
bool is_population_layer(const gpx::Node *n);

// The layer chain feeding a MaterialOutput, top of the stack first. Stops at
// the first node that is not a stack layer, so a plain material returns an
// empty stack rather than nonsense.
std::vector<gpx::Node *> collect_layers(gpx::Graph &g, gpx::Node *mat);

// An ecosystem layer reacts to the nearest population beneath it: link each
// one's "below" to that layer's points (or unlink it when there is none).
void wire_populations_below(gpx::Graph &g, const std::vector<gpx::Node *> &layers);

// Add a layer of `type` on top of the stack (the general form of the two
// below).
gpx::Node *add_stack_layer(gpx::Graph &g, gpx::Node *mat,
                           const std::vector<gpx::Node *> &layers, const char *type);
// An EcosystemLayer on top, wired to the population beneath it.
gpx::Node *add_ecosystem_layer(gpx::Graph &g, gpx::Node *mat,
                               const std::vector<gpx::Node *> &layers);
// Two adjacent layers of different kinds trade places in the chain.
void swap_chain_places(gpx::Graph &g, gpx::Node *x, gpx::Node *y);

// Point a layer's three outputs at the material's three inputs, replacing
// whatever was there.
void wire_layer_to_material(gpx::Graph &g, gpx::Node *layer, gpx::Node *mat);

// Add a layer on top. When the material has no layers yet, whatever was
// driving its channels becomes the new layer's own input, so turning a flat
// material into a stack keeps what it already had.
gpx::Node *add_material_layer(gpx::Graph &g, gpx::Node *mat,
                              const std::vector<gpx::Node *> &layers);

// Remove a layer and close the gap, so the stack below it still reaches the
// material.
void delete_material_layer(gpx::Graph &g, gpx::Node *victim, gpx::Node *mat,
                           const std::vector<gpx::Node *> &layers);

// Swap two layers' settings and their own inputs, leaving the chain wiring
// alone. Nothing is disconnected even for an instant, and the environment
// constraints travel with the layer, which is what "move this one up" means.
void swap_material_layers(gpx::Graph &g, gpx::Node *x, gpx::Node *y);

// Display helpers, here because they are pure string work with no UI in them.
std::string layer_display_name(const gpx::Node *n, size_t index);
std::string layer_presence_summary(const gpx::Node *n);

} // namespace studio
