// Geekatplay TerraForge — the Material Editor screen.
//
// A second view of the same graph. Nodes remain the truth; this window reads
// the MaterialLayer chain feeding a MaterialOutput and shows it the way every
// image editor shows a stack — top layer first, with visibility, opacity and
// blend on the row, and the reason a layer is invisible readable without
// opening it. Vue's Material Editor (reference manual p683-694, p761-764) is
// the model, minus shared layers and the flat-colour debug view.
//
// Reordering swaps the two layers' settings and their own channel links,
// leaving the chain wiring alone. That is Vue's left-button reorder: the
// environment settings travel with the layer, which is what a user means by
// "move this one up".
#include "app.hpp"
#include "material_stack_ops.hpp"
#include "i18n.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace studio {

void draw_panel_material_editor(App &a) {
  if (!a.show_material_editor) return;
  ImGui::SetNextWindowSize(ImVec2(360, 620), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(tr("Material Editor"), &a.show_material_editor)) {
    ImGui::End();
    return;
  }
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) {
    ImGui::TextDisabled("computing...");
    ImGui::End();
    return;
  }
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    ImGui::TextDisabled("Select an object to edit its material.");
    ImGui::End();
    return;
  }
  SceneObject &obj = sc.objects[sc.selected];
  gpx::Node *mat = a.graph.find_node(obj.material_node);
  if (mat && mat->type != "MaterialOutput") mat = nullptr;
  if (!mat) {
    ImGui::TextDisabled("%s has no material.", obj.name.c_str());
    ImGui::TextWrapped("Create one in the Material tab of the Properties "
                       "editor, then its layers appear here.");
    ImGui::End();
    return;
  }

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
  ImGui::TextUnformatted(obj.name.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", mat->attrs.get_s("name").c_str());

  // ---- preview -----------------------------------------------------------
  static float spin = 0.6f;
  static bool turntable = true;
  spin += turntable ? ImGui::GetIO().DeltaTime * 0.5f : 0.f;
  float avail = ImGui::GetContentRegionAvail().x;
  float side = std::min(avail, 150.f);
  unsigned tex = renderer_material_preview((int)std::max(side, 64.f), 0, spin);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - side) * 0.5f);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1),
               ImVec2(1, 0));
  if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
    spin += ImGui::GetIO().MouseDelta.x * 0.01f;
    turntable = false;
  }

  std::vector<gpx::Node *> layers = collect_layers(a.graph, mat);
  static uint64_t sel = 0;
  bool have_sel = false;
  for (gpx::Node *l : layers) have_sel = have_sel || l->id == sel;
  if (!have_sel) sel = layers.empty() ? 0 : layers.front()->id;

  // ---- stack toolbar -----------------------------------------------------
  ImGui::SeparatorText("Layers");
  bool touched = false;
  if (ImGui::SmallButton("Add")) {
    undo_push_locked(a, "add material layer");
    if (gpx::Node *nl = add_material_layer(a.graph, mat, layers)) sel = nl->id;
    touched = true;
  }
  ImGui::SameLine();
  size_t si = 0;
  while (si < layers.size() && layers[si]->id != sel) ++si;
  bool has = si < layers.size();
  ImGui::BeginDisabled(!has);
  if (ImGui::SmallButton("Up") && si > 0) {
    undo_push_locked(a, "move layer up");
    swap_material_layers(a.graph, layers[si - 1], layers[si]);
    sel = layers[si - 1]->id;
    touched = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Down") && has && si + 1 < layers.size()) {
    undo_push_locked(a, "move layer down");
    swap_material_layers(a.graph, layers[si], layers[si + 1]);
    sel = layers[si + 1]->id;
    touched = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Delete") && has) {
    undo_push_locked(a, "delete material layer");
    delete_material_layer(a.graph, layers[si], mat, layers);
    sel = 0;
    touched = true;
  }
  ImGui::EndDisabled();

  if (touched) {
    a.graph.mark_dirty(mat->id);
    a.graph_layout_serial++;
    a.uploaded_serial = 0;
    layers = collect_layers(a.graph, mat);
  }

  if (layers.empty()) {
    ImGui::TextDisabled("No layers yet.");
    ImGui::TextWrapped("Add one to turn this material into a stack. The "
                       "material's current inputs become the bottom layer, so "
                       "nothing is lost.");
    ImGui::End();
    return;
  }

  // ---- the stack, top first ---------------------------------------------
  float rows = std::min((float)layers.size(), 6.f);
  if (ImGui::BeginChild("stack", ImVec2(0, rows * 46.f + 8.f), true)) {
    for (size_t i = 0; i < layers.size(); ++i) {
      gpx::Node *l = layers[i];
      ImGui::PushID((int)l->id);
      gpx::Attribute *en = l->attrs.find("enabled");
      bool vis = en ? en->b : true;
      if (ImGui::Checkbox("##vis", &vis)) {
        undo_push_locked(a, "layer visibility");
        if (en) en->b = vis;
        l->dirty = true;
        a.graph.mark_dirty(l->id);
        a.uploaded_serial = 0;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide this layer. What is underneath shows through.");
      ImGui::SameLine();
      std::string label = layer_display_name(l, i);
      if (i == 0) label += "   (top)";
      if (ImGui::Selectable(label.c_str(), l->id == sel, 0, ImVec2(0, 0)))
        sel = l->id;
      if (!l->error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.3f, 0.2f, 1.f));
        ImGui::TextWrapped("      %s", l->error.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::TextDisabled("      %.0f%%  %s", l->attrs.get_f("opacity", 1.f) * 100.f,
                            layer_presence_summary(l).c_str());
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  // ---- the selected layer ------------------------------------------------
  gpx::Node *cur = nullptr;
  for (gpx::Node *l : layers)
    if (l->id == sel) cur = l;
  if (!cur) {
    ImGui::End();
    return;
  }
  ImGui::SeparatorText(layer_display_name(cur, si).c_str());
  if (ImGui::SmallButton("Show in node editor")) {
    a.selected_node = cur->id;
    a.prop_tab = 5;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("#%llu", (unsigned long long)cur->id);

  bool changed = false;
  std::string open_group = "\x01";
  bool group_open = true;
  for (auto &at : cur->attrs.items) {
    if (at.group != open_group) {
      open_group = at.group;
      group_open = open_group.empty() ||
                   ImGui::CollapsingHeader(open_group.c_str(),
                                           ImGuiTreeNodeFlags_DefaultOpen);
    }
    if (!group_open) continue;
    if (draw_attribute(at)) changed = true;
  }
  if (changed) {
    cur->dirty = true;
    a.graph.mark_dirty(cur->id);
    a.uploaded_serial = 0;
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Connect this layer's maps and mask in the node editor.");
  ImGui::End();
}

} // namespace studio
