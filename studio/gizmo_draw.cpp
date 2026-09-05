// Geekatplay TerraForge - painting the gizmo: the tripod of arrows, boxes or
// diamonds, the rings (full for rotate, dashed for twist, half for bend),
// the padlock of a locked object. Split from gizmo.cpp for the 500-line
// rule; the hit-testing and the drags stay there, and both read the same
// state through gizmo_internal.hpp.
#include "gizmo_internal.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

using namespace gizmo_detail;

// --------------------------------------------------------------------- draw
void gizmo_draw(App &a, int slot, const RenderSettings::ViewConfig &vc,
                ImVec2 origin, int w, int h) {
  (void)a;
  (void)vc;
  if (g_mode == GizmoMode::None || !g_visible) return;
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) return;
  const SceneObject &o = sc.objects[sc.selected];
  if (!o.show_gizmo) return;
  if (g_mode >= GizmoMode::Twist && o.type != SceneObject::Mesh) return;
  const float *mvp = renderer_last_mvp(slot);
  if (!mvp) return;
  float anchor[3];
  if (!anchor_of(o, anchor)) return;
  ImVec2 c;
  if (!project(mvp, anchor, origin, w, h, c)) return;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(origin, ImVec2(origin.x + w, origin.y + h), true);
  if (o.locked) {
    // a small padlock where the gizmo would be: the object is selected, it
    // just cannot be moved from here
    icon_draw(dl, Icon::Lock, ImVec2(c.x, c.y - 14.f), 16.f,
              IM_COL32(240, 238, 232, 200));
    dl->AddCircleFilled(c, 3.f, IM_COL32(240, 238, 232, 200), 12);
    dl->PopClipRect();
    return;
  }
  int mask = axis_mask(o, g_mode);
  if (!mask) { dl->PopClipRect(); return; }

  float px_per_unit[3] = {0, 0, 0};
  const float probe = 0.01f;
  for (int ax = 0; ax < 3; ++ax) {
    float p[3] = {anchor[0], anchor[1], anchor[2]};
    p[ax] += probe;
    ImVec2 q;
    if (project(mvp, p, origin, w, h, q)) {
      ImVec2 d(q.x - c.x, q.y - c.y);
      px_per_unit[ax] = std::sqrt(len2(d)) / probe;
    }
  }
  float best_px = std::max({px_per_unit[0], px_per_unit[1], px_per_unit[2]});
  if (best_px < 1e-4f) { dl->PopClipRect(); return; }
  float L = HANDLE_PX / best_px;
  bool dragging = g_drag.active && g_drag.object == sc.selected;

  if (ring_mode(g_mode)) {
    for (int ax = 0; ax < 3; ++ax) {
      if (!(mask & (1 << ax))) continue;
      bool hot = dragging && g_drag.axis == ax;
      ImU32 col = hot ? IM_COL32(255, 240, 200, 255) : AXIS_COL[ax];
      int u = (ax + 1) % 3, v = (ax + 2) % 3;
      // a twist ring is dashed, a bend ring is half: the gadget says what it does
      dl->PathClear();
      for (int i = 0; i <= 48; ++i) {
        float t = (float)i / 48.f * 6.2831853f;
        float p[3] = {anchor[0], anchor[1], anchor[2]};
        p[u] += std::cos(t) * L;
        p[v] += std::sin(t) * L;
        ImVec2 q;
        if (project(mvp, p, origin, w, h, q)) dl->PathLineTo(q);
      }
      if (g_mode == GizmoMode::Twist) {
        // dashed: every other segment
        int n = dl->_Path.Size;
        for (int i = 0; i + 1 < n; i += 2) dl->AddLine(dl->_Path[i], dl->_Path[i + 1], col, hot ? 3.f : 2.f);
        dl->PathClear();
      } else if (g_mode == GizmoMode::Bend) {
        int n = dl->_Path.Size;
        for (int i = 0; i + 1 < n / 2; ++i) dl->AddLine(dl->_Path[i], dl->_Path[i + 1], col, hot ? 3.f : 2.f);
        dl->PathClear();
      } else {
        dl->PathStroke(col, 0, hot ? 3.f : 2.f);
      }
    }
  } else {
    for (int ax = 0; ax < 3; ++ax) {
      if (!(mask & (1 << ax))) continue;
      bool hot = dragging && g_drag.axis == ax;
      ImU32 col = hot ? IM_COL32(255, 240, 200, 255) : AXIS_COL[ax];
      float p[3] = {anchor[0], anchor[1], anchor[2]};
      p[ax] += L;
      ImVec2 q;
      if (!project(mvp, p, origin, w, h, q)) continue;
      dl->AddLine(c, q, col, hot ? 3.f : 2.f);
      ImVec2 d(q.x - c.x, q.y - c.y);
      float l = std::sqrt(len2(d));
      if (l < 1e-3f) continue;
      d.x /= l;
      d.y /= l;
      ImVec2 n(-d.y, d.x);
      if (g_mode == GizmoMode::Move) { // arrowhead
        const float t = 11.f, s = 5.f;
        dl->AddTriangleFilled(q, ImVec2(q.x - d.x * t + n.x * s, q.y - d.y * t + n.y * s),
                              ImVec2(q.x - d.x * t - n.x * s, q.y - d.y * t - n.y * s),
                              col);
      } else if (g_mode == GizmoMode::Skew) { // a diamond: it slides
        const float s = 6.f;
        dl->AddQuadFilled(ImVec2(q.x, q.y - s), ImVec2(q.x + s, q.y), ImVec2(q.x, q.y + s), ImVec2(q.x - s, q.y), col);
      } else { // a box, the universal "this scales"
        const float s = 4.5f;
        dl->AddRectFilled(ImVec2(q.x - s, q.y - s), ImVec2(q.x + s, q.y + s), col);
      }
    }
    if (mask & 0x8) {
      bool hot = dragging && g_drag.axis == 3;
      ImU32 col = hot ? IM_COL32(255, 240, 200, 255) : IM_COL32(225, 222, 216, 230);
      dl->AddRectFilled(ImVec2(c.x - 5.f, c.y - 5.f), ImVec2(c.x + 5.f, c.y + 5.f), col);
    }
  }
  dl->AddCircleFilled(c, 3.f, IM_COL32(240, 238, 232, 220), 12);
  // the tool's name beside the gadget, as Vue's swatches say which gizmo is up
  dl->AddText(ImVec2(c.x + 10.f, c.y + 6.f), IM_COL32(240, 238, 232, 170), gizmo_mode_name(g_mode));
  dl->PopClipRect();
}

} // namespace studio
