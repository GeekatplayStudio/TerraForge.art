// Geekatplay TerraForge - the animation circle and navigator. See the header.
#include "anim_widgets.hpp"
#include "anim_tracks.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "render_settings.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <imgui_internal.h>

namespace studio {

namespace {

enum class Circle { Static, Animated, Keyed };

// One circle; returns 0 none, 1 add, 2 remove, 3 remove track.
int circle_widget(const char *id, Circle state, bool *right_clicked) {
  ImGuiIO &io = ImGui::GetIO();
  float h = ImGui::GetFrameHeight();
  float d = h * 0.42f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(d + 6.f, h));
  bool hovered = ImGui::IsItemHovered();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 c(p.x + 3.f + d * 0.5f, p.y + h * 0.5f);
  ImU32 col = state == Circle::Static ? theme::text_dim() : theme::accent();
  if (hovered) col = theme::text();
  if (state == Circle::Keyed) dl->AddCircleFilled(c, d * 0.5f, col, 16);
  else dl->AddCircle(c, d * 0.5f - 0.5f, col, 16, state == Circle::Animated ? 1.6f : 1.f);
  if (hovered) {
    ImGui::SetTooltip("%s", state == Circle::Keyed ? tr("Key on this frame - click to remove")
                            : state == Circle::Animated ? tr("Animated - click to key this frame")
                                                        : tr("Click to animate: key this value here"));
  }
  if (right_clicked) *right_clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  if (!ImGui::IsItemClicked(ImGuiMouseButton_Left)) return 0;
  if (io.KeyCtrl && io.KeyShift) return 3;
  if (io.KeyCtrl) return 1;
  if (io.KeyShift) return 2;
  return state == Circle::Keyed ? 2 : 1;
}

void prop_menu(App &a, std::map<std::string, gpx::Track> &m, const AnimProp &p, bool &changed,
               const std::function<bool(int)> &record) {
  float t = a.graph.time;
  bool keyed = anim_prop_keyed_at(m, p, t);
  bool animated = anim_prop_animated(m, p);
  if (ImGui::MenuItem(keyed ? tr("Remove key") : tr("Add key"))) {
    undo_push(a, keyed ? "Remove key" : "Add key");
    if (keyed) anim_unkey(m, p, -1, t); else record(-1);
    changed = true;
  }
  if (animated && ImGui::MenuItem(tr("Remove animation"))) {
    undo_push(a, "Remove animation");
    anim_remove_track(m, p);
    changed = true;
  }
  if (animated) {
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Show in timeline"))) a.show_timeline = true;
    if (ImGui::MenuItem(tr("Show curve"))) { a.show_timeline = true; a.show_curve_editor = true; }
    const char *modes[] = {"Constant", "Linear", "Cycle", "Cycle with offset", "Ping-pong"};
    if (ImGui::BeginMenu(tr("After last key"))) {
      for (int i = 0; i < 5; ++i)
        if (ImGui::MenuItem(tr(modes[i]))) {
          undo_push(a, "Extrapolation");
          for (int c = 0; c < p.comps; ++c) if (gpx::Track *tr_ = anim_find(m, p, c)) tr_->post = (gpx::Extrapolate)i;
          changed = true;
        }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Interpolation"))) {
      struct { const char *n; gpx::anim::Ease e; } eases[] = {{"Smooth (auto)", gpx::anim::Ease::InOut}, {"Linear", gpx::anim::Ease::Linear}, {"Hold", gpx::anim::Ease::Hold}, {"Easy ease", gpx::anim::Ease::Easy}, {"Ease in", gpx::anim::Ease::In}, {"Ease out", gpx::anim::Ease::Out}};
      for (auto &e : eases)
        if (ImGui::MenuItem(tr(e.n))) {
          undo_push(a, "Ease");
          for (int c = 0; c < p.comps; ++c)
            if (gpx::Track *tr_ = anim_find(m, p, c)) {
              std::vector<int> all; for (int i = 0; i < (int)tr_->keys.size(); ++i) all.push_back(i);
              if (e.e == gpx::anim::Ease::InOut) { gpx::anim::set_interp(*tr_, all, gpx::Interp::Bezier); gpx::anim::set_tangent_mode(*tr_, all, gpx::TangentMode::Auto); }
              else gpx::anim::set_ease(*tr_, all, e.e);
            }
          changed = true;
        }
      ImGui::EndMenu();
    }
  }
}

} // namespace

