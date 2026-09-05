// Geekatplay TerraForge — row 3: the tools for the chosen workflow, one
// function per workspace. Commands are palette icons (square, on the icon
// ladder, named in their tooltips); text stays only where a combo or a
// slider needs a caption. The frame around them, the workspace tabs and
// the global tools are in toolbar_bars.cpp.
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
#include <cctype>
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
    const bool active = a.graph.resolution == res;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme::accent()));
    if (ImGui::Button(label, ImVec2(std::max(h, ImGui::CalcTextSize(label).x + 10.f), h))) {
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      a.graph.resolution = res;
      a.graph.mark_all_dirty();
      a.request_eval();
    }
    if (active) ImGui::PopStyleColor();
    tool_gap();
  }
  static int custom_res = 0;
  if (custom_res == 0) custom_res = a.graph.resolution;
  ImGui::SetNextItemWidth(58);
  if (ImGui::InputInt("##customres", &custom_res, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
    custom_res = std::clamp(custom_res, 64, 8192);
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.resolution = custom_res;
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Any resolution 64..8192, Enter to apply."));
}

void camera_tools(App &a) {
  SceneState &sc = scene();
  int active = scene_active_camera();
  std::string label = tr("Free camera");
  if (active >= 0 && active < (int)sc.objects.size()) label = sc.objects[active].name;
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
  tool_gap();
  if (tool_icon(Icon::Camera, "##addcam", tr("Add camera"))) {
    int idx = scene_add_camera();
    scene_active_camera() = idx;
    sc.selected = idx;
    a.scene_selection_serial++;
  }
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

// Terrain: the shape of the ground, and how finely it is computed.
void tools_terrain(App &a) {
  resolution_tools(a);
  tool_sep();
  if (tool_icon(Icon::Bake, "##bake4k",
                tr("Bake 4k exports\n\nRe-evaluate at 4096 with every export node enabled."))) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    for (auto &n : a.graph.nodes)
      if (auto *e = n->attrs.find("auto_export")) e->b = true;
    a.graph.resolution = 4096;
    a.graph.mark_all_dirty();
    a.request_eval();
    a.status = "baking at 4096; export nodes write when done";
  }
  tool_sep();
  SculptState &s = sculpt_state();
  if (tool_icon(Icon::Brush, "##sculpt",
                tr("Sculpt\n\nBrush directly on the terrain. Strokes live in a\n"
                   "TerrainSculpt node, so the procedural chain under\n"
                   "them survives retuning."),
                s.active))
    s.active = !s.active;
}

// Materials: what the surface is made of.
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
  if (tool_icon(Icon::Textured, "##textured", tr("Textured"), rs.use_albedo))
    rs.use_albedo = !rs.use_albedo;
  tool_sep();
  resolution_tools(a);
}

// Atmosphere: the air, the light and the water.
void tools_atmosphere(App &a) {
  RenderSettings &rs = render_settings();
  if (tool_icon(Icon::Cloud, "##clouds", tr("Clouds"), rs.clouds_on)) rs.clouds_on = !rs.clouds_on;
  if (tool_icon(Icon::Water, "##water", tr("Water"), rs.show_water)) rs.show_water = !rs.show_water;
  if (tool_icon(Icon::Sun, "##shadows", tr("Shadows"), rs.shadows)) rs.shadows = !rs.shadows;
  tool_sep();
  tool_label(tr("fog"));
  ImGui::SetNextItemWidth(110);
  static const char *const K[] = {"Off", "Haze", "Fog", "Pollution"};
  ImGui::Combo("##fogtype", &rs.fog_type, tr_combo(K, 4).c_str());
  tool_sep();
  sun_tools(a, "");
}

// Render: the camera and the output.
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
  tool_sep();
  if (tool_icon(Icon::Render, "##rendercam",
                tr("Render the active camera\n\nRender through the active camera with its own\n"
                   "engine, resolution and sample settings.")))
    a.request_camera_render = scene_active_camera();
}

// Objects: what stands in the world. The five primitives share the Mesh
// glyph; the tooltip names each.
void tools_objects(App &a) {
  SceneState &sc = scene();
  struct P {
    const char *kind, *id, *tip;
  } prims[] = {{"cube", "##addcube", "Add cube"},
               {"sphere", "##addsphere", "Add sphere"},
               {"plane", "##addplane", "Add plane"},
               {"cylinder", "##addcyl", "Add cylinder"},
               {"cone", "##addcone", "Add cone"}};
  for (const P &p : prims) {
    if (tool_icon(Icon::Mesh, p.id, tr(p.tip))) {
      undo_push(a, std::string("Add ") + p.kind);
      sc.selected = scene_add_primitive(p.kind, "");
      a.scene_selection_serial++;
    }
    // the type letter, small, in the button's corner
    ImVec2 mx = ImGui::GetItemRectMax();
    char letter[2] = {(char)std::toupper(p.kind[0]), 0};
    ImVec2 ts = ImGui::CalcTextSize(letter);
    ImGui::GetWindowDrawList()->AddText(ImVec2(mx.x - ts.x - 2.f, mx.y - ts.y - 1.f),
                                        theme::text_dim(), letter);
  }
  tool_sep();
  if (tool_icon(Icon::Planet, "##addplanet", tr("om.add_planet_tip"))) {
    undo_push(a, "Add planet");
    sc.selected = scene_add_planet();
    a.scene_selection_serial++;
  }
  if (tool_icon(Icon::Terrain, "##addinf", tr("om.add_infinite_tip"))) {
    undo_push(a, "Add infinite terrain");
    sc.selected = scene_add_infinite_surface(-1);
    a.scene_selection_serial++;
  }
  tool_sep();
  mesh_tool_buttons(a); // import, analyse and repair (panel_mesh.cpp)
  tool_sep();
  ai_tool_buttons(); // generate a model, an image (panel_ai_generate.cpp)
  tool_sep();
  tool_label(tr("%d objects"), (int)sc.objects.size());
}

// Lighting: the sun and the placed lights.
void tools_lighting(App &a) {
  RenderSettings &rs = render_settings();
  sun_tools(a, "2");
  tool_gap();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##sunint", &rs.sun_intensity, 0.f, 10.f, "x%.1f");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Sun intensity."));
  tool_sep();
  if (tool_icon(Icon::Sun, "##shadows2", tr("Shadows"), rs.shadows)) rs.shadows = !rs.shadows;
  tool_sep();
  if (tool_icon(Icon::Light, "##addlight",
                tr("Add light\n\nA point light in the scene. A LightSource node in the\n"
                   "graph does the same and keeps it in the network."))) {
    undo_push(a, "Add light");
    scene().selected = scene_add_light("");
    a.scene_selection_serial++;
  }
}

// Cameras: which one, and a new one.
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
  if (tool_icon(Icon::KeyAdd, "##ak", tr("Key the selected object's transform (K)")))
    anim_key_selection_transform(a);
  if (tool_icon(Icon::NextKey, "##nk", tr("Next key")))
    if (anim_next_key_time(a, t, nt)) anim_set_time(a, nt);
  tool_gap(4.f);
  if (tool_icon(Icon::Autokey, "##autokey", tr("Autokey"), tl.autokey)) tl.autokey = !tl.autokey;
  tool_sep();
  if (tool_icon(Icon::Timeline, "##timeline", tr("Timeline"), a.show_timeline))
    a.show_timeline = !a.show_timeline;
  if (tool_icon(Icon::Curve, "##curves", tr("Curves"), a.show_curve_editor))
    a.show_curve_editor = !a.show_curve_editor;
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
