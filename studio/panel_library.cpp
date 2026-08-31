// Geekatplay Studio â€” node library panel (click to add at view center)
#include "app.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <string>

namespace studio {

void draw_panel_library(App &a) {
  if (!ImGui::Begin("Library", &a.show_library)) {
    ImGui::End();
    return;
  }
  static char filter[64] = "";
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##libfilter", "filter...", filter, sizeof filter);
  ImGui::Separator();
  std::string last_cat;
  bool open = true;
  for (const gpx::NodeDef *d : gpx::NodeRegistry::instance().all()) {
    if (domain_of_category(d->category) != a.workspace) continue;
    if (filter[0]) {
      std::string lt = d->type, lf = filter;
      for (auto &ch : lt) ch = (char)tolower(ch);
      for (auto &ch : lf) ch = (char)tolower(ch);
      if (lt.find(lf) == std::string::npos) continue;
    }
    if (d->category != last_cat) {
      open = ImGui::CollapsingHeader(d->category.c_str(),
                                     ImGuiTreeNodeFlags_DefaultOpen);
      last_cat = d->category;
    }
    if (!open) continue;
    ImGui::Indent(8);
    if (ImGui::Selectable(d->type.c_str())) {
      undo_push(a, "Add " + d->type);
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      // place near last node or at origin
      float x = 40, y = 40;
      if (!a.graph.nodes.empty()) {
        x = a.graph.nodes.back()->pos_x + 220;
        y = a.graph.nodes.back()->pos_y;
      }
      gpx::Node *n = a.graph.add_node(d->type, x, y);
      if (n) {
        a.selected_node = n->id;
        a.request_eval();
      }
    }
    if (ImGui::IsItemHovered() && !d->description.empty())
      ImGui::SetTooltip("%s", d->description.c_str());
    ImGui::Unindent(8);
  }
  ImGui::End();
}

} // namespace studio


