// Geekatplay TerraForge — the bars above and beside the workspace.
//
// The layout every serious 3D application converges on, and the one Cinema 4D
// uses: menus on top, then what you are working on, then the tools for that
// work, with global tools down the left and the properties of whatever is
// selected down the right.
//
//   row 1   File Edit Terrain View Help          — classic text menus only
//   row 2   Terrain | Materials | Atmosphere | Render  — which workflow
//   row 3   the tools for that workflow          — changes with row 2
//   left    global tools                         — the same in every workflow
//   right   properties of the selection          — the Properties editor
//
// It was all one row before, which is why it read as a wall: the menus, the
// workflow tabs, the camera, the resolution and the statistics all had equal
// weight and none of them were grouped.
#include "app.hpp"
#include "i18n.hpp"
#include "gizmo.hpp"
#include "icons.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "sculpt.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

namespace studio {

const char *workspace_name(int ws) {
  switch (ws) {
    case WS_MATERIALS: return tr("workspace.materials");
    case WS_ATMOSPHERE: return tr("workspace.atmosphere");
    case WS_RENDER: return tr("workspace.render");
    case WS_ALL: return tr("workspace.all");
    case WS_OBJECTS: return tr("workspace.objects");
    case WS_LIGHTING: return tr("workspace.lighting");
    case WS_CAMERAS: return tr("workspace.cameras");
    case WS_ANIMATION: return tr("workspace.animation");
    default: return tr("workspace.terrain");
  }
}

// What the graph is currently holding, for the readout at the end of the tool
// row. Buffers dominate; everything else is noise beside them.
static size_t graph_memory_bytes(App &a) {
  size_t total = 0;
  for (auto &n : a.graph.nodes)
    for (auto &p : n->ports) {
      if (p.hmap) total += p.hmap->v.size() * sizeof(float);
      if (p.tex) total += p.tex->v.size() * sizeof(float);
    }
  return total;
}

// ------------------------------------------------------------ row 2: tabs
// Wide, evenly weighted, and clearly the most important control on the screen,
// because choosing the workflow changes everything below it.
void draw_workspace_bar(App &a) {
  ImGuiStyle &st = ImGui::GetStyle();
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, st.ItemSpacing.y));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 5));

  for (int oi = 0; oi < 8; ++oi) {
    const int w = WORKSPACE_ORDER[oi];
    const bool active = a.workspace == w;
    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::ColorConvertU32ToFloat4(theme::accent()));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImGui::ColorConvertU32ToFloat4(theme::accent()));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.09f, 0.08f, 1.f));
    } else {
      ImGui::PushStyleColor(
          ImGuiCol_Button,
          ImGui::ColorConvertU32ToFloat4(theme::shade(theme::panel_bg(), 0.85f)));
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(theme::text_dim()));
    }
    if (ImGui::Button(workspace_name(w)) && a.workspace != w) {
      a.workspace = w;
      // a node from another domain must not linger in the inspector
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        gpx::Node *n = a.graph.find_node(a.selected_node);
        if (!n || domain_of_category(n->category) != w) {
          a.selected_node = 0;
          a.prop_tab = TAB_OBJECT;
        }
      }
      if (w == WS_ATMOSPHERE) ImGui::SetWindowFocus("Environment");
      else if (w == WS_RENDER) ImGui::SetWindowFocus("Render");
      else if (w == WS_OBJECTS) ImGui::SetWindowFocus("Objects");
      else if (w == WS_ANIMATION) ImGui::SetWindowFocus("Timeline");
    }
    ImGui::PopStyleColor(active ? 3 : 2);
    ImGui::SameLine();
  }
  ImGui::NewLine();
  ImGui::PopStyleVar(3);
}

