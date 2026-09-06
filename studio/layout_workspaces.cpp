// Geekatplay TerraForge - one default arrangement per workspace.
//
// Choosing a workflow changes what you look at, so it changes where things
// are: Animation puts the timeline along the whole width under the view
// with the curves beside it; Render puts the picture next to the camera
// view; Objects gives the scene tree room. Each is the arrangement a user
// would build by hand on the first afternoon; they can still rearrange
// everything, and the workspace remembers it (layout_workspace.cpp).
//
// Two rules from AGENTS.md apply. A default builder docks EVERY window the
// application can show - one it forgets floats over the new arrangement
// wherever it last was. And a rebuild starts with DockBuilderRemoveNode,
// which throws away every window the user placed by hand, so these run
// only on a first visit and on Reset layout.
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <imgui_internal.h>

namespace studio {

namespace {

// Every viewport that is open, tabbed into one cell (the Terrain layout
// arranges them side by side through arrange_into; the others keep the
// picture in one place and let the user split it).
void dock_views(ImGuiID cell, unsigned view_mask) {
  bool any = false;
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
    if (view_mask & (1u << i)) {
      ImGui::DockBuilderDockWindow(view_window_name(i), cell);
      any = true;
    }
  if (!any) ImGui::DockBuilderDockWindow(view_window_name(0), cell);
}

// The windows no workflow leads with, tabbed into a cell so they are
// placed rather than floating: the material rooms, the mesh tools, the AI.
void dock_secondary(ImGuiID cell) {
  ImGui::DockBuilderDockWindow("Material Studio", cell);
  ImGui::DockBuilderDockWindow("Material Browser", cell);
  ImGui::DockBuilderDockWindow("Material Editor", cell);
  ImGui::DockBuilderDockWindow("Mesh Tools", cell);
  ImGui::DockBuilderDockWindow("AI", cell);
}

ImGuiID fresh_root(unsigned dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);
  return dockspace_id;
}

// The right column every workflow shares: the Scene tree over the
// Properties editor. `scene_share` is how much of the column the tree gets.
void right_column(ImGuiID &main_id, float width, float scene_share, ImGuiID &scene_cell,
                  ImGuiID &props_cell) {
  ImGuiID right = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, width, nullptr, &main_id);
  props_cell = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 1.f - scene_share, nullptr,
                                           &right);
  scene_cell = right;
  ImGui::DockBuilderDockWindow("Outliner", scene_cell);
  ImGui::DockBuilderDockWindow("Properties", props_cell);
}

// Objects: the picture, the scene tree with room to breathe, the
// properties of the selection, and the graph tucked under the view.
void build_objects_layout(unsigned dockspace_id, unsigned view_mask) {
  ImGuiID main_id = fresh_root(dockspace_id);
  ImGuiID scene_cell, props_cell;
  right_column(main_id, 0.26f, 0.42f, scene_cell, props_cell);
  ImGuiID left = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Left, 0.15f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Library", left);
  ImGui::DockBuilderDockWindow("Node List", left);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.30f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Graph", bottom);
  ImGui::DockBuilderDockWindow("###console", bottom);
  ImGui::DockBuilderDockWindow("Timeline", bottom);
  ImGui::DockBuilderDockWindow("Curve editor", bottom);
  ImGui::DockBuilderDockWindow("Preview", props_cell);
  dock_secondary(props_cell);
  dock_views(main_id, view_mask);
  ImGui::DockBuilderFinish(dockspace_id);
}

// Atmosphere, Lighting, Cameras: the picture is the point, so it gets the
// width; the graph and the console are a strip beneath it; properties on
// the right with the tree above them.
void build_view_layout(unsigned dockspace_id, unsigned view_mask) {
  ImGuiID main_id = fresh_root(dockspace_id);
  ImGuiID scene_cell, props_cell;
  right_column(main_id, 0.25f, 0.30f, scene_cell, props_cell);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.28f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Graph", bottom);
  ImGui::DockBuilderDockWindow("###console", bottom);
  ImGui::DockBuilderDockWindow("Timeline", bottom);
  ImGui::DockBuilderDockWindow("Curve editor", bottom);
  ImGui::DockBuilderDockWindow("Library", bottom);
  ImGui::DockBuilderDockWindow("Node List", bottom);
  ImGui::DockBuilderDockWindow("Preview", scene_cell);
  dock_secondary(props_cell);
  dock_views(main_id, view_mask);
  ImGui::DockBuilderFinish(dockspace_id);
}

// Animation: the view and the curve editor side by side on top, the
// timeline the whole width beneath them - the arrangement every animation
// application converges on, because time runs left to right and a short
// timeline is a timeline you cannot read.
void build_animation_layout(unsigned dockspace_id, unsigned view_mask) {
  ImGuiID main_id = fresh_root(dockspace_id);
  ImGuiID scene_cell, props_cell;
  right_column(main_id, 0.22f, 0.35f, scene_cell, props_cell);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.34f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Timeline", bottom);
  ImGui::DockBuilderDockWindow("Graph", bottom);
  ImGui::DockBuilderDockWindow("###console", bottom);
  ImGuiID curves = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.45f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Curve editor", curves);
  ImGui::DockBuilderDockWindow("Library", props_cell);
  ImGui::DockBuilderDockWindow("Node List", props_cell);
  ImGui::DockBuilderDockWindow("Preview", scene_cell);
  dock_secondary(props_cell);
  dock_views(main_id, view_mask);
  ImGui::DockBuilderFinish(dockspace_id);
}

// Render: the camera view and the rendered picture side by side, large;
// the render graph beneath; the render and camera properties on the right.
void build_render_layout(unsigned dockspace_id, unsigned view_mask) {
  ImGuiID main_id = fresh_root(dockspace_id);
  ImGuiID scene_cell, props_cell;
  right_column(main_id, 0.25f, 0.28f, scene_cell, props_cell);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.26f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Graph", bottom);
  ImGui::DockBuilderDockWindow("###console", bottom);
  ImGui::DockBuilderDockWindow("Timeline", bottom);
  ImGui::DockBuilderDockWindow("Curve editor", bottom);
  ImGui::DockBuilderDockWindow("Library", bottom);
  ImGui::DockBuilderDockWindow("Node List", bottom);
  ImGuiID picture = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.5f, nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Preview", picture);
  dock_secondary(props_cell);
  dock_views(main_id, view_mask);
  ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace

void build_workspace_layout(int ws, unsigned dockspace_id, unsigned view_mask) {
  switch (ws) {
    case WS_MATERIALS: build_materials_layout(dockspace_id, view_mask); break;
    case WS_OBJECTS: build_objects_layout(dockspace_id, view_mask); break;
    case WS_ATMOSPHERE:
    case WS_LIGHTING:
    case WS_CAMERAS: build_view_layout(dockspace_id, view_mask); break;
    case WS_ANIMATION: build_animation_layout(dockspace_id, view_mask); break;
    case WS_RENDER: build_render_layout(dockspace_id, view_mask); break;
    default: build_default_layout(dockspace_id, view_mask); break;
  }
}

} // namespace studio
