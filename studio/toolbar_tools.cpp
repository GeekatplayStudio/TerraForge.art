// Geekatplay TerraForge — row 3: the settings for the chosen workflow, one
// function per workspace. A setting is a value you dial: the resolution,
// the sun's height, which camera, the frame. The modes and tools of the
// workflow are down the left column (toolbar_left.cpp); the frame around
// this row and the global commands are in toolbar_bars.cpp.
#include "anim_widgets.hpp"
#include "app.hpp"
#include "ai_jobs.hpp"
#include "i18n.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "sculpt.hpp"
#include "theme_colors.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include <cmath>
#include <imgui.h>
#include <algorithm>
#include <string>

namespace studio {

namespace {

// The resolution row: four presets as text (numbers are their own icon)
// and a typed value, all at the button height.
void resolution_tools(App &a) {
  const float h = tool_size();
  tool_label(tr("res"));
  for (int res : {256, 512, 1024, 2048}) {
    const char *label = res == 1024 ? "1k" : res == 2048 ? "2k" : res == 256 ? "256" : "512";
    if (tool_text(label, tr("Terrain resolution"), a.graph.resolution == res)) {
      std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
      a.graph.resolution = res;
      a.graph.mark_all_dirty();
      a.request_eval();
    }
  }
  static int custom_res = 0;
  if (custom_res == 0) custom_res = a.graph.resolution;
  // the typed value sits at the tile height, so the row is one row
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(6.f, (h - ImGui::GetTextLineHeight()) * 0.5f));
  ImGui::SetNextItemWidth(58);
  bool typed = ImGui::InputInt("##customres", &custom_res, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::PopStyleVar();
  if (typed) {
    custom_res = std::clamp(custom_res, 64, 8192);
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    a.graph.resolution = custom_res;
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Any resolution 64..8192, Enter to apply."));
}

void camera_tools(App &a) {
  (void)a;
  SceneState &sc = scene();
  int active = scene_active_camera();
  std::string label = tr("Free camera");
  if (active >= 0 && active < (int)sc.objects.size()) label = sc.objects[active].name;
  tool_label(tr("camera"));
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo("##camsel", label.c_str())) {
    if (ImGui::Selectable(tr("Free camera"), active < 0)) scene_active_camera() = -1;
    for (int idx : scene_camera_indices())
      if (ImGui::Selectable(sc.objects[idx].name.c_str(), idx == active)) {
        scene_active_camera() = idx;
        scene_last_used_camera() = idx;
      }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Which camera the perspective views look through."));
}

void sun_tools(App &a, const char *suffix) {
  (void)a;
  RenderSettings &rs = render_settings();
  tool_label(tr("sun"));
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat((std::string("##sunalt") + suffix).c_str(), &rs.sun_altitude, 1.f, 89.f,
                     "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sun altitude."));
  tool_gap();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat((std::string("##sunaz") + suffix).c_str(), &rs.sun_azimuth, 0.f, 360.f,
                     "%.0f\xC2\xB0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sun azimuth."));
}

// Terrain: how finely the ground is computed, and - in sculpt mode - the
// brush's size, strength and edge.
void tools_terrain(App &a) {
  resolution_tools(a);
  if (sculpt_state().active) {
    tool_sep();
    sculpt_params_row(a);
  }
}

// Materials: where the surface's colour comes from.
void tools_materials(App &a) {
  RenderSettings &rs = render_settings();
  tool_label(tr("albedo"));
  ImGui::SetNextItemWidth(150);
  static const char *const K[] = {"Auto (last texture)", "Procedural", "Chosen node"};
  ImGui::Combo("##albsrc", &rs.terrain_material_mode, tr_combo(K, 3).c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Where the terrain's colour comes from when no\n"
                               "MaterialOutput is assigned to it."));
  tool_sep();
  resolution_tools(a);
}

