// Geekatplay TerraForge — the Objects panel: a Cinema 4D Object Manager.
//
// Three zones per row, as the C4D manual describes them: the tree with the
// name on the left, a narrow middle bar (layer colour, the two visibility
// dots, the enable tick, a look-through button on cameras) and the tags on
// the right. Above the tree: the search bar (filters, never selects), the
// type filter, the Show menu and the path bar that pairs with Set As Root.
//
// This file is the frame, the header strip and the tree walk. A row is
// painted in panel_scene_row.cpp, drag and drop and the structural edits
// live in panel_scene_dnd.cpp, the context menu in panel_scene_menu.cpp.
//
// The window keeps the ImGui ID "Outliner" while showing the label "Objects",
// so docking layouts saved before the rename still place it correctly.
#include "app.hpp"
#include "panel_scene_internal.hpp"
#include "panel_float.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "icons.hpp"
#include "render_settings.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cstring>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

Icon scene_type_icon(SceneObject::Type t) {
  switch (t) {
    case SceneObject::Terrain: return Icon::Terrain;
    case SceneObject::Water: return Icon::Water;
    case SceneObject::Sun: return Icon::Light;
    case SceneObject::Atmosphere: return Icon::Sky;
    case SceneObject::Camera: return Icon::Camera;
    case SceneObject::Group: return Icon::Folder;
    case SceneObject::Planet: return Icon::Planet;
    case SceneObject::InfiniteSurface: return Icon::Grid;
    case SceneObject::Light: return Icon::Light;
    default: return Icon::Mesh;
  }
}

const char *scene_type_name(SceneObject::Type t) {
  switch (t) {
    case SceneObject::Terrain: return tr("om.type.terrain");
    case SceneObject::Water: return tr("om.type.water");
    case SceneObject::Sun: return tr("om.type.sun");
    case SceneObject::Atmosphere: return tr("om.type.atmosphere");
    case SceneObject::Camera: return tr("om.type.camera");
    case SceneObject::Group: return tr("om.type.group");
    case SceneObject::Planet: return tr("om.type.planet");
    case SceneObject::InfiniteSurface: return tr("om.type.infinite");
    case SceneObject::Light: return tr("om.type.light");
    default: return tr("om.type.mesh");
  }
}

TreeState &tree_state() {
  static TreeState s;
  return s;
}

float tree_row_height() {
  static const float H[3] = {18.f, 22.f, 26.f};
  int k = tree_state().icon_size;
  return H[k < 0 ? 0 : k > 2 ? 2 : k];
}

float tree_indent() { return tree_row_height() * 0.72f; }

int tree_row_of(int i) {
  const auto &rows = tree_state().rows;
  for (int r = 0; r < (int)rows.size(); ++r)
    if (rows[r].idx == i) return r;
  return -1;
}

