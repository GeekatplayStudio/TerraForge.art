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
#include "scene.hpp"
#include <imgui.h>
#include <cmath>
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
    v.curved = rs.views[i].curved;
    v.ortho_zoom = rs.views[i].ortho_zoom;
    v.ortho_cx = rs.views[i].ortho_cx;
    v.ortho_cy = rs.views[i].ortho_cy;
    r.views.push_back(v);
  }
  renderer_orbit_get(r.orbit.target, r.orbit.yaw, r.orbit.pitch, r.orbit.dist);
  r.orbit.valid = true;
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
    rs.views[i].curved = r.views[i].curved;
    rs.views[i].ortho_zoom = r.views[i].ortho_zoom;
    rs.views[i].ortho_cx = r.views[i].ortho_cx;
    rs.views[i].ortho_cy = r.views[i].ortho_cy;
  }
  if (r.orbit.valid) renderer_orbit_set(r.orbit.target, r.orbit.yaw, r.orbit.pitch, r.orbit.dist);
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

// ------------------------------------------------- views and cameras
// Where a viewport is looking from and at: the camera it looks through,
// the active one, or the free orbit.
static void view_eye_target(const RenderSettings::ViewConfig &vc, float eye[3], float target[3]) {
  SceneState &sc = scene();
  int src = vc.scene_camera >= 0 ? vc.scene_camera
          : (vc.scene_camera == -2 ? scene_active_camera() : -1);
  if (src >= 0 && src < (int)sc.objects.size() && sc.objects[(size_t)src].type == SceneObject::Camera) {
    const CameraData &cd = sc.objects[(size_t)src].cam;
    for (int k = 0; k < 3; ++k) { eye[k] = cd.eye[k]; target[k] = cd.target[k]; }
    return;
  }
  float yaw, pitch, dist;
  renderer_orbit_get(target, yaw, pitch, dist);
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  eye[0] = target[0] + dist * cp * std::sin(yaw);
  eye[1] = target[1] + dist * sp;
  eye[2] = target[2] + dist * cp * std::cos(yaw);
}

int view_to_camera(App &a, int slot, int cam, const std::string &name, bool activate,
                   std::string &err) {
  RenderSettings &rs = render_settings();
  SceneState &sc = scene();
  slot = std::clamp(slot, 0, RenderSettings::MAX_VIEWS - 1);
  float eye[3], target[3];
  view_eye_target(rs.views[slot], eye, target);
  if (cam < 0) cam = scene_add_camera(name);
  if (cam < 0 || cam >= (int)sc.objects.size() || sc.objects[(size_t)cam].type != SceneObject::Camera) {
    err = "no such camera";
    return -1;
  }
  CameraData &cd = sc.objects[(size_t)cam].cam;
  for (int k = 0; k < 3; ++k) { cd.eye[k] = eye[k]; cd.target[k] = target[k]; }
  sc.selected = cam;
  scene_last_used_camera() = cam;
  if (activate) scene_active_camera() = cam;
  a.scene_selection_serial++;
  a.status = "view " + std::to_string(slot + 1) + " saved to " + sc.objects[(size_t)cam].name;
  return cam;
}

bool camera_to_view(App &a, int cam, int slot, bool link) {
  RenderSettings &rs = render_settings();
  SceneState &sc = scene();
  slot = std::clamp(slot, 0, RenderSettings::MAX_VIEWS - 1);
  if (cam < 0 || cam >= (int)sc.objects.size() || sc.objects[(size_t)cam].type != SceneObject::Camera)
    return false;
  RenderSettings::ViewConfig &vc = rs.views[slot];
  vc.camera = 0; // a perspective
  if (link) {
    vc.scene_camera = cam; // the view follows the camera from now on
  } else {
    // the free orbit takes the camera's eye and target, and stays free
    const CameraData &cd = sc.objects[(size_t)cam].cam;
    const float d[3] = {cd.eye[0] - cd.target[0], cd.eye[1] - cd.target[1], cd.eye[2] - cd.target[2]};
    const float dist = std::max(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1e-6f);
    const float yaw = std::atan2(d[0], d[2]);
    const float pitch = std::asin(std::clamp(d[1] / dist, -1.f, 1.f));
    renderer_orbit_set(cd.target, yaw, pitch, dist);
    vc.scene_camera = -1;
  }
  a.status = std::string(link ? "view looks through " : "view moved to ") + sc.objects[(size_t)cam].name;
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
  if (op == "view_to_camera" || op == "camera_to_view") {
    // a viewport's point of view into a camera (new or named), or a
    // camera into a viewport - linked (look through) or copied
    SceneState &sc = scene();
    int slot = act.value("view", a.view_focus + 1) - 1;
    int cam = -1;
    const std::string cname = act.value("camera", std::string());
    if (!cname.empty())
      for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (sc.objects[(size_t)i].type == SceneObject::Camera && sc.objects[(size_t)i].name == cname) cam = i;
    if (op == "view_to_camera") {
      if (!cname.empty() && cam < 0 && !act.value("create", true)) {
        err = "no camera named '" + cname + "'";
        return 0;
      }
      const std::string name = act.value("name", cname);
      return view_to_camera(a, slot, cam, name, act.value("activate", false), err) >= 0 ? 1 : 0;
    }
    if (cam < 0) cam = scene_active_camera();
    if (!camera_to_view(a, cam, slot, act.value("link", true))) {
      err = "camera_to_view needs a camera (by name, or an active one)";
      return 0;
    }
    return 1;
  }
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
