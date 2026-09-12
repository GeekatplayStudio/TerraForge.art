// Geekatplay TerraForge - the bands of air, in the properties panel
// (scene.hpp: SceneObject::AirLayer).
//
// An atmosphere shows what is in it and lets a band be added or taken away;
// a band shows what it is. The numbers live on the graph node that drives the
// object, so this panel edits the node - which is why a change here shows up
// in the node editor and a change there shows up here, and why neither is a
// copy of the other.
#include "app.hpp"
#include "combo_items.hpp"
#include "icons.hpp"
#include "prop_lengths.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include "wheel_widgets.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext); // file_dialogs.cpp

namespace {

// One attribute of the driving node, drawn by what it is. True when the
// person changed it, which is the only time the graph should be disturbed.
bool attr_row(gpx::Attribute &at) {
  const char *label = at.label.empty() ? at.key.c_str() : at.label.c_str();
  const std::string id = "##air_" + at.key;
  bool changed = false;
  switch (at.type) {
    case gpx::AttrType::Float: {
      float v = at.f;
      if (studio::SliderFloatW((label + id).c_str(), &v, at.fmin, at.fmax)) {
        at.f = v;
        changed = true;
      }
      break;
    }
    case gpx::AttrType::Int: {
      int v = at.i;
      if (ImGui::SliderInt((label + id).c_str(), &v, at.imin, at.imax)) {
        at.i = v;
        changed = true;
      }
      break;
    }
    case gpx::AttrType::Bool: {
      bool v = at.b;
      if (studio::Checkbox((label + id).c_str(), &v)) {
        at.b = v;
        changed = true;
      }
      break;
    }
    case gpx::AttrType::Choice: {
      int v = at.i;
      const std::string items = combo_items(at.labels);
      if (ImGui::Combo((label + id).c_str(), &v, items.c_str())) {
        at.i = v;
        changed = true;
      }
      break;
    }
    case gpx::AttrType::Filename: {
      // A cloud layer may be shaped by a picture, and the place to choose it
      // is beside the rest of that layer's settings - not in the node editor,
      // which is not where anyone looks for the sky.
      char buf[512];
      std::snprintf(buf, sizeof buf, "%s", at.s.c_str());
      ImGui::TextUnformatted(label);
      ImGui::SetNextItemWidth(-58);
      if (ImGui::InputText((id + "_t").c_str(), buf, sizeof buf)) {
        at.s = buf;
        changed = true;
      }
      ImGui::SameLine(0, 2);
      if (ImGui::Button(("..." + id).c_str(), ImVec2(24, 0))) {
        const std::string pick = dialog_open_file(
            "Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All files\0*.*\0\0", nullptr);
        if (!pick.empty()) {
          at.s = pick;
          changed = true;
        }
      }
      if (!at.s.empty()) {
        ImGui::SameLine(0, 2);
        if (ImGui::Button(("x" + id).c_str(), ImVec2(24, 0))) {
          at.s.clear();
          changed = true;
        }
      }
      break;
    }
    default: return false;
  }
  if (!at.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", at.tooltip.c_str());
  return changed;
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
  bool changed = false;
  for (gpx::Attribute &at : n->attrs.items) {
    if (at.group != group) {
      group = at.group;
      if (!group.empty()) ImGui::SeparatorText(group.c_str());
    }
    changed = attr_row(at) || changed;
  }
  // Only when something moved. Marking the node dirty every frame the panel
  // drew re-evaluated the graph continuously - the whole scene, several times
  // a second, for as long as the layer was selected.
  if (changed) {
    a.graph.mark_dirty(n->id);
    a.request_eval();
  }
}

} // namespace studio
