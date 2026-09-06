// Geekatplay TerraForge - the transform modes, the deformer tools and Vue's
// Show Gizmos switch (manual p281: Display > Gizmos), as palette groups.
// They are drawn twice: down the left tool column, and inside the selected
// object's Properties under "Transform tool" - because a mode that acts on
// the selected object belongs with the object, not on the application's
// title row. The letters in the tooltips are the shortcuts.
#include "app.hpp"
#include "gizmo.hpp"
#include "i18n.hpp"
#include "toolbar_internal.hpp"
#include <imgui.h>

namespace studio {

void gizmo_transform_tools() {
  GizmoMode &gm = gizmo_mode();
  auto tool = [&](Icon ic, const char *id, GizmoMode m, const char *tip) {
    if (tool_icon(ic, id, tip, gm == m)) gm = gm == m ? GizmoMode::None : m;
  };
  tool(Icon::Transform, "##guniv", GizmoMode::Universal,
       tr("Transform gizmo\n\nArrows move, rings rotate, boxes scale - all on\n"
          "one gadget, so nothing needs a tool change. Hover a\n"
          "handle to see it light up; drag it."));
  tool(Icon::Move, "##gmove", GizmoMode::Move,
       tr("Move tool  (W)\n\nDrag an axis in any viewport to move the\n"
          "selected object. The same numbers are in Properties,\n"
          "in metres."));
  tool(Icon::Rotate, "##grot", GizmoMode::Rotate,
       tr("Rotate tool  (E)\n\nDrag a ring to turn the selected object.\n"
          "Heading, pitch and bank, in degrees."));
  tool(Icon::Scale, "##gscl", GizmoMode::Scale,
       tr("Scale tool  (R)\n\nDrag an axis box to squeeze one axis, or\n"
          "the centre box to resize the whole object."));
}

void gizmo_deform_tools() {
  GizmoMode &gm = gizmo_mode();
  struct T {
    Icon icon;
    const char *id;
    GizmoMode m;
    const char *tip;
  } tools[] = {
      {Icon::Twist, "##gtwist", GizmoMode::Twist,
       "Twist tool  (T)\n\nDrag a dashed ring to twist the object about that axis:\n"
       "the far end turns, the base stays."},
      {Icon::Bend, "##gbend", GizmoMode::Bend,
       "Bend tool  (B)\n\nDrag a half ring to curl the object about that axis, from its base."},
      {Icon::Skew, "##gskew", GizmoMode::Skew,
       "Skew tool  (K)\n\nDrag a diamond to slide the top of the object along that axis."},
      {Icon::Taper, "##gtaper", GizmoMode::Taper,
       "Taper tool  (J)\n\nDrag the centre to narrow or widen the top against the base."}};
  for (const T &t : tools) {
    bool on = gm == t.m;
    if (tool_icon(t.icon, t.id, tr(t.tip), on)) gm = on ? GizmoMode::None : t.m;
  }
}

// Which axes the gadget stands on: the world's, the object's own, or its
// parent's. Cinema 4D's world/object switch, with the parent added.
void gizmo_space_tools() {
  GizmoSpace &sp = gizmo_space();
  struct S {
    Icon icon;
    const char *id;
    GizmoSpace s;
    const char *tip;
  } spaces[] = {
      {Icon::World, "##spworld", GizmoSpace::World,
       "World coordinates\n\nThe gizmo's axes are the world's X, Y and Z."},
      {Icon::Object, "##splocal", GizmoSpace::Local,
       "Object coordinates\n\nThe gizmo's axes turn with the object, so a\n"
       "drag along X slides it along its own X."},
      {Icon::Group, "##spparent", GizmoSpace::Parent,
       "Parent coordinates\n\nThe axes of the object's parent, for moving a\n"
       "child along a turned group."}};
  for (const S &s : spaces)
    if (tool_icon(s.icon, s.id, tr(s.tip), sp == s.s)) sp = s.s;
}

void gizmo_visible_tool() {
  bool &vis = gizmo_visible();
  if (tool_icon(Icon::Fit, "##gizmos",
                tr("Show gizmos  (Ctrl+G)\n\nShow the gizmo on the selected object in every view.\n"
                   "Off, objects are still selected and edited from Properties."),
                vis))
    vis = !vis;
}

// kept for callers that still take an App: the whole set in one row
void gizmo_deform_tools(App &a) {
  (void)a;
  gizmo_deform_tools();
  tool_sep();
  gizmo_visible_tool();
}

} // namespace studio
