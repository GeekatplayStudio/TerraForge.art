// Geekatplay TerraForge — the Node List (P0.4).
//
// Terragen makes a point of this (guide p3, p11, p21): the node network is
// powerful but "quite complex and not immediately intuitive", so it also offers
// a structured list where connections are handled for you, and says outright
// that a whole scene can be built without ever opening the network. The two
// views show the same information and stay in step, so you can start in the
// list and move to the network as you gain confidence.
//
// Ours reads the graph as a tree rooted at whatever feeds the terrain, with
// each node's inputs indented beneath it — so the list is literally the shape
// of the data flow, top to bottom.
#include "app.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace studio {

// Where the chain ends: a TerrainOutput if there is one, otherwise the last
// node that produces a heightmap. Same rule the viewport uses, so the list and
// the render agree about what "the terrain" is.
static uint64_t chain_root(const App &a) {
  uint64_t last = 0;
  for (const App::NodeView &n : a.node_views) {
    if (n.type == "TerrainOutput") return n.id;
    for (const App::PortView &p : n.ports)
      if (!p.is_input && !p.is_texture) last = n.id;
  }
  return last;
}

static const App::NodeView *view_of(const App &a, uint64_t id) {
  for (const App::NodeView &n : a.node_views)
    if (n.id == id) return &n;
  return nullptr;
}

// upstream nodes feeding a given node's inputs, in port order
static std::vector<std::pair<std::string, uint64_t>> feeders(const App &a,
                                                             uint64_t id) {
  std::vector<std::pair<std::string, uint64_t>> out;
  const App::NodeView *n = view_of(a, id);
  if (!n) return out;
  for (const App::PortView &p : n->ports) {
    if (!p.is_input) continue;
    for (const App::LinkView &l : a.link_views)
      if (l.to_node == id && l.to_port == p.name)
        out.emplace_back(p.name, l.from_node);
  }
  return out;
}

static void draw_row(App &a, uint64_t id, int depth, std::set<uint64_t> &seen,
                     const char *port_label) {
  const App::NodeView *n = view_of(a, id);
  if (!n) return;
  // a node feeding two places appears twice; showing it once per path is
  // honest about the flow, but we must not recurse forever
  bool repeat = !seen.insert(id).second;

  ImGui::PushID((int)id);
  if (depth) ImGui::Indent(12.f * depth);

  auto fed = feeders(a, id);
  bool has_children = !fed.empty() && !repeat;
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_SpanAvailWidth |
                             ImGuiTreeNodeFlags_DefaultOpen;
  if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf;
  if (a.selected_node == id) flags |= ImGuiTreeNodeFlags_Selected;

  std::string label = n->type;
  if (port_label && *port_label) label = std::string(port_label) + ": " + n->type;
  if (!n->enabled) label += "  (bypassed)";
  if (repeat) label += "  (also above)";

  if (!n->enabled) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.53f, 0.50f, 1.f));
  else if (!n->error.empty())
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.32f, 0.24f, 1.f));
  else
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.84f, 0.82f, 0.79f, 1.f));
  bool open = ImGui::TreeNodeEx("##row", flags, "%s", label.c_str());
  ImGui::PopStyleColor();

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    a.selected_node = id;
    // selecting here must not yank the properties panel onto another tab —
    // the sticky-tab rule from phase 12 still holds
  }
  if (ImGui::IsItemHovered()) {
    std::string tip = n->type + "  \xC2\xB7 " + n->category;
    if (n->ms > 0.01) tip += "\n" + std::to_string((int)n->ms) + " ms";
    if (!n->error.empty()) tip += "\n" + n->error;
    tip += "\nclick to edit it in Properties";
    ImGui::SetTooltip("%s", tip.c_str());
  }
  // per-row bypass, so the list is useful for "what does this stage do?"
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4);
  bool on = n->enabled;
  if (studio::Checkbox("##en", &on)) {
    undo_push(a, on ? "Enable " + n->type : "Bypass " + n->type);
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    if (gpx::Node *live = a.graph.find_node(id)) {
      live->enabled = on;
      a.graph.mark_dirty(id);
      a.request_eval();
    }
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bypass this stage");

  if (open) {
    if (has_children)
      for (const auto &[port, src] : fed)
        draw_row(a, src, depth + 1, seen, port.c_str());
    ImGui::TreePop();
  }
  if (depth) ImGui::Unindent(12.f * depth);
  ImGui::PopID();
}

void draw_panel_nodelist(App &a) {
  if (!ImGui::Begin("Node List", &a.show_nodelist)) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("The terrain, read from the result backwards.");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Each node's inputs are listed beneath it, so the tree is\n"
                      "the shape of the data flow. This is the same graph the\n"
                      "Graph panel shows - edit in either.");
  ImGui::Separator();

  if (a.node_views.empty()) {
    ImGui::TextDisabled("No nodes yet.");
    ImGui::TextDisabled("Add one from the Library, or pick a style from\n"
                        "the Terrain menu.");
    ImGui::End();
    return;
  }
  uint64_t root = chain_root(a);
  if (!root) {
    ImGui::TextDisabled("Nothing is producing terrain yet.");
    ImGui::End();
    return;
  }
  std::set<uint64_t> seen;
  draw_row(a, root, 0, seen, nullptr);

  // anything not reachable from the result is still part of the project and
  // should not silently vanish from the list
  std::vector<const App::NodeView *> orphans;
  for (const App::NodeView &n : a.node_views)
    if (!seen.count(n.id)) orphans.push_back(&n);
  if (!orphans.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("Not connected to the result");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("These nodes are in the project but nothing downstream\n"
                        "uses them yet.");
    for (const App::NodeView *n : orphans) {
      ImGui::PushID((int)n->id);
      bool sel = a.selected_node == n->id;
      if (ImGui::Selectable(n->type.c_str(), sel)) a.selected_node = n->id;
      ImGui::PopID();
    }
  }
  ImGui::End();
}

} // namespace studio
