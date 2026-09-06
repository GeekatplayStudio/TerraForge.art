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

// Universal is Vue's gizmo: arrows, rings and boxes in one gadget, so an
// object is moved, turned and resized without changing tool. The single
// tools show one part each; the deformers are their own gadgets.
enum class GizmoMode { None = 0, Universal, Move, Rotate, Scale, Twist, Bend, Skew, Taper };
// Which axes the gadget is drawn on: the world's, the object's own, or its
// parent's - Cinema 4D's world/object coordinate switch, with the parent
// added because a child of a turned group is usually dragged along it.
enum class GizmoSpace { World = 0, Local, Parent };
// Vue's Display > Gizmos > Show Gizmos: one switch over every view.
bool &gizmo_visible();

// True while a handle is being dragged; which object and which axis
// (0..2, 3 = the uniform centre).
bool gizmo_dragging(int &object, int &axis);

const char *gizmo_mode_name(GizmoMode m);
const char *gizmo_space_name(GizmoSpace s);
// Which gadget the viewports show. Shared by every view, like the tool it is.
GizmoMode &gizmo_mode();
GizmoSpace &gizmo_space();

// Hit-test and drag. Call once per view, after the view's image has been
// submitted and BEFORE the camera input is read: returns true when the gizmo
// owns the mouse this frame, in which case the caller must not orbit, pan or
// select. `origin` is the top-left of the view image in screen coordinates.
bool gizmo_update(App &a, int slot, const RenderSettings::ViewConfig &vc,
                  ImVec2 origin, int w, int h, bool view_hovered);

// The deformer swatches and the Gizmos switch on the tool row (gizmo_tools.cpp).
void gizmo_deform_tools(App &a);

// Paint it. Call late, so it lands over everything else in the view.
void gizmo_draw(App &a, int slot, const RenderSettings::ViewConfig &vc,
                ImVec2 origin, int w, int h);

} // namespace studio
