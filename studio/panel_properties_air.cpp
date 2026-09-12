// Geekatplay TerraForge - the bands of air, in the properties panel
// (scene.hpp: SceneObject::AirLayer).
//
// An atmosphere shows what is in it and lets a band be added or taken away;
// a band shows what it is. The numbers live on the graph node that drives the
// object, so this panel edits the node - which is why a change here shows up
// in the node editor and a change there shows up here, and why neither is a
// copy of the other.
#include "app.hpp"
#include "icons.hpp"
#include "prop_lengths.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include "wheel_widgets.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace studio {

namespace {

// One attribute of the driving node, drawn by what it is.
void attr_row(gpx::Attribute &at) {
  const char *label = at.label.empty() ? at.key.c_str() : at.label.c_str();
  const std::string id = "##air_" + at.key;
  switch (at.type) {
    case gpx::AttrType::Float: {
      float v = at.f;
      if (studio::SliderFloatW((label + id).c_str(), &v, at.fmin, at.fmax)) at.f = v;
      break;
    }
    case gpx::AttrType::Int: {
      int v = at.i;
      if (ImGui::SliderInt((label + id).c_str(), &v, at.imin, at.imax)) at.i = v;
      break;
    }
    case gpx::AttrType::Bool: {
      bool v = at.b;
      if (studio::Checkbox((label + id).c_str(), &v)) at.b = v;
      break;
    }
    case gpx::AttrType::Choice: {
      std::string items;
      for (const std::string &l : at.labels) {
        items += l;
        items.push_back('\\0');
      }
      items.push_back('\\0');
      int v = at.i;
      if (ImGui::Combo((label + id).c_str(), &v, items.c_str())) at.i = v;
      break;
    }
    default: return;
  }
  if (!at.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", at.tooltip.c_str());
}

} // namespace

// What is in this atmosphere, and the way to put more in it.
void object_air_layers(App &a, int atmosphere_idx) {
  SceneState &sc = scene();
  const std::vector<int> layers = scene_air_layers(atmosphere_idx);
  ImGui::SeparatorText("Layers");
  if (layers.empty())
    ImGui::TextDisabled("No layers yet. A sky is several at once:\n"
                        "low cloud under high, haze under a valley fog.");
  for (int idx : layers) {
    if (idx < 0 || idx >= (int)sc.objects.size()) continue;
    SceneObject &L = sc.objects[(size_t)idx];
    ImGui::PushID(idx);
    bool vis = L.visible;
    if (studio::Checkbox("##vis", &vis)) L.visible = vis;
    ImGui::SameLine();
    if (ImGui::Selectable(L.name.c_str(), sc.selected == idx, 0, ImVec2(0, 0))) {
      sc.selected = idx;
      sc.selection = {idx};
      a.scene_selection_serial++;
    }
    ImGui::PopID();
  }
  ImGui::Spacing();
  if (ImGui::Button("Add cloud layer")) {
    undo_push(a, "Add a cloud layer");
    const int idx = scene_add_air_layer(a, (int)SceneObject::AirLayerData::Cloud, atmosphere_idx);
    if (idx >= 0) {
      sc.selected = idx;
      sc.selection = {idx};
      a.scene_selection_serial++;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Add fog layer")) {
    undo_push(a, "Add a fog layer");
    const int idx = scene_add_air_layer(a, (int)SceneObject::AirLayerData::Fog, atmosphere_idx);
    if (idx >= 0) {
      sc.selected = idx;
      sc.selection = {idx};
      a.scene_selection_serial++;
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Air is never one band: haze to the horizon, a fog lying\n"
                      "in the valley, a brown layer over a town. Every layer is\n"
                      "in the air at once, and they add up the way air does.");
}

// One band: whatever its node says it is.
void object_air_layer(App &a, SceneObject &o) {
  const bool fog = o.air.kind == SceneObject::AirLayerData::Fog;
  gpx::Node *n = o.driver_node ? a.graph.find_node(o.driver_node) : nullptr;
  if (!n) {
    ImGui::TextDisabled("This layer has lost the node that holds its settings.");
    return;
  }
  ImGui::TextDisabled(fog ? "A band of air: haze, fog or pollution at its own height."
                          : "A cloud deck at its own altitude and thickness.");
  // grouped the way the node groups them, so the two panels read alike
  std::string group;
  for (gpx::Attribute &at : n->attrs.items) {
    if (at.group != group) {
      group = at.group;
      if (!group.empty()) ImGui::SeparatorText(group.c_str());
    }
    attr_row(at);
  }
  a.graph.mark_dirty(n->id);
}

} // namespace studio
