// Geekatplay TerraForge — graph internals shared between the graph structure
// (node_graph.cpp: nodes, links, link resolution, dirty propagation) and its
// evaluation (node_graph_eval.cpp). Private to the two.
#pragma once
#include "gpx/node_graph.hpp"

namespace gpx {
// Gives a heightmap filter node the optional "blend" mask port and its
// "blend_invert" attribute; called by Graph::add_node, honoured by evaluate().
void add_universal_blend(Node &n);
} // namespace gpx
