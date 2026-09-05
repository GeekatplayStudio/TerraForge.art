// Geekatplay TerraForge - the Material Hierarchy: the list in the middle of
// Vue's Material Editor (manual p690-691, p762) with the material, its
// layers top first and the materials a mix mixes. Click a line and the tabs
// below edit that layer or sub-material. Each line has the three-state
// visibility switch - normal, invisible, highlighted in a solid colour - and
// the Add / Remove / Up / Down buttons work on the selected layer.
#include "app.hpp"
#include "material_channel_ops.hpp"
#include "material_stack_ops.hpp"
#include "material_ui.hpp"
#include "undo.hpp"
#include <imgui.h>

namespace studio {

namespace {

void after_edit(App &a) {
  a.graph_layout_serial++;
  a.request_eval();
  a.uploaded_serial = 0;
}

// the switch: a small square that cycles normal -> invisible -> highlighted
void visibility_switch(App &a, gpx::Node *n) {
  int state = hier_visibility(n);
  ImVec4 col = state == 0 ? ImVec4(0.75f, 0.75f, 0.75f, 1.f)
               : state == 1 ? ImVec4(0.3f, 0.3f, 0.3f, 1.f)
                            : ImVec4(1.f, 0.25f, 0.9f, 1.f);
  if (state == 2)
    if (const gpx::Attribute *hc = n->attrs.find("highlight_color"))
      col = ImVec4(hc->col[0], hc->col[1], hc->col[2], 1.f);
  ImGui::PushStyleColor(ImGuiCol_Button, col);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
  if (ImGui::Button("##vis", ImVec2(14, 14))) {
    int next = (state + 1) % 3;
    if (next == 2 && n->type != "MaterialLayer") next = 0; // only a layer can be highlighted
    undo_push_locked(a, next == 1 ? "hide" : next == 2 ? "highlight" : "show");
    hier_set_visibility(n, next);
    a.graph.mark_dirty(n->id);
    after_edit(a);
  }
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s\nClick to cycle: visible, invisible, highlighted.",
                      state == 0 ? "Visible" : state == 1 ? "Invisible at render" : "Highlighted");
  if (state == 2 && ImGui::BeginPopupContextItem("##hcol")) {
    if (gpx::Attribute *hc = n->attrs.find("highlight_color"))
      if (ImGui::ColorEdit3("Highlight color", hc->col, ImGuiColorEditFlags_NoInputs)) {
        a.graph.mark_dirty(n->id);
        after_edit(a);
      }
    ImGui::EndPopup();
  }
}

} // namespace

void material_hierarchy_ui(App &a, gpx::Node *mat, float height) {
  MaterialStudioState &st = material_studio();
  std::vector<MatHierItem> items = material_hierarchy(a.graph, mat);
  bool have_sel = false;
  for (const MatHierItem &it : items)
    if (it.node == st.selected) have_sel = true;
  if (!have_sel) st.selected = mat ? mat->id : 0;

  ImGui::BeginChild("##hier", ImVec2(0, height), ImGuiChildFlags_Borders);
  for (size_t i = 0; i < items.size(); ++i) {
    const MatHierItem &it = items[i];
    gpx::Node *n = a.graph.find_node(it.node);
    if (!n) continue;
    ImGui::PushID((int)i);
    if (it.kind != 0) visibility_switch(a, n);
    else ImGui::Dummy(ImVec2(14, 14));
    ImGui::SameLine();
    ImGui::Indent(12.f * it.depth);
    if (ImGui::Selectable(it.label.c_str(), st.selected == it.node)) {
      st.selected = it.node;
      a.selected_node = it.node;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) graph_focus_node(a, it.node);
    if (it.kind == 1) {
      ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.6f);
      ImGui::TextDisabled("%s", layer_presence_summary(n).c_str());
    }
    ImGui::Unindent(12.f * it.depth);
    ImGui::PopID();
  }
  ImGui::EndChild();

  // Vue's buttons beside the hierarchy
  std::vector<gpx::Node *> layers = collect_layers(a.graph, mat);
  gpx::Node *sel = a.graph.find_node(st.selected);
  const bool sel_is_layer = sel && sel->type == "MaterialLayer";
  if (ImGui::SmallButton("Add layer")) {
    undo_push_locked(a, "add material layer");
    gpx::Node *nl = add_material_layer(a.graph, mat, layers);
    if (nl) st.selected = nl->id;
    after_edit(a);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A new layer on top of the stack. A simple material becomes a layered one.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!sel_is_layer);
  if (ImGui::SmallButton("Remove")) {
    undo_push_locked(a, "remove material layer");
    delete_material_layer(a.graph, sel, mat, layers);
    st.selected = mat->id;
    after_edit(a);
  }
  ImGui::SameLine();
  size_t idx = 0;
  for (size_t i = 0; i < layers.size(); ++i)
    if (layers[i] == sel) idx = i;
  ImGui::BeginDisabled(!sel_is_layer || idx == 0);
  if (ImGui::SmallButton("Up")) {
    undo_push_locked(a, "layer up");
    swap_material_layers(a.graph, layers[idx - 1], layers[idx]);
    st.selected = layers[idx - 1]->id;
    after_edit(a);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!sel_is_layer || idx + 1 >= layers.size());
  if (ImGui::SmallButton("Down")) {
    undo_push_locked(a, "layer down");
    swap_material_layers(a.graph, layers[idx], layers[idx + 1]);
    st.selected = layers[idx + 1]->id;
    after_edit(a);
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Up is evaluated earlier: it appears on top. The environment settings travel with the layer.");
}

} // namespace studio
