// Geekatplay TerraForge - the Material Editor's tabs for a Mixed material
// (Vue manual p747-752: Materials to Mix, Alpha, Influence of Environment),
// a layer's Presence tab (p736-738: altitude, slope and orientation
// constraints) and the parameters of a driving node - the population of a
// distribution layer, an effector, a mixed sub-material.
#include "app.hpp"
#include "material_channel_ops.hpp"
#include "material_ui.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <string>

namespace studio {

namespace {

const float LW = 170.f;

void group(App &a, gpx::Node *n, const char *g) {
  for (gpx::Attribute &at : n->attrs.items)
    if (at.group == g) material_attr_widget(a, n, at.key.c_str(), LW);
}
void keys(App &a, gpx::Node *n, std::initializer_list<const char *> ks) {
  for (const char *k : ks) material_attr_widget(a, n, k, LW);
}

// one of the two mixed materials: what feeds it, a preview, and Edit
void mixed_slot(App &a, gpx::Node *stack, int k) {
  const char *port = k == 1 ? "albedo 1" : "albedo 2";
  ImGui::PushID(port);
  ImGui::BeginGroup();
  ImGui::Text("Material %d", k);
  gpx::Node *src = a.graph.upstream_node(*stack, port);
  unsigned tex = src ? previews_get(src->id) : 0;
  if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(96, 96));
  else ImGui::Button("empty", ImVec2(96, 96));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Double-click to edit this material in the hierarchy.");
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && src)
    material_studio().selected = src->id;
  material_channel_ui(a, stack, port, "Source", CHAN_COLOR, nullptr);
  ImGui::EndGroup();
  ImGui::PopID();
}

void tab_materials_to_mix(App &a, gpx::Node *stack) {
  if (stack->attrs.get_choice("mix_mode") != 1) {
    ImGui::TextDisabled("This stack blends up to six layers by their masks. Vue's two-material "
                        "mix is the second mode:");
    material_attr_widget(a, stack, "mix_mode", LW);
    ImGui::Separator();
    for (int k = 1; k <= 6; ++k) {
      std::string port = "albedo " + std::to_string(k);
      if (a.graph.upstream_node(*stack, port.c_str()) || k <= 2)
        material_channel_ui(a, stack, port.c_str(), port.c_str(), CHAN_COLOR, nullptr);
    }
    group(a, stack, "Blending");
    return;
  }
  mixed_slot(a, stack, 1);
  ImGui::SameLine(0, 24);
  mixed_slot(a, stack, 2);
  if (ImGui::SmallButton("Swap")) {
    undo_push_locked(a, "swap mixed materials");
    gpx::Node *s1 = a.graph.upstream_node(*stack, "albedo 1");
    gpx::Node *s2 = a.graph.upstream_node(*stack, "albedo 2");
    std::vector<uint64_t> drop;
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == stack->id && (l.to_port == "albedo 1" || l.to_port == "albedo 2"))
        drop.push_back(l.id);
    for (uint64_t id : drop) a.graph.remove_link(id);
    if (s2) a.graph.add_link(s2->id, "texture", stack->id, "albedo 1");
    if (s1) a.graph.add_link(s1->id, "texture", stack->id, "albedo 2");
    a.graph.mark_dirty(stack->id);
    a.graph_layout_serial++;
    a.request_eval();
  }
  ImGui::SeparatorText("Distribution of materials");
  material_channel_ui(a, stack, "mask 1", "Distribution function", CHAN_VALUE, nullptr);
  keys(a, stack, {"proportion", "strip", "blend_method", "mix_mode"});
}

void tab_mix_alpha(App &a, gpx::Node *stack) {
  ImGui::TextDisabled("The mix's own alpha: where it is absent, the layer below shows.");
  material_channel_ui(a, stack, "mask 2", "Alpha production", CHAN_ALPHA, nullptr);
}

void tab_environment(App &a, gpx::Node *stack) {
  material_channel_ui(a, stack, "terrain", "Terrain (altitude and slope source)", CHAN_VALUE,
                      nullptr);
  ImGui::SeparatorText("Influence of environment");
  group(a, stack, "Environment");
  ImGui::TextDisabled("Altitude: positive puts material 2 higher. Slope: positive puts it on "
                      "steep faces. Orientation: on faces looking toward the azimuth.");
}

} // namespace

void material_tabs_mix_ui(App &a, gpx::Node *stack) {
  struct Tab {
    const char *name;
    void (*fn)(App &, gpx::Node *);
  } tabs[] = {{"Materials to mix", tab_materials_to_mix},
              {"Alpha", tab_mix_alpha},
              {"Influence of environment", tab_environment}};
  for (const Tab &t : tabs)
    if (ImGui::BeginTabItem(t.name)) {
      ImGui::BeginChild("##tab");
      t.fn(a, stack);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
}

void material_tab_presence_ui(App &a, gpx::Node *layer) {
  ImGui::TextDisabled("Where this layer appears, by the environment. The bottom layer is "
                      "everywhere.");
  material_channel_ui(a, layer, "terrain", "Terrain", CHAN_VALUE, nullptr);
  ImGui::SeparatorText("Altitude constraint");
  group(a, layer, "Altitude");
  ImGui::SeparatorText("Slope constraint");
  group(a, layer, "Slope");
  ImGui::SeparatorText("Orientation constraint");
  group(a, layer, "Orientation");
  ImGui::SeparatorText("Alpha boost");
  keys(a, layer, {"alpha_boost", "opacity"});
}

void material_tab_population_ui(App &a, gpx::Node *src) {
  unsigned tex = previews_get(src->id);
  if (tex) {
    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(96, 96));
    ImGui::SameLine();
  }
  ImGui::BeginGroup();
  ImGui::Text("%s #%llu", src->type.c_str(), (unsigned long long)src->id);
  if (ImGui::SmallButton("Edit in the graph")) graph_focus_node(a, src->id);
  ImGui::EndGroup();
  std::string last;
  for (gpx::Attribute &at : src->attrs.items) {
    if (at.type == gpx::AttrType::Field) continue;
    if (at.group != last) {
      last = at.group;
      if (!last.empty()) ImGui::SeparatorText(last.c_str());
    }
    material_attr_widget(a, src, at.key.c_str(), LW);
  }
  // a driving node's own inputs, as channels
  for (const gpx::Port &p : src->ports)
    if (p.dir == gpx::PortDir::In &&
        (p.type == gpx::DataType::Texture || p.type == gpx::DataType::Heightmap))
      material_channel_ui(a, src, p.name.c_str(), p.name.c_str(),
                          p.type == gpx::DataType::Texture ? CHAN_COLOR : CHAN_VALUE, nullptr);
}

} // namespace studio
