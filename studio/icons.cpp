// Geekatplay TerraForge — vector icons.
#include "icons.hpp"
#include "theme_colors.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

namespace {

const float PI = 3.14159265f;

// Everything is authored in a [-1,1] box about the centre, so an icon reads
// the same at any size and no call site ever deals in pixels.
struct Pen {
  ImDrawList *dl;
  ImVec2 c;
  float r; // half the icon box, in pixels
  ImU32 col;
  float w; // stroke width

  ImVec2 p(float x, float y) const { return ImVec2(c.x + x * r, c.y + y * r); }
  void line(float x0, float y0, float x1, float y1) const {
    dl->AddLine(p(x0, y0), p(x1, y1), col, w);
  }
  void rect(float x0, float y0, float x1, float y1, bool filled = false) const {
    if (filled) dl->AddRectFilled(p(x0, y0), p(x1, y1), col);
    else dl->AddRect(p(x0, y0), p(x1, y1), col, 0.f, 0, w);
  }
  void circle(float x, float y, float rad, bool filled = false) const {
    if (filled) dl->AddCircleFilled(p(x, y), rad * r, col, 20);
    else dl->AddCircle(p(x, y), rad * r, col, 20, w);
  }
  void arc(float x, float y, float rad, float a0, float a1) const {
    dl->PathClear();
    dl->PathArcTo(p(x, y), rad * r, a0, a1, 24);
    dl->PathStroke(col, 0, w);
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2) const {
    dl->AddTriangleFilled(p(x0, y0), p(x1, y1), p(x2, y2), col);
  }
};

void paint(const Pen &k, Icon ic) {
  switch (ic) {
    case Icon::Undo:
      k.arc(0.f, 0.15f, 0.62f, PI * 0.95f, PI * 2.05f);
      k.tri(-0.62f, 0.15f, -0.28f, -0.05f, -0.28f, 0.42f);
      break;
    case Icon::Redo:
      k.arc(0.f, 0.15f, 0.62f, PI * 0.95f, PI * 2.05f);
      k.tri(0.62f, 0.15f, 0.28f, -0.05f, 0.28f, 0.42f);
      break;
    case Icon::Refresh:
      k.arc(0.f, 0.f, 0.6f, PI * 0.35f, PI * 1.85f);
      k.tri(0.6f, -0.15f, 0.24f, -0.32f, 0.72f, -0.5f);
      break;
    case Icon::Brush: // a round brush on a handle
      k.line(-0.55f, 0.55f, 0.15f, -0.15f);
      k.circle(0.35f, -0.35f, 0.32f, true);
      break;
    case Icon::Wireframe: // a triangulated quad
      k.rect(-0.6f, -0.6f, 0.6f, 0.6f);
      k.line(-0.6f, -0.6f, 0.6f, 0.6f);
      break;
    case Icon::Grid:
      k.rect(-0.65f, -0.65f, 0.65f, 0.65f);
      k.line(-0.65f, 0.f, 0.65f, 0.f);
      k.line(0.f, -0.65f, 0.f, 0.65f);
      break;
    case Icon::Sky: // sun over a horizon
      k.circle(0.f, -0.2f, 0.3f);
      k.line(-0.75f, 0.55f, 0.75f, 0.55f);
      for (int i = 0; i < 4; ++i) {
        float a = PI * (0.15f + i * 0.23f);
        k.line(std::cos(a) * 0.45f, -0.2f - std::sin(a) * 0.45f,
               std::cos(a) * 0.68f, -0.2f - std::sin(a) * 0.68f);
      }
      break;
    case Icon::Water: // stacked waves
      k.arc(-0.35f, 0.f, 0.35f, PI, PI * 2.f);
      k.arc(0.35f, 0.f, 0.35f, PI, PI * 2.f);
      k.arc(-0.35f, 0.5f, 0.35f, PI, PI * 2.f);
      k.arc(0.35f, 0.5f, 0.35f, PI, PI * 2.f);
      break;
    case Icon::Camera:
      k.rect(-0.65f, -0.25f, 0.35f, 0.5f);
      k.tri(0.35f, 0.f, 0.7f, -0.3f, 0.7f, 0.35f);
      k.rect(-0.42f, -0.45f, -0.12f, -0.25f, true);
      break;
    case Icon::Planet:
      k.circle(0.f, 0.f, 0.5f);
      k.arc(0.f, 0.f, 0.78f, PI * 1.12f, PI * 1.88f);
      break;
    case Icon::Terrain: // two peaks
      k.tri(-0.75f, 0.55f, -0.15f, -0.5f, 0.4f, 0.55f);
      k.tri(0.f, 0.55f, 0.45f, -0.15f, 0.8f, 0.55f);
      break;
    case Icon::Light: // a bulb
      k.circle(0.f, -0.15f, 0.4f);
      k.line(-0.18f, 0.35f, 0.18f, 0.35f);
      k.line(-0.14f, 0.58f, 0.14f, 0.58f);
      break;
    case Icon::Cloud:
      k.circle(-0.3f, 0.1f, 0.32f, true);
      k.circle(0.1f, -0.05f, 0.42f, true);
      k.circle(0.45f, 0.15f, 0.28f, true);
      k.rect(-0.32f, 0.1f, 0.46f, 0.42f, true);
      break;
    case Icon::Mesh: // a cube in wireframe
      k.rect(-0.6f, -0.35f, 0.3f, 0.6f);
      k.line(-0.6f, -0.35f, -0.3f, -0.62f);
      k.line(0.3f, -0.35f, 0.6f, -0.62f);
      k.line(0.3f, 0.6f, 0.6f, 0.32f);
      k.line(-0.3f, -0.62f, 0.6f, -0.62f);
      k.line(0.6f, -0.62f, 0.6f, 0.32f);
      break;
    case Icon::Folder:
      k.line(-0.65f, -0.4f, -0.05f, -0.4f);
      k.line(-0.05f, -0.4f, 0.1f, -0.18f);
      k.rect(-0.65f, -0.18f, 0.65f, 0.5f);
      break;
    case Icon::Eye:
      k.arc(0.f, 0.35f, 0.72f, PI * 1.22f, PI * 1.78f);
      k.arc(0.f, -0.35f, 0.72f, PI * 0.22f, PI * 0.78f);
      k.circle(0.f, 0.f, 0.2f, true);
      break;
    case Icon::EyeOff:
      k.arc(0.f, 0.35f, 0.72f, PI * 1.22f, PI * 1.78f);
      k.arc(0.f, -0.35f, 0.72f, PI * 0.22f, PI * 0.78f);
      k.line(-0.6f, -0.6f, 0.6f, 0.6f);
      break;
    case Icon::Plus:
      k.line(-0.55f, 0.f, 0.55f, 0.f);
      k.line(0.f, -0.55f, 0.f, 0.55f);
      break;
    case Icon::Minus:
      k.line(-0.55f, 0.f, 0.55f, 0.f);
      break;
    case Icon::Trash:
      k.line(-0.55f, -0.4f, 0.55f, -0.4f);
      k.rect(-0.4f, -0.4f, 0.4f, 0.6f);
      k.line(-0.15f, -0.62f, 0.15f, -0.62f);
      break;
    case Icon::Gear:
      k.circle(0.f, 0.f, 0.28f);
      for (int i = 0; i < 6; ++i) {
        float a = PI * i / 3.f;
        k.line(std::cos(a) * 0.42f, std::sin(a) * 0.42f, std::cos(a) * 0.68f,
               std::sin(a) * 0.68f);
      }
      break;
    case Icon::Search:
      k.circle(-0.12f, -0.12f, 0.42f);
      k.line(0.2f, 0.2f, 0.62f, 0.62f);
      break;
    case Icon::Chevron:
      k.tri(-0.3f, -0.4f, -0.3f, 0.4f, 0.35f, 0.f);
      break;
    case Icon::ChevronDown:
      k.tri(-0.4f, -0.3f, 0.4f, -0.3f, 0.f, 0.35f);
      break;
    // ---- viewport: the four projections ----
    // Perspective is a frustum. Each orthographic view is the plane it shows,
    // seen edge-on, with an arrow coming down the axis you look along - which
    // is the one thing that actually distinguishes top from front from right.
    case Icon::ViewPersp:
      k.line(-0.16f, -0.62f, -0.72f, 0.62f);
      k.line(0.16f, -0.62f, 0.72f, 0.62f);
      k.line(-0.16f, -0.62f, 0.16f, -0.62f);
      k.line(-0.72f, 0.62f, 0.72f, 0.62f);
      k.line(-0.44f, 0.f, 0.44f, 0.f);
      break;
    case Icon::ViewTop:
      k.line(-0.75f, 0.62f, 0.75f, 0.62f);
      k.line(0.f, -0.72f, 0.f, 0.18f);
      k.tri(-0.24f, 0.14f, 0.24f, 0.14f, 0.f, 0.46f);
      break;
    case Icon::ViewFront:
      k.line(0.62f, -0.75f, 0.62f, 0.75f);
      k.line(-0.72f, 0.f, 0.18f, 0.f);
      k.tri(0.14f, -0.24f, 0.14f, 0.24f, 0.46f, 0.f);
      break;
    case Icon::ViewRight:
      k.line(-0.62f, -0.75f, -0.62f, 0.75f);
      k.line(0.72f, 0.f, -0.18f, 0.f);
      k.tri(-0.14f, -0.24f, -0.14f, 0.24f, -0.46f, 0.f);
      break;
    // ---- shading ----
    case Icon::Shaded: // a lit sphere: filled, with the terminator implied
      k.circle(0.f, 0.f, 0.62f);
      k.arc(0.f, 0.f, 0.62f, PI * 0.25f, PI * 1.25f);
      k.arc(0.f, 0.f, 0.55f, PI * 0.3f, PI * 1.2f);
      k.arc(0.f, 0.f, 0.46f, PI * 0.35f, PI * 1.15f);
      break;
    case Icon::Textured: // the same sphere, checkered
      k.circle(0.f, 0.f, 0.62f);
      k.rect(-0.44f, -0.44f, 0.f, 0.f, true);
      k.rect(0.f, 0.f, 0.44f, 0.44f, true);
      break;
    case Icon::Outline: // a shape with a highlight ring around it
      k.rect(-0.32f, -0.32f, 0.32f, 0.32f, true);
      k.rect(-0.66f, -0.66f, 0.66f, 0.66f);
      break;
    case Icon::Link:
      k.arc(-0.25f, 0.f, 0.38f, PI * 0.55f, PI * 1.45f);
      k.arc(0.25f, 0.f, 0.38f, PI * 1.55f, PI * 2.45f);
      k.line(-0.2f, 0.f, 0.2f, 0.f);
      break;
    case Icon::Unlink:
      k.arc(-0.3f, 0.f, 0.38f, PI * 0.55f, PI * 1.45f);
      k.arc(0.3f, 0.f, 0.38f, PI * 1.55f, PI * 2.45f);
      k.line(-0.55f, -0.55f, 0.55f, 0.55f);
      break;
    case Icon::Save:
      k.rect(-0.6f, -0.6f, 0.6f, 0.6f);
      k.rect(-0.32f, -0.6f, 0.32f, -0.15f, true);
      k.rect(-0.4f, 0.15f, 0.4f, 0.6f);
      break;
    case Icon::Open:
      k.line(-0.65f, -0.35f, -0.05f, -0.35f);
      k.rect(-0.65f, -0.35f, 0.4f, 0.45f);
      k.line(-0.4f, 0.45f, 0.68f, 0.45f);
      break;
    case Icon::Move:
      k.line(-0.6f, 0.f, 0.6f, 0.f);
      k.line(0.f, -0.6f, 0.f, 0.6f);
      k.tri(0.6f, 0.f, 0.35f, -0.18f, 0.35f, 0.18f);
      k.tri(-0.6f, 0.f, -0.35f, -0.18f, -0.35f, 0.18f);
      k.tri(0.f, -0.6f, -0.18f, -0.35f, 0.18f, -0.35f);
      k.tri(0.f, 0.6f, -0.18f, 0.35f, 0.18f, 0.35f);
      break;
    case Icon::Rotate:
      k.arc(0.f, 0.f, 0.55f, PI * 0.25f, PI * 1.75f);
      k.tri(0.39f, -0.39f, 0.1f, -0.5f, 0.5f, -0.72f);
      break;
    case Icon::Scale:
      k.rect(-0.62f, -0.62f, 0.05f, 0.05f);
      k.rect(-0.05f, -0.05f, 0.62f, 0.62f);
      break;
    case Icon::Material: // a shaded sphere
      k.circle(0.f, 0.f, 0.55f);
      k.arc(-0.15f, -0.15f, 0.3f, PI * 0.9f, PI * 1.9f);
      break;
    case Icon::Node:
      k.rect(-0.55f, -0.35f, 0.55f, 0.35f);
      k.circle(-0.55f, 0.f, 0.14f, true);
      k.circle(0.55f, 0.f, 0.14f, true);
      break;
    case Icon::Render:
      k.rect(-0.62f, -0.45f, 0.62f, 0.45f);
      k.tri(-0.12f, -0.22f, -0.12f, 0.22f, 0.28f, 0.f);
      break;
    case Icon::Scene:
      k.rect(-0.62f, -0.5f, 0.62f, 0.5f);
      k.line(-0.62f, 0.15f, 0.62f, 0.15f);
      k.tri(-0.35f, 0.15f, -0.1f, -0.25f, 0.15f, 0.15f);
      break;
    case Icon::World:
      k.circle(0.f, 0.f, 0.58f);
      k.line(-0.58f, 0.f, 0.58f, 0.f);
      k.arc(0.f, 0.f, 0.58f, PI * 0.5f, PI * 1.5f);
      break;
    case Icon::Object:
      k.rect(-0.5f, -0.5f, 0.5f, 0.5f);
      break;
    default:
      k.circle(0.f, 0.f, 0.4f);
      break;
  }
}

} // namespace

void icon_draw(ImDrawList *dl, Icon ic, ImVec2 centre, float size, ImU32 col) {
  Pen k{dl, centre, size * 0.5f, col, std::max(1.f, size * 0.085f)};
  paint(k, ic);
}

void IconText(Icon ic, float size, ImU32 col) {
  if (size <= 0.f) size = ImGui::GetFontSize();
  if (!col) col = theme::text();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  icon_draw(ImGui::GetWindowDrawList(), ic,
            ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.82f, col);
}

bool IconButton(Icon ic, const char *id, const char *tip, bool active,
                float size) {
  // Sized from the font, so the button grows with the UI scale instead of
  // clipping the moment the text size goes up.
  if (size <= 0.f)
    size = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f + 6.f;
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
            ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.58f, col);
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
            ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.8f,
            selected ? theme::accent() : theme::text_dim());
  return hit;
}

} // namespace studio
