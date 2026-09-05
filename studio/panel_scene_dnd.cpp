// Geekatplay TerraForge — drag and drop in the Objects tree, and the edits
// that change the array: move, duplicate, group, delete.
//
// Object order is the array order, so a reorder moves elements in
// scene().objects and every index that pointed at them must follow. That
// fix-up lives in one place, scene_move_object (scene.cpp), and this file
// only decides where an object goes.
#include "app.hpp"
#include "panel_scene_internal.hpp"
#include "i18n.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <algorithm>
#include <string>

namespace studio {

void tree_dnd_source(const SceneState &sc, int i) {
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
    ImGui::SetDragDropPayload("SCENE_OBJ", &i, sizeof(int));
    ImGui::TextUnformatted(sc.objects[i].name.c_str());
    if (ImGui::GetIO().KeyCtrl) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", tr("om.drag_copy"));
    }
    ImGui::EndDragDropSource();
  }
}

// The drop zone is split three ways: the top quarter inserts before, the
// bottom quarter after, the middle makes the row the parent. The insertion
// line and the coloured parent row are the C4D signposts.
void tree_dnd_target(const SceneState &sc, const TreeRow &r, ImVec2 mn,
                     ImVec2 mx) {
  TreeState &g = tree_state();
  if (!ImGui::BeginDragDropTarget()) return;
  const ImGuiPayload *pl = ImGui::AcceptDragDropPayload(
      "SCENE_OBJ", ImGuiDragDropFlags_AcceptBeforeDelivery |
                       ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
  if (pl && pl->DataSize == (int)sizeof(int)) {
    int src = *(const int *)pl->Data;
    float y = ImGui::GetIO().MousePos.y;
    float q = (mx.y - mn.y) * 0.25f;
    int mode = y < mn.y + q ? 1 : y > mx.y - q ? 2 : 3;
    if (g.flat || g.by_layer) mode = mode == 3 ? 3 : 0; // no reorder there
    bool legal = src >= 0 && src < (int)sc.objects.size() && src != r.idx &&
                 !scene_is_descendant(sc, r.idx, src);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (!legal) {
      // the prohibition sign
      ImVec2 c(mx.x - 12.f, (mn.y + mx.y) * 0.5f);
      dl->AddCircle(c, 5.f, theme::error(), 12, 1.5f);
      dl->AddLine(ImVec2(c.x - 3.5f, c.y - 3.5f), ImVec2(c.x + 3.5f, c.y + 3.5f),
                  theme::error(), 1.5f);
    } else if (mode == 1) {
      dl->AddRectFilled(ImVec2(mn.x, mn.y - 1.f), ImVec2(mx.x, mn.y + 1.f),
                        theme::accent());
    } else if (mode == 2) {
      dl->AddRectFilled(ImVec2(mn.x, mx.y - 1.f), ImVec2(mx.x, mx.y + 1.f),
                        theme::accent());
    } else if (mode == 3) {
      dl->AddRectFilled(mn, mx, theme::fade(theme::accent(), 0.3f));
    }
    if (pl->IsDelivery() && legal && mode) {
      g.drop_src = src;
      g.drop_target = r.idx;
      g.drop_mode = mode;
      g.drop_copy = ImGui::GetIO().KeyCtrl;
    }
  }
  ImGui::EndDragDropTarget();
}

int tree_duplicate(App &a, SceneState &sc, int i) {
  if (i < 0 || i >= (int)sc.objects.size()) return -1;
  undo_push(a, tr("om.undo.duplicate"));
  SceneObject c = sc.objects[i];
  c.vao = c.vbo = 0;
  c.gpu_dirty = c.vert_count > 0;
  c.inst.clear();
  c.builtin = false;
  c.name += std::string(" ") + tr("om.copy_suffix");
  sc.objects.push_back(c);
  int n = (int)sc.objects.size() - 1;
  // right after the original, as its sibling
  int to = i + 1;
  return scene_move_object(n, to, sc.objects[i].parent);
}

namespace {

// the position, in the array after `src` is taken out, that sits before or
// after `anchor`
int slot(int src, int anchor, bool after) {
  int a = anchor - (src < anchor ? 1 : 0);
  return after ? a + 1 : a;
}

void apply_drop(App &a, SceneState &sc) {
  TreeState &g = tree_state();
  int src = g.drop_src, tgt = g.drop_target, mode = g.drop_mode;
  g.drop_src = -1;
  if (src < 0 || src >= (int)sc.objects.size() || tgt < 0 ||
      tgt >= (int)sc.objects.size() || src == tgt)
    return;
  if (scene_is_descendant(sc, tgt, src)) return;
  if (g.drop_copy) {
    src = tree_duplicate(a, sc, src);
    if (src < 0) return;
    if (tgt >= src) tgt++; // the copy went in before the target
  } else {
    undo_push(a, mode == 3 ? tr("om.undo.reparent") : tr("om.undo.reorder"));
  }
  int moved = -1;
  if (mode == 3) {
    moved = scene_move_object(src, (int)sc.objects.size() - 1, tgt);
    if (moved >= 0) {
      int p = sc.objects[moved].parent;
      if (p >= 0) sc.objects[p].expanded = true;
    }
  } else {
    moved = scene_move_object(src, slot(src, tgt, mode == 2),
                              sc.objects[tgt].parent);
  }
  if (moved >= 0) {
    sc.selected = moved;
    sc.selection = {moved};
    a.scene_selection_serial++;
    g.seen_serial = a.scene_selection_serial;
  }
}

void apply_group(App &a, SceneState &sc) {
  TreeState &g = tree_state();
  g.req_group = false;
  std::vector<int> sel;
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (tree_is_selected(sc, i)) sel.push_back(i);
  if (sel.empty()) return;
  undo_push(a, tr("om.undo.group"));
  SceneObject grp;
  grp.type = SceneObject::Group;
  grp.name = tr("om.group_name");
  grp.parent = sc.objects[sel[0]].parent;
  sc.objects.push_back(grp);
  int gi = scene_move_object((int)sc.objects.size() - 1, sel[0], grp.parent);
  if (gi < 0) return;
  // the selection was fixed up by the move; only top-level picks move under
  // the group, so a parent and its child keep their own relationship
  std::vector<int> now;
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (i != gi && tree_is_selected(sc, i)) now.push_back(i);
  for (int i : now) {
    bool nested = false;
    for (int p : now)
      if (p != i && scene_is_descendant(sc, i, p)) { nested = true; break; }
    if (!nested) sc.objects[i].parent = gi;
  }
  sc.objects[gi].expanded = true;
  sc.selected = gi;
  sc.selection = {gi};
  a.scene_selection_serial++;
  g.seen_serial = a.scene_selection_serial;
}

void apply_delete(App &a, SceneState &sc) {
  TreeState &g = tree_state();
  std::vector<int> want = g.req_delete;
  g.req_delete.clear();
  // the whole subtree of each, builtins other than cameras excluded
  std::vector<int> del;
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (o.builtin && o.type != SceneObject::Camera) continue;
    for (int w : want)
      if (scene_is_descendant(sc, i, w)) { del.push_back(i); break; }
  }
  if (del.empty()) return;
  undo_push(a, tr("om.undo.delete"));
  std::sort(del.rbegin(), del.rend());
  for (int d : del) {
    auto fix = [&](int &v) {
      if (v > d) v--;
      else if (v == d) v = -1;
    };
    for (auto &o : sc.objects) {
      if (o.parent > d) o.parent--;
      else if (o.parent == d) o.parent = -1;
    }
    fix(scene_active_camera());
    fix(scene_last_used_camera());
    if (g.root == d) g.root = -1;
    else if (g.root > d) g.root--;
    sc.objects.erase(sc.objects.begin() + d);
  }
  if (sc.selected >= (int)sc.objects.size()) sc.selected = 0;
  sc.selection = {sc.selected};
  g.rename = -1;
  a.scene_selection_serial++;
  g.seen_serial = a.scene_selection_serial;
}

} // namespace

void tree_apply_edits(App &a, SceneState &sc) {
  TreeState &g = tree_state();
  if (g.drop_src >= 0) apply_drop(a, sc);
  if (g.req_duplicate >= 0) {
    int d = tree_duplicate(a, sc, g.req_duplicate);
    g.req_duplicate = -1;
    if (d >= 0) {
      sc.selected = d;
      sc.selection = {d};
      a.scene_selection_serial++;
      g.seen_serial = a.scene_selection_serial;
    }
  }
  if (g.req_group) apply_group(a, sc);
  if (!g.req_delete.empty()) apply_delete(a, sc);
}

} // namespace studio
