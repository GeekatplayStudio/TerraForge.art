// Geekatplay TerraForge â€” viewport windows. Each view is its own dockable,
// resizable, floatable window with a Blender-style header toolbar.
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <imgui.h>
#include <cstdio>

namespace studio {

static const char *CAMERA_NAMES[] = {"Perspective", "Top", "Front", "Right"};
static const char *DISPLAY_NAMES[] = {"Wireframe", "Solid", "Textured"};
static const char *ENGINE_NAMES[] = {"Rasterized PBR", "Cinematic raymarch"};

const char *view_window_name(int slot) {
  static const char *names[6] = {"View 1", "View 2", "View 3",
                                 "View 4", "View 5", "View 6"};
  return names[slot < 0 || slot > 5 ? 0 : slot];
}

static void draw_scale_bar(ImDrawList *dl, ImVec2 corner, float view_px_w,
                           const RenderSettings::ViewConfig &vc) {
  RenderSettings &rs = render_settings();
  float view_m = renderer_view_width_m(vc);
  if (view_m <= 0) return;
  bool imperial = rs.units == 1;
  float unit_per_m = imperial ? 3.28084f : 1.f;
  float view_units = view_m * unit_per_m;
  float target = view_units / 5.f;
  float mag = std::pow(10.f, std::floor(std::log10(std::max(target, 1e-6f))));
  float bar_units = mag;
  if (target / mag >= 5) bar_units = 5 * mag;
  else if (target / mag >= 2) bar_units = 2 * mag;
  float bar_px = bar_units / view_units * view_px_w;
  if (bar_px < 8 || bar_px > view_px_w) return;
  char label[48];
  if (imperial) {
    if (bar_units >= 5280) snprintf(label, sizeof label, "%.4g mi", bar_units / 5280.f);
    else snprintf(label, sizeof label, "%.4g ft", bar_units);
  } else {
    if (bar_units >= 1000) snprintf(label, sizeof label, "%.4g km", bar_units / 1000.f);
    else snprintf(label, sizeof label, "%.4g m", bar_units);
  }
  ImVec2 p0(corner.x - bar_px - 14, corner.y - 16);
  ImVec2 p1(corner.x - 14, corner.y - 16);
  ImU32 col = IM_COL32(235, 233, 228, 230);
  ImU32 sh = IM_COL32(0, 0, 0, 140);
  dl->AddLine(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x + 1, p1.y + 1), sh, 3.f);
  dl->AddLine(p0, p1, col, 2.f);
  dl->AddLine(ImVec2(p0.x, p0.y - 4), ImVec2(p0.x, p0.y + 4), col, 2.f);
  dl->AddLine(ImVec2(p1.x, p1.y - 4), ImVec2(p1.x, p1.y + 4), col, 2.f);
  ImVec2 ts = ImGui::CalcTextSize(label);
  ImVec2 tp((p0.x + p1.x - ts.x) * 0.5f, p0.y - ts.y - 3);
  dl->AddText(ImVec2(tp.x + 1, tp.y + 1), sh, label);
  dl->AddText(tp, col, label);
}

// shared options menu â€” used by the header button and by right-click
static void view_options_menu(App &a, int slot, RenderSettings::ViewConfig &vc) {
  RenderSettings &rs = render_settings();
  const float W = 250.f;
  ImGui::SeparatorText("This view");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Camera", &vc.camera, "Perspective\0Top\0Front\0Right\0");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Shading", &vc.display, "Wireframe\0Solid\0Textured\0");
  studio::Checkbox("Atmosphere", &vc.atmosphere);
  studio::Checkbox("Water", &vc.show_water_view);
  studio::Checkbox("Grid", &vc.grid);
  studio::Checkbox("Selection outline", &vc.outlines);

  ImGui::SeparatorText("Viewport windows");
  int count = prefs().view_count;
  ImGui::TextDisabled("How many view windows:");
  for (int n = 1; n <= 6; ++n) {
    char lbl[8];
    snprintf(lbl, sizeof lbl, "%d", n);
    bool active = count == n;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::Button(lbl, ImVec2(34, 0))) {
      prefs().view_count = n;
      prefs_save();
      a.request_layout_reset = true;
    }
    if (active) ImGui::PopStyleColor();
    if (n < 6) ImGui::SameLine();
  }
  ImGui::TextDisabled("Each view is a normal window: drag its tab to\n"
                      "float or re-dock it. Your layout is remembered.");

  ImGui::SeparatorText("Real-time engine");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Engine", &rs.viewport_engine,
               "Rasterized PBR\0Cinematic raymarch\0");
  ImGui::TextDisabled("Cinematic adds raymarched AO, softer shadows,\n"
                      "cloud shadows and sharper reflections.");

  ImGui::SeparatorText("Background (all views)");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Mode", &rs.background_mode, "Sky\0Gradient\0Solid color\0");
  if (rs.background_mode != 0) {
    ImGui::ColorEdit3("Color", rs.bg_color);
    if (rs.background_mode == 1) ImGui::ColorEdit3("Bottom", rs.bg_color2);
  }

  ImGui::SeparatorText("Units & scale");
  ImGui::SetNextItemWidth(W);
  ImGui::Combo("Units", &rs.units, "Metric\0Imperial\0");
  ImGui::SetNextItemWidth(W);
  ImGui::DragFloat("Terrain size (m)", &rs.terrain_size_m, 50.f, 100.f, 100000.f,
                   "%.0f");
}