namespace {

bool name_matches(const SceneObject &o, const char *needle) {
  if (!needle[0]) return true;
  std::string a = o.name, b = needle;
  for (char &c : a) c = (char)tolower((unsigned char)c);
  for (char &c : b) c = (char)tolower((unsigned char)c);
  return a.find(b) != std::string::npos;
}

// The search keeps an object whose descendant matches, so the path to a hit
// stays readable.
bool subtree_matches(const SceneState &sc, int i, const char *needle) {
  if (name_matches(sc.objects[i], needle)) return true;
  for (int c = 0; c < (int)sc.objects.size(); ++c)
    if (sc.objects[c].parent == i && subtree_matches(sc, c, needle))
      return true;
  return false;
}

std::vector<int> children_of(const SceneState &sc, int parent) {
  TreeState &g = tree_state();
  std::vector<int> out;
  for (int c = 0; c < (int)sc.objects.size(); ++c) {
    const SceneObject &o = sc.objects[c];
    if (o.parent != parent) continue;
    if (!g.type_on[o.type]) continue;
    if (!subtree_matches(sc, c, g.search)) continue;
    out.push_back(c);
  }
  if (g.sort_name)
    std::stable_sort(out.begin(), out.end(), [&](int x, int y) {
      return sc.objects[x].name < sc.objects[y].name;
    });
  return out;
}

void walk(const SceneState &sc, int parent, int depth, uint64_t guides) {
  TreeState &g = tree_state();
  std::vector<int> kids = children_of(sc, parent);
  for (size_t k = 0; k < kids.size(); ++k) {
    int i = kids[k];
    TreeRow r;
    r.idx = i;
    r.depth = depth;
    r.last = k + 1 == kids.size();
    r.guides = guides;
    for (const auto &c : sc.objects)
      if (c.parent == i) { r.has_children = true; break; }
    g.rows.push_back(r);
    if (r.has_children && sc.objects[i].expanded && depth < 60)
      walk(sc, i, depth + 1,
           r.last ? guides : (guides | (uint64_t(1) << depth)));
  }
}

void build_rows(const SceneState &sc) {
  TreeState &g = tree_state();
  g.rows.clear();
  if (g.root >= (int)sc.objects.size()) g.root = -1;
  if (g.by_layer) {
    for (int li = 0; li < (int)sc.layers.size(); ++li) {
      TreeRow h;
      h.layer = li;
      g.rows.push_back(h);
      for (int i = 0; i < (int)sc.objects.size(); ++i) {
        const SceneObject &o = sc.objects[i];
        if (o.layer != li || !g.type_on[o.type] ||
            !name_matches(o, g.search))
          continue;
        TreeRow r;
        r.idx = i;
        r.depth = 1;
        g.rows.push_back(r);
      }
    }
  } else if (g.flat) {
    std::vector<int> all;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (g.type_on[sc.objects[i].type] &&
          name_matches(sc.objects[i], g.search))
        all.push_back(i);
    if (g.sort_name)
      std::stable_sort(all.begin(), all.end(), [&](int x, int y) {
        return sc.objects[x].name < sc.objects[y].name;
      });
    for (int i : all) {
      TreeRow r;
      r.idx = i;
      g.rows.push_back(r);
    }
  } else {
    walk(sc, g.root, 0, 0);
  }
}

void add_bar(App &a, SceneState &sc) {
  TreeState &g = tree_state();
  if (IconButton(Icon::Camera, "##addcam", tr("om.add_camera_tip"))) {
    undo_push(a, tr("om.undo.add_camera"));
    sc.selected = scene_add_camera();
    sc.selection = {sc.selected};
    a.scene_selection_serial++;
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Planet, "##addplanet", tr("om.add_planet_tip"))) {
    undo_push(a, tr("om.undo.add_planet"));
    sc.selected = scene_add_planet();
    sc.selection = {sc.selected};
    a.scene_selection_serial++;
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Grid, "##addinf", tr("om.add_infinite_tip"))) {
    undo_push(a, tr("om.undo.add_infinite"));
    int parent = -1;
    if (sc.selected >= 0 && sc.selected < (int)sc.objects.size()) {
      if (sc.objects[sc.selected].type == SceneObject::Planet)
        parent = sc.selected;
      else if (sc.objects[sc.selected].type == SceneObject::InfiniteSurface)
        parent = sc.objects[sc.selected].parent;
    }
    sc.selected = scene_add_infinite_surface(parent);
    sc.selection = {sc.selected};
    a.scene_selection_serial++;
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Mesh, "##import", tr("om.import_tip"))) {
    std::string p =
        dialog_open_file("Wavefront OBJ\0*.obj\0All files\0*.*\0", "obj");
    if (!p.empty()) {
      std::string err;
      int idx = scene_import_obj(p, err);
      if (idx >= 0) {
        sc.selected = idx;
        sc.selection = {idx};
        a.scene_selection_serial++;
      } else {
        a.status = std::string(tr("om.import_failed")) + ": " + err;
        log_error("scene", "OBJ import failed: " + err);
      }
    }
  }
  bool can_delete = sc.selected >= 0 && sc.selected < (int)sc.objects.size() &&
                    (!sc.objects[sc.selected].builtin ||
                     sc.objects[sc.selected].type == SceneObject::Camera);
  float bw = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f + 6.f;
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - bw);
  ImGui::BeginDisabled(!can_delete);
  if (IconButton(Icon::Trash, "##del", tr("om.delete_tip")))
    g.req_delete = {sc.selected};
  ImGui::EndDisabled();
}

