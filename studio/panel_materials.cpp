// Geekatplay TerraForge — Material panel. Shows the material of whatever is
// selected in the scene (terrain, water, imported mesh), the way Cinema 4D's
// material editor follows the selection.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <imgui.h>
#include <string>

namespace studio {

// combo listing every texture-producing node in the graph
static bool node_combo(App &a, const char *label, unsigned long long &id,
                       bool locked) {
  bool changed = false;
  gpx::Node *cur = a.graph.find_node(id);
  std::string cl = cur ? cur->type + " #" + std::to_string(cur->id)
                       : std::string("(none)");
  ImGui::SetNextItemWidth(-1);
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(-1);
  ImGui::PushID(label);
  if (ImGui::BeginCombo("##n", cl.c_str())) {
    if (ImGui::Selectable("(none)", id == 0)) {
      id = 0;
      changed = true;
    }
    for (auto &n : a.graph.nodes) {
      if (!n->first_out(gpx::DataType::Texture)) continue;
      std::string item = n->type + " #" + std::to_string(n->id);
      if (ImGui::Selectable(item.c_str(), n->id == id)) {
        id = n->id;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  (void)locked;
  return changed;
}

static void terrain_material(App &a) {
  RenderSettings &rs = render_settings();
  ImGui::SeparatorText("Albedo source");
  ImGui::RadioButton("Auto (last material node)", &rs.terrain_material_mode, 0);
  ImGui::RadioButton("Procedural (function + color)", &rs.terrain_material_mode, 1);
  ImGui::RadioButton("Assigned node", &rs.terrain_material_mode, 2);
  if (rs.terrain_material_mode == 2) {
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock()) {
      if (node_combo(a, "Albedo node", rs.terrain_material_node, false))
        a.uploaded_serial = 0;
    } else {
      ImGui::TextDisabled("computing...");
    }
  }

  ImGui::SeparatorText("Surface (PBR)");
  ImGui::SliderFloat("Roughness", &rs.mat_roughness, 0.02f, 1.f);
  ImGui::SliderFloat("Metallic", &rs.mat_metallic, 0.f, 1.f);
  ImGui::SliderFloat("Specular", &rs.mat_specular, 0.f, 1.f);
  ImGui::SliderFloat("Reflection", &rs.mat_reflection, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Sky reflection strength (fresnel weighted).");
  ImGui::SliderFloat("Translucency", &rs.mat_translucency, 0.f, 1.f);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Light bleeding through thin material toward the camera —\n"
                      "backlit foliage, ice, thin rock.");
  ImGui::SliderFloat("Transparency", &rs.mat_transparency, 0.f, 1.f);

  ImGui::SeparatorText("Maps");
  {
    std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
    if (lk.owns_lock()) {
      node_combo(a, "Normal map node", rs.map_normal_node, false);
      ImGui::SliderFloat("Normal strength", &rs.mat_normal_strength, 0.f, 4.f);
      node_combo(a, "Roughness map node", rs.map_roughness_node, false);
      node_combo(a, "Displacement map node", rs.map_displacement_node, false);
      ImGui::SliderFloat("Displacement", &rs.mat_displacement, 0.f, 0.1f, "%.4f");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Adds real geometric displacement from the map\n"
                          "on top of the terrain height.");
    } else {
      ImGui::TextDisabled("computing...");
    }
  }
  ImGui::TextDisabled("Build materials with nodes in the Materials workspace:\n"
                      "PBRMaterial (photoscans), TerrainTexture (procedural),\n"
                      "SplatMaterial (multilayer), AlbedoToPBR, ColorAdjust.");
}

static void water_material() {
  RenderSettings &rs = render_settings();
  ImGui::SeparatorText("Body");
  ImGui::ColorEdit3("Deep color", rs.water_deep_color);
  ImGui::ColorEdit3("Shallow color", rs.water_shallow_color);
  ImGui::SliderFloat("Clarity", &rs.water_clarity, 1.f, 60.f);
  ImGui::SliderFloat("Opacity", &rs.water_opacity, 0.3f, 1.f);
  ImGui::SeparatorText("Waves");
  ImGui::SliderFloat("Amplitude", &rs.water_wave_amp, 0.f, 4.f);
  ImGui::SliderFloat("Scale", &rs.water_wave_scale, 0.2f, 6.f);
  ImGui::SliderFloat("Speed", &rs.water_wave_speed, 0.f, 5.f);
  ImGui::SeparatorText("Foam");
  ImGui::Checkbox("Enabled", &rs.water_foam);
  if (rs.water_foam) {
    ImGui::ColorEdit3("Foam color", rs.foam_color);
    ImGui::SliderFloat("Shoreline", &rs.foam_amount, 0.f, 2.f);
    ImGui::SliderFloat("Crests", &rs.foam_crests, 0.f, 1.f);
    ImGui::SliderFloat("Pattern scale", &rs.foam_scale, 0.5f, 10.f);
  }
}

// Material tab of the Properties editor
void material_properties_ui(App &a) {
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    ImGui::TextDisabled("select an object to edit its material");
    return;
  }
  SceneObject &o = sc.objects[sc.selected];
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
  ImGui::Text("%s", o.name.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextDisabled("material");
  ImGui::Separator();

  switch (o.type) {
    case SceneObject::Terrain: terrain_material(a); break;
    case SceneObject::Water: water_material(); break;
    case SceneObject::Mesh:
      ImGui::SeparatorText("Surface");
      ImGui::ColorEdit3("Color", o.color);
      ImGui::TextDisabled("Imported meshes use a simple lit color material.\n"
                          "Full PBR material assignment for meshes is planned.");
      break;
    default:
      ImGui::TextDisabled("This object has no surface material.");
      if (o.type == SceneObject::Sun)
        ImGui::TextDisabled("Sun settings live in Properties / Environment.");
      if (o.type == SceneObject::Atmosphere)
        ImGui::TextDisabled("Atmosphere settings live in the World tab.");
      break;
  }
}

} // namespace studio
