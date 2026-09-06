// Geekatplay TerraForge - what gizmo.cpp (hit-test, drag) and gizmo_draw.cpp
// (paint) share: the handle model, the drag in flight, the projection, the
// anchor and axis rules. Internal to the two; the public face is gizmo.hpp.
#pragma once
#include "app.hpp"
#include "gizmo.hpp"
#include "scene.hpp"
#include <imgui.h>

namespace studio {
namespace gizmo_detail {

extern const float HANDLE_PX;
extern const ImU32 AXIS_COL[3];

// The parts of the gadget. The universal gizmo shows the first four at
// once; a single-purpose tool shows only its own.
enum Part { P_NONE = -1, P_MOVE = 0, P_RING = 1, P_SCALE = 2, P_CENTRE = 3 };

// The handle under the pointer this frame, in the view that owns it.
struct Hot {
  int part = P_NONE;
  int axis = -1;
  int slot = -1;
};
extern Hot g_hot;

struct Drag {
  bool active = false;
  int axis = -1;
  int object = -1;
  GizmoMode mode = GizmoMode::None; // what the grabbed part does
  ImVec2 start_mouse{0, 0};
  ImVec2 dir_screen{1, 0};
  float px_per_unit = 1.f;
  float axis_world[3] = {1, 0, 0};  // the frame axis being dragged along
  float start_angle = 0.f;
  float sign = 1.f;
  float start_pos[3] = {0, 0, 0};
  float start_scl[3] = {1, 1, 1};
  float start_scale = 1.f;
  float start_rot[3] = {0, 0, 0};
  float start_extra = 0.f;
  float start_extra2 = 0.f;
};
extern Drag g_drag;
extern GizmoMode g_mode;
extern GizmoSpace g_space;
extern bool g_visible;

bool project(const float *mvp, const float p[3], ImVec2 origin, int w, int h, ImVec2 &out);
float len2(ImVec2 v);
bool anchor_of(const SceneObject &o, float out[3]);
int axis_mask(const SceneObject &o, GizmoMode m);
bool ring_mode(GizmoMode m);

// The three axes the gadget is drawn on, as unit world vectors: the world's,
// the object's own, or its parent's, per gizmo_space().
void gizmo_frame(const SceneObject &o, float axes[3][3]);

// The gadget's geometry in one view: where the anchor lands on screen, the
// world length of a handle, the axis masks of each part, and the frame.
struct Layout {
  ImVec2 c;
  float L = 0.f;
  float axes[3][3];
  int mask_move = 0, mask_rot = 0, mask_scl = 0;
  bool universal = false;
};
bool gizmo_layout(const SceneObject &o, const float *mvp, ImVec2 origin, int w, int h,
                  Layout &out);

// Where the scale box of an axis sits: past the arrow in the universal
// gadget, at the arrow's end in the Scale tool.
float scale_box_reach(const Layout &lay);
// The rings' radius as a fraction of a handle: inside the arrows in the
// universal gadget, so arrows and boxes stay visible past them.
float ring_reach(const Layout &lay);

} // namespace gizmo_detail
} // namespace studio