static void view_options_popup(App &a, int slot, RenderSettings::ViewConfig &vc,
                               const char *id) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 9));
  if (ImGui::BeginPopup(id)) {
    view_options_menu(a, slot, vc);
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(2);
}

// slim header strip: camera | display | engine | toggles | more
static void view_header(App &a, int slot, RenderSettings::ViewConfig &vc) {
  RenderSettings &rs = render_settings();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 3));
  ImGui::SetNextItemWidth(108);
  ImGui::Combo("##cam", &vc.camera, "Perspective\0Top\0Front\0Right\0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera / projection for this view");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(96);
  ImGui::Combo("##disp", &vc.display, "Wireframe\0Solid\0Textured\0");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shading mode");
  ImGui::SameLine();
  if (vc.camera == 0) {
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("##engine", &rs.viewport_engine,
                 "Rasterized PBR\0Cinematic raymarch\0");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Real-time engine for perspective views.\n"
                        "Cinematic raymarch adds ray-traced soft shadows,\n"
                        "ambient occlusion and reflections (slower).");
    ImGui::SameLine();
  }
  auto toggle = [&](const char *label, bool *v, const char *tip) {
    if (*v) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::SmallButton(label)) *v = !*v;
    if (*v) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine();
  };
  toggle("atmo", &vc.atmosphere, "Sky, fog and clouds in this view");
  toggle("water", &vc.show_water_view, "Show the water surface");
  toggle("grid", &vc.grid, "Ground reference grid");
  toggle("outline", &vc.outlines, "Highlight the selected object with an outline");

  // "more" menu â€” roomy, padded, with section headers
  if (ImGui::SmallButton("view options")) ImGui::OpenPopup("view_more");
  view_options_popup(a, slot, vc, "view_more");
  ImGui::PopStyleVar(); // frame padding
}

static void view_body(App &a, int slot, RenderSettings::ViewConfig &vc) {
  ImVec2 avail = ImGui::GetContentRegionAvail();
  int w = (int)avail.x, h = (int)avail.y;
  if (w < 16 || h < 16) return;
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  unsigned tex = renderer_draw_view(slot, vc, w, h, ImGui::GetIO().DeltaTime);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)w, (float)h), ImVec2(0, 1),
               ImVec2(1, 0));
  if (ImGui::IsItemHovered()) {
    ImGuiIO &io = ImGui::GetIO();
    bool rot = ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f);
    bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
               ImGui::IsMouseDown(ImGuiMouseButton_Right);
    // Ctrl+drag dollies (moves the camera along its view axis)
    bool dolly = io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (vc.camera == 0)
      renderer_camera_input(io.MouseDelta.x, io.MouseDelta.y, io.MouseWheel,
                            rot && !dolly, pan, dolly);
    else
      renderer_view_input(vc, io.MouseDelta.x, io.MouseDelta.y, io.MouseWheel, rot,
                          pan, w);
    // click (without dragging) selects the object under the cursor
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f)) {
      float u = (io.MousePos.x - p0.x) / (float)w;
      float v = (io.MousePos.y - p0.y) / (float)h;
      int hit = renderer_pick(slot, vc, u, v, w, h);
      if (hit >= 0) {
        scene().selected = hit; // shared: updates every view and the panels
        a.scene_selection_serial++;
      }
    }
    // right-click (without panning) opens the same options menu
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.f))
      ImGui::OpenPopup("view_ctx");
  }
  view_options_popup(a, slot, vc, "view_ctx");
  ImDrawList *dl = ImGui::GetWindowDrawList();
  // corner labels
  ImU32 sh = IM_COL32(0, 0, 0, 150), fg = IM_COL32(225, 222, 216, 210);
  const char *cam = CAMERA_NAMES[vc.camera & 3];
  dl->AddText(ImVec2(p0.x + 9, p0.y + 7), sh, cam);
  dl->AddText(ImVec2(p0.x + 8, p0.y + 6), fg, cam);
  if (vc.camera == 0) {
    const char *eng = ENGINE_NAMES[render_settings().viewport_engine & 1];
    ImVec2 ts = ImGui::CalcTextSize(eng);
    dl->AddText(ImVec2(p0.x + w - ts.x - 8, p0.y + 6), fg, eng);
  }
  // selected object name
  SceneState &sc = scene();
  if (sc.selected >= 0 && sc.selected < (int)sc.objects.size()) {
    std::string sel = "selected: " + sc.objects[sc.selected].name;
    dl->AddText(ImVec2(p0.x + 9, p0.y + h - 21), sh, sel.c_str());
    dl->AddText(ImVec2(p0.x + 8, p0.y + h - 22), IM_COL32(230, 150, 70, 230),
                sel.c_str());
  }
  draw_scale_bar(dl, ImVec2(p0.x + w, p0.y + h), (float)w, vc);
}

void draw_panel_viewport(App &a) {
  RenderSettings &rs = render_settings();
  int view_count = std::clamp(prefs().view_count, 1, 6);
  for (int slot = 0; slot < view_count; ++slot) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::Begin(view_window_name(slot), nullptr,
                             ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (open) {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
      ImGui::BeginChild("##hdr", ImVec2(0, ImGui::GetFrameHeight() + 8),
                        ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
      view_header(a, slot, rs.views[slot]);
      ImGui::EndChild();
      ImGui::PopStyleVar();
      view_body(a, slot, rs.views[slot]);
    }
    ImGui::End();
  }
}

} // namespace studio




