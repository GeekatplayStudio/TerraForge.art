// Geekatplay TerraForge â€” Scene panel: object list (Sun, Water, Terrain,
// imported meshes...), layers with visibility/grouping, OBJ import,
// per-object quick properties.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <imgui.h>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);

static const char *type_icon(SceneObject::Type t) {
  switch (t) {
    case SceneObject::Terrain: return "[T]";
    case SceneObject::Water: return "[W]";
    case SceneObject::Sun: return "[S]";
    case SceneObject::Atmosphere: return "[A]";
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
    ImGui::Checkbox("##vis", &sc.layers[li].visible);
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

  // ---- objects ----
  ImGui::SeparatorText("Objects");
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    SceneObject &o = sc.objects[i];
    ImGui::PushID(i);
    ImGui::Checkbox("##vis", &o.visible);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", type_icon(o.type));
    ImGui::SameLine();
    bool sel = sc.selected == i;
    if (ImGui::Selectable(o.name.c_str(), sel,
                          ImGuiSelectableFlags_AllowDoubleClick,
                          ImVec2(ImGui::GetContentRegionAvail().x - 92, 0))) {
      sc.selected = i;
      a.scene_selection_serial++;
      a.prop_tab = a.prop_tab == TAB_MATERIAL ? TAB_MATERIAL : TAB_OBJECT;
    }
    ImGui::SameLine();
    // layer assignment combo (compact)
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
    ImGui::BeginDisabled(o.builtin);
    if (ImGui::SmallButton("x")) {
      sc.objects.erase(sc.objects.begin() + i);
      if (sc.selected >= (int)sc.objects.size()) sc.selected = 0;
      ImGui::EndDisabled();
      ImGui::PopID();
      break;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
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

