// Geekatplay TerraForge — the wheel over a number changes the number.
//
// ImGui's drag and slider widgets scroll the panel when the wheel turns
// over them. Every property in this application is a number a person
// wants to nudge while looking at the picture, so the wheel over a value
// adjusts it - a notch is a hundredth of the range, or three drag steps
// when the range is open - and the panel scrolls only over something that
// is not a value. scalar_float (panel_properties_widgets.cpp) did this
// first; these are the same behaviour on ImGui's own widgets, for every
// panel that calls them directly. Drop-in: the signatures are ImGui's.
#pragma once
#include <imgui.h>
#include <algorithm>

namespace studio {

// After a widget: if the pointer is over it and the wheel turned, move the
// value by `step` a notch and keep the panel from scrolling. Returns true
// when it changed the value.
inline bool wheel_nudge(float *v, float lo, float hi, float step) {
  if (!ImGui::IsItemHovered()) return false;
  ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
  const float wheel = ImGui::GetIO().MouseWheel;
  if (wheel == 0.f) return false;
  float nv = *v + wheel * step;
  if (lo < hi) nv = std::clamp(nv, lo, hi);
  if (nv == *v) return false;
  *v = nv;
  return true;
}
inline bool wheel_nudge_int(int *v, int lo, int hi) {
  if (!ImGui::IsItemHovered()) return false;
  ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
  const float wheel = ImGui::GetIO().MouseWheel;
  if (wheel == 0.f) return false;
  int nv = *v + (wheel > 0.f ? 1 : -1);
  if (lo < hi) nv = std::clamp(nv, lo, hi);
  if (nv == *v) return false;
  *v = nv;
  return true;
}
inline float wheel_step(float speed, float lo, float hi) {
  return lo < hi ? (hi - lo) * 0.01f : speed * 3.f;
}

inline bool DragFloatW(const char *label, float *v, float speed = 1.f, float lo = 0.f, float hi = 0.f,
                       const char *fmt = "%.3f", ImGuiSliderFlags flags = 0) {
  bool ch = ImGui::DragFloat(label, v, speed, lo, hi, fmt, flags);
  return wheel_nudge(v, lo, hi, wheel_step(speed, lo, hi)) || ch;
}
inline bool DragFloat2W(const char *label, float v[2], float speed = 1.f, float lo = 0.f, float hi = 0.f,
                        const char *fmt = "%.3f", ImGuiSliderFlags flags = 0) {
  bool ch = ImGui::DragFloat2(label, v, speed, lo, hi, fmt, flags);
  // a wheel over a vector moves every component together
  const float step = wheel_step(speed, lo, hi);
  bool w = false;
  if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.f)
    for (int i = 0; i < 2; ++i) w |= wheel_nudge(&v[i], lo, hi, step);
  return w || ch;
}
inline bool DragFloat3W(const char *label, float v[3], float speed = 1.f, float lo = 0.f, float hi = 0.f,
                        const char *fmt = "%.3f", ImGuiSliderFlags flags = 0) {
  bool ch = ImGui::DragFloat3(label, v, speed, lo, hi, fmt, flags);
  const float step = wheel_step(speed, lo, hi);
  bool w = false;
  if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.f)
    for (int i = 0; i < 3; ++i) w |= wheel_nudge(&v[i], lo, hi, step);
  return w || ch;
}
inline bool SliderFloatW(const char *label, float *v, float lo, float hi, const char *fmt = "%.3f",
                         ImGuiSliderFlags flags = 0) {
  bool ch = ImGui::SliderFloat(label, v, lo, hi, fmt, flags);
  return wheel_nudge(v, lo, hi, (hi - lo) * 0.01f) || ch;
}
inline bool DragIntW(const char *label, int *v, float speed = 1.f, int lo = 0, int hi = 0,
                     const char *fmt = "%d", ImGuiSliderFlags flags = 0) {
  bool ch = ImGui::DragInt(label, v, speed, lo, hi, fmt, flags);
  return wheel_nudge_int(v, lo, hi) || ch;
}
inline bool SliderIntW(const char *label, int *v, int lo, int hi, const char *fmt = "%d",
                       ImGuiSliderFlags flags = 0) {
  bool ch = ImGui::SliderInt(label, v, lo, hi, fmt, flags);
  return wheel_nudge_int(v, lo, hi) || ch;
}

} // namespace studio
