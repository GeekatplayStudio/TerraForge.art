// Geekatplay TerraForge — what a node type's ports are, before one exists.
//
// Dragging a wire off a port and dropping it on empty canvas should offer only
// the nodes that could actually accept it. Answering that means knowing a node
// type's ports *before* any node of that type has been created, which the node
// itself cannot tell you.
//
// Hesiod solved this by reading a generated JSON of node documentation, and
// then had to add an invariant check to catch the JSON drifting out of step
// with the code (see docs/design/2026-07-22-port-aware-drag-to-create-design).
// We do not have that problem: the catalog is built by creating one node of
// every registered type in a scratch graph, on first use, and reading the
// ports off it. That is the same construction path a real node takes — setup
// plus whatever the graph adds afterwards — so it cannot drift, and it keeps
// true declaration order rather than a JSON object's alphabetical key order.
//
// Cost: one construction sweep, once. Construction declares ports and
// attributes; it allocates no buffers, touches no GPU and evaluates nothing.
#pragma once
#include "node_graph.hpp"
#include <string>
#include <vector>

namespace gpx {

// One port of a node type, in declaration order.
struct PortInfo {
  std::string name;
  PortDir dir = PortDir::In;
  DataType type = DataType::Heightmap;
  FieldType field_type = FieldType::Number;
  bool optional = false;
};

// The ports a node type declares. Empty for an unknown type.
const std::vector<PortInfo> &port_catalog(const std::string &node_type);

// Could a node of this type take part in a link carrying `type`, on the side
// `want_dir`? An unknown type answers true: a node missing from the catalog
// must stay reachable in the menu rather than silently disappear from it.
//
// The rule matches what Graph::add_link enforces — DataType equality — so the
// menu can never offer something the link layer would then refuse.
bool node_offers(const std::string &node_type, DataType type, PortDir want_dir);

// Which port of a live node to connect, once it exists. Returns an empty
// string when nothing fits.
//
// Order of preference:
//   1. a conventionally named port for the direction sought — "input"/"in",
//      "output"/"out" (case-insensitive)
//   2. for field ports, one whose field kind also matches, so a colour lands
//      on a colour input rather than on the first number it meets
//   3. the first declared port of the right direction and type
//
// Deterministic and dull on purpose: a node with several equally plausible
// inputs connects the first one and the user rewires, which beats a picker
// interrupting the gesture.
std::string select_port(const Node &live, DataType type, PortDir want_dir,
                        FieldType prefer_field = FieldType::Number);

} // namespace gpx
