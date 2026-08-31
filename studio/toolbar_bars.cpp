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

static const char *WORKSPACES[4] = {"Terrain", "Materials", "Atmosphere",
                                    "Render"};

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

  for (int w = 0; w < 4; ++w) {
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
    if (ImGui::Button(WORKSPACES[w]) && a.workspace != w) {
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
      if (w == 2) ImGui::SetWindowFocus("Environment");
      else if (w == 3) ImGui::SetWindowFocus("Render");
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

} // namespace

void draw_tool_bar(App &a) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  switch (a.workspace) {
    case 1: tools_materials(a); break;
    case 2: tools_atmosphere(a); break;
    case 3: tools_render(a); break;
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
void draw_global_tools(App &a) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
  const ImVec2 sq(48, 24);

  auto tool = [&](const char *label, const char *tip, bool active = false) {
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::ColorConvertU32ToFloat4(theme::accent()));
    bool hit = ImGui::Button(label, sq);
    if (active) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return hit;
  };

  if (tool("undo", "Undo the last change (Ctrl+Z)")) {
    if (undo_perform(a)) a.status = "undo";
  }
  if (tool("redo", "Redo (Ctrl+Y)")) {
    if (redo_perform(a)) a.status = "redo";
  }
  ImGui::Separator();

  if (tool("eval", "Recompute the whole graph (F5)")) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  ImGui::Separator();

  SculptState &s = sculpt_state();
  if (tool("brush", "Sculpt: brush directly on the terrain", s.active))
    s.active = !s.active;
  ImGui::Separator();

  RenderSettings &rs = render_settings();
  RenderSettings::ViewConfig &vc = rs.views[0];
  if (tool("wire", "Wireframe in the main view", vc.display == 0))
    vc.display = vc.display == 0 ? 2 : 0;
  if (tool("grid", "Ground grid in the main view", vc.grid))
    vc.grid = !vc.grid;
  if (tool("sky", "Sky and fog in the main view", vc.atmosphere))
    vc.atmosphere = !vc.atmosphere;

  ImGui::PopStyleVar(2);
}

} // namespace studio
