// Geekatplay TerraForge - the Properties rows for a plant's two attribute
// types: a curve you draw, and a number with a spread.
//
// A Curve row is the curve itself, drawn in the row and edited in place:
// drag a key, double-click to add one, right-click to delete, and a small
// menu for straight or smooth. A Random row is the value slider with the
// spread beside it, and a menu for how the spread is read (absolute, a
// fraction, gaussian), when a new draw is made, and the two shaping curves
// - along the part and by its place in the hierarchy - each opened in a
// popup with the same curve editor. Split from panel_properties_node.cpp,
// which was already at the module size and which knows nothing about
// plants; these are the only rows it delegates.
#include "app.hpp"
#include "gpx/attribute.hpp"
#include "gpx/plant_curve.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace studio {

namespace {

// The curve strip: keys are dots; the curve is polyline-sampled. Returns true
// when a key moved, was added or was deleted.
bool curve_strip(const char *id, gpx::Curve &c, float height, int &selected) {
  bool changed = false;
  ImGui::PushID(id);
  const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 80.f), height);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + size.x, p0.y + size.y);
  ImGui::InvisibleButton("##curve", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.f);
  const float xs = c.xmax - c.xmin > 1e-9f ? c.xmax - c.xmin : 1.f;
  const float ys = c.ymax - c.ymin > 1e-9f ? c.ymax - c.ymin : 1.f;
  auto to_px = [&](float x, float y) {
    return ImVec2(p0.x + (x - c.xmin) / xs * size.x, p1.y - (y - c.ymin) / ys * size.y);
  };
  auto from_px = [&](ImVec2 q, float &x, float &y) {
    x = c.xmin + (q.x - p0.x) / size.x * xs;
    y = c.ymin + (p1.y - q.y) / size.y * ys;
  };
  // grid: the unit lines
  const ImU32 grid = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f));
  for (int i = 1; i < 4; ++i) {
    dl->AddLine(ImVec2(p0.x + size.x * i / 4.f, p0.y), ImVec2(p0.x + size.x * i / 4.f, p1.y), grid);
    dl->AddLine(ImVec2(p0.x, p0.y + size.y * i / 4.f), ImVec2(p1.x, p0.y + size.y * i / 4.f), grid);
  }
  if (c.ymin < 1.f && c.ymax > 1.f) {
    ImVec2 a = to_px(c.xmin, 1.f), b = to_px(c.xmax, 1.f);
    dl->AddLine(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f)));
  }
  // the curve
  const ImU32 line = IM_COL32(0xd8, 0x8a, 0x3a, 0xff);
  const int N = 64;
  ImVec2 prev = to_px(c.xmin, c.eval(c.xmin));
  for (int i = 1; i <= N; ++i) {
    const float x = c.xmin + xs * i / N;
    ImVec2 q = to_px(x, c.eval(x));
    q.y = std::clamp(q.y, p0.y, p1.y);
    dl->AddLine(prev, q, line, 1.6f);
    prev = q;
  }
  // keys
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  int hot = -1;
  for (size_t k = 0; k < c.keys.size(); ++k) {
    ImVec2 q = to_px(c.keys[k].x, c.keys[k].y);
    const float d = std::hypot(mouse.x - q.x, mouse.y - q.y);
    if (hovered && d < 7.f && hot < 0) hot = (int)k;
    const bool sel = (int)k == selected;
    dl->AddCircleFilled(q, sel ? 5.f : 3.5f, sel ? IM_COL32(255, 235, 200, 255) : line);
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) selected = hot;
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hot < 0) {
    float x, y;
    from_px(mouse, x, y);
    c.keys.push_back({x, y, 0.f, 0.f});
    c.normalise();
    c.auto_slopes();
    changed = true;
    for (size_t k = 0; k < c.keys.size(); ++k)
      if (std::fabs(c.keys[k].x - x) < 1e-6f) selected = (int)k;
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hot >= 0 && c.keys.size() > 2) {
    c.keys.erase(c.keys.begin() + hot);
    c.auto_slopes();
    selected = -1;
    changed = true;
  }
  if (selected >= 0 && selected < (int)c.keys.size() && ImGui::IsItemActive() &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
    float x, y;
    from_px(mouse, x, y);
    gpx::CurveKey &k = c.keys[(size_t)selected];
    // the end keys keep their x, so the curve always spans its domain
    const bool edge = selected == 0 || selected + 1 == (int)c.keys.size();
    if (!edge) {
      const float lo = c.keys[(size_t)selected - 1].x + 1e-4f, hi = c.keys[(size_t)selected + 1].x - 1e-4f;
      k.x = std::clamp(x, lo, hi);
    }
    k.y = std::clamp(y, c.ymin - ys, c.ymax + ys);
    c.auto_slopes();
    changed = true;
  }
  if (hovered) ImGui::SetTooltip("drag a key; double-click adds one; right-click deletes");
  ImGui::PopID();
  return changed;
}

bool curve_editor(const char *id, gpx::CurveSet &cs, float height) {
  if (cs.curves.empty()) {
    cs.curves = {gpx::Curve::constant(1.f)};
    cs.weights = {1.f};
  }
  gpx::Curve &c = cs.curves[0];
  static int selected = -1;
  bool changed = curve_strip(id, c, height, selected);
  ImGui::PushID(id);
  int interp = c.interp;
  ImGui::SetNextItemWidth(90);
  if (ImGui::Combo("##interp", &interp, "Linear\0Smooth\0")) {
    c.interp = interp;
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Flat 1")) {
    c = gpx::Curve::constant(1.f, c.xmin, c.xmax);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Fade out")) {
    c = gpx::Curve::line(1.f, 0.f, c.xmin, c.xmax);
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Fade in")) {
    c = gpx::Curve::line(0.f, 1.f, c.xmin, c.xmax);
    changed = true;
  }
  if (cs.curves.size() > 1) {
    ImGui::TextDisabled("%d alternatives; one is chosen per plant", (int)cs.curves.size());
  }
  ImGui::PopID();
  return changed;
}

