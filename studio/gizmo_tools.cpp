// Geekatplay TerraForge - the deformer tools and Vue's Show Gizmos switch
// (manual p281: Display > Gizmos), drawn on the global tool row next to
// Move / Rotate / Scale as palette icons. The letters in the tooltips are
// the shortcuts.
#include "app.hpp"
#include "gizmo.hpp"
#include "i18n.hpp"
#include "toolbar_internal.hpp"
#include <imgui.h>

namespace studio {

void gizmo_deform_tools(App &a) {
  (void)a;
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
  tool_sep();
  bool &vis = gizmo_visible();
  if (tool_icon(Icon::Fit, "##gizmos",
                tr("Show gizmos  (Ctrl+G)\n\nShow the gizmo on the selected object in every view.\n"
                   "Off, objects are still selected and edited from Properties."),
                vis))
    vis = !vis;
}

} // namespace studio
