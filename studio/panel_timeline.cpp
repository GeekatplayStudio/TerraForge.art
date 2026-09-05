// Geekatplay TerraForge - the Timeline (dope sheet).
//
// Transport on top; below it a two-column table: the track tree on the
// left (owner > group > property component), the key rows on the right,
// with the ruler frozen as the first row. Everything in the key column is
// drawn by hand on the row's draw list so the rows stay aligned with the
// tree for free. Key interaction is in panel_timeline_keys.cpp, the
// transport and ruler in panel_timeline_transport.cpp, the curve view in
// panel_curve_editor.cpp. See docs/ANIMATION.md.
#include "anim_widgets.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "panel_float.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "timeline_internal.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <map>

namespace studio {

TimelineState &tl_state() {
  static TimelineState s;
  return s;
}

bool tl_is_selected(const TimelineState &s, const std::string &track, float time) {
  for (const KeySel &k : s.sel)
    if (k.track == track && std::fabs(k.time - time) < 1e-4f) return true;
  return false;
}
void tl_select(TimelineState &s, const std::string &track, float time, bool add) {
  if (!add) s.sel.clear();
  if (!tl_is_selected(s, track, time)) s.sel.push_back({track, time});
}
void tl_deselect(TimelineState &s, const std::string &track, float time) {
  s.sel.erase(std::remove_if(s.sel.begin(), s.sel.end(),
                             [&](const KeySel &k) { return k.track == track && std::fabs(k.time - time) < 1e-4f; }),
              s.sel.end());
}
void tl_clear_selection(TimelineState &s) { s.sel.clear(); }

namespace {

bool matches_filter(const TrackRef &r, const char *f) {
  if (!f[0]) return true;
  auto has = [&](const std::string &s) {
    std::string a = s, b = f;
    for (char &c : a) c = (char)tolower((unsigned char)c);
    for (char &c : b) c = (char)tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
  };
  return has(r.owner) || has(r.group) || has(r.label);
}

std::string comp_suffix(const TrackRef &r) {
  if (r.comp < 0) return "";
  const char *xyz[] = {" X", " Y", " Z"}, *rgb[] = {" R", " G", " B"};
  return (r.color ? rgb : xyz)[std::clamp(r.comp, 0, 2)];
}

// The tree: owners, their groups, their tracks; every row's key area drawn
// in column 1. Rows are recorded in state.rows for prev/next and box select.
void draw_tree(App &a, std::vector<TrackRef> &tracks) {
  TimelineState &s = tl_state();
  s.rows.clear();
  // group by owner, then by group, keeping first-seen order
  std::vector<std::string> owners;
  std::map<std::string, std::vector<std::string>> groups;
  std::map<std::string, std::vector<TrackRef *>> by_group;
  for (TrackRef &r : tracks) {
    if (!matches_filter(r, s.filter)) continue;
    if (std::find(owners.begin(), owners.end(), r.owner) == owners.end()) owners.push_back(r.owner);
    std::string gk = r.owner + "\n" + r.group;
    if (!by_group.count(gk)) groups[r.owner].push_back(r.group);
    by_group[gk].push_back(&r);
  }
  const float w = ImGui::GetContentRegionAvail().x;
  (void)w;
  for (const std::string &owner : owners) {
    ImGui::TableNextRow(0, s.row_h);
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(owner.c_str());
    bool open = ImGui::TreeNodeEx(owner.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
    // clicking an owner selects the object
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
      for (TrackRef *r : by_group[owner + "\n" + groups[owner].front()])
        if (r->kind == TrackRef::Object) { scene().selected = r->object; a.scene_selection_serial++; break; }
        else if (r->kind == TrackRef::Node) { a.selected_node = r->node; break; }
    }
    ImGui::TableSetColumnIndex(1);
    {
      std::vector<const TrackRef *> all;
      for (const std::string &g : groups[owner]) for (TrackRef *r : by_group[owner + "\n" + g]) all.push_back(r);
      tl_draw_summary_row(a, all, ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
    }
    if (open) {
      for (const std::string &g : groups[owner]) {
        ImGui::TableNextRow(0, s.row_h);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(g.c_str());
        bool gopen = ImGui::TreeNodeEx(g.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        ImGui::TableSetColumnIndex(1);
        {
          std::vector<const TrackRef *> all(by_group[owner + "\n" + g].begin(), by_group[owner + "\n" + g].end());
          tl_draw_summary_row(a, all, ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
        }
        if (gopen) {
          for (TrackRef *r : by_group[owner + "\n" + g]) {
            ImGui::TableNextRow(0, s.row_h);
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(r->id.c_str());
            std::string label = r->label + comp_suffix(*r);
            ImU32 col = r->comp >= 0 ? anim_comp_color(r->comp, r->color) : theme::text();
            ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            bool sel_row = false;
            for (const KeySel &k : s.sel) if (k.track == r->id) { sel_row = true; break; }
            if (ImGui::Selectable(label.c_str(), sel_row, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
              // select every key on the track
              bool add = ImGui::GetIO().KeyCtrl;
              if (!add) s.sel.clear();
              if (r->track) for (const gpx::Key &k : r->track->keys) tl_select(s, r->id, k.time, true);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
              if (std::find(s.curves.begin(), s.curves.end(), r->id) == s.curves.end()) s.curves.push_back(r->id);
              a.show_curve_editor = true;
            }
            // the curve toggle at the right of the tree cell
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 18.f);
            bool shown = std::find(s.curves.begin(), s.curves.end(), r->id) != s.curves.end();
            if (ImGui::SmallButton(shown ? "~" : ">")) {
              if (shown) s.curves.erase(std::find(s.curves.begin(), s.curves.end(), r->id));
              else s.curves.push_back(r->id);
              if (!shown) a.show_curve_editor = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Show this track's curve"));
            ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
            ImGui::TableSetColumnIndex(1);
            tl_draw_key_row(a, *r, ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x);
            s.rows.push_back(*r);
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

} // namespace

void draw_panel_timeline(App &a) {
  if (!a.show_timeline) return;
  panel_float_prepare(a, "Timeline");
  ImGui::SetNextWindowSize(ImVec2(900, 320), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Timeline", &a.show_timeline)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Timeline");
  TimelineState &s = tl_state();
  tl_draw_transport(a);

  // filter row
  ImGui::SetNextItemWidth(160);
  ImGui::InputTextWithHint("##filter", tr("filter tracks"), s.filter, sizeof s.filter);
  ImGui::SameLine();
  studio::Checkbox(tr("Animated only"), &s.animated_only);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Off: every keyable property of the selected object and node is listed so a key can be added here."));
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Fit"))) tl_fit_view(a, false);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", tr("wheel+Ctrl zoom, middle-drag pan, Home fit, A fit selection"));

  std::vector<TrackRef> tracks = anim_collect(a, s.animated_only);
  const float ruler_h = 22.f;
  if (ImGui::BeginTable("##tl", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX)) {
    ImGui::TableSetupColumn(tr("Track"), ImGuiTableColumnFlags_WidthFixed, 260.f);
    ImGui::TableSetupColumn(tr("Keys"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupScrollFreeze(0, 1);
    // the ruler, frozen
    ImGui::TableNextRow(0, ruler_h);
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", tr("Track"));
    ImGui::TableSetColumnIndex(1);
    {
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      float w = ImGui::GetContentRegionAvail().x;
      s.area_x0 = p0.x;
      s.area_x1 = p0.x + w;
      tl_draw_ruler(a, p0, w, ruler_h);
    }
    if (tracks.empty()) {
      ImGui::TableNextRow(0, s.row_h);
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("%s", s.animated_only ? tr("Nothing is animated. Click a property's circle to key it.") : tr("Select an object or a node."));
    } else {
      draw_tree(a, tracks);
    }
    // the playhead over the whole key column
    {
      ImVec2 wmin = ImGui::GetWindowPos();
      ImVec2 wmax(wmin.x + ImGui::GetWindowSize().x, wmin.y + ImGui::GetWindowSize().y);
      tl_draw_playhead(a, ImVec2(s.area_x0, wmin.y), wmax.y - wmin.y);
    }
    ImGui::EndTable();
  }
  tl_finish_interaction(a);
  ImGui::End();
  draw_panel_curve_editor(a);
}

} // namespace studio
