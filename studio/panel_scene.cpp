// Geekatplay TerraForge â€” Scene panel: object list (Sun, Water, Terrain,
// imported meshes...), layers with visibility/grouping, OBJ import,
// per-object quick properties.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <functional>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

static const char *type_icon(SceneObject::Type t) {
  switch (t) {
    case SceneObject::Terrain: return "[T]";
    case SceneObject::Water: return "[W]";
    case SceneObject::Sun: return "[S]";
    case SceneObject::Atmosphere: return "[A]";
    case SceneObject::Camera: return "[C]";
    case SceneObject::Group: return "[+]";
    case SceneObject::Planet: return "[O]";
    case SceneObject::InfiniteSurface: return "[~]";
    default: return "[M]";
  }
}

[[maybe_unused]] static void object_quick_props(App &a, SceneObject &o) {
  RenderSettings &rs = render_settings();
  ImGui::SeparatorText(o.name.c_str());
  switch (o.type) {
    case SceneObject::Sun:
      ImGui::SliderFloat("Azimuth", &rs.sun_azimuth, 0, 360, "%.0f\xC2\xB0");
      ImGui::SliderFloat("Altitude", &rs.sun_altitude, 1, 89, "%.0f\xC2\xB0");
      ImGui::ColorEdit3("Color", rs.sun_color);
      ImGui::SliderFloat("Intensity", &rs.sun_intensity, 0.2f, 8.f);
      ImGui::TextDisabled("more: Environment > Sun");
      break;
    case SceneObject::Water:
      ImGui::SliderFloat("Level", &rs.water_level, 0.f, 1.f);
      ImGui::ColorEdit3("Deep", rs.water_deep_color);
      ImGui::ColorEdit3("Shallow", rs.water_shallow_color);
      ImGui::TextDisabled("more: Environment > Water");
      break;
    case SceneObject::Atmosphere:
      ImGui::Combo("Fog type", &rs.fog_type, "Off\0Haze\0Fog\0Pollution\0");
      ImGui::SliderFloat("Density", &rs.fog_density, 0.f, 6.f);
      ImGui::TextDisabled("more: Environment > Fog / Atmosphere");
      break;
    case SceneObject::Terrain:
      ImGui::SliderFloat("Height scale", &rs.height_scale, 0.02f, 0.8f);
      ImGui::TextDisabled("material: Scene Materials panel");
      ImGui::TextDisabled("shape: the node graph");
      break;
    case SceneObject::Mesh:
      ImGui::DragFloat("Position X", &o.pos[0], 0.005f, -0.5f, 1.5f);
      ImGui::DragFloat("Position Z", &o.pos[2], 0.005f, -0.5f, 1.5f);
      ImGui::DragFloat("Height", &o.pos[1], 0.005f, -0.5f, 2.f);
      ImGui::DragFloat("Scale", &o.scale, 0.002f, 0.005f, 1.f);
      ImGui::SliderFloat("Rotation", &o.yaw, -180.f, 180.f, "%.0f\xC2\xB0");
      ImGui::ColorEdit3("Color", o.color);
      ImGui::TextDisabled("%d triangles", o.vert_count / 3);
      break;
  }
}