// ------------------------------------------------- row 3: workflow tools
namespace {

void tool_sep() {
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
}

void resolution_tools(App &a) {
  ImGui::TextDisabled("res");
  for (int res : {256, 512, 1024, 2048}) {
    ImGui::SameLine();
    const char *label = res == 1024 ? "1k"
                        : res == 2048 ? "2k"
                        : res == 256 ? "256"
                                     : "512";
    const bool active = a.graph.resolution == res;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::ColorConvertU32ToFloat4(theme::accent()));
    if (ImGui::SmallButton(label)) {
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      a.graph.resolution = res;
      a.graph.mark_all_dirty();
      a.request_eval();
    }
    if (active) ImGui::PopStyleColor();
  }
  ImGui::SameLine();
  static int custom_res = 0;
  if (custom_res == 0) custom_res = a.graph.resolution;
  ImGui::SetNextItemWidth(58);
  if (ImGui::InputInt("##customres", &custom_res, 0, 0,
                      ImGuiInputTextFlags_EnterReturnsTrue)) {
    custom_res = std::clamp(custom_res, 64, 8192);
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.resolution = custom_res;
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Any resolution 64..8192, Enter to apply.");
}

void camera_tools(App &a) {
  SceneState &sc = scene();
  int active = scene_active_camera();
  std::string label = "Free camera";
  if (active >= 0 && active < (int)sc.objects.size())
    label = sc.objects[active].name;
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo("##camsel", label.c_str())) {
    if (ImGui::Selectable("Free camera", active < 0)) scene_active_camera() = -1;
    for (int idx : scene_camera_indices())
      if (ImGui::Selectable(sc.objects[idx].name.c_str(), idx == active)) {
        scene_active_camera() = idx;
        scene_last_used_camera() = idx;
      }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Which camera the perspective views look through.");
  ImGui::SameLine();
  if (ImGui::SmallButton("+ camera")) {
    int idx = scene_add_camera();
    scene_active_camera() = idx;
    sc.selected = idx;
    a.scene_selection_serial++;
  }
}

// Terrain: the shape of the ground, and how finely it is computed.
void tools_terrain(App &a) {
  resolution_tools(a);
  tool_sep();
  if (ImGui::SmallButton("bake 4k exports")) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    for (auto &n : a.graph.nodes)
      if (auto *e = n->attrs.find("auto_export")) e->b = true;
    a.graph.resolution = 4096;
    a.graph.mark_all_dirty();
    a.request_eval();
    a.status = "baking at 4096; export nodes write when done";
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Re-evaluate at 4096 with every export node enabled.");
  tool_sep();
  SculptState &s = sculpt_state();
  if (s.active)
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::ColorConvertU32ToFloat4(theme::accent()));
  if (ImGui::SmallButton("sculpt")) s.active = !s.active;
  if (s.active) ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Brush directly on the terrain. Strokes live in a\n"
                      "TerrainSculpt node, so the procedural chain under\n"
                      "them survives retuning.");
}

// Materials: what the surface is made of.
void tools_materials(App &a) {
  RenderSettings &rs = render_settings();
  ImGui::TextDisabled("albedo");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  ImGui::Combo("##albsrc", &rs.terrain_material_mode,
               "Auto (last texture)\0Procedural\0Chosen node\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Where the terrain's colour comes from when no\n"
                      "MaterialOutput is assigned to it.");
  tool_sep();
  studio::Checkbox("textured", &rs.use_albedo);
  tool_sep();
  resolution_tools(a);
}