bool anim_circle(App &a, SceneObject &o, const AnimProp &p, int comp) {
  float t = a.graph.time;
  Circle st = anim_prop_keyed_at(o.anim, p, t) ? Circle::Keyed
              : anim_prop_animated(o.anim, p) ? Circle::Animated : Circle::Static;
  ImGui::PushID(p.path);
  ImGui::PushID(comp);
  bool right = false;
  int act = circle_widget("##circle", st, &right);
  bool changed = false;
  if (act == 1) { undo_push(a, "Add key"); anim_record(o, p, comp, t); changed = true; }
  else if (act == 2) { undo_push(a, "Remove key"); anim_unkey(o.anim, p, comp, t); changed = true; }
  else if (act == 3) { undo_push(a, "Remove animation"); anim_remove_track(o.anim, p); changed = true; }
  if (right) ImGui::OpenPopup("##animmenu");
  if (ImGui::BeginPopup("##animmenu")) {
    prop_menu(a, o.anim, p, changed, [&](int c) { return anim_record(o, p, c, t); });
    ImGui::EndPopup();
  }
  ImGui::PopID();
  ImGui::PopID();
  ImGui::SameLine(0.f, 0.f);
  if (changed) a.scene_selection_serial++;
  return changed;
}

bool anim_circle_world(App &a, const AnimProp &p, int comp) {
  float t = a.graph.time;
  std::map<std::string, gpx::Track> &m = scene().world_anim;
  Circle st = anim_prop_keyed_at(m, p, t) ? Circle::Keyed
              : anim_prop_animated(m, p) ? Circle::Animated : Circle::Static;
  ImGui::PushID(p.path);
  ImGui::PushID(comp);
  bool right = false;
  int act = circle_widget("##circle", st, &right);
  bool changed = false;
  RenderSettings &rs = render_settings();
  if (act == 1) { undo_push(a, "Add key"); anim_record_world(rs, p, comp, t); changed = true; }
  else if (act == 2) { undo_push(a, "Remove key"); anim_unkey(m, p, comp, t); changed = true; }
  else if (act == 3) { undo_push(a, "Remove animation"); anim_remove_track(m, p); changed = true; }
  if (right) ImGui::OpenPopup("##animmenu");
  if (ImGui::BeginPopup("##animmenu")) {
    prop_menu(a, m, p, changed, [&](int c) { return anim_record_world(rs, p, c, t); });
    ImGui::EndPopup();
  }
  ImGui::PopID();
  ImGui::PopID();
  ImGui::SameLine(0.f, 0.f);
  return changed;
}

