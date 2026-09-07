// Geekatplay TerraForge — the Preview panel.
//
// The working viewports are for working: atmosphere off, clouds off, water
// off, whatever makes them fast. This panel is for seeing the picture: the
// chosen camera's view with its own switches for sky, clouds, water and
// shadows and its own quality, redrawn as the graph changes, so an edit to
// a node shows up here at once — and, on request, the final engine's
// progressive result in the same frame, at the camera's aspect ratio.
#include "app.hpp"
#include "i18n.hpp"
#include "perf.hpp"
#include "panel_float.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace studio {

namespace {
// -3 follows whatever camera is selected, -2 the active one, -1 the free
// viewport, >= 0 a particular object.
constexpr int PV_SELECTED = -3, PV_ACTIVE = -2, PV_VIEWPORT = -1;

struct PreviewState {
  int camera = PV_SELECTED;
  int quality = 1;    // 0 = 25 %, 1 = 50 %, 2 = 100 %
  bool live = true;   // redraw every frame; otherwise on Refresh
  bool refresh = false;
  bool atmosphere = true, clouds = true, water = true, shadows = true;
  bool show_final = false; // the engine's result instead of the live view
  unsigned last_tex = 0;
  RenderSettings::ViewConfig vc;
};
PreviewState P;
constexpr int PREVIEW_SLOT = SLOT_PREVIEW;

bool is_camera(const SceneState &sc, int i) {
  return i >= 0 && i < (int)sc.objects.size() &&
         sc.objects[(size_t)i].type == SceneObject::Camera;
}

// Which camera the panel is actually looking through. Selecting a camera in
// the Objects tree should show it - that is the whole point of having both
// panels open - and selecting something that is not a camera should leave
// the picture alone rather than blanking it, so that case falls through to
// the active camera and then to the first one in the scene.
int resolve_camera(int mode) {
  SceneState &sc = scene();
  if (mode == PV_SELECTED) {
    if (is_camera(sc, sc.selected)) return sc.selected;
    mode = PV_ACTIVE;
  }
  if (mode == PV_ACTIVE) {
    const int a = scene_active_camera();
    if (is_camera(sc, a)) return a;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (is_camera(sc, i)) return i;
    return -1;
  }
  return is_camera(sc, mode) ? mode : -1;
}

// Everything about the shot that, if it changes, means the picture is stale.
//
// This used to be the orbit camera's eye position, read from
// renderer_get_camera - which is not the camera being previewed at all. Move
// a scene camera, or animate one, or change its lens, and the panel showed
// the old frame until the refresh timer happened to come round. AGENTS.md
// has the rule: the viewport's orbit camera is not the scene's camera, and
// asking the wrong one is a bug that looks like a stale cache.
struct ShotSig {
  int index = -2;
  float eye[3] = {0, 0, 0}, target[3] = {0, 0, 0};
  float focal = 0.f, aperture = 0.f, distortion = 0.f;
  int format = -1;
  bool optics = false;
  bool operator!=(const ShotSig &o) const {
    for (int k = 0; k < 3; ++k)
      if (eye[k] != o.eye[k] || target[k] != o.target[k]) return true;
    return index != o.index || focal != o.focal || aperture != o.aperture ||
           distortion != o.distortion || format != o.format ||
           optics != o.optics;
  }
};

ShotSig shot_signature(int cam_index) {
  ShotSig s;
  s.index = cam_index;
  if (cam_index < 0) {
    // the free orbit: its own eye is the thing that moves
    float fov;
    renderer_get_camera(s.eye, s.target, &fov);
    s.focal = fov;
    return s;
  }
  const SceneObject &o = scene().objects[(size_t)cam_index];
  for (int k = 0; k < 3; ++k) {
    s.eye[k] = o.cam.eye[k];
    s.target[k] = o.cam.target[k];
  }
  s.focal = o.cam.focal_mm;
  s.aperture = o.cam.aperture;
  s.distortion = o.cam.distortion;
  s.format = o.cam.format;
  s.optics = o.cam.optics;
  return s;
}
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
  const int cam_index = P.camera == PV_VIEWPORT ? -1 : resolve_camera(P.camera);
  // The label says which camera is actually on screen, not just which mode
  // is chosen - "Selected" alone tells you nothing when the selection is a
  // rock and the panel has quietly fallen back to the active camera.
  std::string label;
  if (P.camera == PV_VIEWPORT) {
    label = tr("Viewport");
  } else if (cam_index < 0) {
    label = tr("No camera");
  } else {
    const std::string who = P.camera == PV_SELECTED  ? tr("Selected")
                            : P.camera == PV_ACTIVE  ? tr("Active")
                                                     : std::string();
    label = who.empty() ? sc.objects[(size_t)cam_index].name
                        : who + ": " + sc.objects[(size_t)cam_index].name;
  }
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("##pvcam", label.c_str())) {
    if (ImGui::Selectable(tr("Selected camera"), P.camera == PV_SELECTED))
      P.camera = PV_SELECTED;
    if (ImGui::Selectable(tr("Active camera"), P.camera == PV_ACTIVE))
      P.camera = PV_ACTIVE;
    if (ImGui::Selectable(tr("Viewport (free orbit)"), P.camera == PV_VIEWPORT))
      P.camera = PV_VIEWPORT;
    for (int i : cams)
      if (ImGui::Selectable(sc.objects[i].name.c_str(), P.camera == i)) P.camera = i;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Whose view to show. Selected follows whatever\n"
                      "camera you pick in the Objects tree, and falls back to\n"
                      "the active one when the selection is not a camera.\n"
                      "Active follows whatever is active for rendering."));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(72);
  P.quality = std::min(prefs().preview_quality, perf_quality().preview_quality_cap);
  static const char *const KQ[] = {"25%", "50%", "100%"};
  if (ImGui::Combo("##pvq", &P.quality, tr_combo(KQ, 3).c_str())) {
    prefs().preview_quality = P.quality;
    prefs_save();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Render scale of the live view: lower is faster."));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(78);
  {
    // how often the live picture is redrawn; a node edit or a camera move
    // forces one at once regardless
    const int RATES[6] = {1, 2, 5, 10, 20, 30};
    int cur = 3;
    for (int i = 0; i < 6; ++i)
      if (RATES[i] == prefs().preview_fps) cur = i;
    static const char *const KF[] = {"1 fps", "2 fps", "5 fps", "10 fps", "20 fps", "30 fps"};
    if (ImGui::Combo("##pvfps", &cur, tr_combo(KF, 6).c_str())) {
      prefs().preview_fps = RATES[cur];
      prefs_save();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", tr("Refresh rate of the live view. Changes to the graph\n"
                        "or the camera redraw it immediately anyway."));
  }
  ImGui::SameLine();
  studio::Checkbox(tr("live"), &P.live);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tr("Keep redrawing at the rate chosen; off, only Refresh."));
  if (!P.live) {
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Refresh"))) P.refresh = true;
  }

  // ---- what to include, independently of the working views
  studio::Checkbox(tr("sky"), &P.atmosphere);
  ImGui::SameLine();
  studio::Checkbox(tr("clouds"), &P.clouds);
  ImGui::SameLine();
  studio::Checkbox(tr("water"), &P.water);
  ImGui::SameLine();
  studio::Checkbox(tr("shadows"), &P.shadows);

  // ---- the final engine
  const bool have_cam = cam_index >= 0;
  int engine = have_cam ? sc.objects[cam_index].cam.render.engine : 0;
  int rw = 0, rh = 0;
  bool busy = false;
  std::string line;
  unsigned final_tex = render_live_texture(rw, rh, busy, line);
  {
    char btn[96];
    std::snprintf(btn, sizeof btn, tr("Render with %s"), tr(render_engine_label(engine)));
    if (!have_cam) ImGui::BeginDisabled();
    if (ImGui::Button(btn)) {
      a.request_camera_render = cam_index;
      P.show_final = true;
    }
    if (!have_cam) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", have_cam ? tr("The camera's own render settings (Render tab):\n"
                                   "engine, size, samples. The result refines\n"
                                   "here pass by pass.")
                                 : tr("Add a camera first (Objects panel)."));
    if (busy) {
      ImGui::SameLine();
      if (ImGui::SmallButton(tr("Cancel"))) render_cancel();
    }
    if (final_tex) {
      ImGui::SameLine();
      if (ImGui::RadioButton((std::string(tr("live")) + "##rlive").c_str(), !P.show_final)) P.show_final = false;
      ImGui::SameLine();
      if (ImGui::RadioButton(tr("final"), P.show_final)) P.show_final = true;
    }
    if (busy || (P.show_final && !line.empty())) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", line.empty() ? tr("starting...") : line.c_str());
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
  // The picture lives in a child that can never grow a scrollbar. It used to
  // sit directly in the window, and a picture sized from the available width
  // is a feedback loop: one pixel too tall adds a scrollbar, the scrollbar
  // takes width, the narrower picture no longer needs one - and the panel
  // shrinks and unshrinks every frame. Whole pixels for the same reason: a
  // fractional size resamples differently frame to frame and shimmers.
  ImGui::BeginChild("##picture", avail, ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  float w = std::floor(avail.x), h = std::floor(w / aspect);
  if (h > avail.y) {
    h = std::floor(avail.y);
    w = std::floor(h * aspect);
  }
  ImVec2 pos = ImGui::GetCursorScreenPos();
  pos.x += std::floor((avail.x - w) * 0.5f);
  ImGui::SetCursorScreenPos(pos);

  if (P.show_final && final_tex) {
    ImGui::Image((ImTextureID)(intptr_t)final_tex, ImVec2(w, h));
  } else {
    // due: the rate says so, or something the picture depends on changed
    static double last_draw = -1.0;
    static uint64_t last_serial = 0;
    static ShotSig last_shot;
    const ShotSig shot = shot_signature(cam_index);
    const double now = ImGui::GetTime();
    const bool changed = a.eval_serial != last_serial || shot != last_shot;
    bool due = now - last_draw >= 1.0 / std::max(std::min(prefs().preview_fps, perf_quality().preview_fps_cap), 1);
    if ((P.live && (due || changed)) || P.refresh || !P.last_tex) {
      last_draw = now;
      last_serial = a.eval_serial;
      last_shot = shot;
      P.refresh = false;
      const float q = P.quality == 0 ? 0.25f : P.quality == 1 ? 0.5f : 1.f;
      // Quantised to 8 pixels: a one-pixel change in the panel would
      // otherwise reallocate the render target every single frame.
      auto quant = [](float v) { return std::max(16, ((int)v / 8) * 8); };
      int pw = quant(w * q), ph = quant(h * q);
      // this view's switches, without touching what the working views use
      P.vc.camera = 0;
      P.vc.display = 2;
      P.vc.atmosphere = P.atmosphere;
      P.vc.show_water_view = P.water;
      P.vc.grid = false;
      P.vc.outlines = false;
      // The resolved index, never the mode: the renderer must be told which
      // camera, not "whichever one the panel meant".
      P.vc.scene_camera = cam_index;
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
  ImGui::EndChild();
  ImGui::End();
}

} // namespace studio