// Atmosphere: the air, the light and the water.
void tools_atmosphere(App &a) {
  (void)a;
  RenderSettings &rs = render_settings();
  studio::Checkbox("clouds", &rs.clouds_on);
  tool_sep();
  studio::Checkbox("water", &rs.show_water);
  tool_sep();
  studio::Checkbox("shadows", &rs.shadows);
  tool_sep();
  ImGui::TextDisabled("fog");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::Combo("##fogtype", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
  tool_sep();
  ImGui::TextDisabled("sun");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("##sunalt", &rs.sun_altitude, 1.f, 89.f, "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sun altitude.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("##sunaz", &rs.sun_azimuth, 0.f, 360.f, "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sun azimuth.");
}

// Render: the camera and the output.
void tools_render(App &a) {
  camera_tools(a);
  tool_sep();
  ImGui::TextDisabled("engine");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  RenderSettings &rs = render_settings();
  ImGui::Combo("##vpengine", &rs.viewport_engine,
               "Rasterized PBR\0Cinematic raymarch\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How the viewport itself draws. Offline engines are\n"
                      "chosen per camera in the Render properties.");
  tool_sep();
  if (ImGui::SmallButton("render active camera"))
    a.request_camera_render = scene_active_camera();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Render through the active camera with its own\n"
                      "engine, resolution and sample settings.");
}

// Objects: what stands in the world.
void tools_objects(App &a) {
  SceneState &sc = scene();
  ImGui::TextDisabled("add");
  for (const char *kind : {"cube", "sphere", "plane", "cylinder", "cone"}) {
    ImGui::SameLine();
    if (ImGui::SmallButton(kind)) {
      undo_push(a, std::string("Add ") + kind);
      sc.selected = scene_add_primitive(kind, "");
      a.scene_selection_serial++;
    }
  }
  tool_sep();
  if (ImGui::SmallButton("+ planet")) {
    undo_push(a, "Add planet");
    sc.selected = scene_add_planet();
    a.scene_selection_serial++;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ infinite terrain")) {
    undo_push(a, "Add infinite terrain");
    sc.selected = scene_add_infinite_surface(-1);
    a.scene_selection_serial++;
  }
  tool_sep();
  ImGui::TextDisabled("%d objects", (int)sc.objects.size());
}

// Lighting: the sun and the placed lights.
void tools_lighting(App &a) {
  RenderSettings &rs = render_settings();
  ImGui::TextDisabled("sun");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("##sunalt2", &rs.sun_altitude, 1.f, 89.f, "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sun altitude.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("##sunaz2", &rs.sun_azimuth, 0.f, 360.f, "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sun azimuth.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##sunint", &rs.sun_intensity, 0.f, 10.f, "x%.1f");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sun intensity.");
  tool_sep();
  studio::Checkbox("shadows", &rs.shadows);
  tool_sep();
  if (ImGui::SmallButton("+ light")) {
    undo_push(a, "Add light");
    scene().selected = scene_add_light("");
    a.scene_selection_serial++;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A point light in the scene. A LightSource node in the\n"
                      "graph does the same and keeps it in the network.");
}

// Cameras: which one, and a new one.
void tools_cameras(App &a) {
  camera_tools(a);
  tool_sep();
  ImGui::TextDisabled("focal");
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (active >= 0 && active < (int)sc.objects.size() &&
      sc.objects[active].type == SceneObject::Camera) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##focal", &sc.objects[active].cam.focal_mm, 8.f, 800.f,
                       "%.0f mm", ImGuiSliderFlags_Logarithmic);
  } else {
    ImGui::SameLine();
    ImGui::TextDisabled("(free camera)");
  }
}

// Animation: the transport, so the graph can be scrubbed from any panel.
void tools_animation(App &a) {
  if (ImGui::SmallButton(a.anim_playing ? "pause" : "play"))
    a.anim_playing = !a.anim_playing;
  ImGui::SameLine();
  if (ImGui::SmallButton("stop")) {
    a.anim_playing = false;
    a.graph.time = a.anim_start;
    a.request_eval();
  }
  tool_sep();
  ImGui::TextDisabled("time");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160);
  if (ImGui::SliderFloat("##anim_t", &a.graph.time, a.anim_start, a.anim_end,
                         "%.2f s"))
    a.request_eval();
  tool_sep();
  ImGui::TextDisabled("range");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("##anim_s", &a.anim_start, 0.1f, 0.f, a.anim_end, "%.1f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("##anim_e", &a.anim_end, 0.1f, a.anim_start, 100000.f, "%.1f");
  tool_sep();
  ImGui::TextDisabled("%s", a.seq_active ? "rendering sequence..." : "sequence idle");
}

} // namespace

