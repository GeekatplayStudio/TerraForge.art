// Geekatplay TerraForge - the Curve editor (F-curves): value against time
// for the tracks chosen in the Timeline, in their axis colours. Keys drag
// in both axes; tangent handles drag (Alt breaks them); Speed shows the
// derivative; Norm scales every curve to 0..1 so an opacity and an
// altitude compare on one graph. A ghost of the curve before the drag
// stays visible during it. Modifier and expression rows sit under the
// graph. See docs/ANIMATION.md, part E.
#include "anim_widgets.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "panel_float.hpp"
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

struct CurveView {
  float v0 = -1.f, v1 = 1.f; // value range shown
  bool fitted = false;
  // drag
  bool dragging = false;
  bool drag_started = false;
  std::string drag_track;
  float drag_time = 0.f;
  int drag_handle = 0; // 0 key, -1 in, 1 out
  gpx::Track ghost;
  std::string ghost_track;
};
CurveView g_cv;

float value_of(const gpx::Track &t, float time, bool speed) {
  if (!speed) return t.sample(time);
  const float h = 1.f / 120.f;
  return (t.sample(time + h) - t.sample(time - h)) / (2.f * h);
}

struct Shown { TrackRef ref; float lo = 0.f, hi = 1.f; };

void draw_modifiers(App &a, Shown &s) {
  gpx::Track &t = *s.ref.track;
  ImGui::PushID(s.ref.id.c_str());
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(anim_comp_color(s.ref.comp, s.ref.color)), "%s . %s", s.ref.owner.c_str(), s.ref.label.c_str());
  ImGui::SameLine();
  {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", t.expr.c_str());
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputTextWithHint("##expr", tr("expression, e.g. sin(t*2)*0.3 + value"), buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue)) {
      undo_push(a, "Expression");
      t.expr = buf;
      anim_touched(a, s.ref);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("t, frame, fps, value, pi; sin cos abs min max clamp lerp pow noise; \"Camera 1\".cam.focal_mm; world.sun_azimuth"));
    if (!t.expr.empty()) {
      std::string err;
      float v;
      gpx::ExprContext ctx = anim_expr_context(a.graph.time);
      ctx.value = t.curve(a.graph.time);
      if (!gpx::expr_eval(t.expr, ctx, v, &err) && !err.empty()) { ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme::error()), "%s", err.c_str()); }
    }
  }
  int remove = -1;
  for (size_t i = 0; i < t.modifiers.size(); ++i) {
    gpx::Modifier &m = t.modifiers[i];
    ImGui::PushID((int)i);
    const char *names[] = {"Noise", "Oscillator", "Offset", "Limit", "Smooth"};
    bool changed = false;
    changed |= studio::Checkbox("##en", &m.enabled);
    ImGui::SameLine();
    ImGui::Text("%s", tr(names[(int)m.type]));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    switch (m.type) {
      case gpx::ModType::Noise:
        changed |= ImGui::DragFloat(tr("amp"), &m.a, 0.01f); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        changed |= ImGui::DragFloat(tr("freq"), &m.b, 0.01f, 0.001f, 100.f); ImGui::SameLine(); ImGui::SetNextItemWidth(60);
        changed |= ImGui::DragInt(tr("oct"), &m.octaves, 0.1f, 1, 6); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        { int sd = (int)m.seed; if (ImGui::DragInt(tr("seed"), &sd, 1.f, 0, 100000)) { m.seed = (uint32_t)std::max(sd, 0); changed = true; } }
        break;
      case gpx::ModType::Oscillator:
        changed |= ImGui::DragFloat(tr("amp"), &m.a, 0.01f); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        changed |= ImGui::DragFloat(tr("freq"), &m.b, 0.01f, 0.001f, 100.f); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        changed |= ImGui::DragFloat(tr("phase"), &m.c, 0.01f); ImGui::SameLine(); ImGui::SetNextItemWidth(80);
        changed |= ImGui::Combo(tr("shape"), &m.shape, "sine\0triangle\0square\0");
        break;
      case gpx::ModType::Offset:
        changed |= ImGui::DragFloat(tr("value"), &m.a, 0.01f); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        changed |= ImGui::DragFloat(tr("time"), &m.b, 0.01f);
        break;
      case gpx::ModType::Limit:
        changed |= ImGui::DragFloat(tr("min"), &m.a, 0.01f); ImGui::SameLine(); ImGui::SetNextItemWidth(70);
        changed |= ImGui::DragFloat(tr("max"), &m.b, 0.01f);
        break;
      case gpx::ModType::Smooth:
        changed |= ImGui::DragFloat(tr("window"), &m.a, 0.01f, 0.f, 10.f, "%.2f s");
        break;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("remove"))) remove = (int)i;
    if (changed) anim_touched(a, s.ref);
    ImGui::PopID();
  }
  if (remove >= 0) { undo_push(a, "Remove modifier"); t.modifiers.erase(t.modifiers.begin() + remove); anim_touched(a, s.ref); }
  ImGui::PopID();
}

} // namespace

