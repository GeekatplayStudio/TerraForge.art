// Geekatplay TerraForge - the viewport transform gizmo.
//
// One gadget, in every view, for every object that has somewhere to be. It
// works in screen space: the handles are the object's world axes projected
// with the same view-projection the frame was drawn with, and a drag moves
// the object so that its projection follows the pointer. That is what makes
// it behave identically in a perspective view and in an orthographic one,
// with no special cases and no depth buffer read-back.
//
// The numbers it writes are the same ones the Properties transform block
// types into, so dragging here and typing there are two doors into one room.
#pragma once
#include "render_settings.hpp"
#include <imgui.h>

namespace studio {

struct App;

enum class GizmoMode { None = 0, Move, Rotate, Scale };

// Which gadget the viewports show. Shared by every view, like the tool it is.
GizmoMode &gizmo_mode();

// Hit-test and drag. Call once per view, after the view's image has been
// submitted and BEFORE the camera input is read: returns true when the gizmo
// owns the mouse this frame, in which case the caller must not orbit, pan or
// select. `origin` is the top-left of the view image in screen coordinates.
bool gizmo_update(App &a, int slot, const RenderSettings::ViewConfig &vc,
                  ImVec2 origin, int w, int h, bool view_hovered);

// Paint it. Call late, so it lands over everything else in the view.
void gizmo_draw(App &a, int slot, const RenderSettings::ViewConfig &vc,
                ImVec2 origin, int w, int h);

} // namespace studio
