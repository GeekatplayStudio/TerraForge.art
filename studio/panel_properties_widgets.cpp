// Geekatplay TerraForge - the primitives the Properties rows are built from.
//
// One row's worth of widget each: the slider look, the number drags that have
// no hard limit, the tooltip, the scene-object picker. Split out of
// panel_properties_node.cpp, which is the layout and the per-type dispatch and
// was over the module size on its own.
#include "app.hpp"
#include "scene.hpp"
#include "gpx/attribute.hpp"
#include <algorithm>
#include <imgui.h>
#include <string>

namespace studio {

// studio/panel_attr_tips.cpp
const char *attr_tooltip(const std::string &key);

void show_attr_tooltip(const gpx::Attribute &at) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip |
                            ImGuiHoveredFlags_AllowWhenDisabled))
    return;
  const char *tip = !at.tooltip.empty() ? at.tooltip.c_str() : attr_tooltip(at.key);
  if (tip) ImGui::SetTooltip("%s", tip);
}

// Draws the filled progress bar that gives these rows their slider look,
// then leaves the frame transparent so the drag widget renders on top.
static void slider_fill(float value, float mn, float mx) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  float w = ImGui::CalcItemWidth(), h = ImGui::GetFrameHeight();
  float t = (mx - mn) > 1e-9f ? (value - mn) / (mx - mn) : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                    ImGui::GetColorU32(ImGuiCol_FrameBg));
  if (t > 0.f)
    dl->AddRectFilled(p, ImVec2(p.x + w * t, p.y + h),
                      ImGui::GetColorU32(ImVec4(0.55f, 0.33f, 0.13f, 0.85f)));
}

// A range that grows when you push against it.
//
// No control in the application has a hard limit. The declared range is
// where the slider *starts*: drag to its end and keep going, or type a
// number past it, and the range extends to fit - half again on the side
// being pushed, doubled for a logarithmic one, which keeps its floor above
// zero. A declared range is the node author's guess at the useful span, and
// the person using it is the one who knows when the guess was wrong.
//
// `mn` and `mx` are the attribute's own bounds, by reference, so the widened
// range is remembered on that node and saved with the project.
static void extend_float(float *v, float &mn, float &mx, bool pushed_lo,
                         bool pushed_hi, bool log_scale) {
  const float span = std::max(mx - mn, 1e-6f);
  if (pushed_hi || *v > mx) mx = log_scale ? std::max(mx * 2.f, *v) : std::max(mx + span * 0.5f, *v);
  if (pushed_lo || *v < mn) mn = log_scale ? std::min(std::max(mn * 0.5f, 1e-9f), *v) : std::min(mn - span * 0.5f, *v);
}

// float row: [-] [slider-look drag: click types, drag slides, wheel steps] [+]
bool scalar_float(const char *id, float *v, float &mn, float &mx,
                  bool log_scale, float width) {
  bool changed = false;
  float step = (mx - mn) / 200.f;   // wheel/button step: slow, fine control
  ImGui::PushID(id);
  float btn = ImGui::GetFrameHeight();
  if (ImGui::Button("-", ImVec2(btn, btn))) {
    *v -= step;
    changed = true;
  }
  ImGui::SameLine(0, 2);
  // a caller's cap is for the whole row, so the two buttons come out of it
  ImGui::SetNextItemWidth(width > 0.f ? width - (btn + 2.f) * 2.f : -btn - 2);
  slider_fill(*v, mn, mx);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.10f));
  // Not AlwaysClamp: a typed value past the range is accepted, and the
  // range then grows to hold it. Dragging still stops at the edge, so the
  // edge is detected below and pushed outward instead.
  changed |= ImGui::DragFloat("##v", v, step * 0.5f, mn, mx, "%.3f",
                              log_scale ? ImGuiSliderFlags_Logarithmic : 0);
  const bool dragging = ImGui::IsItemActive();
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered()) {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // wheel adjusts, not scrolls
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f) {
      *v += wheel * step;
      changed = true;
    }
  }
  ImGui::SameLine(0, 2);
  if (ImGui::Button("+", ImVec2(btn, btn))) {
    *v += step;
    changed = true;
  }
  // Pushed against the end while dragging, or carried past it by a button,
  // the wheel or a typed number: the range follows.
  const float eps = (mx - mn) * 1e-4f;
  extend_float(v, mn, mx, dragging && *v <= mn + eps, dragging && *v >= mx - eps,
               log_scale);
  ImGui::PopID();
  return changed;
}

bool scalar_int(const char *id, int *v, int &mn, int &mx, float width) {
  bool changed = false;
  int step = std::max(1, (mx - mn) / 200);
  ImGui::PushID(id);
  float btn = ImGui::GetFrameHeight();
  if (ImGui::Button("-", ImVec2(btn, btn))) {
    *v -= step;
    changed = true;
  }
  ImGui::SameLine(0, 2);
  ImGui::SetNextItemWidth(width > 0.f ? width - (btn + 2.f) * 2.f : -btn - 2);
  slider_fill((float)*v, (float)mn, (float)mx);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.10f));
  // no hard limit: see extend_float above - the same rule, in integers
  changed |= ImGui::DragInt("##v", v, 0.25f, mn, mx, "%d");
  const bool dragging = ImGui::IsItemActive();
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered()) {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f) {
      *v += (wheel > 0 ? step : -step);
      changed = true;
    }
  }
  ImGui::SameLine(0, 2);
  if (ImGui::Button("+", ImVec2(btn, btn))) {
    *v += step;
    changed = true;
  }
  // pushed against an end, or carried past it: the range follows
  const int span = std::max(mx - mn, 1);
  if ((dragging && *v >= mx) || *v > mx) mx = std::max(mx + span / 2 + 1, *v);
  if ((dragging && *v <= mn) || *v < mn) mn = std::min(mn - span / 2 - 1, *v);
  ImGui::PopID();
  return changed;
}

// A picker over the scene's objects, for a text attribute that names one.
//
// The scene changes while the graph is open, so the list is built each time
// rather than declared with the attribute - which is exactly why this cannot
// be an ordinary Choice. A name that no longer matches anything is kept and
// shown, because deleting an object should not silently repoint a node at
// something else.
bool object_ref_combo(gpx::Attribute &at) {
  const SceneState &sc = scene();
  bool changed = false;
  const std::string current = at.s;
  if (ImGui::BeginCombo("##objref", current.empty() ? "(none)" : current.c_str())) {
    if (ImGui::Selectable("(none)", current.empty())) {
      at.s.clear();
      changed = true;
    }
    for (const SceneObject &o : sc.objects) {
      if (o.name.empty()) continue;
      ImGui::PushID(&o);
      if (ImGui::Selectable(o.name.c_str(), o.name == current)) {
        at.s = o.name;
        changed = true;
      }
      ImGui::PopID();
    }
    if (!current.empty() &&
        std::none_of(sc.objects.begin(), sc.objects.end(),
                     [&](const SceneObject &o) { return o.name == current; })) {
      ImGui::Separator();
      ImGui::TextDisabled("'%s' is not in the scene", current.c_str());
    }
    ImGui::EndCombo();
  }
  return changed;
}

} // namespace studio