// search, filter, Show menu; then the path bar
void header_strip(SceneState &sc) {
  TreeState &g = tree_state();
  const float fs = ImGui::GetFontSize();
  const float bw = fs + ImGui::GetStyle().FramePadding.y * 2.f + 6.f;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw * 3.f - 8.f);
  ImGui::InputTextWithHint("##search", tr("om.search_hint"), g.search,
                           sizeof g.search);
  ImGui::SameLine(0, 2);
  ImGui::BeginDisabled(!g.search[0]);
  if (ImGui::SmallButton(tr("om.search_x"))) g.search[0] = 0;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("om.search_clear"));
  ImGui::EndDisabled();
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Eye, "##filter", tr("om.filter_tip")))
    ImGui::OpenPopup("##filter_pop");
  if (ImGui::BeginPopup("##filter_pop")) {
    ImGui::TextDisabled("%s", tr("om.filter_title"));
    for (int t = 0; t <= SceneObject::Light; ++t) {
      ImGui::PushID(t);
      bool &on = g.type_on[t];
      if (IconButton(on ? Icon::Eye : Icon::EyeOff, "##t",
                     tr("om.filter_type_tip"), on)) {
        // Alt: hide this one, show the rest; Ctrl: show only this one
        if (ImGui::GetIO().KeyAlt) {
          for (bool &b : g.type_on) b = true;
          on = false;
        } else if (ImGui::GetIO().KeyCtrl) {
          for (bool &b : g.type_on) b = false;
          on = true;
        } else {
          on = !on;
        }
      }
      ImGui::SameLine();
      IconText(scene_type_icon((SceneObject::Type)t), fs,
               on ? theme::text() : theme::text_dim());
      ImGui::SameLine();
      ImGui::TextUnformatted(scene_type_name((SceneObject::Type)t));
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Object, "##show", tr("om.show_tip")))
    ImGui::OpenPopup("##show_pop");
  if (ImGui::BeginPopup("##show_pop")) {
    if (ImGui::MenuItem(tr("om.show.flat"), nullptr, g.flat)) {
      g.flat = !g.flat;
      if (g.flat) g.by_layer = false;
    }
    if (ImGui::MenuItem(tr("om.show.by_layer"), nullptr, g.by_layer)) {
      g.by_layer = !g.by_layer;
      if (g.by_layer) g.flat = false;
    }
    ImGui::MenuItem(tr("om.show.sort_name"), nullptr, &g.sort_name);
    ImGui::MenuItem(tr("om.show.tags"), nullptr, &g.show_tags);
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("om.show.icon_size"));
    if (ImGui::MenuItem(tr("om.show.small"), nullptr, g.icon_size == 0))
      g.icon_size = 0;
    if (ImGui::MenuItem(tr("om.show.medium"), nullptr, g.icon_size == 1))
      g.icon_size = 1;
    if (ImGui::MenuItem(tr("om.show.large"), nullptr, g.icon_size == 2))
      g.icon_size = 2;
    ImGui::EndPopup();
  }

  // ---- path bar: Home, Up one level, then the breadcrumb ----
  if (IconButton(Icon::Folder, "##home", tr("om.path_home"), g.root < 0))
    g.root = -1;
  ImGui::SameLine(0, 2);
  ImGui::BeginDisabled(g.root < 0);
  if (IconButton(Icon::Chevron, "##up", tr("om.path_up")))
    g.root = g.root >= 0 && g.root < (int)sc.objects.size()
                 ? sc.objects[g.root].parent
                 : -1;
  ImGui::EndDisabled();
  ImGui::SameLine(0, 6);
  std::vector<int> chain;
  for (int i = g.root, guard = (int)sc.objects.size();
       i >= 0 && i < (int)sc.objects.size() && guard-- > 0;
       i = sc.objects[i].parent)
    chain.push_back(i);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::text_dim());
  ImGui::TextUnformatted(tr("om.path_scene"));
  ImGui::PopStyleColor();
  for (int k = (int)chain.size() - 1; k >= 0; --k) {
    ImGui::SameLine(0, 4);
    IconText(Icon::Chevron, fs * 0.8f, theme::text_dim());
    ImGui::SameLine(0, 4);
    ImGui::PushID(chain[k]);
    if (ImGui::SmallButton(sc.objects[chain[k]].name.c_str()))
      g.root = chain[k];
    ImGui::PopID();
  }
}

void layer_header_row(SceneState &sc, const TreeRow &r) {
  const float h = tree_row_height(), fs = ImGui::GetFontSize();
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  float w = ImGui::GetContentRegionAvail().x;
  ImGui::PushID(1000000 + r.layer);
  ImGui::InvisibleButton("##layer", ImVec2(w > 1.f ? w : 1.f, h));
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h),
                    theme::shade(theme::panel_bg(), 0.85f));
  SceneLayer &L = sc.layers[r.layer];
  ImU32 c = ImGui::ColorConvertFloat4ToU32(
      ImVec4(L.color[0], L.color[1], L.color[2], 1.f));
  float s = fs * 0.6f;
  dl->AddRectFilled(ImVec2(p0.x + 6.f, p0.y + (h - s) * 0.5f),
                    ImVec2(p0.x + 6.f + s, p0.y + (h + s) * 0.5f), c);
  dl->AddText(ImVec2(p0.x + 12.f + s, p0.y + (h - fs) * 0.5f),
              L.visible ? theme::text() : theme::text_dim(), L.name.c_str());
  if (ImGui::IsItemClicked()) L.visible = !L.visible;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("om.layer_toggle_tip"));
  ImGui::PopID();
}

