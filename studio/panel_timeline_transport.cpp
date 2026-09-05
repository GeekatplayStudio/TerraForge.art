// Geekatplay TerraForge - the Timeline's transport row and ruler: go to
// start / previous key / previous frame / play / next frame / next key /
// end, the frame field, range and fps, loop mode, Autokey, the time
// display, and the ruler with its ticks, preview band, markers and scrub.
#include "anim_widgets.hpp"
#include "app.hpp"
#include "icons.hpp"
#include "i18n.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "timeline_internal.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace studio {

namespace {

bool tbtn(Icon ic, const char *id, const char *tip, bool active = false) {
  bool r = IconButton(ic, id, tip, active);
  ImGui::SameLine(0.f, 2.f);
  return r;
}

void step_frame(App &a, int n) {
  gpx::Timeline &tl = scene().timeline;
  float f = std::round(a.graph.time * tl.fps) + (float)n;
  anim_set_time(a, tl.time_of(f));
}

} // namespace

void tl_draw_transport(App &a) {
  gpx::Timeline &tl = scene().timeline;
  TimelineState &s = tl_state();
  float nt;
  if (tbtn(Icon::ToStart, "##tstart", tr("Go to start (Home)"))) anim_set_time(a, tl.play_start());
  if (tbtn(Icon::PrevKey, "##tpk", tr("Previous key"))) { if (anim_prev_key_time(a, a.graph.time, nt)) anim_set_time(a, nt); }
  if (tbtn(Icon::Chevron, "##tpf", tr("Previous frame"))) step_frame(a, -1);
  if (tbtn(a.anim_playing ? Icon::Pause : Icon::Play, "##tplay", tr("Play / pause (Space in the timeline)"), a.anim_playing)) a.anim_playing = !a.anim_playing;
  if (tbtn(Icon::ChevronDown, "##tnf", tr("Next frame"))) step_frame(a, 1);
  if (tbtn(Icon::NextKey, "##tnk", tr("Next key"))) { if (anim_next_key_time(a, a.graph.time, nt)) anim_set_time(a, nt); }
  if (tbtn(Icon::ToEnd, "##tend", tr("Go to end (End)"))) anim_set_time(a, tl.play_end());
  if (tbtn(Icon::Stop, "##tstop", tr("Stop and go to start"))) { a.anim_playing = false; anim_set_time(a, tl.play_start()); }

  // the current frame field, in the display mode
  ImGui::SameLine(0.f, 10.f);
  {
    char buf[48];
    snprintf(buf, sizeof buf, "%s", tl.format(a.graph.time).c_str());
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputText("##frame", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue)) {
      float t;
      if (tl.parse(buf, t)) anim_set_time(a, t);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Current frame. Type a frame, a timecode or seconds (1.5s)."));
  }
  ImGui::SameLine();
  {
    int d = (int)tl.display;
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("##disp", &d, "Frames\0Timecode\0Seconds\0")) tl.display = (gpx::TimeDisplay)d;
  }
  // range and rate
  ImGui::SameLine(0.f, 10.f);
  ImGui::TextDisabled("%s", tr("range"));
  ImGui::SameLine();
  {
    float fs = tl.frame_of(tl.start), fe = tl.frame_of(tl.end);
    ImGui::SetNextItemWidth(60);
    if (ImGui::DragFloat("##rs", &fs, 1.f, -1e6f, 1e6f, "%.0f")) { tl.start = tl.time_of(std::round(fs)); if (tl.end <= tl.start) tl.end = tl.start + tl.time_of(1); }
    ImGui::SameLine(0.f, 2.f);
    ImGui::SetNextItemWidth(60);
    if (ImGui::DragFloat("##re", &fe, 1.f, -1e6f, 1e6f, "%.0f")) tl.end = std::max(tl.time_of(std::round(fe)), tl.start + tl.time_of(1));
  }
  ImGui::SameLine();
  {
    static const float rates[] = {24.f, 25.f, 30.f, 48.f, 50.f, 60.f, 120.f};
    char cur[16];
    snprintf(cur, sizeof cur, "%g fps", tl.fps);
    ImGui::SetNextItemWidth(78);
    if (ImGui::BeginCombo("##fps", cur)) {
      for (float r : rates)
        if (ImGui::Selectable((std::to_string((int)r) + " fps").c_str(), r == tl.fps)) tl.fps = r;
      ImGui::EndCombo();
    }
  }
  ImGui::SameLine();
  {
    int l = (int)tl.loop;
    ImGui::SetNextItemWidth(88);
    if (ImGui::Combo("##loop", &l, "Once\0Loop\0Ping-pong\0")) tl.loop = (gpx::LoopMode)l;
  }
  ImGui::SameLine();
  studio::Checkbox(tr("Preview range"), &tl.preview);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Play and render only the band on the ruler (drag its ends)."));
  ImGui::SameLine();
  {
    if (tl.autokey) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0xc0, 0x38, 0x30, 0xff));
    if (IconButton(Icon::Autokey, "##autokey", tr("Autokey - on: editing an animated property writes a key at the current frame"), tl.autokey)) tl.autokey = !tl.autokey;
    if (tl.autokey) ImGui::PopStyleColor();
  }
  ImGui::SameLine();
  studio::Checkbox(tr("Every frame"), &tl.all_frames);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Play every frame instead of keeping real time."));
  ImGui::SameLine();
  studio::Checkbox(tr("Snap"), &tl.snap);
  ImGui::SameLine(0.f, 10.f);
  anim_key_nav(a);

  // keyboard when the timeline window has focus
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) a.anim_playing = !a.anim_playing;
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) tl_fit_view(a, false);
    if (ImGui::IsKeyPressed(ImGuiKey_A, false) && !s.sel.empty()) tl_fit_view(a, true);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) anim_set_time(a, tl.play_end());
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) step_frame(a, -1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) step_frame(a, 1);
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) tl_selection_ease(a, gpx::anim::Ease::Easy);
  }
}

