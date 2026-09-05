// Geekatplay TerraForge — vector icons: the public API and the pen.
// The glyphs themselves live in icons_glyphs.cpp and icons_glyphs2.cpp.
#include "icons.hpp"
#include "icons_pen.hpp"
#include "prefs.hpp"
#include "theme_colors.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

const int *icon_size_ladder() {
  static const int ladder[3] = {18, 26, 36};
  return ladder;
}

float icon_toolbar_size() {
  int i = std::clamp(prefs().icon_size, 0, 2);
  return static_cast<float>(icon_size_ladder()[i]);
}

void icon_draw(ImDrawList *dl, Icon ic, ImVec2 centre, float size, ImU32 col) {
  // 1 px at 18, 2 px at 26, 3 px at 36 — the stroke follows the ladder, and
  // the same 2 px inset at every step keeps the glyphs from touching the box.
  float w = std::max(1.f, std::round(size / 14.f));
  float half = std::max(1.f, size * 0.5f - ICON_PAD);
  Pen k{dl, centre, half, col, w};
  if (!paint_glyphs_a(k, ic)) paint_glyphs_b(k, ic);
}

void IconText(Icon ic, float size, ImU32 col) {
  if (size <= 0.f) size = ImGui::GetFontSize();
  if (!col) col = theme::text();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  icon_draw(ImGui::GetWindowDrawList(), ic,
            ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size, col);
}

bool IconButton(Icon ic, const char *id, const char *tip, bool active,
                float size) {
  // The button is the glyph plus the frame padding, square, so a row of
  // tools sits at the palette size the user chose rather than at whatever
  // the font happens to be.
  float glyph = icon_toolbar_size();
  if (size <= 0.f) size = glyph + ImGui::GetStyle().FramePadding.y * 2.f;
  else glyph = std::min(glyph, size - ImGui::GetStyle().FramePadding.y * 2.f);
  if (active)
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::ColorConvertU32ToFloat4(theme::accent()));
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool hit = ImGui::Button(id, ImVec2(size, size));
  if (active) ImGui::PopStyleColor();
  ImU32 col = active
                  ? theme::text_on_header()
                  : (ImGui::IsItemHovered() ? theme::text() : theme::text_dim());
  icon_draw(ImGui::GetWindowDrawList(), ic,
            ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), glyph, col);
  if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  return hit;
}

bool IconMenuItem(Icon ic, const char *label, bool selected) {
  float s = ImGui::GetFontSize();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(s + 6.f, s));
  ImGui::SameLine(0, 0);
  bool hit = ImGui::MenuItem(label, nullptr, selected);
  icon_draw(ImGui::GetWindowDrawList(), ic,
            ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s,
            selected ? theme::accent() : theme::text_dim());
  return hit;
}

} // namespace studio
