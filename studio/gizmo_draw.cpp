// Geekatplay TerraForge - painting the gizmo: the arrows, the rings and the
// boxes of the universal gadget, or the one part a single tool shows; the
// dashed ring of twist, the half ring of bend, the diamonds of skew; the
// padlock of a locked object. Split from gizmo.cpp for the 500-line rule;
// the hit-testing and the drags stay there, and both read the same state
// through gizmo_internal.hpp.
//
// The handle under the pointer is painted bright and thick (g_hot), the one
// being dragged brighter still, so the gadget always says what a click
// would do before it is clicked.
#include "gizmo_internal.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace studio {

using namespace gizmo_detail;

namespace {

const ImU32 HOT_COL = IM_COL32(255, 232, 120, 255);
const ImU32 DRAG_COL = IM_COL32(255, 250, 235, 255);

struct Painter {
  ImDrawList *dl;
  const float *mvp;
  ImVec2 origin;
  int w, h;
  const Layout &lay;
  const float *anchor;
  bool dragging;

  bool at(int ax, float reach, ImVec2 &q) const {
    float p[3] = {anchor[0] + lay.axes[ax][0] * lay.L * reach,
                  anchor[1] + lay.axes[ax][1] * lay.L * reach,
                  anchor[2] + lay.axes[ax][2] * lay.L * reach};
    return project(mvp, p, origin, w, h, q);
  }
  // colour and width of a part, per its state
  ImU32 col(int part, int ax, float &width) const {
    bool drag = dragging && g_drag.axis == ax &&
                ((part == P_RING) == ring_mode(g_drag.mode)) &&
                ((part == P_SCALE || part == P_CENTRE) ==
                 (g_drag.mode == GizmoMode::Scale || g_drag.mode == GizmoMode::Taper));
    bool hot = !dragging && g_hot.part == part && g_hot.axis == ax;
    width = drag ? 4.2f : hot ? 3.8f : 2.7f;
    if (drag) return DRAG_COL;
    if (hot) return HOT_COL;
    return ax < 3 ? AXIS_COL[ax] : IM_COL32(225, 222, 216, 230);
  }

  void rings() const {
    for (int ax = 0; ax < 3; ++ax) {
      if (!(lay.mask_rot & (1 << ax))) continue;
      float wd;
      ImU32 c = col(P_RING, ax, wd);
      int u = (ax + 1) % 3, v = (ax + 2) % 3;
      dl->PathClear();
      for (int i = 0; i <= 48; ++i) {
        float t = (float)i / 48.f * 6.2831853f;
        float p[3];
        for (int k = 0; k < 3; ++k)
          p[k] = anchor[k] + (lay.axes[u][k] * std::cos(t) + lay.axes[v][k] * std::sin(t)) * lay.L * ring_reach(lay);
        ImVec2 q;
        if (project(mvp, p, origin, w, h, q)) dl->PathLineTo(q);
      }
      if (g_mode == GizmoMode::Twist) { // dashed: every other segment
        int n = dl->_Path.Size;
        for (int i = 0; i + 1 < n; i += 2) dl->AddLine(dl->_Path[i], dl->_Path[i + 1], c, wd);
        dl->PathClear();
      } else if (g_mode == GizmoMode::Bend) { // half
        int n = dl->_Path.Size;
        for (int i = 0; i + 1 < n / 2; ++i) dl->AddLine(dl->_Path[i], dl->_Path[i + 1], c, wd);
        dl->PathClear();
      } else {
        dl->PathStroke(c, 0, wd);
      }
    }
  }

  void arrows() const {
    for (int ax = 0; ax < 3; ++ax) {
      if (!(lay.mask_move & (1 << ax))) continue;
      float wd;
      ImU32 c = col(P_MOVE, ax, wd);
      ImVec2 q;
      if (!at(ax, 1.f, q)) continue;
      dl->AddLine(lay.c, q, c, wd);
      ImVec2 d(q.x - lay.c.x, q.y - lay.c.y);
      float l = std::sqrt(len2(d));
      if (l < 1e-3f) continue;
      d.x /= l;
      d.y /= l;
      ImVec2 n(-d.y, d.x);
      if (g_mode == GizmoMode::Skew) { // a diamond: it slides
        const float s = 6.f;
        dl->AddQuadFilled(ImVec2(q.x, q.y - s), ImVec2(q.x + s, q.y), ImVec2(q.x, q.y + s),
                          ImVec2(q.x - s, q.y), c);
      } else { // an arrowhead
        const float t = 13.f, s = 6.f;
        dl->AddTriangleFilled(q, ImVec2(q.x - d.x * t + n.x * s, q.y - d.y * t + n.y * s),
                              ImVec2(q.x - d.x * t - n.x * s, q.y - d.y * t - n.y * s), c);
      }
    }
  }

  void boxes() const {
    const float reach = scale_box_reach(lay);
    for (int ax = 0; ax < 3; ++ax) {
      if (!(lay.mask_scl & (1 << ax))) continue;
      float wd;
      ImU32 c = col(P_SCALE, ax, wd);
      ImVec2 q;
      if (!at(ax, reach, q)) continue;
      if (lay.universal) {
        // a short stalk from the arrow tip to the box, so the box reads as
        // part of the same axis
        ImVec2 q0;
        if (at(ax, 1.f, q0)) dl->AddLine(q0, q, c, 1.5f);
      } else {
        dl->AddLine(lay.c, q, c, wd);
      }
      const float s = wd > 3.f ? 6.f : 5.f;
      dl->AddRectFilled(ImVec2(q.x - s, q.y - s), ImVec2(q.x + s, q.y + s), c);
    }
    if (lay.mask_scl & 0x8) {
      float wd;
      ImU32 c = col(P_CENTRE, 3, wd);
      const float s = wd > 3.f ? 6.5f : 5.5f;
      dl->AddRectFilled(ImVec2(lay.c.x - s, lay.c.y - s), ImVec2(lay.c.x + s, lay.c.y + s), c);
    }
  }
};

} // namespace

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
  Layout lay;
  if (!gizmo_layout(o, mvp, origin, w, h, lay)) return;
  float anchor[3];
  anchor_of(o, anchor);
  const ImVec2 c = lay.c;
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
  const bool dragging = g_drag.active && g_drag.object == sc.selected;
  Painter P{dl, mvp, origin, w, h, lay, anchor, dragging};

  if (!(lay.mask_move | lay.mask_rot | lay.mask_scl)) {
    // The tool has no handle on this object (Rotate on a camera). Still
    // show where the object is: a thin axis tripod and the centre dot, so
    // the selection is never invisible.
    for (int ax = 0; ax < 3; ++ax) {
      ImVec2 q;
      if (P.at(ax, 0.45f, q)) dl->AddLine(c, q, AXIS_COL[ax], 1.5f);
    }
  } else {
    // rings behind, arrows over them, boxes on top: the small targets win
    P.rings();
    P.arrows();
    P.boxes();
  }
  dl->AddCircleFilled(c, 3.f, IM_COL32(240, 238, 232, 220), 12);
  // the tool and the coordinate space beside the gadget, as Vue's swatches
  // say which gizmo is up
  char label[64];
  std::snprintf(label, sizeof label, "%s \xc2\xb7 %s", gizmo_mode_name(g_mode),
                gizmo_space_name(g_space));
  dl->AddText(ImVec2(c.x + 10.f, c.y + 8.f), IM_COL32(240, 238, 232, 170), label);
  dl->PopClipRect();
}

} // namespace studio