// A tiny thumbnail of a curve set beside a Random row, opening the editor.
bool curve_button(const char *id, const char *label, gpx::CurveSet &cs) {
  bool changed = false;
  ImGui::PushID(id);
  const gpx::Curve &c = cs.primary();
  const ImVec2 size(34.f, ImGui::GetFrameHeight());
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  if (ImGui::Button("##thumb", size)) ImGui::OpenPopup("curve_popup");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float xs = c.xmax - c.xmin > 1e-9f ? c.xmax - c.xmin : 1.f;
  const float ys = std::max(c.ymax - c.ymin, 1e-3f);
  ImVec2 prev;
  for (int i = 0; i <= 12; ++i) {
    const float x = c.xmin + xs * i / 12.f;
    const float y = std::clamp((c.eval(x) - c.ymin) / ys, 0.f, 1.f);
    ImVec2 q(p0.x + 3 + (size.x - 6) * i / 12.f, p0.y + size.y - 3 - (size.y - 6) * y);
    if (i) dl->AddLine(prev, q, IM_COL32(0xd8, 0x8a, 0x3a, 0xff), 1.2f);
    prev = q;
  }
  if (ImGui::BeginPopup("curve_popup")) {
    ImGui::TextDisabled("%s", label);
    ImGui::Dummy(ImVec2(260, 0));
    changed = curve_editor("ed", cs, 110.f);
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return changed;
}

} // namespace

bool draw_attr_curve(gpx::Attribute &at) {
  ImGui::NewLine();
  return curve_editor(at.key.c_str(), at.curves, 90.f);
}

bool draw_attr_random(gpx::Attribute &at) {
  bool changed = false;
  const float avail = ImGui::GetContentRegionAvail().x;
  // value | ± spread | menu | along | hierarchy
  const float right = 34.f * 2 + ImGui::GetFrameHeight() + 62.f + ImGui::GetStyle().ItemSpacing.x * 5;
  ImGui::SetNextItemWidth(std::max(avail - right, 60.f));
  if (scalar_float("f", &at.f, at.fmin, at.fmax, at.log_scale)) changed = true;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(62.f);
  const char *fmt = at.spread_mode == 1 ? "±%.0f%%" : "±%.3g";
  float shown = at.spread_mode == 1 ? at.spread * 100.f : at.spread;
  if (ImGui::DragFloat("##spread", &shown, at.spread_mode == 1 ? 0.5f : (at.fmax - at.fmin) / 400.f, 0.f,
                       at.spread_mode == 1 ? 200.f : (at.fmax - at.fmin) * 2.f, fmt)) {
    at.spread = std::max(at.spread_mode == 1 ? shown / 100.f : shown, 0.f);
    changed = true;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far a draw may stray from the value.\nRight-click the menu for absolute, relative or gaussian,\nand for when a new draw is made.");
  ImGui::SameLine();
  if (ImGui::Button("v", ImVec2(ImGui::GetFrameHeight(), 0))) ImGui::OpenPopup("random_menu");
  if (ImGui::BeginPopup("random_menu")) {
    ImGui::TextDisabled("Spread is read as");
    for (int m = 0; m < 3; ++m) {
      static const char *modes[3] = {"Absolute (value ± spread)", "Relative (value ± spread %)", "Gaussian (deviation)"};
      if (ImGui::RadioButton(modes[m], at.spread_mode == m)) {
        at.spread_mode = m;
        changed = true;
      }
    }
    ImGui::Separator();
    ImGui::TextDisabled("New random value");
    for (int s = 0; s < 4; ++s) {
      static const char *scopes[4] = {"Each time", "Every new primitive", "Once per plant", "Once per ancestor"};
      if (ImGui::RadioButton(scopes[s], at.scope == s)) {
        at.scope = s;
        changed = true;
      }
    }
    if (at.scope == 3) {
      ImGui::SetNextItemWidth(80);
      if (ImGui::InputInt("Ancestor level", &at.hier_level)) {
        at.hier_level = std::clamp(at.hier_level, 1, 12);
        changed = true;
      }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Hierarchy curve");
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("Parent level", &at.hier_level)) {
      at.hier_level = std::clamp(at.hier_level, 1, 12);
      changed = true;
    }
    if (ImGui::Checkbox("Cascade through every level", &at.hier_cascade)) changed = true;
    ImGui::Separator();
    if (ImGui::Checkbox("Published (on the species' preset sheet)", &at.published)) changed = true;
    if (at.published) {
      char buf[96];
      std::snprintf(buf, sizeof buf, "%s", at.pub_name.c_str());
      ImGui::SetNextItemWidth(160);
      if (ImGui::InputText("Shown as", buf, sizeof buf)) {
        at.pub_name = buf;
        changed = true;
      }
      std::snprintf(buf, sizeof buf, "%s", at.pub_group.c_str());
      ImGui::SetNextItemWidth(160);
      if (ImGui::InputText("Group", buf, sizeof buf)) {
        at.pub_group = buf;
        changed = true;
      }
      if (ImGui::Checkbox("Allow external access", &at.external)) changed = true;
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (curve_button("along", "Along the part: base to tip", at.curve_along)) changed = true;
  ImGui::SameLine();
  if (curve_button("hier", "By its place on the parent: base to tip", at.curve_hier)) changed = true;
  return changed;
}

} // namespace studio