void draw_tool_bar(App &a) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  switch (a.workspace) {
    case WS_MATERIALS: tools_materials(a); break;
    case WS_ATMOSPHERE: tools_atmosphere(a); break;
    case WS_RENDER: tools_render(a); break;
    case WS_OBJECTS: tools_objects(a); break;
    case WS_LIGHTING: tools_lighting(a); break;
    case WS_CAMERAS: tools_cameras(a); break;
    case WS_ANIMATION: tools_animation(a); break;
    default: tools_terrain(a); break;
  }

  // Status and statistics sit at the right-hand end of the tool row, where
  // they are readable but never in the way of a control.
  const char *right = nullptr;
  static char buf[256];
  if (a.eval.running.load()) {
    int done = a.eval.progress_done.load(), total = a.eval.progress_total.load();
    std::string cur;
    {
      std::lock_guard<std::mutex> lk(a.eval.mtx);
      cur = a.eval.current_node;
    }
    snprintf(buf, sizeof buf, "computing %s (%d/%d)", cur.c_str(), done, total);
    right = buf;
  } else {
    static double total_ms = 0;
    static size_t mem = 0, count = 0;
    {
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        mem = graph_memory_bytes(a);
        total_ms = 0;
        for (auto &n : a.graph.nodes) total_ms += n->last_compute_ms;
        count = a.graph.nodes.size();
      }
    }
    snprintf(buf, sizeof buf, "%zu nodes \xC2\xB7 %.0f MB \xC2\xB7 %.0f ms",
             count, mem / (1024.0 * 1024.0), total_ms);
    right = buf;
  }
  // Right-aligned, but only when it fits. Forcing the cursor past the content
  // region makes the row scrollable, and a scrolled toolbar clips its first
  // control — which is how "res" lost its r.
  const float w = ImGui::CalcTextSize(right).x;
  const float avail = ImGui::GetWindowContentRegionMax().x;
  ImGui::SameLine();
  const float want = avail - w - 8.f;
  if (want > ImGui::GetCursorPosX() + 12.f) ImGui::SetCursorPosX(want);
  if (a.eval.running.load()) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::ColorConvertU32ToFloat4(theme::accent()));
    ImGui::TextUnformatted(right);
    ImGui::PopStyleColor();
  } else {
    ImGui::TextDisabled("%s", right);
  }
  ImGui::PopStyleVar();
}

// --------------------------------------------------- left: global tools
// The same in every workflow, because these are things you do *to* the
// project rather than to one part of it. Nothing goes here that does not
// work — a palette of dead buttons is worse than no palette.
// The tools that mean the same thing in every workflow, as icons on the menu
// row. Vertical text buttons down the left edge cost a whole column of window
// and put undo where nobody looks for it.
void draw_global_tools(App &a) {
  const float h = ImGui::GetFrameHeight();

  if (IconButton(Icon::Undo, "##undo", "Undo the last change  (Ctrl+Z)", false, h)) {
    if (undo_perform(a)) a.status = "undo";
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Redo, "##redo", "Redo  (Ctrl+Y)", false, h)) {
    if (redo_perform(a)) a.status = "redo";
  }

  ImGui::SameLine(0, 10);
  if (IconButton(Icon::Refresh, "##eval", "Recompute the whole graph  (F5)", false, h)) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }

  ImGui::SameLine(0, 10);
  SculptState &s = sculpt_state();
  if (IconButton(Icon::Brush, "##brush", "Sculpt: brush directly on the terrain",
                 s.active, h))
    s.active = !s.active;

  // Transform tools. These used to be duplicates of the per-view wireframe,
  // grid and sky toggles, which now live in each viewport's own header where
  // they belong - a global bar should carry what is global.
  ImGui::SameLine(0, 10);
  GizmoMode &gm = gizmo_mode();
  auto tool = [&](Icon ic, const char *id, GizmoMode m, const char *tip) {
    if (IconButton(ic, id, tip, gm == m, h)) gm = gm == m ? GizmoMode::None : m;
    ImGui::SameLine(0, 2);
  };
  tool(Icon::Move, "##gmove", GizmoMode::Move,
       "Move tool  (W)\n\nDrag an axis in any viewport to move the\n"
       "selected object. The same numbers are in Properties,\n"
       "in metres.");
  tool(Icon::Rotate, "##grot", GizmoMode::Rotate,
       "Rotate tool  (E)\n\nDrag a ring to turn the selected object.\n"
       "Heading, pitch and bank, in degrees.");
  tool(Icon::Scale, "##gscl", GizmoMode::Scale,
       "Scale tool  (R)\n\nDrag an axis box to squeeze one axis, or\n"
       "the centre box to resize the whole object.");
  ImGui::SameLine(0, 10);
  if (IconButton(Icon::Node, "##console", "Show the console", a.show_console, h))
    a.show_console = !a.show_console;
}

} // namespace studio
