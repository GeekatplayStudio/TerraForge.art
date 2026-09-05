// Geekatplay TerraForge - what gizmo.cpp (hit-test, drag) and gizmo_draw.cpp
// (paint) share: the drag in flight, the projection, the anchor and axis
// rules. Internal to the two; the public face is gizmo.hpp.
#pragma once
#include "app.hpp"
#include "gizmo.hpp"
#include "scene.hpp"
#include <imgui.h>

namespace studio {
namespace gizmo_detail {

extern const float HANDLE_PX;
extern const ImU32 AXIS_COL[3];

struct Drag {
  bool active = false;
  int axis = -1;
  int object = -1;
  GizmoMode mode = GizmoMode::None;
  ImVec2 start_mouse{0, 0};
  ImVec2 dir_screen{1, 0};
  float px_per_unit = 1.f;
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
extern bool g_visible;

bool project(const float *mvp, const float p[3], ImVec2 origin, int w, int h, ImVec2 &out);
float len2(ImVec2 v);
bool anchor_of(const SceneObject &o, float out[3]);
int axis_mask(const SceneObject &o, GizmoMode m);
bool ring_mode(GizmoMode m);

} // namespace gizmo_detail
} // namespace studio
