// Geekatplay TerraForge - the graph panel's internals, shared between the
// interaction half (panel_graph.cpp) and the drawing half
// (panel_graph_draw.cpp). Private to those two.
#pragma once
#include "app.hpp"
#include <cstdint>
#include <vector>
#include <imgui_node_editor.h>

namespace studio {


// pin id encoding: node_id * 4096 + port index + 1
inline uint64_t pin_id(uint64_t node, size_t port) { return node * 4096 + port + 1; }
inline void decode_pin(uint64_t pin, uint64_t &node, size_t &port) {
  node = pin / 4096;
  port = (size_t)(pin % 4096) - 1;
}

// Node metrics. Cinema 4D publishes almost no numbers — palette icon sizes are
// the only pixel values in the whole manual — so these come from measuring the
// reference screenshots rather than from documentation.
// The card: a rounded body with a coloured title bar, connectors sitting on
// the left and right edges (n8n's reading: the wire meets the card at its
// border, not inside it), one row per port with the label beside the dot.
namespace nodemetric {
constexpr float HEADER_H = 24.f;   // the coloured title bar
constexpr float PORT_R = 5.f;      // connector radius
constexpr float ROW_H = 18.f;      // one port row
constexpr float PREVIEW = 96.f;    // thumbnail edge
constexpr float PAD_X = 12.f;      // inset of labels from the edge
constexpr float COL_GAP = 22.f;    // clear space between the two port columns
constexpr float ROUNDING = 7.f;
constexpr float CHEVRON_W = 18.f;  // the collapse toggle at the header's right
} // namespace nodemetric

// Collapse changes are asked for while drawing (which never touches the
// graph) and applied by the editor on a frame that holds the lock.
struct CollapseRequest {
  uint64_t node;
  int mode; // -1 = cycle
};
inline std::vector<CollapseRequest> g_collapse_requests;

// What a drag that ended on empty canvas was carrying (panel_graph.cpp sets
// it inside BeginCreate; the create menu consumes it). Zero node = the menu
// was opened the ordinary way and filters nothing.
struct DragCreate {
  uint64_t node = 0;
  std::string port;
  gpx::DataType type = gpx::DataType::Heightmap;
  gpx::FieldType field_type = gpx::FieldType::Number;
  gpx::PortDir want_dir = gpx::PortDir::In;
  bool active() const { return node != 0; }
  void clear() { node = 0; port.clear(); }
};
inline DragCreate g_drag_create;

// panel_graph_draw.cpp
void draw_node(App &a, const App::NodeView &n);
void add_node_popup(App &a);

} // namespace studio
