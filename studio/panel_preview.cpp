// Geekatplay TerraForge — the Preview panel.
//
// The working viewports are for working: atmosphere off, clouds off, water
// off, whatever makes them fast. This panel is for seeing the picture: the
// chosen camera's view with its own switches for sky, clouds, water and
// shadows and its own quality, redrawn as the graph changes, so an edit to
// a node shows up here at once — and, on request, the final engine's
// progressive result in the same frame, at the camera's aspect ratio.
#include "app.hpp"
#include "panel_float.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace studio {

namespace {
struct PreviewState {
  int camera = -2;    // -2 active camera, -1 free viewport, >= 0 object index
  int quality = 1;    // 0 = 25 %, 1 = 50 %, 2 = 100 %
  bool live = true;   // redraw every frame; otherwise on Refresh
  bool refresh = false;
  bool atmosphere = true, clouds = true, water = true, shadows = true;
  bool show_final = false; // the engine's result instead of the live view
  unsigned last_tex = 0;
  RenderSettings::ViewConfig vc;
};
PreviewState P;
constexpr int PREVIEW_SLOT = 6;
} // namespace

void draw_panel_preview(App &a) {
  if (!a.show_preview) return;
  panel_float_prepare(a, "Preview");
  ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Preview", &a.show_preview)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Preview");
  SceneState &sc = scene();
  RenderSettings &rs = render_settings();

  // ---- which camera
  std::vector<int> cams;
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[i].type == SceneObject::Camera) cams.push_back(i);
  int cam_index = P.camera;
  if (cam_index == -2) {
    cam_index = scene_active_camera();
    // no camera active: the first one in the scene is still the shot
    if (cam_index < 0 && !cams.empty()) cam_index = cams.front();
  }
  if (cam_index >= (int)sc.objects.size() ||
      (cam_index >= 0 && sc.objects[cam_index].type != SceneObject::Camera))
    cam_index = -1;
  std::string label = P.camera == -2 ? "Active camera"
                      : P.camera == -1 ? "Viewport"
                                       : sc.objects[cam_index < 0 ? 0 : cam_index].name;
  if (P.camera >= 0 && cam_index < 0) label = "Viewport";
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo("##pvcam", label.c_str())) {
    if (ImGui::Selectable("Active camera", P.camera == -2)) P.camera = -2;
    if (ImGui::Selectable("Viewport (free orbit)", P.camera == -1)) P.camera = -1;
    for (int i : cams)
      if (ImGui::Selectable(sc.objects[i].name.c_str(), P.camera == i)) P.camera = i;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Whose view to show. The active camera follows\n"
                      "whatever camera is active for rendering.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(72);
  P.quality = prefs().preview_quality;
  if (ImGui::Combo("##pvq", &P.quality, "25%\0" "50%\0" "100%\0")) {
    prefs().preview_quality = P.quality;
    prefs_save();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Render scale of the live view: lower is faster.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(78);
  {
    // how often the live picture is redrawn; a node edit or a camera move
    // forces one at once regardless
    const int RATES[6] = {1, 2, 5, 10, 20, 30};
    int cur = 3;
    for (int i = 0; i < 6; ++i)
      if (RATES[i] == prefs().preview_fps) cur = i;
    if (ImGui::Combo("##pvfps", &cur, "1 fps\0" "2 fps\0" "5 fps\0" "10 fps\0"
                                      "20 fps\0" "30 fps\0")) {
      prefs().preview_fps = RATES[cur];
      prefs_save();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Refresh rate of the live view. Changes to the graph\n"
                        "or the camera redraw it immediately anyway.");
  }
  ImGui::SameLine();
  studio::Checkbox("live", &P.live);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Keep redrawing at the rate chosen; off, only Refresh.");
  if (!P.live) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) P.refresh = true;
  }

  // ---- what to include, independently of the working views
  studio::Checkbox("sky", &P.atmosphere);
  ImGui::SameLine();
  studio::Checkbox("clouds", &P.clouds);
  ImGui::SameLine();
  studio::Checkbox("water", &P.water);
  ImGui::SameLine();
  studio::Checkbox("shadows", &P.shadows);

  // ---- the final engine
  const bool have_cam = cam_index >= 0;
  int engine = have_cam ? sc.objects[cam_index].cam.render.engine : 0;
  int rw = 0, rh = 0;
  bool busy = false;
  std::string line;
  unsigned final_tex = render_live_texture(rw, rh, busy, line);
  {
    char btn[96];
    std::snprintf(btn, sizeof btn, "Render with %s", render_engine_label(engine));
    if (!have_cam) ImGui::BeginDisabled();
    if (ImGui::Button(btn)) {
      a.request_camera_render = cam_index;
      P.show_final = true;
    }
    if (!have_cam) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(have_cam ? "The camera's own render settings (Render tab):\n"
                                   "engine, size, samples. The result refines\n"
                                   "here pass by pass."
                                 : "Add a camera first (Objects panel).");
    if (busy) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Cancel")) render_cancel();
    }
    if (final_tex) {
      ImGui::SameLine();
      if (ImGui::RadioButton("live", !P.show_final)) P.show_final = false;
      ImGui::SameLine();
      if (ImGui::RadioButton("final", P.show_final)) P.show_final = true;
    }
    if (busy || (P.show_final && !line.empty())) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", line.empty() ? "starting..." : line.c_str());
    }
  }

  // ---- the picture, at the camera's aspect ratio
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 32 || avail.y < 32) {
    ImGui::End();
    return;
  }
  float aspect = 16.f / 9.f;
  if (have_cam) {
    const RenderAssign &r = sc.objects[cam_index].cam.render;
    if (r.width > 0 && r.height > 0) aspect = (float)r.width / (float)r.height;
  }
  float w = avail.x, h = w / aspect;
  if (h > avail.y) {
    h = avail.y;
    w = h * aspect;
  }
  ImVec2 pos = ImGui::GetCursorScreenPos();
  pos.x += (avail.x - w) * 0.5f;
  ImGui::SetCursorScreenPos(pos);

  if (P.show_final && final_tex) {
    ImGui::Image((ImTextureID)(intptr_t)final_tex, ImVec2(w, h));
  } else {
    // due: the rate says so, or something the picture depends on changed
    static double last_draw = -1.0;
    static uint64_t last_serial = 0;
    static float last_eye[3] = {0, 0, 0};
    float eye[3], tgt[3], fov;
    renderer_get_camera(eye, tgt, &fov);
    const double now = ImGui::GetTime();
    bool changed = a.eval_serial != last_serial || eye[0] != last_eye[0] ||
                   eye[1] != last_eye[1] || eye[2] != last_eye[2];
    bool due = now - last_draw >= 1.0 / std::max(prefs().preview_fps, 1);
    if ((P.live && (due || changed)) || P.refresh || !P.last_tex) {
      last_draw = now;
      last_serial = a.eval_serial;
      for (int k = 0; k < 3; ++k) last_eye[k] = eye[k];
      P.refresh = false;
      const float q = P.quality == 0 ? 0.25f : P.quality == 1 ? 0.5f : 1.f;
      int pw = std::max(16, (int)(w * q)), ph = std::max(16, (int)(h * q));
      // this view's switches, without touching what the working views use
      P.vc.camera = 0;
      P.vc.display = 2;
      P.vc.atmosphere = P.atmosphere;
      P.vc.show_water_view = P.water;
      P.vc.grid = false;
      P.vc.outlines = false;
      P.vc.scene_camera = P.camera == -2 ? -2 : (cam_index >= 0 ? cam_index : -1);
      const bool clouds_saved = rs.clouds_on, shadows_saved = rs.shadows;
      rs.clouds_on = P.clouds && clouds_saved;
      rs.shadows = P.shadows && shadows_saved;
      P.last_tex = renderer_draw_view(PREVIEW_SLOT, P.vc, pw, ph,
                                      ImGui::GetIO().DeltaTime);
      rs.clouds_on = clouds_saved;
      rs.shadows = shadows_saved;
    }
    if (P.last_tex)
      ImGui::Image((ImTextureID)(intptr_t)P.last_tex, ImVec2(w, h), ImVec2(0, 1),
                   ImVec2(1, 0));
  }
  // a hairline frame, so the picture reads as a picture
  ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + w, pos.y + h),
                                      theme::fade(theme::text_dim(), 0.5f));
  ImGui::End();
}

} // namespace studio