// Atmosphere: the air and the light.
void tools_atmosphere(App &a) {
  RenderSettings &rs = render_settings();
  tool_label(tr("fog"));
  ImGui::SetNextItemWidth(110);
  static const char *const K[] = {"Off", "Haze", "Fog", "Pollution"};
  ImGui::Combo("##fogtype", &rs.fog_type, tr_combo(K, 4).c_str());
  tool_sep();
  // Coverage rather than the on/off, which is the button in the left column:
  // clear to overcast is the value anyone actually moves, and it is the one
  // that changes the light on the ground.
  tool_label(tr("cloud"));
  ImGui::SetNextItemWidth(110);
  ImGui::SliderFloat("##cloudcov", &rs.cloud_coverage, 0.f, 1.f, "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Cloud coverage: 0 clear, 1 overcast."));
  tool_sep();
  sun_tools(a, "");
}

// Render: the camera and the viewport engine.
void tools_render(App &a) {
  camera_tools(a);
  tool_sep();
  tool_label(tr("engine"));
  ImGui::SetNextItemWidth(150);
  RenderSettings &rs = render_settings();
  static const char *const K[] = {"Rasterized PBR", "Cinematic raymarch"};
  ImGui::Combo("##vpengine", &rs.viewport_engine, tr_combo(K, 2).c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("How the viewport itself draws. Offline engines are\n"
                               "chosen per camera in the Render properties."));
}

// Objects: the mesh and AI commands that need a word, and the count.
void tools_objects(App &a) {
  SceneState &sc = scene();
  mesh_tool_buttons(a); // import, analyse and repair (panel_mesh.cpp)
  tool_sep();
  ai_tool_buttons(); // generate a model, an image (panel_ai_generate.cpp)
  tool_sep();
  tool_label(tr("%d objects"), (int)sc.objects.size());
}

// Lighting: the sun.
void tools_lighting(App &a) {
  RenderSettings &rs = render_settings();
  sun_tools(a, "2");
  tool_gap();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##sunint", &rs.sun_intensity, 0.f, 10.f, "x%.1f");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sun intensity."));
}

// Cameras: which one, and its lens.
void tools_cameras(App &a) {
  camera_tools(a);
  tool_sep();
  tool_label(tr("focal"));
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (active >= 0 && active < (int)sc.objects.size() &&
      sc.objects[active].type == SceneObject::Camera) {
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##focal", &sc.objects[active].cam.focal_mm, 8.f, 800.f, "%.0f mm",
                       ImGuiSliderFlags_Logarithmic);
  } else {
    tool_label(tr("(free camera)"));
  }
}

// Animation: the transport, so the graph can be scrubbed from any panel.
void tools_animation(App &a) {
  gpx::Timeline &tl = scene().timeline;
  if (tool_icon(Icon::ToStart, "##tostart", tr("Go to start"))) {
    a.anim_playing = false;
    anim_set_time(a, tl.play_start());
  }
  if (tool_icon(a.anim_playing ? Icon::Pause : Icon::Play, "##play",
                a.anim_playing ? tr("pause") : tr("play"), a.anim_playing))
    a.anim_playing = !a.anim_playing;
  if (tool_icon(Icon::Stop, "##stop", tr("stop"))) {
    a.anim_playing = false;
    anim_set_time(a, tl.play_start());
  }
  if (tool_icon(Icon::ToEnd, "##toend", tr("Go to end"))) {
    a.anim_playing = false;
    anim_set_time(a, tl.play_end());
  }
  tool_sep();
  tool_label(tr("frame"));
  ImGui::SetNextItemWidth(160);
  float f = tl.frame_of(a.graph.time);
  if (ImGui::SliderFloat("##anim_t", &f, tl.frame_of(tl.play_start()),
                         tl.frame_of(tl.play_end()), "%.0f"))
    anim_set_time(a, tl.time_of(std::round(f)));
  tool_sep();
  float t = a.graph.time, nt;
  if (tool_icon(Icon::PrevKey, "##pk", tr("Previous key")))
    if (anim_prev_key_time(a, t, nt)) anim_set_time(a, nt);
  if (tool_icon(Icon::NextKey, "##nk", tr("Next key")))
    if (anim_next_key_time(a, t, nt)) anim_set_time(a, nt);
  tool_sep();
  tool_label("%s", a.seq_active ? tr("rendering sequence...") : tl.format(a.graph.time).c_str());
}

} // namespace

void draw_workspace_tools(App &a) {
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
}

} // namespace studio