bool anim_circle_node(App &a, gpx::Node &n, gpx::Attribute &at, int comp) {
  float t = a.graph.time;
  gpx::Track &tr_ = comp < 0 ? at.anim : at.anim_comp(comp);
  Circle st = tr_.has_key_at(t) ? Circle::Keyed : tr_.animated() ? Circle::Animated : Circle::Static;
  ImGui::PushID(at.key.c_str());
  ImGui::PushID(comp);
  bool right = false;
  int act = circle_widget("##circle", st, &right);
  bool changed = false;
  auto value = [&]() -> float {
    switch (at.type) {
      case gpx::AttrType::Float: return at.f;
      case gpx::AttrType::Int: case gpx::AttrType::Choice: return (float)at.i;
      case gpx::AttrType::Bool: return at.b ? 1.f : 0.f;
      case gpx::AttrType::Seed: return (float)at.seed;
      case gpx::AttrType::Vec2: case gpx::AttrType::Range: return at.v2[std::clamp(comp, 0, 1)];
      case gpx::AttrType::Color: return at.col[std::clamp(comp, 0, 3)];
      default: return 0.f;
    }
  };
  if (act == 1) { undo_push(a, "Add key"); tr_.set_key(t, value()); changed = true; }
  else if (act == 2) { undo_push(a, "Remove key"); tr_.remove_key(t); changed = true; }
  else if (act == 3) { undo_push(a, "Remove animation"); tr_.clear(); changed = true; }
  if (right) ImGui::OpenPopup("##animmenu");
  if (ImGui::BeginPopup("##animmenu")) {
    if (ImGui::MenuItem(tr_.has_key_at(t) ? tr("Remove key") : tr("Add key"))) {
      undo_push(a, "Key");
      if (tr_.has_key_at(t)) tr_.remove_key(t); else tr_.set_key(t, value());
      changed = true;
    }
    if (tr_.animated() && ImGui::MenuItem(tr("Remove animation"))) { undo_push(a, "Remove animation"); tr_.clear(); changed = true; }
    if (tr_.animated() && ImGui::MenuItem(tr("Show in timeline"))) a.show_timeline = true;
    ImGui::EndPopup();
  }
  ImGui::PopID();
  ImGui::PopID();
  ImGui::SameLine(0.f, 0.f);
  if (changed) { n.dirty = true; a.request_eval(); }
  return changed;
}

void anim_autokey(App &a, SceneObject &o, const AnimProp &p, int comp) {
  if (!scene().timeline.autokey || !anim_prop_animated(o.anim, p)) return;
  anim_record(o, p, comp, a.graph.time);
}
void anim_autokey_world(App &a, const AnimProp &p, int comp) {
  if (!scene().timeline.autokey || !anim_prop_animated(scene().world_anim, p)) return;
  anim_record_world(render_settings(), p, comp, a.graph.time);
}

void anim_set_time(App &a, float t) {
  a.graph.time = t;
  a.request_eval();
  a.scene_selection_serial++;
}

static bool neighbour_key(App &a, float from, bool next, float &out) {
  bool found = false;
  for (const TrackRef &r : anim_collect(a, true)) {
    if (!r.track) continue;
    for (const gpx::Key &k : r.track->keys) {
      if (next ? k.time > from + 1e-4f : k.time < from - 1e-4f) {
        if (!found || (next ? k.time < out : k.time > out)) { out = k.time; found = true; }
      }
    }
  }
  return found;
}
bool anim_prev_key_time(App &a, float from, float &out) { return neighbour_key(a, from, false, out); }
bool anim_next_key_time(App &a, float from, float &out) { return neighbour_key(a, from, true, out); }

void anim_key_nav(App &a) {
  float t = a.graph.time, nt;
  if (ImGui::SmallButton("<##pk")) { if (anim_prev_key_time(a, t, nt)) anim_set_time(a, nt); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Previous key"));
  ImGui::SameLine(0.f, 2.f);
  if (ImGui::SmallButton("o##ak")) anim_key_selection_transform(a);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Key the selected object's transform (K)"));
  ImGui::SameLine(0.f, 2.f);
  if (ImGui::SmallButton(">##nk")) { if (anim_next_key_time(a, t, nt)) anim_set_time(a, nt); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Next key"));
}

void anim_key_selection_transform(App &a) {
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) return;
  SceneObject &o = sc.objects[(size_t)sc.selected];
  undo_push(a, "Key transform");
  for (const char *path : {"pos", "rot", "scale"})
    if (const AnimProp *p = anim_find_prop(o, path)) anim_record(o, *p, -1, a.graph.time);
  a.scene_selection_serial++;
  a.status = o.name + ": " + tr("transform keyed at") + " " + sc.timeline.format(a.graph.time);
}

} // namespace studio
