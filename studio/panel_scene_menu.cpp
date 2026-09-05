// Geekatplay TerraForge — the Objects tree's context menu.
#include "app.hpp"
#include "panel_scene_internal.hpp"
#include "i18n.hpp"
#include "icons.hpp"
#include "render_settings.hpp"
#include "undo.hpp"
#include <string>

namespace studio {

namespace {

void visibility_menu(App &a, SceneState &sc, int i, int kind, const char *title) {
  if (!ImGui::BeginMenu(title)) return;
  SceneObject &o = sc.objects[i];
  int cur = kind == 0 ? o.vis_viewport : o.vis_render;
  const char *labels[3] = {tr("om.vis.default"), tr("om.vis.on"), tr("om.vis.off")};
  for (int v = 0; v < 3; ++v)
    if (ImGui::MenuItem(labels[v], nullptr, cur == v)) {
      undo_push(a, tr("om.undo.visibility"));
      for (int k = 0; k < (int)sc.objects.size(); ++k)
        if (tree_is_selected(sc, k)) tree_set_state(sc, k, kind, v, false);
    }
  ImGui::EndMenu();
}

void fold(SceneState &sc, int i, bool on) {
  sc.objects[i].expanded = on;
  for (int c = 0; c < (int)sc.objects.size(); ++c)
    if (sc.objects[c].parent == i) fold(sc, c, on);
}

} // namespace

void tree_context_menu(App &a, SceneState &sc, int i) {
  TreeState &g = tree_state();
  if (!ImGui::BeginPopupContextItem("##ctx")) return;
  SceneObject &o = sc.objects[i];
  bool has_children = false;
  for (const auto &c : sc.objects)
    if (c.parent == i) { has_children = true; break; }
  ImGui::TextDisabled("%s", scene_type_name(o.type));
  ImGui::Separator();
  if (IconMenuItem(Icon::Object, tr("om.menu.rename"))) tree_begin_rename(sc, i);
  if (ImGui::MenuItem(tr("om.menu.duplicate"))) g.req_duplicate = i;
  ImGui::BeginDisabled(o.builtin && o.type != SceneObject::Camera);
  if (IconMenuItem(Icon::Trash, tr("om.menu.delete"))) {
    g.req_delete.clear();
    for (int k = 0; k < (int)sc.objects.size(); ++k)
      if (tree_is_selected(sc, k)) g.req_delete.push_back(k);
    if (g.req_delete.empty()) g.req_delete.push_back(i);
  }
  ImGui::EndDisabled();
  ImGui::Separator();
  if (IconMenuItem(Icon::Folder, tr("om.menu.group"))) g.req_group = true;
  if (o.parent >= 0 && IconMenuItem(Icon::Unlink, tr("om.menu.unparent"))) {
    undo_push(a, tr("om.undo.unparent"));
    for (int k = 0; k < (int)sc.objects.size(); ++k)
      if (tree_is_selected(sc, k)) sc.objects[k].parent = -1;
  }
  if (has_children && ImGui::MenuItem(tr("om.menu.set_root"))) g.root = i;
  if (ImGui::BeginMenu(tr("om.menu.move_to_layer"))) {
    for (int li = 0; li < (int)sc.layers.size(); ++li)
      if (ImGui::MenuItem(sc.layers[li].name.c_str(), nullptr, li == o.layer)) {
        undo_push(a, tr("om.undo.layer"));
        for (int k = 0; k < (int)sc.objects.size(); ++k)
          if (tree_is_selected(sc, k)) sc.objects[k].layer = li;
      }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  visibility_menu(a, sc, i, 0, tr("om.menu.viewport_vis"));
  visibility_menu(a, sc, i, 1, tr("om.menu.render_vis"));
  if (ImGui::MenuItem(o.enabled ? tr("om.menu.disable") : tr("om.menu.enable"))) {
    undo_push(a, tr("om.undo.enable"));
    bool on = !o.enabled;
    for (int k = 0; k < (int)sc.objects.size(); ++k)
      if (tree_is_selected(sc, k)) sc.objects[k].enabled = on;
  }
  if (o.type == SceneObject::Camera || o.type == SceneObject::Planet)
    ImGui::Separator();
  if (o.type == SceneObject::Camera &&
      IconMenuItem(Icon::Camera, tr("om.menu.look_through"))) {
    scene_active_camera() = i;
    scene_last_used_camera() = i;
    a.status = std::string(tr("om.status.looking_through")) + " " + o.name;
  }
  if (o.type == SceneObject::Planet &&
      IconMenuItem(Icon::Planet, tr("om.menu.fly_here"))) {
    scene_active_camera() = -1;
    renderer_camera_look_at(o.pos, o.planet.radius * 3.5f);
    a.status = std::string(tr("om.status.flying_to")) + " " + o.name;
  }
  ImGui::Separator();
  if (ImGui::MenuItem(tr("om.menu.select_children"))) tree_select_subtree(a, sc, i);
  if (ImGui::MenuItem(tr("om.menu.unfold_all"))) {
    for (int k = 0; k < (int)sc.objects.size(); ++k) sc.objects[k].expanded = true;
  }
  if (ImGui::MenuItem(tr("om.menu.fold_all"))) {
    for (int k = 0; k < (int)sc.objects.size(); ++k) sc.objects[k].expanded = false;
  }
  if (has_children) {
    if (ImGui::MenuItem(tr("om.menu.unfold_selected"))) fold(sc, i, true);
    if (ImGui::MenuItem(tr("om.menu.fold_selected"))) fold(sc, i, false);
  }
  ImGui::EndPopup();
}

} // namespace studio
