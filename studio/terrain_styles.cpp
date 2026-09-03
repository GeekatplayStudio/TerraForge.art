// Geekatplay TerraForge - the Terrain menu's style presets.
//
// Vue's "predefined terrain styles", with one difference that matters: a
// style here is not a baked result, it is a small node chain dropped into the
// graph. Pick "Eroded mountain" and the Noise, Hydraulic and Thermal nodes
// that made it are sitting in the editor, wired up and retunable. A preset
// that cannot be taken apart is a dead end.
#include "app.hpp"
#include "undo.hpp"
#include "gpx/node_graph.hpp"
#include <imgui.h>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// ---- terrain style presets -------------------------------------------------
// One-click starting points (Vue's "predefined terrain styles"): each builds a
// small node chain so the result stays fully editable — open the graph and
// every stage of the style is right there to retune.
struct StyleNode {
  const char *type;
  std::vector<std::pair<const char *, float>> floats;
  std::vector<std::pair<const char *, int>> ints;
};

static void apply_terrain_style(App &a, const char *name,
                                const std::vector<StyleNode> &chain) {
  undo_push(a, std::string("Terrain style: ") + name);
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  // fresh seed on every click, the way Vue randomizes the fractal origin
  static uint32_t style_serial = 1;
  uint32_t seed = style_serial++ * 2654435761u;

  gpx::Node *prev = nullptr;
  float x = 60, y = 60;
  // place the chain below the existing graph so nothing overlaps
  for (auto &n : a.graph.nodes) y = std::max(y, n->pos_y + 260);
  gpx::Node *first = nullptr;
  for (const StyleNode &sn : chain) {
    gpx::Node *n = a.graph.add_node(sn.type, x, y);
    if (!n) continue;
    if (!first) first = n;
    for (auto &kv : sn.floats)
      if (gpx::Attribute *at = n->attrs.find(kv.first)) at->f = kv.second;
    for (auto &kv : sn.ints)
      if (gpx::Attribute *at = n->attrs.find(kv.first)) {
        at->i = kv.second;
        at->seed = (uint32_t)kv.second;
      }
    if (gpx::Attribute *sd = n->attrs.find("seed")) sd->seed = seed;
    if (prev) {
      gpx::Port *po = prev->first_out(gpx::DataType::Heightmap);
      if (po) a.graph.add_link(prev->id, po->name, n->id, "input");
    }
    prev = n;
    x += 190;
  }
  if (!prev) return;

  // route the style into the TerrainOutput (creating one if needed), so the
  // new terrain shows up immediately
  gpx::Node *out_node = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") out_node = n.get();
  if (!out_node) out_node = a.graph.add_node("TerrainOutput", x, y);
  if (out_node) {
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap") {
        a.graph.remove_link(l.id);
        break;
      }
    gpx::Port *po = prev->first_out(gpx::DataType::Heightmap);
    if (po) a.graph.add_link(prev->id, po->name, out_node->id, "heightmap");
  }
  a.selected_node = prev->id;
  a.view_node = 0; // follow the TerrainOutput again
  a.graph_layout_serial++;
  a.request_eval();
  a.status = std::string("terrain style: ") + name;
}

void menu_terrain(App &a) {
  if (!ImGui::BeginMenu("Terrain")) return;
  ImGui::TextDisabled("Style presets (build editable node chains)");
  ImGui::Separator();
  // The realistic chain: the way real ranges form. A fractal for the
  // initial uplift, stream power carving the drainage network the way rivers
  // do over geological time (implicit solver with uplift, so ridges keep
  // rising while valleys deepen), then the hydraulic and thermal pass that
  // gives slopes their angle of repose - and ErosionLayers on the end, so the
  // materials come from the same simulation.
  // Tuned headlessly (tools/chain_preview) against a dozen alternatives: the
  // implicit uplift solver makes plateaus against the fixed borders, and
  // droplets alone leave grain; this order - a swiss-ridge base, the pipe
  // model to settle coherent valleys, explicit stream power to cut the
  // drainage network, then thermal + a light droplet pass with the material
  // masks - reads as a real range at 512 in about two and a half seconds.
  if (ImGui::MenuItem("Realistic mountain range"))
    apply_terrain_style(
        a, "Realistic mountain range",
        {{"Noise", {}, {{"type", 3}, {"octaves", 8}}},
         {"Hydraulic", {}, {{"method", 1}, {"iterations", 200}}},
         {"StreamPower", {{"k_erode", 0.12f}, {"smooth", 0.06f}},
          {{"iterations", 60}, {"method", 0}}},
         {"ErosionLayers", {{"strength", 0.5f}, {"talus", 1.6f}, {"snowline", 0.78f}},
          {{"method", 3}, {"thermal_iters", 80}}}});
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Eroded ridges, then the shallow-water solver settles\n"
                      "the valleys, stream power cuts the drainage network,\n"
                      "and ErosionLayers adds talus and gullies - with the\n"
                      "material masks (rock, scree, soil, grass, snow...) on\n"
                      "its outputs, ready for a MaterialStack.");
  if (ImGui::MenuItem("Mountain"))
    apply_terrain_style(a, "Mountain",
                        {{"Noise", {}, {{"octaves", 10}}}});
  if (ImGui::MenuItem("Ridged peaks"))
    apply_terrain_style(a, "Ridged peaks",
                        {{"Noise", {}, {{"type", 1}, {"octaves", 11}}},
                         {"Peaks", {{"strength", 0.45f}}, {}}});
  if (ImGui::MenuItem("Eroded mountain"))
    apply_terrain_style(a, "Eroded mountain",
                        {{"Noise", {}, {{"octaves", 10}}},
                         {"Hydraulic", {}, {}},
                         {"Thermal", {}, {}}});
  if (ImGui::MenuItem("Canyon"))
    apply_terrain_style(a, "Canyon",
                        {{"Noise", {}, {{"octaves", 9}}},
                         {"Terrace", {{"shape", 6.f}}, {{"levels", 7}}},
                         {"Dissolve", {{"amount", 0.4f}}, {}}});
  if (ImGui::MenuItem("Dunes"))
    apply_terrain_style(a, "Dunes", {{"Dunes", {}, {}}});
  if (ImGui::MenuItem("Iceberg"))
    apply_terrain_style(
        a, "Iceberg",
        {{"Noise", {}, {{"octaves", 8}}},
         {"TerrainClip", {{"softness", 0.04f}}, {{"high_mode", 1}}},
         {"Glaciation", {{"strength", 0.7f}}, {}}});
  if (ImGui::MenuItem("Lunar"))
    apply_terrain_style(a, "Lunar",
                        {{"Noise", {{"gain", 0.42f}}, {{"octaves", 8}}},
                         {"Crater", {}, {{"profile", 1}}},
                         {"Grit", {{"amount", 0.015f}}, {}}});
  ImGui::Separator();
  ImGui::TextDisabled("Each style drops a fresh chain into the graph\n"
                      "and wires it to the Terrain Output — the old\n"
                      "chain stays in the graph, and Ctrl+Z undoes it.");
  ImGui::EndMenu();
}

} // namespace studio
