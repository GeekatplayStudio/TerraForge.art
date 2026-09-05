// Geekatplay TerraForge - keys in the Timeline: how they are drawn, picked,
// selected (click, Ctrl-click, box), moved (drag, keys merge when they
// land on each other), retimed (the handles at either end of a
// selection), copied, pasted, deleted, eased and mirrored. Every edit is
// one undo step per gesture.
#include "anim_widgets.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "timeline_internal.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <map>

namespace studio {

namespace {

const float KEY_R = 4.5f;

void glyph(ImDrawList *dl, ImVec2 c, gpx::Interp i, ImU32 col, bool selected, bool hollow) {
  ImU32 fill = selected ? IM_COL32(0xff, 0xff, 0xff, 0xff) : col;
  switch (i) {
    case gpx::Interp::Constant:
      if (hollow) dl->AddRect(ImVec2(c.x - KEY_R + 1, c.y - KEY_R + 1), ImVec2(c.x + KEY_R - 1, c.y + KEY_R - 1), fill);
      else dl->AddRectFilled(ImVec2(c.x - KEY_R + 1, c.y - KEY_R + 1), ImVec2(c.x + KEY_R - 1, c.y + KEY_R - 1), fill);
      break;
    case gpx::Interp::Linear:
      if (hollow) dl->AddCircle(c, KEY_R - 0.5f, fill, 12);
      else dl->AddCircleFilled(c, KEY_R - 0.5f, fill, 12);
      break;
    default: {
      ImVec2 p[4] = {{c.x, c.y - KEY_R}, {c.x + KEY_R, c.y}, {c.x, c.y + KEY_R}, {c.x - KEY_R, c.y}};
      if (hollow) dl->AddPolyline(p, 4, fill, ImDrawFlags_Closed, 1.f);
      else dl->AddConvexPolyFilled(p, 4, fill);
    }
  }
  if (selected) {
    ImVec2 p[4] = {{c.x, c.y - KEY_R - 1}, {c.x + KEY_R + 1, c.y}, {c.x, c.y + KEY_R + 1}, {c.x - KEY_R - 1, c.y}};
    dl->AddPolyline(p, 4, col, ImDrawFlags_Closed, 1.f);
  }
}

// selection bounds in time
bool sel_bounds(const TimelineState &s, float &lo, float &hi) {
  if (s.sel.empty()) return false;
  lo = 1e30f; hi = -1e30f;
  for (const KeySel &k : s.sel) { lo = std::min(lo, k.time); hi = std::max(hi, k.time); }
  return true;
}

// group the selection by track and resolve to (track, indices)
struct SelGroup { std::string id; gpx::Track *track; std::vector<int> idx; TrackRef ref; };
std::vector<SelGroup> groups(App &a) {
  TimelineState &s = tl_state();
  std::map<std::string, SelGroup> g;
  for (const KeySel &k : s.sel) {
    SelGroup &sg = g[k.track];
    if (!sg.track) { sg.id = k.track; sg.track = anim_resolve(a, k.track, &sg.ref); }
    if (!sg.track) continue;
    int i = sg.track->index_at(k.time);
    if (i >= 0) sg.idx.push_back(i);
  }
  std::vector<SelGroup> out;
  for (auto &kv : g) if (kv.second.track && !kv.second.idx.empty()) out.push_back(kv.second);
  return out;
}

// rebuild the selection from (track, indices) after an edit moved keys
void reselect(App &a, const std::vector<SelGroup> &gs) {
  TimelineState &s = tl_state();
  s.sel.clear();
  for (const SelGroup &g : gs) {
    gpx::Track *t = anim_resolve(a, g.id);
    if (!t) continue;
    for (int i : g.idx) if (i >= 0 && i < (int)t->keys.size()) s.sel.push_back({g.id, t->keys[(size_t)i].time});
  }
}

void apply_shift(App &a, float dt) {
  TimelineState &s = tl_state();
  std::vector<SelGroup> gs = groups(a);
  for (SelGroup &g : gs) { gpx::anim::move_keys(*g.track, g.idx, dt, 0.f); anim_touched(a, g.ref); }
  reselect(a, gs);
  (void)s;
}

} // namespace

void tl_draw_key_row(App &a, const TrackRef &r, ImVec2 p0, float width) {
  TimelineState &s = tl_state();
  gpx::Timeline &tl = scene().timeline;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float h = s.row_h;
  ImVec2 p1(p0.x + width, p0.y + h);
  ImGui::SetCursorScreenPos(p0);
  ImGui::PushID(r.id.c_str());
  ImGui::InvisibleButton("##row", ImVec2(std::max(width, 1.f), h));
  bool hovered = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();
  dl->PushClipRect(ImVec2(s.area_x0, p0.y), ImVec2(s.area_x1, p1.y), true);
  // the keyed span, faintly
  if (r.track && r.track->keys.size() > 1) {
    float xa = tl_x_of(s, r.track->first_time()), xb = tl_x_of(s, r.track->last_time());
    dl->AddRectFilled(ImVec2(xa, p0.y + h * 0.5f - 1.5f), ImVec2(xb, p0.y + h * 0.5f + 1.5f), theme::fade(theme::text_dim(), 0.35f));
  }
  ImU32 col = anim_comp_color(r.comp, r.color);
  float cy = p0.y + h * 0.5f;
  int hit = -1;
  if (r.track) {
    for (size_t i = 0; i < r.track->keys.size(); ++i) {
      const gpx::Key &k = r.track->keys[i];
      float x = tl_x_of(s, k.time);
      if (x < s.area_x0 - KEY_R || x > s.area_x1 + KEY_R) continue;
      bool selected = tl_is_selected(s, r.id, k.time);
      bool off_frame = std::fabs(k.time * tl.fps - std::round(k.time * tl.fps)) > 1e-3f;
      glyph(dl, ImVec2(x, cy), r.track->effective(k), col, selected, off_frame);
      if (hovered && std::fabs(io.MousePos.x - x) <= KEY_R + 2.f) hit = (int)i;
    }
    if (!r.track->expr.empty()) dl->AddText(ImVec2(s.area_x0 + 4.f, p0.y + 2.f), theme::fade(theme::accent(), 0.7f), ("= " + r.track->expr).c_str());
    if (!r.track->modifiers.empty()) dl->AddText(ImVec2(s.area_x1 - 60.f, p0.y + 2.f), theme::fade(theme::text_dim(), 0.8f), tr("modifiers"));
  }
  // retime handles at the ends of a multi-key selection on this row
  float lo, hi;
  bool has_sel_here = false;
  for (const KeySel &k : s.sel) if (k.track == r.id) { has_sel_here = true; break; }
  if (has_sel_here && sel_bounds(s, lo, hi) && hi > lo + 1e-4f) {
    float xl = tl_x_of(s, lo) - 9.f, xr = tl_x_of(s, hi) + 9.f;
    dl->AddRectFilled(ImVec2(xl - 2, cy - 6), ImVec2(xl + 2, cy + 6), theme::text());
    dl->AddRectFilled(ImVec2(xr - 2, cy - 6), ImVec2(xr + 2, cy + 6), theme::text());
    if (hovered && ImGui::IsMouseClicked(0) && std::fabs(io.MousePos.x - xl) < 5.f) { s.retime_handle = -1; s.retime_pivot = hi; s.retime_ref = lo; }
    if (hovered && ImGui::IsMouseClicked(0) && std::fabs(io.MousePos.x - xr) < 5.f) { s.retime_handle = 1; s.retime_pivot = lo; s.retime_ref = hi; }
  }
  dl->PopClipRect();
  ImGui::PopID();

  // hover feedback
  if (hit >= 0 && r.track) {
    const gpx::Key &k = r.track->keys[(size_t)hit];
    ImGui::SetTooltip("%s %s\n%s %.4g", tl.format(k.time).c_str(), r.label.c_str(), tr("value"), k.value);
  }
  // mouse down: select / start drag / start box
  if (hovered && ImGui::IsMouseClicked(0) && s.retime_handle == 0) {
    if (hit >= 0 && r.track) {
      float kt = r.track->keys[(size_t)hit].time;
      if (io.KeyCtrl) { if (tl_is_selected(s, r.id, kt)) tl_deselect(s, r.id, kt); else tl_select(s, r.id, kt, true); }
      else if (!tl_is_selected(s, r.id, kt)) tl_select(s, r.id, kt, false);
      s.dragging = true;
      s.drag_started = false;
      s.drag_t0 = tl_t_of(s, io.MousePos.x);
      s.drag_last_dt = 0.f;
    } else {
      if (!io.KeyCtrl) s.sel.clear();
      s.boxing = true;
      s.box0 = s.box1 = io.MousePos;
    }
  }
  if (hovered && ImGui::IsMouseDoubleClicked(0) && hit < 0) {
    // double-click on empty: add a key here at the property's current value
    float v;
    float t = tl.snap_time(tl_t_of(s, io.MousePos.x));
    if (anim_current_value(a, r, v)) {
      undo_push(a, "Add key");
      gpx::Track *t_ = r.track;
      if (!t_) {
        // create the track through the owner's map
        if (r.kind == TrackRef::Object && r.prop) t_ = &anim_get(scene().objects[(size_t)r.object].anim, *r.prop, std::max(r.comp, 0));
        else if (r.kind == TrackRef::World && r.prop) t_ = &anim_get(scene().world_anim, *r.prop, std::max(r.comp, 0));
        else t_ = anim_resolve(a, r.id);
      }
      if (t_) { t_->set_key(t, v); anim_touched(a, r); }
    }
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    if (hit >= 0 && r.track && !tl_is_selected(s, r.id, r.track->keys[(size_t)hit].time)) tl_select(s, r.id, r.track->keys[(size_t)hit].time, false);
    ImGui::OpenPopup("##keymenu");
  }
  if (ImGui::BeginPopup("##keymenu")) {
    tl_key_context_menu(a, &r, tl.snap_time(tl_t_of(s, io.MousePos.x)));
    ImGui::EndPopup();
  }
}

void tl_draw_summary_row(App &a, const std::vector<const TrackRef *> &children, ImVec2 p0, float width) {
  (void)a;
  TimelineState &s = tl_state();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float cy = p0.y + s.row_h * 0.5f;
  dl->PushClipRect(ImVec2(s.area_x0, p0.y), ImVec2(s.area_x1, p0.y + s.row_h), true);
  std::vector<float> times;
  for (const TrackRef *r : children) if (r->track) for (const gpx::Key &k : r->track->keys) times.push_back(k.time);
  std::sort(times.begin(), times.end());
  times.erase(std::unique(times.begin(), times.end(), [](float a_, float b_) { return std::fabs(a_ - b_) < 1e-4f; }), times.end());
  for (float t : times) {
    float x = tl_x_of(s, t);
    if (x < s.area_x0 - KEY_R || x > s.area_x1 + KEY_R) continue;
    ImVec2 p[4] = {{x, cy - 3.5f}, {x + 3.5f, cy}, {x, cy + 3.5f}, {x - 3.5f, cy}};
    dl->AddConvexPolyFilled(p, 4, theme::fade(theme::text_dim(), 0.9f));
  }
  dl->PopClipRect();
  ImGui::Dummy(ImVec2(std::max(width, 1.f), s.row_h));
}

void tl_finish_interaction(App &a) {
  TimelineState &s = tl_state();
  gpx::Timeline &tl = scene().timeline;
  ImGuiIO &io = ImGui::GetIO();
  // move drag
  if (s.dragging) {
    if (ImGui::IsMouseDown(0)) {
      float dt = tl_t_of(s, io.MousePos.x) - s.drag_t0;
      if (tl.snap) dt = std::round(dt * tl.fps) / tl.fps;
      if (std::fabs(dt - s.drag_last_dt) > 1e-6f) {
        if (!s.drag_started) { undo_push(a, "Move keys"); s.drag_started = true; }
        apply_shift(a, dt - s.drag_last_dt);
        s.drag_last_dt = dt;
      }
    } else s.dragging = false;
  }
  // retime drag
  if (s.retime_handle != 0) {
    if (ImGui::IsMouseDown(0)) {
      float target = tl_t_of(s, io.MousePos.x) - (float)s.retime_handle * 9.f / s.px_per_s;
      if (tl.snap) target = tl.snap_time(target);
      float span_old = s.retime_ref - s.retime_pivot, span_new = target - s.retime_pivot;
      if (std::fabs(span_old) > 1e-5f && std::fabs(span_new) > 1e-5f && (span_old > 0) == (span_new > 0)) {
        float factor = span_new / span_old;
        if (std::fabs(factor - 1.f) > 1e-6f) {
          if (!s.drag_started) { undo_push(a, "Retime keys"); s.drag_started = true; }
          std::vector<SelGroup> gs = groups(a);
          for (SelGroup &g : gs) { gpx::anim::retime(*g.track, g.idx, s.retime_pivot, factor); anim_touched(a, g.ref); }
          reselect(a, gs);
          s.retime_ref = target;
        }
      }
    } else { s.retime_handle = 0; s.drag_started = false; }
  }
  // box select
  if (s.boxing) {
    s.box1 = io.MousePos;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 a0(std::min(s.box0.x, s.box1.x), std::min(s.box0.y, s.box1.y)), a1(std::max(s.box0.x, s.box1.x), std::max(s.box0.y, s.box1.y));
    dl->AddRectFilled(a0, a1, theme::fade(theme::accent(), 0.15f));
    dl->AddRect(a0, a1, theme::accent());
    if (!ImGui::IsMouseDown(0)) {
      s.boxing = false;
      // rows are listed in order with their screen y unknown here; use the
      // key column geometry: every row is row_h tall below the ruler, in
      // s.rows order — recorded by the table walk
      float t_lo = tl_t_of(s, a0.x), t_hi = tl_t_of(s, a1.x);
      for (const TrackRef &r : s.rows) {
        if (!r.track) continue;
        for (const gpx::Key &k : r.track->keys)
          if (k.time >= t_lo && k.time <= t_hi) {
            float x = tl_x_of(s, k.time);
            // y is checked against the row's recorded rect via the ImGui item
            // rects is not available after the fact; accept by time when the
            // box spans any part of the column
            (void)x;
            tl_select(s, r.id, k.time, true);
          }
      }
    }
  }
  // keyboard
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) tl_selection_delete(a);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) tl_selection_copy(a);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) tl_selection_paste(a, a.graph.time);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) { tl_selection_copy(a); tl_selection_paste(a, a.graph.time); }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) s.sel.clear();
  }
  // middle-drag pans the key column
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.f)) {
    s.t0 -= io.MouseDelta.x / s.px_per_s;
  }
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.KeyCtrl && io.MouseWheel != 0.f && io.MousePos.x >= s.area_x0) {
    float before = tl_t_of(s, io.MousePos.x);
    s.px_per_s = std::clamp(s.px_per_s * (io.MouseWheel > 0 ? 1.15f : 1.f / 1.15f), 2.f, 4000.f);
    s.t0 += before - tl_t_of(s, io.MousePos.x);
  }
}