void draw_panel_curve_editor(App &a) {
  if (!a.show_curve_editor) return;
  TimelineState &s = tl_state();
  gpx::Timeline &tl = scene().timeline;
  panel_float_prepare(a, "Curve editor");
  ImGui::SetNextWindowSize(ImVec2(760, 360), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Curve editor", &a.show_curve_editor)) { ImGui::End(); return; }
  panel_float_controls(a, "Curve editor");

  // resolve the shown tracks
  std::vector<Shown> shown;
  for (auto it = s.curves.begin(); it != s.curves.end();) {
    Shown sh;
    if (anim_resolve(a, *it, &sh.ref)) { shown.push_back(sh); ++it; }
    else it = s.curves.erase(it);
  }
  // toolbar
  studio::Checkbox(tr("Speed"), &s.curve_speed); ImGui::SameLine();
  studio::Checkbox(tr("Norm"), &s.curve_normalised); ImGui::SameLine();
  if (ImGui::SmallButton(tr("Fit"))) g_cv.fitted = false;
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Smooth"))) { std::vector<int> all; for (Shown &sh : shown) { all.clear(); for (int i = 0; i < (int)sh.ref.track->keys.size(); ++i) all.push_back(i); undo_push(a, "Smooth"); gpx::anim::smooth(*sh.ref.track, all); anim_touched(a, sh.ref); } }
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Flatten"))) { std::vector<int> all; for (Shown &sh : shown) { all.clear(); for (int i = 0; i < (int)sh.ref.track->keys.size(); ++i) all.push_back(i); undo_push(a, "Flatten"); gpx::anim::flatten(*sh.ref.track, all); anim_touched(a, sh.ref); } }
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Bake"))) for (Shown &sh : shown) { undo_push(a, "Bake"); gpx::anim::bake(*sh.ref.track, tl.fps); anim_touched(a, sh.ref); }
  ImGui::SameLine();
  static float simp_tol = 0.01f;
  ImGui::SetNextItemWidth(70);
  ImGui::DragFloat("##tol", &simp_tol, 0.001f, 0.0001f, 10.f, "%.3f");
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Simplify"))) for (Shown &sh : shown) { undo_push(a, "Simplify"); gpx::anim::simplify(*sh.ref.track, simp_tol, tl.fps); anim_touched(a, sh.ref); }
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Clear list"))) s.curves.clear();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", tr("drag keys and handles; Alt breaks a handle; wheel zooms value, Ctrl+wheel time"));

  // graph area
  float mods_h = 0.f;
  for (Shown &sh : shown) mods_h += ImGui::GetFrameHeightWithSpacing() * (1.f + (float)sh.ref.track->modifiers.size());
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  float w = ImGui::GetContentRegionAvail().x, h = std::max(ImGui::GetContentRegionAvail().y - mods_h - 8.f, 80.f);
  ImVec2 p1(p0.x + w, p0.y + h);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, p1, theme::shade(theme::panel_bg(), 0.8f));
  const float left = 46.f; // value ruler
  float gx0 = p0.x + left, gx1 = p1.x;
  // time axis shares the timeline's view but is re-mapped to this width
  float t_lo = s.t0, t_hi = s.t0 + (s.area_x1 - s.area_x0 > 10.f ? (s.area_x1 - s.area_x0) : w - left) / s.px_per_s;
  if (t_hi <= t_lo) t_hi = t_lo + 1.f;
  auto x_of = [&](float t) { return gx0 + (t - t_lo) / (t_hi - t_lo) * (gx1 - gx0); };
  auto t_of = [&](float x) { return t_lo + (x - gx0) / (gx1 - gx0) * (t_hi - t_lo); };
  // value range: fit once, then keep
  if (!g_cv.fitted || s.curve_normalised) {
    float lo = 1e30f, hi = -1e30f;
    for (Shown &sh : shown) {
      float slo = 1e30f, shi = -1e30f;
      for (int i = 0; i <= 200; ++i) { float v = value_of(*sh.ref.track, t_lo + (t_hi - t_lo) * i / 200.f, s.curve_speed); slo = std::min(slo, v); shi = std::max(shi, v); }
      for (const gpx::Key &k : sh.ref.track->keys) if (!s.curve_speed) { slo = std::min(slo, k.value); shi = std::max(shi, k.value); }
      if (shi - slo < 1e-6f) { slo -= 0.5f; shi += 0.5f; }
      sh.lo = slo; sh.hi = shi;
      lo = std::min(lo, slo); hi = std::max(hi, shi);
    }
    if (!s.curve_normalised && lo < hi) { float pad = (hi - lo) * 0.1f; g_cv.v0 = lo - pad; g_cv.v1 = hi + pad; g_cv.fitted = true; }
    if (s.curve_normalised) { g_cv.v0 = -0.1f; g_cv.v1 = 1.1f; }
  }
  auto norm = [&](const Shown &sh, float v) { return s.curve_normalised ? (v - sh.lo) / std::max(sh.hi - sh.lo, 1e-9f) : v; };
  auto denorm = [&](const Shown &sh, float n) { return s.curve_normalised ? sh.lo + n * (sh.hi - sh.lo) : n; };
  auto y_of = [&](float v) { return p1.y - (v - g_cv.v0) / (g_cv.v1 - g_cv.v0) * h; };
  auto v_of = [&](float y) { return g_cv.v0 + (p1.y - y) / h * (g_cv.v1 - g_cv.v0); };
  dl->PushClipRect(p0, p1, true);
  // grid + rulers
  {
    float range = g_cv.v1 - g_cv.v0;
    float step = std::pow(10.f, std::floor(std::log10(std::max(range, 1e-6f)))) ;
    if (range / step > 8.f) step *= 2.f;
    if (range / step < 3.f) step *= 0.5f;
    for (float v = std::ceil(g_cv.v0 / step) * step; v <= g_cv.v1; v += step) {
      float y = y_of(v);
      dl->AddLine(ImVec2(gx0, y), ImVec2(gx1, y), theme::fade(theme::text_dim(), std::fabs(v) < 1e-6f ? 0.6f : 0.2f));
      char b[32]; snprintf(b, sizeof b, "%.3g", v);
      dl->AddText(ImVec2(p0.x + 3.f, y - 7.f), theme::text_dim(), b);
    }
    float fstep = std::max(1.f, std::round((t_hi - t_lo) * tl.fps / 10.f));
    for (float f = std::ceil(t_lo * tl.fps / fstep) * fstep; f <= t_hi * tl.fps; f += fstep) {
      float x = x_of(tl.time_of(f));
      dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), theme::fade(theme::text_dim(), 0.2f));
      dl->AddText(ImVec2(x + 2.f, p1.y - 14.f), theme::text_dim(), tl.format(tl.time_of(f)).c_str());
    }
    // preview band, playhead
    if (tl.preview) dl->AddRectFilled(ImVec2(x_of(tl.preview_start), p0.y), ImVec2(x_of(tl.preview_end), p1.y), theme::fade(theme::accent(), 0.08f));
    float px = x_of(a.graph.time);
    dl->AddLine(ImVec2(px, p0.y), ImVec2(px, p1.y), IM_COL32(0xf0, 0xe0, 0xc0, 0xd0), 1.5f);
  }
  // curves
  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetCursorScreenPos(p0);
  ImGui::InvisibleButton("##graph", ImVec2(std::max(w, 1.f), h));
  bool hovered = ImGui::IsItemHovered();
  struct Hit { std::string track; float time; int handle; float d; } best{"", 0.f, 0, 1e30f};
  for (Shown &sh : shown) {
    gpx::Track &t = *sh.ref.track;
    ImU32 col = anim_comp_color(sh.ref.comp, sh.ref.color);
    // ghost
    if (g_cv.dragging && g_cv.ghost_track == sh.ref.id) {
      ImVec2 prev; bool first = true;
      for (int i = 0; i <= 240; ++i) { float tt = t_lo + (t_hi - t_lo) * i / 240.f; ImVec2 p(x_of(tt), y_of(norm(sh, value_of(g_cv.ghost, tt, s.curve_speed)))); if (!first) dl->AddLine(prev, p, theme::fade(col, 0.3f)); prev = p; first = false; }
    }
    ImVec2 prev; bool first = true;
    for (int i = 0; i <= 300; ++i) {
      float tt = t_lo + (t_hi - t_lo) * i / 300.f;
      ImVec2 p(x_of(tt), y_of(norm(sh, value_of(t, tt, s.curve_speed))));
      if (!first) dl->AddLine(prev, p, col, 1.5f);
      prev = p; first = false;
    }
    if (s.curve_speed) continue;
    for (const gpx::Key &k : t.keys) {
      ImVec2 c(x_of(k.time), y_of(norm(sh, k.value)));
      bool selected = tl_is_selected(s, sh.ref.id, k.time);
      dl->AddCircleFilled(c, 4.f, selected ? IM_COL32(0xff, 0xff, 0xff, 0xff) : col, 12);
      // tangent handles for bezier keys
      gpx::Interp ei = t.effective(k);
      if (ei == gpx::Interp::Bezier || ei == gpx::Interp::Smooth) {
        float hl = 0.15f * (t_hi - t_lo); // handle length in seconds
        float sc = s.curve_normalised ? 1.f / std::max(sh.hi - sh.lo, 1e-9f) : 1.f;
        ImVec2 hin(x_of(k.time - hl), y_of(norm(sh, k.value) - k.tan_in * hl * sc));
        ImVec2 hout(x_of(k.time + hl), y_of(norm(sh, k.value) + k.tan_out * hl * sc));
        ImU32 hc = theme::fade(col, 0.7f);
        dl->AddLine(c, hin, hc); dl->AddLine(c, hout, hc);
        dl->AddCircle(hin, 3.f, hc, 8); dl->AddCircle(hout, 3.f, hc, 8);
        if (hovered) {
          float din = std::hypot(io.MousePos.x - hin.x, io.MousePos.y - hin.y), dout = std::hypot(io.MousePos.x - hout.x, io.MousePos.y - hout.y);
          if (din < 7.f && din < best.d) best = {sh.ref.id, k.time, -1, din};
          if (dout < 7.f && dout < best.d) best = {sh.ref.id, k.time, 1, dout};
        }
      }
      if (hovered) { float d = std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y); if (d < 7.f && d < best.d) best = {sh.ref.id, k.time, 0, d}; }
    }
  }
  dl->PopClipRect();
  // interaction
  if (hovered && ImGui::IsMouseClicked(0) && !best.track.empty()) {
    if (best.handle == 0) { if (io.KeyCtrl) tl_select(s, best.track, best.time, true); else if (!tl_is_selected(s, best.track, best.time)) tl_select(s, best.track, best.time, false); }
    g_cv.dragging = true; g_cv.drag_started = false; g_cv.drag_track = best.track; g_cv.drag_time = best.time; g_cv.drag_handle = best.handle;
    if (gpx::Track *gt = anim_resolve(a, best.track)) { g_cv.ghost = *gt; g_cv.ghost_track = best.track; }
  } else if (hovered && ImGui::IsMouseClicked(0) && !io.KeyCtrl) s.sel.clear();
  if (g_cv.dragging) {
    TrackRef ref;
    gpx::Track *t = anim_resolve(a, g_cv.drag_track, &ref);
    if (!ImGui::IsMouseDown(0) || !t) { g_cv.dragging = false; }
    else if (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f) {
      if (!g_cv.drag_started) { undo_push(a, g_cv.drag_handle ? "Tangent" : "Move key"); g_cv.drag_started = true; }
      int i = t->index_at(g_cv.drag_time);
      if (i >= 0) {
        Shown *sh = nullptr; for (Shown &x : shown) if (x.ref.id == g_cv.drag_track) sh = &x;
        gpx::Key &k = t->keys[(size_t)i];
        if (g_cv.drag_handle == 0) {
          float nt = tl.snap_time(t_of(io.MousePos.x));
          float nv = sh ? denorm(*sh, v_of(io.MousePos.y)) : v_of(io.MousePos.y);
          std::vector<int> idx{i};
          gpx::anim::move_keys(*t, idx, nt - k.time, nv - k.value);
          if (!idx.empty()) { g_cv.drag_time = t->keys[(size_t)idx[0]].time; s.sel.clear(); s.sel.push_back({g_cv.drag_track, g_cv.drag_time}); }
        } else {
          float sc = (sh && s.curve_normalised) ? std::max(sh->hi - sh->lo, 1e-9f) : 1.f;
          float dt = t_of(io.MousePos.x) - k.time;
          float dv = (sh ? denorm(*sh, v_of(io.MousePos.y)) : v_of(io.MousePos.y)) - k.value;
          (void)sc;
          float slope = std::fabs(dt) > 1e-5f ? dv / dt : 0.f;
          if (g_cv.drag_handle < 0 && dt > 0) slope = k.tan_in; // handle dragged past the key: keep
          if (g_cv.drag_handle > 0 && dt < 0) slope = k.tan_out;
          k.interp = gpx::Interp::Bezier;
          if (io.KeyAlt || k.tangent == gpx::TangentMode::Broken) { k.tangent = gpx::TangentMode::Broken; (g_cv.drag_handle < 0 ? k.tan_in : k.tan_out) = slope; }
          else { k.tangent = gpx::TangentMode::User; k.tan_in = k.tan_out = slope; }
          t->update_tangents();
        }
        anim_touched(a, ref);
      }
    }
  }
  if (hovered && io.MouseWheel != 0.f) {
    if (io.KeyCtrl) { float before = t_of(io.MousePos.x); s.px_per_s = std::clamp(s.px_per_s * (io.MouseWheel > 0 ? 1.15f : 1.f / 1.15f), 2.f, 4000.f); float after_hi = s.t0 + (s.area_x1 - s.area_x0 > 10.f ? (s.area_x1 - s.area_x0) : w - left) / s.px_per_s; (void)after_hi; s.t0 += before - t_of(io.MousePos.x); }
    else { float c = v_of(io.MousePos.y); float f = io.MouseWheel > 0 ? 1.f / 1.15f : 1.15f; g_cv.v0 = c + (g_cv.v0 - c) * f; g_cv.v1 = c + (g_cv.v1 - c) * f; g_cv.fitted = true; }
  }
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) { s.t0 -= io.MouseDelta.x / (gx1 - gx0) * (t_hi - t_lo); float dv = io.MouseDelta.y / h * (g_cv.v1 - g_cv.v0); g_cv.v0 += dv; g_cv.v1 += dv; }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##curvemenu");
  if (ImGui::BeginPopup("##curvemenu")) {
    TrackRef ref; const TrackRef *rp = nullptr;
    if (!best.track.empty() && anim_resolve(a, best.track, &ref)) rp = &ref;
    else if (shown.size() == 1) rp = &shown[0].ref;
    tl_key_context_menu(a, rp, tl.snap_time(t_of(io.MousePos.x)));
    ImGui::EndPopup();
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) tl_selection_delete(a);
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) tl_selection_ease(a, gpx::anim::Ease::Easy);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) g_cv.fitted = false;
  }
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 4.f));
  if (shown.empty()) ImGui::TextDisabled("%s", tr("Choose tracks in the Timeline (the > at the right of a row, or double-click it)."));
  for (Shown &sh : shown) draw_modifiers(a, sh);
  ImGui::End();
}

} // namespace studio
