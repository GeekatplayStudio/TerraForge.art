// Geekatplay TerraForge — Properties: the surface graph of planets and infinite terrain (Planet, InfiniteSurface). Split from panel_properties_object.cpp for the 500-line module rule.
#include "app.hpp"
#include "undo.hpp"
#include <algorithm>
#include <imgui.h>
#include <mutex>
#include <string>
#include <vector>

namespace studio {

namespace {

// ------------------------------------------------- the surface node graph
// Planets and the endless ground plane have no heightmap: they are evaluated
// on the GPU from parameters, at whatever detail the camera has earned. That
// is why they cost no memory, and it is also why their shape cannot be painted
// or baked. The way to author a function is to author a function - so this
// finds (or builds) the SurfaceDisplacement sink and shows it in the editor,
// with a FieldNoise already wired into it to start from.
// `assign` (may be null) receives the node's id, so the planet or surround
// that asked owns that graph from now on; `create_new` forces a fresh one
// even when the graph already has a SurfaceDisplacement.
void open_surface_graph(App &a, unsigned long long *assign, bool create_new) {
  uint64_t focus = 0;
  {
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (!lk.owns_lock()) {
      a.status = "the graph is evaluating - try again in a moment";
      return;
    }
    gpx::Node *sink = nullptr;
    if (assign && *assign) sink = a.graph.find_node(*assign);
    if (!sink && !create_new)
      for (auto &n : a.graph.nodes)
        if (n->type == "SurfaceDisplacement") sink = n.get();
    if (!sink) {
      undo_push_locked(a, "Add surface displacement");
      float x = 0.f, y = 260.f;
      for (auto &n : a.graph.nodes) x = std::max(x, n->pos_x);
      gpx::Node *src = a.graph.add_node("FieldNoise", x, y);
      sink = a.graph.add_node("SurfaceDisplacement", x + 260.f, y);
      if (src && sink) a.graph.add_link(src->id, "out", sink->id, "field");
      a.graph_layout_serial++;
      a.request_eval();
      a.status = "added a surface displacement graph";
    }
    focus = sink ? sink->id : 0;
    if (assign && sink) *assign = sink->id;
  }
  if (focus) graph_focus_node(a, focus); // takes the lock itself
}

} // namespace

// The picker: which SurfaceDisplacement node shapes this surface. Listed by
// id with what feeds them, so two graphs can be told apart.
void surface_graph_picker(App &a, unsigned long long *node) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  std::vector<std::pair<unsigned long long, std::string>> sinks;
  if (lk.owns_lock())
    for (auto &n : a.graph.nodes)
      if (n->type == "SurfaceDisplacement") {
        gpx::Node *src = a.graph.upstream_node(*n, "field");
        sinks.push_back({n->id, "#" + std::to_string(n->id) + "  " +
                                    (src ? src->type : std::string("(unwired)"))});
      }
  if (lk.owns_lock()) lk.unlock();
  std::string label = "the graph's first";
  for (auto &s : sinks)
    if (s.first == *node) label = s.second;
  if (*node && label == "the graph's first") label = "(missing) built-in layers";
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo("##surfgraph", label.c_str())) {
    if (ImGui::Selectable("the graph's first SurfaceDisplacement", *node == 0))
      *node = 0;
    for (auto &s : sinks)
      if (ImGui::Selectable(s.second.c_str(), s.first == *node)) *node = s.first;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The SurfaceDisplacement node whose field graph\n"
                      "displaces this surface - each world can have its own,\n"
                      "as a Terragen planet has its own terrain network.");
  if (ImGui::Button("Edit graph")) open_surface_graph(a, node, false);
  ImGui::SameLine();
  if (ImGui::Button("New graph for this world")) open_surface_graph(a, node, true);
}

} // namespace studio
