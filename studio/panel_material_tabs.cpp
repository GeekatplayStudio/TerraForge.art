// Geekatplay TerraForge - the Material Studio's property tabs, laid out as
// Vue's Material Editor lays them out: Color, Bump, Highlights, Transparency,
// Reflection, Translucency, Effects - plus Layers, Population or Effector
// when the material's type calls for one.
//
// Every control edits an attribute on the MaterialOutput node (declared in
// engine/material_params.cpp, so the label, range and tooltip live in one
// place), and every channel has a mode - None, Picture, Procedural - that
// connects or disconnects a node behind it. The graph stays the truth.
#include "app.hpp"
#include "material_stack_ops.hpp"
#include "material_ui.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

namespace {

void touched(App &a, gpx::Node *mat) {
  a.graph.mark_dirty(mat->id);
  a.request_eval();
  a.uploaded_serial = 0;
}

// One attribute as the widget its type wants, label on the left.
void attr_widget(App &a, gpx::Node *mat, const char *key, float label_w) {
  gpx::Attribute *at = mat->attrs.find(key);
  if (!at) return;
  ImGui::PushID(key);
  bool changed = false;
  ImGui::SetNextItemWidth(-label_w);
  switch (at->type) {
  case gpx::AttrType::Float:
    changed = ImGui::SliderFloat(at->label.c_str(), &at->f, at->fmin, at->fmax,
                                 "%.3f", at->log_scale ? ImGuiSliderFlags_Logarithmic : 0);
    break;
  case gpx::AttrType::Bool:
    changed = studio::Checkbox(at->label.c_str(), &at->b);
    break;
  case gpx::AttrType::Color:
    changed = ImGui::ColorEdit3(at->label.c_str(), at->col, ImGuiColorEditFlags_NoInputs);
    break;
  case gpx::AttrType::Choice: {
    std::vector<const char *> items;
    for (const std::string &s : at->labels) items.push_back(s.c_str());
    changed = ImGui::Combo(at->label.c_str(), &at->i, items.data(), (int)items.size());
    break;
  }
  case gpx::AttrType::Vec2:
    changed = ImGui::DragFloat2(at->label.c_str(), at->v2, 0.01f, at->v2min, at->v2max);
    break;
  default:
    break;
  }
  if (ImGui::IsItemHovered() && !at->tooltip.empty()) ImGui::SetTooltip("%s", at->tooltip.c_str());
  if (changed) touched(a, mat);
  ImGui::PopID();
}

// A channel's mode: what feeds it, and a way to change that. "Picture" adds
// a TextureFile node with a file the user picks; "Procedural" adds a
// FractalColor; "None" disconnects and leaves the node in the graph.
void channel_mode(App &a, gpx::Node *mat, const char *port, const char *human) {
  gpx::Node *src = a.graph.upstream_node(*mat, port);
  ImGui::PushID(port);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(human);
  ImGui::SameLine(150);
  const char *mode = !src ? "None" : (src->type == "TextureFile" ? "Picture" : "Procedural");
  std::string shown = src ? std::string(mode) + ": " + src->type + " #" +
                                std::to_string(src->id)
                          : std::string(mode);
  ImGui::SetNextItemWidth(230);
  if (ImGui::BeginCombo("##mode", shown.c_str())) {
    if (ImGui::Selectable("None", !src) && src) {
      undo_push_locked(a, std::string("disconnect ") + human);
      if (gpx::Link *lk = layer_incoming(a.graph, mat->id, port)) a.graph.remove_link(lk->id);
      touched(a, mat);
    }
    if (ImGui::Selectable("Picture...", src && src->type == "TextureFile")) {
      std::string path = dialog_open_file("Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0", "png");
      if (!path.empty()) {
        undo_push_locked(a, std::string("picture for ") + human);
        gpx::Node *tf = a.graph.add_node("TextureFile", mat->pos_x - 280, mat->pos_y + 40);
        if (tf) {
          if (gpx::Attribute *pa = tf->attrs.find("path")) pa->s = path;
          if (src) if (gpx::Link *lk = layer_incoming(a.graph, mat->id, port)) a.graph.remove_link(lk->id);
          a.graph.add_link(tf->id, "texture", mat->id, port);
          a.graph_layout_serial++;
          touched(a, mat);
        }
      }
    }
    if (ImGui::Selectable("Procedural", src && src->type != "TextureFile")) {
      if (!src || src->type == "TextureFile") {
        undo_push_locked(a, std::string("procedural ") + human);
        gpx::Node *fc = a.graph.add_node("FractalColor", mat->pos_x - 280, mat->pos_y + 40);
        if (fc) {
          if (src) if (gpx::Link *lk = layer_incoming(a.graph, mat->id, port)) a.graph.remove_link(lk->id);
          a.graph.add_link(fc->id, "texture", mat->id, port);
          a.graph_layout_serial++;
          touched(a, mat);
        }
      }
    }
    ImGui::EndCombo();
  }
  if (src) {
    ImGui::SameLine();
    if (ImGui::SmallButton("edit")) graph_focus_node(a, src->id);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the node and show it in the graph.");
  }
  ImGui::PopID();
}

void group(App &a, gpx::Node *mat, const char *group_name, float lw) {
  for (gpx::Attribute &at : mat->attrs.items)
    if (at.group == group_name) attr_widget(a, mat, at.key.c_str(), lw);
}

void tab_color(App &a, gpx::Node *mat, float lw) {
  channel_mode(a, mat, "base color", "Color");
  channel_mode(a, mat, "ambient occlusion", "Ambient occlusion");
  ImGui::Spacing();
  group(a, mat, "Color", lw);
}
void tab_bump(App &a, gpx::Node *mat, float lw) {
  channel_mode(a, mat, "normal", "Normal map");
  channel_mode(a, mat, "height", "Displacement");
  ImGui::Spacing();
  group(a, mat, "Bump", lw);
}
void tab_highlights(App &a, gpx::Node *mat, float lw) {
  channel_mode(a, mat, "roughness", "Roughness map");
  ImGui::Spacing();
  group(a, mat, "Highlights", lw);
}
void tab_reflection(App &a, gpx::Node *mat, float lw) {
  channel_mode(a, mat, "metallic", "Metalness map");
  ImGui::Spacing();
  group(a, mat, "Reflection", lw);
}

void tab_layers(App &a, gpx::Node *mat) {
  std::vector<gpx::Node *> layers = collect_layers(a.graph, mat);
  if (ImGui::SmallButton("Add layer")) {
    undo_push_locked(a, "add material layer");
    add_material_layer(a.graph, mat, layers);
    a.graph_layout_serial++;
    a.request_eval();
    return;
  }
  for (size_t i = 0; i < layers.size(); ++i) {
    gpx::Node *l = layers[i];
    ImGui::PushID((int)i);
    std::string name = layer_display_name(l, i);
    if (ImGui::Selectable(name.c_str(), a.selected_node == l->id)) a.selected_node = l->id;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::TextDisabled("%s", layer_presence_summary(l).c_str());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Its presence rules. Select the layer and edit them\n"
                        "in Properties, or double-click to open its node.");
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) graph_focus_node(a, l->id);
    ImGui::PopID();
  }
  if (layers.empty()) ImGui::TextDisabled("No layers yet.");
  // the selected layer's own properties, right here
  if (a.selected_node && a.selected_node != mat->id)
    for (gpx::Node *l : layers)
      if (l->id == a.selected_node) {
        ImGui::Separator();
        node_properties_ui(a, l->id, true);
      }
}

} // namespace