void tl_key_context_menu(App &a, const TrackRef *r, float t_under) {
  TimelineState &s = tl_state();
  bool any = !s.sel.empty();
  if (ImGui::MenuItem(tr("Add key here"), nullptr, false, r != nullptr)) {
    float v;
    if (r && anim_current_value(a, *r, v)) {
      undo_push(a, "Add key");
      gpx::Track *t_ = r->track;
      if (!t_) {
        if (r->kind == TrackRef::Object && r->prop) t_ = &anim_get(scene().objects[(size_t)r->object].anim, *r->prop, std::max(r->comp, 0));
        else if (r->kind == TrackRef::World && r->prop) t_ = &anim_get(scene().world_anim, *r->prop, std::max(r->comp, 0));
        else t_ = anim_resolve(a, r->id);
      }
      if (t_) { t_->set_key(t_under, v); anim_touched(a, *r); }
    }
  }
  if (ImGui::MenuItem(tr("Delete"), "Del", false, any)) tl_selection_delete(a);
  if (ImGui::MenuItem(tr("Copy"), "Ctrl+C", false, any)) tl_selection_copy(a);
  if (ImGui::MenuItem(tr("Paste at frame"), "Ctrl+V", false, !s.clipboard.empty())) tl_selection_paste(a, a.graph.time);
  if (ImGui::MenuItem(tr("Duplicate at frame"), "Ctrl+D", false, any)) { tl_selection_copy(a); tl_selection_paste(a, a.graph.time); }
  ImGui::Separator();
  if (ImGui::BeginMenu(tr("Interpolation"), any)) {
    if (ImGui::MenuItem(tr("Smooth (auto tangents)"))) { tl_selection_interp(a, gpx::Interp::Bezier); }
    if (ImGui::MenuItem(tr("Linear"))) tl_selection_ease(a, gpx::anim::Ease::Linear);
    if (ImGui::MenuItem(tr("Hold (step)"))) tl_selection_ease(a, gpx::anim::Ease::Hold);
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Easy ease"), "F9")) tl_selection_ease(a, gpx::anim::Ease::Easy);
    if (ImGui::MenuItem(tr("Ease in"))) tl_selection_ease(a, gpx::anim::Ease::In);
    if (ImGui::MenuItem(tr("Ease out"))) tl_selection_ease(a, gpx::anim::Ease::Out);
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem(tr("Mirror in time"), nullptr, false, any)) tl_selection_mirror(a);
  if (ImGui::MenuItem(tr("Snap to frames"), nullptr, false, any)) tl_selection_snap(a);
  if (r && r->track) {
    ImGui::Separator();
    const char *modes[] = {"Constant", "Linear", "Cycle", "Cycle with offset", "Ping-pong"};
    if (ImGui::BeginMenu(tr("Before first key"))) {
      for (int i = 0; i < 5; ++i) if (ImGui::MenuItem(tr(modes[i]), nullptr, (int)r->track->pre == i)) { undo_push(a, "Extrapolation"); r->track->pre = (gpx::Extrapolate)i; anim_touched(a, *r); }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("After last key"))) {
      for (int i = 0; i < 5; ++i) if (ImGui::MenuItem(tr(modes[i]), nullptr, (int)r->track->post == i)) { undo_push(a, "Extrapolation"); r->track->post = (gpx::Extrapolate)i; anim_touched(a, *r); }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Add modifier"))) {
      const char *names[] = {"Noise", "Oscillator", "Offset", "Limit", "Smooth"};
      for (int i = 0; i < 5; ++i) if (ImGui::MenuItem(tr(names[i]))) {
        undo_push(a, "Add modifier");
        gpx::Modifier m; m.type = (gpx::ModType)i;
        if (m.type == gpx::ModType::Noise) { m.a = 0.1f; m.b = 1.f; m.octaves = 2; }
        if (m.type == gpx::ModType::Oscillator) { m.a = 0.1f; m.b = 1.f; }
        if (m.type == gpx::ModType::Offset) { m.a = 0.f; m.b = 0.f; }
        if (m.type == gpx::ModType::Limit) { m.a = 0.f; m.b = 1.f; }
        if (m.type == gpx::ModType::Smooth) { m.a = 0.25f; }
        r->track->modifiers.push_back(m); anim_touched(a, *r);
        if (std::find(s.curves.begin(), s.curves.end(), r->id) == s.curves.end()) s.curves.push_back(r->id);
        a.show_curve_editor = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem(tr("Show curve"))) { if (std::find(s.curves.begin(), s.curves.end(), r->id) == s.curves.end()) s.curves.push_back(r->id); a.show_curve_editor = true; }
    if (ImGui::MenuItem(tr("Bake to frames"))) { undo_push(a, "Bake"); gpx::anim::bake(*r->track, scene().timeline.fps); anim_touched(a, *r); }
    if (ImGui::MenuItem(tr("Remove animation"))) { undo_push(a, "Remove animation"); r->track->clear(); anim_touched(a, *r); s.sel.clear(); }
  }
}

