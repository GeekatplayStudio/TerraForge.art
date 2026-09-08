// Geekatplay Studio — node library panel (click to add at view center)
#include "app.hpp"
#include "node_library.hpp"
#include "gpx/node_search.hpp"
#include "undo.hpp"
#include <vector>
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

  auto matches = [&](const std::string &s) {
    if (!filter[0]) return true;
    std::string lt = s, lf = filter;
    for (auto &ch : lt) ch = (char)tolower(ch);
    for (auto &ch : lf) ch = (char)tolower(ch);
    return lt.find(lf) != std::string::npos;
  };

  // Saved MetaNodes sit at the top, beside the built-ins — that is the whole
  // point of saving one: it becomes a node you reach for like any other.
  {
    const std::vector<SavedMetaNode> &lib = node_library();
    int shown = 0;
    for (const SavedMetaNode &m : lib)
      if (matches(m.name)) ++shown;
    if (shown && ImGui::CollapsingHeader("My nodes",
                                         ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent(8);
      std::string to_delete;
      for (const SavedMetaNode &m : lib) {
        if (!matches(m.name)) continue;
        ImGui::PushID(m.path.c_str());
        if (ImGui::Selectable(m.name.c_str())) {
          undo_push(a, "Add " + m.name);
          float x = 40, y = 40;
          {
            std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
            if (!a.graph.nodes.empty()) {
              x = a.graph.nodes.back()->pos_x + 220;
              y = a.graph.nodes.back()->pos_y;
            }
          }
          std::string err;
          unsigned long long id = node_library_load(a, m.path, x, y, err);
          if (id) {
            a.selected_node = id;
            a.status = "added " + m.name;
          } else {
            a.status = "could not add " + m.name + ": " + err;
          }
        }
        if (ImGui::IsItemHovered()) {
          std::string tip = m.note.empty() ? std::string("Saved node group")
                                           : m.note;
          tip += "\n" + std::to_string(m.inner_nodes) + " nodes inside";
          if (m.published)
            tip += ", " + std::to_string(m.published) + " exposed parameter" +
                   (m.published == 1 ? "" : "s");
          tip += "\nright-click to remove from the library";
          ImGui::SetTooltip("%s", tip.c_str());
        }
        if (ImGui::BeginPopupContextItem("##ctx")) {
          if (ImGui::MenuItem("Remove from library")) to_delete = m.path;
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
      ImGui::Unindent(8);
      if (!to_delete.empty()) {
        node_library_delete(to_delete);
        a.status = "removed from the library";
      }
    }
  }

  // Adding a node, wherever the click came from.
  auto add_node = [&](const std::string &type) {
    undo_push(a, "Add " + type);
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    float x = 40, y = 40;
    if (!a.graph.nodes.empty()) {
      x = a.graph.nodes.back()->pos_x + 220;
      y = a.graph.nodes.back()->pos_y;
    }
    if (gpx::Node *n = a.graph.add_node(type, x, y)) {
      a.selected_node = n->id;
      a.request_eval();
    }
  };
  auto row = [&](const gpx::NodeDef *d, const std::string &label) {
    if (ImGui::Selectable(label.c_str())) add_node(d->type);
    if (ImGui::IsItemHovered() && !d->description.empty())
      ImGui::SetTooltip("%s", d->description.c_str());
  };

  // Typing turns the list into a search. A substring filter can only find a
  // node whose name you already know; this ranks the whole catalogue by what
  // the words mean, so "rocks" finds the stone nodes and "wear down the
  // mountains" finds the erosion ones. Ranked flat, because an ordering is
  // the answer and re-grouping it by category would throw that away.
  if (filter[0]) {
    const auto hits = gpx::search::find_nodes(filter, 40);
    int shown = 0;
    for (const auto &h : hits) {
      const gpx::NodeDef *d = gpx::NodeRegistry::instance().find(h.type);
      if (!d || domain_of_category(d->category) != a.workspace) continue;
      ++shown;
      ImGui::Indent(8);
      row(d, h.label);
      ImGui::SameLine();
      ImGui::TextDisabled("%s", d->category.c_str());
      ImGui::Unindent(8);
    }
    if (!shown) {
      ImGui::Indent(8);
      ImGui::TextDisabled("nothing matches in this workspace");
      ImGui::Unindent(8);
    } else {
      // Say what it searched for as well as what it found, so a surprising
      // result is explainable rather than mysterious.
      const auto words = gpx::search::expand_query(filter);
      if (words.size() > 1) {
        std::string also;
        for (size_t i = 1; i < words.size() && i < 8; ++i)
          also += (also.empty() ? "" : ", ") + words[i];
        if (!also.empty()) {
          ImGui::Separator();
          ImGui::TextDisabled("also searched: %s", also.c_str());
        }
      }
    }
    ImGui::End();
    return;
  }

  std::string last_cat;
  bool open = true;
  for (const gpx::NodeDef *d : gpx::NodeRegistry::instance().all()) {
    if (domain_of_category(d->category) != a.workspace) continue;
    if (d->category != last_cat) {
      open = ImGui::CollapsingHeader(d->category.c_str(),
                                     ImGuiTreeNodeFlags_DefaultOpen);
      last_cat = d->category;
    }
    if (!open) continue;
    ImGui::Indent(8);
    row(d, gpx::node_display_name(d->type));
    ImGui::Unindent(8);
  }
  ImGui::End();
}

} // namespace studio