void material_tabs_ui(App &a, gpx::Node *mat) {
  const float lw = 170.f;
  int type = material_type_of(a.graph, mat);
  if (!ImGui::BeginTabBar("##mattabs", ImGuiTabBarFlags_FittingPolicyScroll)) return;
  struct Tab {
    const char *name;
    void (*fn)(App &, gpx::Node *, float);
    const char *group;
  };
  const Tab tabs[] = {{"Color", tab_color, nullptr},
                      {"Bump", tab_bump, nullptr},
                      {"Highlights", tab_highlights, nullptr},
                      {"Transparency", nullptr, "Transparency"},
                      {"Reflection", tab_reflection, nullptr},
                      {"Translucency", nullptr, "Translucency"},
                      {"Effects", nullptr, "Effects"}};
  for (const Tab &t : tabs)
    if (ImGui::BeginTabItem(t.name)) {
      ImGui::BeginChild("##tab", ImVec2(0, 0));
      if (t.fn) t.fn(a, mat, lw);
      else group(a, mat, t.group, lw);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  if (type == MAT_LAYERED && ImGui::BeginTabItem("Layers")) {
    ImGui::BeginChild("##tab");
    tab_layers(a, mat);
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  if ((type == MAT_DISTRIBUTION || type == MAT_EFFECTOR) &&
      ImGui::BeginTabItem(type == MAT_DISTRIBUTION ? "Population" : "Effector")) {
    ImGui::BeginChild("##tab");
    if (gpx::Node *src = a.graph.upstream_node(*mat, "base color"))
      node_properties_ui(a, src->id, true);
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  if (type == MAT_MIXED && ImGui::BeginTabItem("Mix")) {
    ImGui::BeginChild("##tab");
    if (gpx::Node *src = a.graph.upstream_node(*mat, "base color"))
      node_properties_ui(a, src->id, true);
    ImGui::EndChild();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

} // namespace studio