void draw_panel_scene(App &a) {
  if (!ImGui::Begin("Outliner")) {
    ImGui::End();
    return;
  }
  SceneState &sc = scene();

  // ---- layers ----
  ImGui::SeparatorText("Layers");
  for (int li = 0; li < (int)sc.layers.size(); ++li) {
    ImGui::PushID(li);
    studio::Checkbox("##vis", &sc.layers[li].visible);
    ImGui::SameLine();
    char buf[64];
    snprintf(buf, sizeof buf, "%s", sc.layers[li].name.c_str());
    ImGui::SetNextItemWidth(-46);
    if (ImGui::InputText("##name", buf, sizeof buf)) sc.layers[li].name = buf;
    ImGui::SameLine();
    ImGui::BeginDisabled(li == 0); // Default layer stays
    if (ImGui::SmallButton("x")) {
      // move objects back to Default, drop the layer
      for (auto &o : sc.objects)
        if (o.layer == li) o.layer = 0;
        else if (o.layer > li) o.layer--;
      sc.layers.erase(sc.layers.begin() + li);
      ImGui::EndDisabled();
      ImGui::PopID();
      break;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (ImGui::SmallButton("+ add layer"))
    sc.layers.push_back({"Layer " + std::to_string(sc.layers.size()), true});

  // ---- objects (hierarchical: groups expand and collapse) ----
  ImGui::SeparatorText("Objects");
  int delete_idx = -1;
  std::function<void(int, int)> draw_row = [&](int i, int depth) {
    SceneObject &o = sc.objects[i];
    ImGui::PushID(i);
    if (depth) ImGui::Indent(14.f * depth);
    bool has_children = false;
    for (const auto &c : sc.objects)
      if (c.parent == i) has_children = true;
    if (has_children) {
      if (ImGui::SmallButton(o.expanded ? "-" : "+")) o.expanded = !o.expanded;
      ImGui::SameLine();
    }
    studio::Checkbox("##vis", &o.visible);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", type_icon(o.type));
    ImGui::SameLine();
    bool sel = sc.selected == i;
    bool active_cam = o.type == SceneObject::Camera && scene_active_camera() == i;
    if (active_cam)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.62f, 0.24f, 1.f));
    if (ImGui::Selectable(o.name.c_str(), sel,
                          ImGuiSelectableFlags_AllowDoubleClick,
                          ImVec2(ImGui::GetContentRegionAvail().x - 100, 0))) {
      sc.selected = i;
      a.scene_selection_serial++;
      if (o.type == SceneObject::Camera &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        scene_active_camera() = i;
        scene_last_used_camera() = i;
      }
      if (o.type == SceneObject::Planet &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        // fly the free camera out to the planet
        scene_active_camera() = -1;
        renderer_camera_look_at(o.pos, o.planet.radius * 3.5f);
        a.status = "flying to " + o.name;
      }
    }
    if (active_cam) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && o.type == SceneObject::Camera)
      ImGui::SetTooltip("%s\ndouble-click to look through this camera",
                        o.name.c_str());
    if (ImGui::IsItemHovered() && o.type == SceneObject::Planet)
      ImGui::SetTooltip("%s\ndouble-click to fly the camera to this planet",
                        o.name.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    int layer = o.layer;
    std::string lname = layer < (int)sc.layers.size() ? sc.layers[layer].name : "?";
    if (ImGui::BeginCombo("##layer", lname.c_str(), ImGuiComboFlags_NoArrowButton)) {
      for (int li = 0; li < (int)sc.layers.size(); ++li)
        if (ImGui::Selectable(sc.layers[li].name.c_str(), li == o.layer))
          o.layer = li;
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(o.builtin && o.type != SceneObject::Camera);
    if (ImGui::SmallButton("x")) delete_idx = i;
    ImGui::EndDisabled();
    if (depth) ImGui::Unindent(14.f * depth);
    ImGui::PopID();
    if (has_children && o.expanded)
      for (int c = 0; c < (int)sc.objects.size(); ++c)
        if (sc.objects[c].parent == i) draw_row(c, depth + 1);
  };
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (sc.objects[i].parent < 0) draw_row(i, 0);

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
  }

  if (ImGui::Button("+ add camera", ImVec2(-1, 0))) {
    undo_push(a, "Add camera");
    int idx = scene_add_camera();
    sc.selected = idx;
    a.scene_selection_serial++;
    a.status = "added " + sc.objects[idx].name;
  }
  if (ImGui::Button("+ add planet", ImVec2(-1, 0))) {
    undo_push(a, "Add planet");
    int idx = scene_add_planet();
    sc.selected = idx;
    a.scene_selection_serial++;
    a.status = "added " + sc.objects[idx].name +
               " - double-click it to fly there";
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A whole procedural world, generated on the GPU from\n"
                      "its parameters - planets cost no memory, add as many\n"
                      "as you like. Zoom out to see them all; double-click\n"
                      "one in this list to fly to it.");
  if (ImGui::Button("+ add infinite terrain", ImVec2(-1, 0))) {
    undo_push(a, "Add infinite terrain");
    // to the selected planet, otherwise extending the home ground plane
    int parent = -1;
    if (sc.selected >= 0 && sc.selected < (int)sc.objects.size()) {
      if (sc.objects[sc.selected].type == SceneObject::Planet)
        parent = sc.selected;
      else if (sc.objects[sc.selected].type == SceneObject::InfiniteSurface)
        parent = sc.objects[sc.selected].parent;
    }
    int idx = scene_add_infinite_surface(parent);
    sc.selected = idx;
    a.scene_selection_serial++;
    a.status = "added " + sc.objects[idx].name;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("An endless procedural terrain layer. Added to the\n"
                      "selected planet it shapes that planet's surface; at\n"
                      "the root it extends the home terrain past the tile's\n"
                      "edges to the horizon. Layers stack - add several.");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("New cameras inherit the lens, exposure, film and\n"
                      "render settings of the last camera you used.");
  if (ImGui::Button("+ import 3D object (OBJ)", ImVec2(-1, 0))) {
    std::string p = dialog_open_file("Wavefront OBJ\0*.obj\0All files\0*.*\0", "obj");
    if (!p.empty()) {
      std::string err;
      int idx = scene_import_obj(p, err);
      if (idx >= 0) {
        sc.selected = idx;
        a.status = "imported " + sc.objects[idx].name;
      } else {
        a.status = "IMPORT FAILED: " + err;
      }
    }
  }

  ImGui::TextDisabled("Full properties for the selection are in the\n"
                      "Properties tab; its material in the Material tab.");
  ImGui::End();
}

} // namespace studio




