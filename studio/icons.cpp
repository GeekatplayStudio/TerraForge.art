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

// ------------------------------------------------------------ the colour code
namespace icon_hue {
ImU32 geometry()  { return IM_COL32(0x62, 0x9c, 0xe8, 0xff); } // blue
ImU32 generator() { return IM_COL32(0x74, 0xc2, 0x6a, 0xff); } // green
ImU32 deformer()  { return IM_COL32(0xb8, 0x7a, 0xe0, 0xff); } // purple
ImU32 tool()      { return theme::accent(); }                   // orange
ImU32 light()     { return IM_COL32(0xe8, 0xc4, 0x4e, 0xff); } // yellow
ImU32 camera()    { return IM_COL32(0x58, 0xc0, 0xc0, 0xff); } // teal
ImU32 sky()       { return IM_COL32(0x88, 0xb8, 0xe6, 0xff); } // pale blue
ImU32 record()    { return IM_COL32(0xd8, 0x50, 0x3c, 0xff); } // red
ImU32 neutral()   { return theme::text(); }
} // namespace icon_hue

ImU32 icon_color(Icon ic) {
  using namespace icon_hue;
  switch (ic) {
    // geometry: what stands in the world
    case Icon::Object: case Icon::Mesh: case Icon::Terrain: case Icon::Planet:
    case Icon::Grid: case Icon::Wireframe:
    case Icon::Sphere: case Icon::Plane: case Icon::Cylinder: case Icon::Cone:
      return geometry();
    // generators and hierarchy: things that make or hold other things
    case Icon::Group: case Icon::Null: case Icon::Expression: case Icon::Bake:
    case Icon::Node: case Icon::Layer:
      return generator();
    // deformers
    case Icon::Twist: case Icon::Bend: case Icon::Skew: case Icon::Taper:
    case Icon::Modifier:
      return deformer();
    // tools and systems
    case Icon::Transform:
    case Icon::Move: case Icon::Rotate: case Icon::Scale: case Icon::Brush:
    case Icon::Snap: case Icon::Magnet: case Icon::Fit: case Icon::Lock:
    case Icon::Unlock: case Icon::Material: case Icon::Render:
    case Icon::Raise: case Icon::Flatten: case Icon::Smooth: case Icon::Terrace:
    case Icon::Noise: case Icon::Erase:
    case Icon::Key: case Icon::KeyAdd: case Icon::KeyRemove: case Icon::PrevKey:
    case Icon::NextKey: case Icon::Marker: case Icon::Curve: case Icon::Timeline:
      return tool();
    // lights
    case Icon::Light: case Icon::Sun:
      return light();
    // cameras and views
    case Icon::Camera: case Icon::ViewPersp: case Icon::ViewTop:
    case Icon::ViewFront: case Icon::ViewRight: case Icon::Views: case Icon::Scene:
      return camera();
    // the air and the water
    case Icon::Sky: case Icon::Cloud: case Icon::Atmosphere: case Icon::Water:
    case Icon::World:
      return sky();
    // record and destroy
    case Icon::Autokey: case Icon::Trash:
      return record();
    default:
      return neutral();
  }
}

void icon_draw_tinted(ImDrawList *dl, Icon ic, ImVec2 centre, float size,
                      ImU32 base, ImU32 hue) {
  // 1 px at 18, 2 px at 26, 3 px at 36 — the stroke follows the ladder, and
  // the same 2 px inset at every step keeps the glyphs from touching the box.
  // 1 px at 18, 2 px at 26, 3 px at 36; the glyph fills the box to within
  // a pixel and a half, so the icon reads as a picture rather than a mark.
  float w = std::max(1.f, std::round(size / 12.f));
  float half = std::max(1.f, size * 0.5f - ICON_PAD);
  Pen k{dl, centre, half, base, w, hue};
  if (!paint_glyphs_a(k, ic)) paint_glyphs_b(k, ic);
}

void icon_draw(ImDrawList *dl, Icon ic, ImVec2 centre, float size, ImU32 col) {
  icon_draw_tinted(dl, ic, centre, size, col, col);
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
                float size, bool expandable) {
  // The button is the glyph plus the frame padding, square, so a row of
  // tools sits at the palette size the user chose rather than at whatever
  // the font happens to be.
  float glyph = icon_toolbar_size();
  if (size <= 0.f) size = glyph + ImGui::GetStyle().FramePadding.y * 2.f;
  else glyph = std::min(glyph, size - ImGui::GetStyle().FramePadding.y * 2.f);
  // The button itself is invisible; the tile is painted by hand so the three
  // states read the way Cinema 4D's do: a raised tile at rest, lighter under
  // the pointer, and a warm fill with an accent frame when the tool is on.
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool hit = ImGui::Button(id, ImVec2(size, size));
  ImGui::PopStyleColor(3);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p1(p.x + size, p.y + size);
  // Cinema 4D's tile: a light grey square with softened corners, lighter
  // still under the pointer, and the accent behind the tool that is on.
  ImU32 fill, frame;
  if (active) {
    fill = theme::fade(theme::accent(), held ? 0.70f : hovered ? 0.55f : 0.42f);
    frame = theme::accent();
  } else {
    fill = theme::shade(theme::LEAD_SURFACE, held ? 1.25f : hovered ? 2.3f : 1.8f);
    frame = theme::shade(theme::LEAD_SURFACE, 0.60f);
  }
  const float rounding = 4.f;
  dl->AddRectFilled(p, p1, fill, rounding);
  dl->AddRect(p, p1, frame, rounding);
  // The glyph keeps its functional colour in every state - that is the whole
  // point of a colour code.
  ImU32 hue = icon_color(ic);
  ImU32 base = theme::text();
  if (!active && !hovered) base = theme::shade(theme::text(), 0.85f);
  icon_draw_tinted(dl, ic, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), glyph,
                   base, hue);
  // The group mark: a small grey triangle tucked into the bottom-right
  // corner, which is how Cinema 4D says "there are more tools under this
  // one". Grey rather than the functional colour, because it describes the
  // button and not what the button makes.
  if (expandable) {
    const float t = std::max(4.f, std::round(size * 0.24f));
    const float in = std::max(2.f, rounding * 0.5f);
    ImVec2 corner(p1.x - in, p1.y - in);
    dl->AddTriangleFilled(ImVec2(corner.x - t, corner.y), corner,
                          ImVec2(corner.x, corner.y - t),
                          theme::fade(theme::text_dim(), hovered ? 1.f : 0.7f));
  }
  if (tip && hovered) ImGui::SetTooltip("%s", tip);
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
