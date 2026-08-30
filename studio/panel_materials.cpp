// Geekatplay TerraForge — Material editor.
// Blender's material system (named materials assigned to objects, a Material
// Output node that gathers the shading channels) combined with Substance's
// channel-graph workflow: every channel is produced by nodes in the Materials
// workspace and lands on one MaterialOutput node, previewed live on a sphere.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace studio {

struct MatEntry {
  uint64_t id;
  std::string name;
};

static std::vector<MatEntry> collect_materials(App &a) {
  std::vector<MatEntry> out;
  for (auto &n : a.graph.nodes)
    if (n->type == "MaterialOutput") {
      std::string nm = n->attrs.get_s("name");
      if (nm.empty()) nm = "Material";
      out.push_back({n->id, nm + "  #" + std::to_string(n->id)});
    }
  return out;
}

// one channel row: name, whether a node feeds it, and what feeds it
static void channel_row(App &a, gpx::Node *mat, const char *port,
                        const char *human) {
  gpx::Node *src = a.graph.upstream_node(*mat, port);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(human);
  ImGui::TableNextColumn();
  if (src) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 0.45f, 1.f));
    ImGui::Text("%s #%llu", src->type.c_str(), (unsigned long long)src->id);
    ImGui::PopStyleColor();
  } else {
    ImGui::TextDisabled("not connected");
  }
}

static void material_editor(App &a, SceneObject &obj) {
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) {
    ImGui::TextDisabled("computing...");
    return;
  }
  std::vector<MatEntry> mats = collect_materials(a);
  gpx::Node *mat = a.graph.find_node(obj.material_node);
  if (mat && mat->type != "MaterialOutput") mat = nullptr;

  // ---- material slot (Blender: the material data-block on the object) ----
  ImGui::SeparatorText("Material");
  ImGui::SetNextItemWidth(-92);
  std::string label = mat ? mat->attrs.get_s("name") + "  #" +
                                std::to_string(mat->id)
                          : std::string("(none)");
  if (ImGui::BeginCombo("##matsel", label.c_str())) {
    if (ImGui::Selectable("(none)", mat == nullptr)) {
      obj.material_node = 0;
      a.uploaded_serial = 0;
    }
    for (const MatEntry &m : mats)
      if (ImGui::Selectable(m.name.c_str(), mat && m.id == mat->id)) {
        obj.material_node = m.id;
        a.uploaded_serial = 0;
      }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("New", ImVec2(-1, 0))) {
    float x = 900, y = 120;
    for (auto &n : a.graph.nodes)
      if (n->type == "MaterialOutput") x = std::max(x, n->pos_x + 260);
    gpx::Node *nn = a.graph.add_node("MaterialOutput", x, y);
    if (nn) {
      gpx::Attribute *na = nn->attrs.find("name");
      if (na) na->s = obj.name + " material";
      obj.material_node = nn->id;
      a.selected_node = nn->id;
      a.graph_layout_serial++;
      a.workspace = 1; // jump to the Materials workspace to wire it up
      a.request_eval();
    }
  }
  if (!mat) {
    ImGui::TextDisabled("No material assigned.");
    ImGui::TextWrapped("Create one, then build its channels with nodes in the "
                       "Materials workspace and connect them to the "
                       "MaterialOutput node.");
    return;
  }

  {
    char buf[128];
    snprintf(buf, sizeof buf, "%s", mat->attrs.get_s("name").c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##matname", buf, sizeof buf)) {
      gpx::Attribute *na = mat->attrs.find("name");
      if (na) na->s = buf;
    }
  }

  // ---- live preview sphere ----
  ImGui::SeparatorText("Preview");
  float avail = ImGui::GetContentRegionAvail().x;
  float side = std::min(avail, 168.f);
  unsigned tex = renderer_material_preview((int)std::max(side, 64.f));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - side) * 0.5f);
  ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side), ImVec2(0, 1),
               ImVec2(1, 0));
  ImGui::TextDisabled("Lit with the scene sun and sky.");

  // ---- channels (Substance output set) ----
  ImGui::SeparatorText("Channels");
  if (ImGui::BeginTable("chan", 2, ImGuiTableFlags_SizingStretchProp)) {
    channel_row(a, mat, "base color", "Base color");
    channel_row(a, mat, "normal", "Normal");
    channel_row(a, mat, "roughness", "Roughness");
    channel_row(a, mat, "metallic", "Metallic");
    channel_row(a, mat, "height", "Height / displacement");
    channel_row(a, mat, "ambient occlusion", "Ambient occlusion");
    ImGui::EndTable();
  }
  if (ImGui::Button("Edit channels in the node graph", ImVec2(-1, 0))) {
    a.workspace = 1;
    a.selected_node = mat->id;
    a.prop_tab = TAB_NODE;
  }
  ImGui::TextDisabled("Useful channel nodes: PBRMaterial (photoscans),\n"
                      "TextureFile, TerrainTexture, SplatMaterial, Levels,\n"
                      "GradientMap, NormalBlend, TextureTransform,\n"
                      "AOFromHeight, CurvatureFromHeight, ChannelMix.");

  // ---- surface values (Blender's Principled inputs) ----
  ImGui::SeparatorText("Surface");
  bool changed = false;
  auto slider = [&](const char *key, const char *label, float lo, float hi,
                    const char *tip = nullptr) {
    gpx::Attribute *at = mat->attrs.find(key);
    if (!at) return;
    ImGui::SetNextItemWidth(-130);
    if (ImGui::SliderFloat(label, &at->f, lo, hi)) changed = true;
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  };
  slider("roughness", "Roughness", 0.02f, 1.f,
         "Multiplies the roughness map when one is connected.");
  slider("metallic", "Metallic", 0.f, 1.f);
  slider("specular", "Specular", 0.f, 1.f);
  slider("reflection", "Sky reflection", 0.f, 1.f);
  slider("translucency", "Translucency", 0.f, 1.f,
         "Light passing through thin material toward the camera.");
  slider("transparency", "Transparency", 0.f, 1.f);
  slider("normal_strength", "Normal strength", 0.f, 4.f);
  slider("displacement", "Displacement", 0.f, 0.1f,
         "Real geometric displacement from the height channel.");
  if (changed) {
    a.graph.mark_dirty(mat->id);
    a.request_eval();
    a.uploaded_serial = 0;
  }
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
    case SceneObject::Terrain:
    case SceneObject::Mesh: material_editor(a, o); break;
    case SceneObject::Water: water_material(); break;
    default:
      ImGui::TextDisabled("This object has no surface material.");
      if (o.type == SceneObject::Sun)
        ImGui::TextDisabled("Sun settings live in Properties / World.");
      if (o.type == SceneObject::Atmosphere)
        ImGui::TextDisabled("Atmosphere settings live in the World tab.");
      break;
  }
}

} // namespace studio
