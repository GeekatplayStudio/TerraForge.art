// Geekatplay TerraForge — the Objects panel: the scene tree.
//
// Cinema 4D's Object Manager is the model. One row per object, the hierarchy
// carried by indentation, and nothing on a row but what you need in order to
// *find* something: its kind, its name, and whether it is visible. Everything
// else — layer, transform, material, lens — belongs to the Attribute Manager,
// which is our Properties editor. A list that also tries to be an editor ends
// up too narrow to do either job.
//
// The window keeps the ImGui ID "Outliner" while showing the label "Objects",
// so docking layouts saved before the rename still place it correctly.
#include "app.hpp"
#include "panel_float.hpp"
#include "console.hpp"
#include "icons.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <cstring>
#include <functional>
#include <imgui.h>
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
    default: return Icon::Mesh;
  }
}

const char *scene_type_name(SceneObject::Type t) {
  switch (t) {
    case SceneObject::Terrain: return "Terrain";
    case SceneObject::Water: return "Water";
    case SceneObject::Sun: return "Sun";
    case SceneObject::Atmosphere: return "Atmosphere";
    case SceneObject::Camera: return "Camera";
    case SceneObject::Group: return "Group";
    case SceneObject::Planet: return "Planet";
    case SceneObject::InfiniteSurface: return "Infinite terrain layer";
    default: return "Mesh";
  }
}

namespace {

int g_rename = -1;      // row being renamed in place, -1 = none
bool g_rename_focus = false;
char g_rename_buf[80] = {0};

// A glyph that is also a button, with no frame around it: a row of these reads
// as a row of state, not as a row of controls competing with the name.
bool row_icon(const char *id, Icon ic, bool on, const char *tip) {
  const float s = ImGui::GetFontSize();
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool hit = ImGui::InvisibleButton(id, ImVec2(s, s));
  ImU32 col = ImGui::IsItemHovered() ? theme::accent()
                                     : (on ? theme::text() : theme::text_dim());
  icon_draw(ImGui::GetWindowDrawList(), ic,
            ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.82f, col);
  if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  return hit;
}

// True when `node` is `root` or sits under it. Dropping a parent onto its own
// descendant would cut the branch loose from the tree, so the drop is refused.
// The walk is bounded: a corrupt parent chain must not hang the UI.
bool is_descendant(const SceneState &sc, int node, int root) {
  int guard = (int)sc.objects.size() + 1;
  for (int i = node; i >= 0 && guard-- > 0; i = sc.objects[i].parent)
    if (i == root) return true;
  return false;
}

void add_bar(App &a, SceneState &sc, int &delete_idx) {
  if (IconButton(Icon::Camera, "##addcam", "Add camera\n\n"
                 "Inherits the lens, exposure, film and render settings\n"
                 "of the last camera you used.")) {
    undo_push(a, "Add camera");
    sc.selected = scene_add_camera();
    a.scene_selection_serial++;
    a.status = "added " + sc.objects[sc.selected].name;
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Planet, "##addplanet", "Add planet\n\n"
                 "A whole procedural world, generated on the GPU from its\n"
                 "parameters - planets cost no memory, add as many as you\n"
                 "like. Double-click one in the list to fly to it.")) {
    undo_push(a, "Add planet");
    sc.selected = scene_add_planet();
    a.scene_selection_serial++;
    a.status = "added " + sc.objects[sc.selected].name +
               " - double-click it to fly there";
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Grid, "##addinf", "Add infinite terrain layer\n\n"
                 "An endless procedural terrain layer. Added to the selected\n"
                 "planet it shapes that planet's surface; at the root it\n"
                 "extends the home terrain to the horizon. Layers stack.")) {
    undo_push(a, "Add infinite terrain");
    int parent = -1;
    if (sc.selected >= 0 && sc.selected < (int)sc.objects.size()) {
      if (sc.objects[sc.selected].type == SceneObject::Planet)
        parent = sc.selected;
      else if (sc.objects[sc.selected].type == SceneObject::InfiniteSurface)
        parent = sc.objects[sc.selected].parent;
    }
    sc.selected = scene_add_infinite_surface(parent);
    a.scene_selection_serial++;
    a.status = "added " + sc.objects[sc.selected].name;
  }
  ImGui::SameLine(0, 2);
  if (IconButton(Icon::Mesh, "##import", "Import a 3D object (Wavefront OBJ)")) {
    std::string p =
        dialog_open_file("Wavefront OBJ\0*.obj\0All files\0*.*\0", "obj");
    if (!p.empty()) {
      std::string err;
      int idx = scene_import_obj(p, err);
      if (idx >= 0) {
        sc.selected = idx;
        a.scene_selection_serial++;
        a.status = "imported " + sc.objects[idx].name;
      } else {
        a.status = "IMPORT FAILED: " + err;
        log_error("scene", "OBJ import failed: " + err);
      }
    }
  }

  // Delete sits apart from the four that create, at the right edge, so the
  // destructive button is never the one next to the one you meant.
  bool can_delete = sc.selected >= 0 && sc.selected < (int)sc.objects.size() &&
                    (!sc.objects[sc.selected].builtin ||
                     sc.objects[sc.selected].type == SceneObject::Camera);
  float bw = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.f + 6.f;
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - bw);
  ImGui::BeginDisabled(!can_delete);
  if (IconButton(Icon::Trash, "##del", "Delete the selected object"))
    delete_idx = sc.selected;
  ImGui::EndDisabled();
}

} // namespace