// Keys when the tree has focus and nothing is being typed: arrows move the
// selection, Return / F2 rename, Delete deletes.
void keyboard(App &a, SceneState &sc) {
  TreeState &g = tree_state();
  if (g.rename >= 0 || !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ||
      ImGui::IsAnyItemActive())
    return;
  int row = tree_row_of(sc.selected);
  int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow)  ? 1
             : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1
                                                     : 0;
  if (step && !g.rows.empty()) {
    int r = row < 0 ? 0 : row + step;
    while (r >= 0 && r < (int)g.rows.size() && g.rows[r].idx < 0) r += step;
    if (r >= 0 && r < (int)g.rows.size())
      tree_select(a, sc, g.rows[r].idx, false, ImGui::GetIO().KeyShift);
  }
  if (row >= 0 && sc.selected < (int)sc.objects.size()) {
    if (ImGui::IsKeyPressed(ImGuiKey_F2) || ImGui::IsKeyPressed(ImGuiKey_Enter))
      tree_begin_rename(sc, sc.selected);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
      g.req_delete.clear();
      for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (tree_is_selected(sc, i)) g.req_delete.push_back(i);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) sc.objects[sc.selected].expanded = true;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) sc.objects[sc.selected].expanded = false;
  }
}

} // namespace

// The layer list, drawn inside Properties > Scene.
void scene_layers_ui(App &a) {
  SceneState &sc = scene();
  int erase = -1;
  for (int li = 0; li < (int)sc.layers.size(); ++li) {
    ImGui::PushID(li);
    studio::Checkbox("##vis", &sc.layers[li].visible);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("om.layer_hide_tip"));
    ImGui::SameLine();
    ImGui::ColorEdit3("##col", sc.layers[li].color,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    char buf[64];
    snprintf(buf, sizeof buf, "%s", sc.layers[li].name.c_str());
    ImGui::SetNextItemWidth(-(ImGui::GetFontSize() + 12.f));
    if (ImGui::InputText("##name", buf, sizeof buf)) sc.layers[li].name = buf;
    ImGui::SameLine();
    ImGui::BeginDisabled(li == 0); // the Default layer always exists
    if (IconButton(Icon::Trash, "##x",
                   li == 0 ? tr("om.layer_default_tip") : tr("om.layer_remove_tip")))
      erase = li;
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (erase > 0) {
    undo_push(a, tr("om.undo.remove_layer"));
    for (auto &o : sc.objects) {
      if (o.layer == erase) o.layer = 0;
      else if (o.layer > erase) o.layer--;
    }
    sc.layers.erase(sc.layers.begin() + erase);
  }
  if (ImGui::SmallButton(tr("om.layer_add"))) {
    SceneLayer l{std::string(tr("om.layer_name")) + " " +
                     std::to_string(sc.layers.size()),
                 true};
    scene_layer_default_color((int)sc.layers.size(), l.color);
    sc.layers.push_back(l);
  }
  ImGui::TextDisabled("%s", tr("om.layer_hint"));
}

void draw_panel_scene(App &a) {
  panel_float_prepare(a, "Objects###Outliner");
  if (!ImGui::Begin("Objects###Outliner")) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Objects###Outliner");
  SceneState &sc = scene();
  TreeState &g = tree_state();

  // A selection made elsewhere (viewport pick, an op) replaces ours.
  if (g.seen_serial != a.scene_selection_serial) {
    g.seen_serial = a.scene_selection_serial;
    sc.selection = {sc.selected};
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) g.paint_kind = -1;

  add_bar(a, sc);
  header_strip(sc);
  ImGui::Separator();

  build_rows(sc);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
  if (ImGui::BeginChild("##tree", ImVec2(0, 0))) {
    for (int r = 0; r < (int)g.rows.size(); ++r) {
      if (g.rows[r].idx < 0) layer_header_row(sc, g.rows[r]);
      else tree_row_draw(a, sc, g.rows[r], r);
    }
    keyboard(a, sc);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();

  tree_apply_edits(a, sc);
  ImGui::End();
}

} // namespace studio
