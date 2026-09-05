// Geekatplay TerraForge - the deformer gizmo swatches and Vue's Show Gizmos
// switch (manual p281: Display > Gizmos), drawn on the global tool row next
// to Move / Rotate / Scale. Text swatches; the letters are the shortcuts.
#include "app.hpp"
#include "gizmo.hpp"
#include <imgui.h>

namespace studio {

void gizmo_deform_tools(App &a, float h) {
  (void)a;
  GizmoMode &gm = gizmo_mode();
  struct T {
    const char *label;
    GizmoMode m;
    const char *tip;
  } tools[] = {
      {"Tw", GizmoMode::Twist,
       "Twist tool  (T)\n\nDrag a dashed ring to twist the object about that axis:\n"
       "the far end turns, the base stays."},
      {"Bd", GizmoMode::Bend,
       "Bend tool  (B)\n\nDrag a half ring to curl the object about that axis, from its base."},
      {"Sk", GizmoMode::Skew,
       "Skew tool  (K)\n\nDrag a diamond to slide the top of the object along that axis."},
      {"Tp", GizmoMode::Taper,
       "Taper tool  (J)\n\nDrag the centre to narrow or widen the top against the base."}};
  for (const T &t : tools) {
    bool on = gm == t.m;
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
    if (ImGui::Button(t.label, ImVec2(h, h))) gm = on ? GizmoMode::None : t.m;
    if (on) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.tip);
    ImGui::SameLine(0, 2);
  }
  ImGui::SameLine(0, 6);
  bool &vis = gizmo_visible();
  studio::Checkbox("Gizmos", &vis);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Show the gizmo on the selected object in every view (Ctrl+G).\n"
                      "Off, objects are still selected and edited from Properties.");
  ImGui::SameLine(0, 2);
}

} // namespace studio