// The layer list. It lives in Properties > Scene rather than here: layers are
// a property of the scene, they are edited rarely, and giving each row of the
// tree a layer combo cost a third of the panel's width for something almost
// nobody changes twice.
void scene_layers_ui(App &a) {
  SceneState &sc = scene();
  int erase = -1;
  for (int li = 0; li < (int)sc.layers.size(); ++li) {
    ImGui::PushID(li);
    studio::Checkbox("##vis", &sc.layers[li].visible);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Hide every object on this layer");
    ImGui::SameLine();
    char buf[64];
    snprintf(buf, sizeof buf, "%s", sc.layers[li].name.c_str());
    ImGui::SetNextItemWidth(-(ImGui::GetFontSize() + 12.f));
    if (ImGui::InputText("##name", buf, sizeof buf)) sc.layers[li].name = buf;
    ImGui::SameLine();
    ImGui::BeginDisabled(li == 0); // the Default layer always exists
    if (row_icon("##x", Icon::Trash, false,
                 li == 0 ? "The Default layer cannot be removed"
                         : "Remove this layer (its objects move to Default)"))
      erase = li;
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (erase > 0) {
    undo_push(a, "Remove layer");
    for (auto &o : sc.objects) {
      if (o.layer == erase) o.layer = 0;
      else if (o.layer > erase) o.layer--;
    }
    sc.layers.erase(sc.layers.begin() + erase);
  }
  if (ImGui::SmallButton("+ add layer"))
    sc.layers.push_back({"Layer " + std::to_string(sc.layers.size()), true});
  ImGui::TextDisabled("Put an object on a layer from the Objects panel:\n"
                      "right-click its row > Move to layer.");
}

void draw_panel_scene(App &a) {
  panel_float_prepare(a, "Objects###Outliner");
  if (!ImGui::Begin("Objects###Outliner")) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Objects###Outliner");
  SceneState &sc = scene();
  int delete_idx = -1;
  add_bar(a, sc, delete_idx);
  ImGui::Separator();

  int drag_child = -1, drag_parent = -2; // -2 = no drop this frame

  std::function<void(int, int)> draw_row = [&](int i, int depth) {
    SceneObject &o = sc.objects[i];
    ImGui::PushID(i);
    const float fs = ImGui::GetFontSize();
    const float step = fs * 0.9f;
    bool has_children = false;
    for (const auto &c : sc.objects)
      if (c.parent == i) { has_children = true; break; }
    bool active_cam =
        o.type == SceneObject::Camera && scene_active_camera() == i;

    // The row is one full-width selectable with the glyphs drawn over it, so
    // clicking anywhere on the line selects — a two-pixel gap between widgets
    // that swallows the click is the classic outliner annoyance.
    ImVec2 row0 = ImGui::GetCursorPos();
    if (ImGui::Selectable("##row", sc.selected == i,
                          ImGuiSelectableFlags_AllowOverlap |
                              ImGuiSelectableFlags_AllowDoubleClick)) {
      sc.selected = i;
      a.scene_selection_serial++;
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (o.type == SceneObject::Camera) {
          scene_active_camera() = i;
          scene_last_used_camera() = i;
          a.status = "looking through " + o.name;
        } else if (o.type == SceneObject::Planet) {
          scene_active_camera() = -1;
          renderer_camera_look_at(o.pos, o.planet.radius * 3.5f);
          a.status = "flying to " + o.name;
        } else {
          g_rename = i;
          g_rename_focus = true;
          snprintf(g_rename_buf, sizeof g_rename_buf, "%s", o.name.c_str());
        }
      }
    }
    ImVec2 after = ImGui::GetCursorPos();

    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload("SCENE_OBJ", &i, sizeof(int));
      ImGui::TextUnformatted(o.name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("SCENE_OBJ")) {
        drag_child = *(const int *)pl->Data;
        drag_parent = i;
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("##ctx")) {
      sc.selected = i;
      ImGui::TextDisabled("%s", scene_type_name(o.type));
      ImGui::Separator();
      if (IconMenuItem(Icon::Object, "Rename")) {
        g_rename = i;
        g_rename_focus = true;
        snprintf(g_rename_buf, sizeof g_rename_buf, "%s", o.name.c_str());
      }
      if (o.type == SceneObject::Camera &&
          IconMenuItem(Icon::Camera, "Look through this camera")) {
        scene_active_camera() = i;
        scene_last_used_camera() = i;
      }
      if (o.type == SceneObject::Planet &&
          IconMenuItem(Icon::Planet, "Fly the camera here")) {
        scene_active_camera() = -1;
        renderer_camera_look_at(o.pos, o.planet.radius * 3.5f);
      }
      if (o.parent >= 0 && IconMenuItem(Icon::Unlink, "Unparent")) {
        undo_push(a, "Unparent object");
        o.parent = -1;
      }
      if (ImGui::BeginMenu("Move to layer")) {
        for (int li = 0; li < (int)sc.layers.size(); ++li)
          if (ImGui::MenuItem(sc.layers[li].name.c_str(), nullptr,
                              li == o.layer))
            o.layer = li;
        ImGui::EndMenu();
      }
      ImGui::Separator();
      ImGui::BeginDisabled(o.builtin && o.type != SceneObject::Camera);
      if (IconMenuItem(Icon::Trash, "Delete")) delete_idx = i;
      ImGui::EndDisabled();
      ImGui::EndPopup();
    }

    // ---- the glyphs, drawn back over the selectable ----
    ImGui::SetCursorPos(ImVec2(row0.x + step * depth, row0.y));
    if (has_children) {
      if (row_icon("##exp", o.expanded ? Icon::ChevronDown : Icon::Chevron,
                   true, nullptr))
        o.expanded = !o.expanded;
    } else {
      ImGui::Dummy(ImVec2(fs, fs));
    }
    ImGui::SameLine(0, 3);
    bool shown = sc.object_visible(o);
    IconText(scene_type_icon(o.type), fs,
             active_cam ? theme::accent()
                        : theme::fade(theme::text_dim(), shown ? 1.f : 0.45f));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", scene_type_name(o.type));
    ImGui::SameLine(0, 6);

    const float eye_x = ImGui::GetContentRegionMax().x - fs - 2.f;
    if (g_rename == i) {
      ImGui::SetNextItemWidth(eye_x - ImGui::GetCursorPosX() - 4.f);
      if (g_rename_focus) {
        ImGui::SetKeyboardFocusHere();
        g_rename_focus = false;
      }
      if (ImGui::InputText("##rn", g_rename_buf, sizeof g_rename_buf,
                           ImGuiInputTextFlags_EnterReturnsTrue) ||
          (!ImGui::IsItemActive() && !ImGui::IsItemFocused())) {
        if (g_rename_buf[0]) {
          undo_push(a, "Rename object");
          o.name = g_rename_buf;
        }
        g_rename = -1;
      }
    } else {
      ImU32 c = active_cam ? theme::accent()
                           : theme::fade(theme::text(), shown ? 1.f : 0.45f);
      ImGui::PushStyleColor(ImGuiCol_Text, c);
      ImGui::TextUnformatted(o.name.c_str());
      ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(eye_x);
    if (row_icon("##vis", o.visible ? Icon::Eye : Icon::EyeOff, o.visible,
                 o.visible ? "Visible - click to hide"
                           : "Hidden - click to show"))
      o.visible = !o.visible;

    // Back to the line below the row. ImGui grows a window only when an item
    // is submitted, so moving the cursor without one leaves the content size
    // short by a row - hence the zero-size Dummy.
    ImGui::SetCursorPos(after);
    ImGui::Dummy(ImVec2(0.f, 0.f));
    ImGui::PopID();

    if (has_children && o.expanded)
      for (int c = 0; c < (int)sc.objects.size(); ++c)
        if (sc.objects[c].parent == i) draw_row(c, depth + 1);
  };

  if (ImGui::BeginChild("##tree", ImVec2(0, 0))) {
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (sc.objects[i].parent < 0) draw_row(i, 0);
  }
  ImGui::EndChild();

  if (drag_parent != -2 && drag_child >= 0 &&
      drag_child < (int)sc.objects.size() && drag_child != drag_parent &&
      !is_descendant(sc, drag_parent, drag_child)) {
    undo_push(a, "Reparent object");
    sc.objects[drag_child].parent = drag_parent;
    sc.objects[drag_parent].expanded = true;
  }

  if (delete_idx >= 0) {
    undo_push(a, "Delete object");
    auto fix = [&](int &v) {
      if (v > delete_idx) v--;
      else if (v == delete_idx) v = -1;
    };
    for (auto &o : sc.objects) {
      if (o.parent > delete_idx) o.parent--;
      else if (o.parent == delete_idx) o.parent = -1;
    }
    fix(scene_active_camera());
    fix(scene_last_used_camera());
    sc.objects.erase(sc.objects.begin() + delete_idx);
    if (sc.selected >= (int)sc.objects.size()) sc.selected = 0;
    a.scene_selection_serial++;
  }
  ImGui::End();
}

} // namespace studio