void tl_draw_ruler(App &a, ImVec2 p0, float width, float height) {
  gpx::Timeline &tl = scene().timeline;
  TimelineState &s = tl_state();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p1(p0.x + width, p0.y + height);
  dl->AddRectFilled(p0, p1, theme::shade(theme::panel_bg(), 0.85f));
  // preview band
  float bs = tl_x_of(s, tl.preview_start), be = tl_x_of(s, tl.preview_end);
  if (tl.preview) dl->AddRectFilled(ImVec2(std::max(bs, p0.x), p0.y), ImVec2(std::min(be, p1.x), p1.y), theme::fade(theme::accent(), 0.18f));
  // document range edges
  float rs = tl_x_of(s, tl.start), re = tl_x_of(s, tl.end);
  dl->AddLine(ImVec2(rs, p0.y), ImVec2(rs, p1.y), theme::text_dim());
  dl->AddLine(ImVec2(re, p0.y), ImVec2(re, p1.y), theme::text_dim());
  // ticks: pick a frame step that keeps labels ~70px apart
  float frames_per_px = 1.f / (s.px_per_s / tl.fps);
  const int steps[] = {1, 2, 5, 10, 12, 24, 25, 30, 48, 50, 60, 100, 120, 240, 250, 300, 600, 1200, 3000, 6000};
  int step = steps[0];
  for (int st : steps) { step = st; if ((float)st / frames_per_px >= 70.f) break; }
  float f0 = std::floor(tl_t_of(s, p0.x) * tl.fps / (float)step) * (float)step;
  for (float f = f0;; f += (float)step) {
    float x = tl_x_of(s, tl.time_of(f));
    if (x > p1.x) break;
    if (x < p0.x) continue;
    dl->AddLine(ImVec2(x, p1.y - 6.f), ImVec2(x, p1.y), theme::text_dim());
    // minor ticks
    for (int m = 1; m < 5; ++m) {
      float mx = tl_x_of(s, tl.time_of(f + (float)step * (float)m / 5.f));
      if (mx < p1.x) dl->AddLine(ImVec2(mx, p1.y - 3.f), ImVec2(mx, p1.y), theme::fade(theme::text_dim(), 0.5f));
    }
    dl->AddText(ImVec2(x + 3.f, p0.y + 2.f), theme::text_dim(), tl.format(tl.time_of(f)).c_str());
  }
  // markers
  for (size_t i = 0; i < tl.markers.size(); ++i) {
    gpx::Marker &m = tl.markers[i];
    float x = tl_x_of(s, m.time);
    if (x < p0.x - 6 || x > p1.x + 6) continue;
    dl->AddTriangleFilled(ImVec2(x - 5, p0.y), ImVec2(x + 5, p0.y), ImVec2(x, p0.y + 7), theme::accent());
    if (!m.name.empty()) dl->AddText(ImVec2(x + 6, p0.y + 8.f), theme::accent(), m.name.c_str());
  }
  // interaction: scrub, drag markers, right-click menu, wheel zoom
  ImGui::SetCursorScreenPos(p0);
  ImGui::InvisibleButton("##ruler", ImVec2(std::max(width, 1.f), height));
  bool hovered = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();
  float mt = tl_t_of(s, io.MousePos.x);
  if (ImGui::IsItemActivated()) {
    s.dragging_marker = -1;
    for (size_t i = 0; i < tl.markers.size(); ++i)
      if (std::fabs(tl_x_of(s, tl.markers[i].time) - io.MousePos.x) < 6.f && io.MousePos.y < p0.y + 8.f) s.dragging_marker = (int)i;
    if (s.dragging_marker < 0) s.scrubbing = true;
  }
  if (ImGui::IsItemActive()) {
    if (s.dragging_marker >= 0 && s.dragging_marker < (int)tl.markers.size()) tl.markers[(size_t)s.dragging_marker].time = tl.snap_time(mt);
    else if (s.scrubbing) {
      float t = io.KeyCtrl ? mt : tl.snap_time(mt);
      if (io.KeyCtrl) { float nt; float best = 1e30f; for (const TrackRef &r : s.rows) if (r.track) for (const gpx::Key &k : r.track->keys) if (std::fabs(k.time - mt) < best) { best = std::fabs(k.time - mt); nt = k.time; } if (best < 1e29f) t = nt; }
      if (t != a.graph.time) anim_set_time(a, t);
    }
  }
  if (ImGui::IsItemDeactivated()) { s.scrubbing = false; s.dragging_marker = -1; }
  if (hovered && io.MouseWheel != 0.f) {
    float before = mt;
    s.px_per_s *= io.MouseWheel > 0 ? 1.15f : 1.f / 1.15f;
    s.px_per_s = std::clamp(s.px_per_s, 2.f, 4000.f);
    s.t0 += before - tl_t_of(s, io.MousePos.x);
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##rulermenu");
  if (ImGui::BeginPopup("##rulermenu")) {
    static float menu_t = 0.f;
    if (ImGui::IsWindowAppearing()) menu_t = tl.snap_time(mt);
    if (ImGui::MenuItem(tr("Add marker"))) { undo_push(a, "Add marker"); tl.markers.push_back({menu_t, tr("Marker")}); }
    int under = -1;
    for (size_t i = 0; i < tl.markers.size(); ++i) if (std::fabs(tl.markers[i].time - menu_t) < 0.5f / tl.fps + 1e-4f) under = (int)i;
    if (under >= 0) {
      static char name[64];
      if (ImGui::IsWindowAppearing()) snprintf(name, sizeof name, "%s", tl.markers[(size_t)under].name.c_str());
      ImGui::SetNextItemWidth(140);
      if (ImGui::InputText(tr("Name"), name, sizeof name)) tl.markers[(size_t)under].name = name;
      if (ImGui::MenuItem(tr("Remove marker"))) { undo_push(a, "Remove marker"); tl.markers.erase(tl.markers.begin() + under); }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Set preview start here"))) { tl.preview_start = menu_t; tl.preview = true; }
    if (ImGui::MenuItem(tr("Set preview end here"))) { tl.preview_end = menu_t; tl.preview = true; }
    if (ImGui::MenuItem(tr("Preview range from selection"), nullptr, false, !s.sel.empty())) {
      float lo = 1e30f, hi = -1e30f;
      for (const KeySel &k : s.sel) { lo = std::min(lo, k.time); hi = std::max(hi, k.time); }
      tl.preview_start = lo; tl.preview_end = std::max(hi, lo + tl.time_of(1)); tl.preview = true;
    }
    if (ImGui::MenuItem(tr("Set range start here"))) tl.start = menu_t;
    if (ImGui::MenuItem(tr("Set range end here"))) tl.end = std::max(menu_t, tl.start + tl.time_of(1));
    ImGui::EndPopup();
  }
}

void tl_draw_playhead(App &a, ImVec2 p0, float height) {
  TimelineState &s = tl_state();
  float x = tl_x_of(s, a.graph.time);
  if (x < s.area_x0 || x > s.area_x1) return;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(ImVec2(s.area_x0, p0.y), ImVec2(s.area_x1, p0.y + height), true);
  dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + height), IM_COL32(0xf0, 0xe0, 0xc0, 0xd0), 1.5f);
  dl->PopClipRect();
}

} // namespace studio