void tl_selection_ease(App &a, gpx::anim::Ease e) {
  std::vector<SelGroup> gs = groups(a);
  if (gs.empty()) return;
  undo_push(a, "Ease");
  for (SelGroup &g : gs) { gpx::anim::set_ease(*g.track, g.idx, e); anim_touched(a, g.ref); }
}
void tl_selection_interp(App &a, gpx::Interp i) {
  std::vector<SelGroup> gs = groups(a);
  if (gs.empty()) return;
  undo_push(a, "Interpolation");
  for (SelGroup &g : gs) { gpx::anim::set_interp(*g.track, g.idx, i); gpx::anim::set_tangent_mode(*g.track, g.idx, gpx::TangentMode::Auto); anim_touched(a, g.ref); }
}
void tl_selection_delete(App &a) {
  std::vector<SelGroup> gs = groups(a);
  if (gs.empty()) return;
  undo_push(a, "Delete keys");
  for (SelGroup &g : gs) {
    std::vector<float> times;
    for (int i : g.idx) times.push_back(g.track->keys[(size_t)i].time);
    for (float t : times) g.track->remove_key(t);
    anim_touched(a, g.ref);
  }
  tl_state().sel.clear();
}
void tl_selection_copy(App &a) {
  TimelineState &s = tl_state();
  s.clipboard.clear();
  float lo = 1e30f;
  for (const KeySel &k : s.sel) lo = std::min(lo, k.time);
  for (SelGroup &g : groups(a)) {
    TimelineState::ClipTrack ct;
    ct.track = g.id;
    for (int i : g.idx) { gpx::Key k = g.track->keys[(size_t)i]; k.time -= lo; ct.keys.push_back(k); }
    s.clipboard.push_back(ct);
  }
}
void tl_selection_paste(App &a, float at) {
  TimelineState &s = tl_state();
  if (s.clipboard.empty()) return;
  undo_push(a, "Paste keys");
  s.sel.clear();
  for (const TimelineState::ClipTrack &ct : s.clipboard) {
    TrackRef ref;
    gpx::Track *t = anim_resolve(a, ct.track, &ref);
    if (!t) continue;
    gpx::anim::paste_keys(*t, ct.keys, at);
    for (const gpx::Key &k : ct.keys) s.sel.push_back({ct.track, k.time + at});
    anim_touched(a, ref);
  }
}
void tl_selection_mirror(App &a) {
  std::vector<SelGroup> gs = groups(a);
  if (gs.empty()) return;
  undo_push(a, "Mirror keys");
  for (SelGroup &g : gs) { gpx::anim::mirror(*g.track, g.idx); anim_touched(a, g.ref); }
  reselect(a, gs);
}
void tl_selection_snap(App &a) {
  std::vector<SelGroup> gs = groups(a);
  if (gs.empty()) return;
  undo_push(a, "Snap keys");
  for (SelGroup &g : gs) { gpx::anim::snap_to_frames(*g.track, scene().timeline.fps); anim_touched(a, g.ref); }
  tl_state().sel.clear();
}
void tl_fit_view(App &a, bool selection_only) {
  TimelineState &s = tl_state();
  float lo = 1e30f, hi = -1e30f;
  if (selection_only) { for (const KeySel &k : s.sel) { lo = std::min(lo, k.time); hi = std::max(hi, k.time); } }
  else {
    for (const TrackRef &r : anim_collect(a, true)) if (r.track && !r.track->keys.empty()) { lo = std::min(lo, r.track->first_time()); hi = std::max(hi, r.track->last_time()); }
    gpx::Timeline &tl = scene().timeline;
    lo = std::min(lo, tl.start); hi = std::max(hi, tl.end);
  }
  if (lo > hi) return;
  float w = std::max(s.area_x1 - s.area_x0, 50.f);
  float span = std::max(hi - lo, 0.5f);
  s.px_per_s = std::clamp(w / (span * 1.1f), 2.f, 4000.f);
  s.t0 = lo - span * 0.05f;
}

} // namespace studio
