// Geekatplay TerraForge - saving and loading named window layouts.
//
// The data half is layout_record.cpp. This half is the two operations that
// need the live UI: reading the current arrangement out of ImGui, and putting
// a saved one back.
//
// Putting one back has one constraint worth stating: ImGui's window state can
// only be replaced between frames, never while windows are being submitted.
// So a load applies our own state immediately and parks the ini text in
// App::pending_layout_ini, which app.cpp hands to ImGui at the top of the
// next frame - before the first Begin.
#include "app.hpp"
#include "layout_record.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <algorithm>
#include <json.hpp>

using nlohmann::json;

namespace studio {

LayoutRecord layout_capture(App &a, const std::string &name) {
  LayoutRecord r;
  r.name = name;
  size_t size = 0;
  if (const char *ini = ImGui::SaveIniSettingsToMemory(&size))
    r.ini.assign(ini, size);
  r.view_mask = prefs().view_mask;
  const RenderSettings &rs = render_settings();
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i) {
    LayoutRecord::View v;
    v.camera = rs.views[i].camera;
    v.display = rs.views[i].display;
    v.scene_camera = rs.views[i].scene_camera;
    v.atmosphere = rs.views[i].atmosphere;
    v.water = rs.views[i].show_water_view;
    v.grid = rs.views[i].grid;
    v.outlines = rs.views[i].outlines;
    r.views.push_back(v);
  }
  r.editor_domains = prefs().editor_domains;
  r.library = a.show_library;
  r.nodelist = a.show_nodelist;
  r.properties = a.show_properties;
  r.viewport = a.show_viewport;
  r.console = a.show_console;
  r.timeline = a.show_timeline;
  r.preview = a.show_preview;
  r.material_editor = a.show_material_editor;
  r.workspace = a.workspace;
  return r;
}

void layout_apply(App &a, const LayoutRecord &r) {
  prefs().view_mask = r.view_mask ? r.view_mask : 1u;
  RenderSettings &rs = render_settings();
  for (size_t i = 0; i < r.views.size() && i < RenderSettings::MAX_VIEWS; ++i) {
    rs.views[i].camera = std::clamp(r.views[i].camera, 0, 3);
    rs.views[i].display = std::clamp(r.views[i].display, 0, 2);
    rs.views[i].scene_camera = r.views[i].scene_camera;
    rs.views[i].atmosphere = r.views[i].atmosphere;
    rs.views[i].show_water_view = r.views[i].water;
    rs.views[i].grid = r.views[i].grid;
    rs.views[i].outlines = r.views[i].outlines;
  }
  a.show_library = r.library;
  a.show_nodelist = r.nodelist;
  a.show_properties = r.properties;
  a.show_viewport = r.viewport;
  a.show_console = r.console;
  a.show_timeline = r.timeline;
  a.show_preview = r.preview;
  a.show_material_editor = r.material_editor;
  a.workspace = std::clamp(r.workspace, 0, WS_COUNT - 1);
  graph_editors_set(a, r.editor_domains);
  a.view_focus = 0;
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
    if (prefs().view_mask & (1u << i)) { a.view_focus = i; break; }
  // The windows themselves move next frame (see the note at the top).
  a.pending_layout_ini = r.ini;
  prefs().current_layout = r.name;
  prefs_save();
}

bool layout_save_current(App &a, const std::string &name, std::string &err) {
  const std::string safe = layout_safe_name(name);
  // SaveIniSettingsToMemory() serialises the live state, so what is captured
  // is what is on screen now - not whatever ImGui last flushed to disk.
  LayoutRecord r = layout_capture(a, safe);
  if (!layout_write(r, err)) return false;
  prefs().current_layout = safe;
  prefs_save();
  a.status = "saved layout '" + safe + "'";
  return true;
}

bool layout_load_named(App &a, const std::string &name, std::string &err) {
  LayoutRecord r;
  if (!layout_read(name, r, err)) return false;
  layout_apply(a, r);
  a.status = "layout '" + r.name + "'";
  return true;
}

// ------------------------------------------------------------------ the ops
// Everything the Layouts menu and the viewport menu can do, reachable from
// the assistant, the Python API and MCP - the standing rule that anything the
// UI can do, scripting can do.
//
// Returns 1 when handled and something changed, 0 when handled and it failed,
// -1 when the op is not ours.
int ai_layout_op(App &a, const std::string &op, const json &act,
                 std::string &err) {
  if (op == "save_layout") {
    std::string name = act.value("name", std::string());
    if (name.empty()) {
      err = "save_layout needs a 'name'";
      return 0;
    }
    return layout_save_current(a, name, err) ? 1 : 0;
  }

  if (op == "load_layout") {
    std::string name = act.value("name", std::string());
    if (name.empty()) {
      err = "load_layout needs a 'name'";
      return 0;
    }
    return layout_load_named(a, name, err) ? 1 : 0;
  }

  if (op == "delete_layout") {
    std::string name = act.value("name", std::string());
    if (name.empty()) {
      err = "delete_layout needs a 'name'";
      return 0;
    }
    if (!layout_erase(name, err)) return 0;
    if (prefs().current_layout == layout_safe_name(name)) {
      prefs().current_layout.clear();
      prefs_save();
    }
    a.status = "deleted layout '" + name + "'";
    return 1;
  }

  if (op == "list_layouts") {
    std::vector<std::string> names = layout_list();
    std::string line;
    for (const std::string &n : names) line += (line.empty() ? "" : ", ") + n;
    a.status = names.empty() ? "no saved layouts" : ("layouts: " + line);
    return 1;
  }

  if (op == "reset_layout") {
    a.request_layout_reset = true;
    a.status = "layout reset";
    return 1;
  }

  if (op == "add_view") {
    int slot = act.contains("view") ? act.value("view", 1) - 1 : view_first_free();
    if (slot < 0) {
      err = "all " + std::to_string((int)RenderSettings::MAX_VIEWS) +
            " viewports are already open";
      return 0;
    }
    if (act.value("split", false)) {
      // "view" names the one to split when it is given, so a script does not
      // depend on which viewport the mouse happened to be over.
      int from = act.contains("view") ? act.value("view", 1) - 1 : a.view_focus;
      if (from < 0 || from >= RenderSettings::MAX_VIEWS) from = a.view_focus;
      view_split(a, from, act.value("vertical", false));
      return 1;
    }
    view_open(a, slot);
    return 1;
  }

  if (op == "close_view") {
    int slot = act.value("view", 0) - 1;
    if (slot < 0 || slot >= RenderSettings::MAX_VIEWS) {
      err = "close_view needs 'view' between 1 and " +
            std::to_string((int)RenderSettings::MAX_VIEWS);
      return 0;
    }
    unsigned before = prefs().view_mask;
    view_close(a, slot);
    if (prefs().view_mask == before) {
      err = "the last viewport cannot be closed";
      return 0;
    }
    return 1;
  }

  if (op == "arrange_views") {
    int n = act.value("count", 0);
    if (n < 1 || n > RenderSettings::MAX_VIEWS) {
      err = "arrange_views needs 'count' between 1 and " +
            std::to_string((int)RenderSettings::MAX_VIEWS);
      return 0;
    }
    views_arrange(a, n);
    return 1;
  }

  return -1;
}

} // namespace studio
