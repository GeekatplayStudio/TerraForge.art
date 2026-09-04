// Geekatplay TerraForge - the View menu's Viewports and Layouts sections,
// and the small dialog that names a layout being saved. Split from
// toolbar.cpp for the 500-line module rule.
//
// The rule these menus follow: nothing here rebuilds the layout except the
// items that say so. Adding a viewport, closing one, or splitting one edits
// a single dock node and leaves everything the user arranged alone.
#include "app.hpp"
#include "layout_record.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace studio {

namespace {
bool g_save_layout_open = false;
char g_save_layout_name[64] = "";
} // namespace

void menu_view_viewports(App &a) {
  if (!ImGui::BeginMenu("Viewports")) return;
  const unsigned mask = prefs().view_mask;
  const int free_slot = view_first_free();
  const bool room = free_slot >= 0;

  if (ImGui::MenuItem("Add viewport", "", false, room)) view_open(a, free_slot);
  if (ImGui::MenuItem("Split right", "", false, room))
    view_split(a, a.view_focus, false);
  if (ImGui::MenuItem("Split down", "", false, room))
    view_split(a, a.view_focus, true);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Splits the viewport you last worked in");

  ImGui::Separator();
  if (ImGui::BeginMenu("Arrange")) {
    // The shapes people actually ask for. These rebuild the viewport region
    // only - the node editor, properties and console keep their places.
    struct Row { const char *label; int n; };
    static const Row rows[] = {
        {"Single", 1},        {"Two, side by side", 2}, {"One and two", 3},
        {"Quad (2 x 2)", 4},  {"Five", 5},              {"Six (3 x 2)", 6},
        {"Eight (4 x 2)", 8}};
    int open_count = 0;
    for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
      if (mask & (1u << i)) ++open_count;
    for (const Row &r : rows)
      if (ImGui::MenuItem(r.label, nullptr, open_count == r.n))
        views_arrange(a, r.n);
    ImGui::Separator();
    ImGui::MenuItem("Rearranges the viewport area only", nullptr, false, false);
    ImGui::EndMenu();
  }

  ImGui::Separator();
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i) {
    const bool on = (mask & (1u << i)) != 0;
    const bool last_one = on && (mask & ~(1u << i)) == 0;
    if (ImGui::MenuItem(view_window_name(i), nullptr, on, !last_one)) {
      if (on)
        view_close(a, i);
      else
        view_open(a, i);
    }
  }
  ImGui::Separator();
  ImGui::MenuItem("Drag any viewport's tab to move, split or float it", nullptr,
                  false, false);
  ImGui::EndMenu();
}

void menu_view_layouts(App &a) {
  if (!ImGui::BeginMenu("Layouts")) return;
  std::vector<std::string> names = layout_list();
  if (names.empty()) {
    ImGui::MenuItem("(none saved yet)", nullptr, false, false);
  } else {
    for (const std::string &n : names) {
      const bool current = prefs().current_layout == n;
      if (ImGui::MenuItem(n.c_str(), nullptr, current)) {
        std::string err;
        if (!layout_load_named(a, n, err)) a.status = "layout: " + err;
      }
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Save current layout...")) {
    // Offer the name being worked in, so re-saving is one dialog and Enter.
    std::snprintf(g_save_layout_name, sizeof g_save_layout_name, "%s",
                  prefs().current_layout.empty() ? "My layout"
                                                 : prefs().current_layout.c_str());
    g_save_layout_open = true;
  }
  if (ImGui::BeginMenu("Delete", !names.empty())) {
    for (const std::string &n : names)
      if (ImGui::MenuItem(n.c_str())) {
        std::string err;
        if (!layout_erase(n, err)) a.status = "layout: " + err;
        else a.status = "deleted layout '" + n + "'";
      }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  ImGui::MenuItem("A layout holds the windows, the viewports and", nullptr,
                  false, false);
  ImGui::MenuItem("what each one shows - never the scene", nullptr, false,
                  false);
  ImGui::Separator();
  if (ImGui::MenuItem("Reset to the default layout")) a.request_layout_reset = true;
  ImGui::EndMenu();
}

void layout_dialogs(App &a) {
  if (g_save_layout_open) {
    ImGui::OpenPopup("Save layout");
    g_save_layout_open = false;
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Save layout", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted("Name this arrangement of windows:");
  ImGui::SetNextItemWidth(320);
  ImGui::SetKeyboardFocusHere();
  const bool entered = ImGui::InputText(
      "##layoutname", g_save_layout_name, sizeof g_save_layout_name,
      ImGuiInputTextFlags_EnterReturnsTrue);
  const std::string safe = layout_safe_name(g_save_layout_name);
  bool exists = false;
  for (const std::string &n : layout_list()) exists |= (n == safe);
  ImGui::TextDisabled("Saves as \"%s\"%s", safe.c_str(),
                      exists ? " (replaces the existing one)" : "");
  ImGui::Spacing();
  const bool ok = ImGui::Button("Save", ImVec2(120, 0)) || entered;
  ImGui::SameLine();
  const bool cancel = ImGui::Button("Cancel", ImVec2(120, 0)) ||
                      ImGui::IsKeyPressed(ImGuiKey_Escape);
  if (ok) {
    std::string err;
    if (!layout_save_current(a, g_save_layout_name, err))
      a.status = "layout: " + err;
    ImGui::CloseCurrentPopup();
  } else if (cancel) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace studio
