// Geekatplay TerraForge - one window arrangement per workspace.
//
// Vue, Cinema 4D and every other application with workspaces keep a layout
// per workspace: the material editor is arranged like a material editor and
// the terrain screen like a terrain screen, and each remembers what you did
// to it. That is what this file does with the layout store we already have:
// leaving a workspace captures its arrangement into layouts/workspace-<n>,
// entering one loads that file - or builds the workspace's own default the
// first time, and the shared default for workspaces without one.
#include "app.hpp"
#include "layout_record.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <string>

namespace studio {

namespace {

std::string workspace_layout_name(int ws) {
  return std::string("workspace-") + workspace_name(ws);
}

// The windows a workspace opens by itself. Materials brings its studio and
// browser; every other workspace closes them, because a materials browser
// under a terrain viewport is clutter.
void apply_workspace_panels(App &a, int ws) {
  const bool mats = ws == WS_MATERIALS;
  a.show_material_studio = mats;
  a.show_material_browser = mats;
}

} // namespace

LayoutRecord layout_capture(App &a, const std::string &name);
void layout_apply(App &a, const LayoutRecord &r);

void workspace_layout_switch(App &a, int from, int to) {
  if (from == to) return;
  // Remember where the user left the workspace they are leaving.
  if (from >= 0 && from < WS_COUNT) {
    std::string err;
    LayoutRecord r = layout_capture(a, workspace_layout_name(from));
    layout_write(r, err);
  }
  apply_workspace_panels(a, to);
  // Come back to where they left the one they are entering.
  LayoutRecord r;
  std::string err;
  if (layout_read(workspace_layout_name(to), r, err)) {
    // The record carries panel flags from when it was saved; the workspace's
    // own windows follow the workspace, not the file.
    r.workspace = to;
    layout_apply(a, r);
    apply_workspace_panels(a, to);
    return;
  }
  // First visit: the workspace's own default, built into the dockspace next
  // frame. Materials has one of its own; the rest share the default layout.
  a.request_layout_reset = true;
}

// Called by app.cpp when a layout reset is due: which builder applies.
void build_workspace_default_layout(App &a, unsigned dockspace_id) {
  if (a.workspace == WS_MATERIALS)
    build_materials_layout(dockspace_id, prefs().view_mask);
  else
    build_default_layout(dockspace_id, prefs().view_mask);
}

} // namespace studio
