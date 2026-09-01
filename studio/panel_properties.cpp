// Geekatplay Studio — properties panel: auto-generated UI from attributes
#include "app.hpp"
#include "ai_assist.hpp"
#include "icons.hpp"
#include "node_library.hpp"
#include "undo.hpp"
#include "gpx/metanode.hpp"
#include <vector>
#include "render_settings.hpp"
#include "scene.hpp"
#include <imgui.h>
#include <cstring>
#include <map>
#include <string>

namespace studio {

// ------------------------------------------------- Blender-style properties
static char g_prop_search[64] = "";

bool prop_filter_active() { return g_prop_search[0] != 0; }

bool prop_filter_match(const char *text) {
  if (!g_prop_search[0]) return true;
  std::string hay = text ? text : "", needle = g_prop_search;
  for (auto &c : hay) c = (char)tolower(c);
  for (auto &c : needle) c = (char)tolower(c);
  return hay.find(needle) != std::string::npos;
}


// Blender's Properties editor: a vertical tab column on the left, a
// breadcrumb and search at the top, collapsible panels in the body. Tabs
// appear only when they apply to the current selection, and the active tab
// is sticky across selections.
void draw_panel_properties(App &a) {
  SceneState &sc = scene();
  bool have_obj = sc.selected >= 0 && sc.selected < (int)sc.objects.size();
  SceneObject::Type otype = have_obj ? sc.objects[sc.selected].type
                                     : SceneObject::Mesh;
  bool has_material = have_obj && (otype == SceneObject::Terrain ||
                                   otype == SceneObject::Water ||
                                   otype == SceneObject::Mesh);
  bool is_camera = have_obj && otype == SceneObject::Camera;
  bool has_node = a.selected_node != 0;

  // Which tabs apply. A tab that has nothing to do with what is selected is
  // not shown at all: rendering belongs to a camera, the world belongs to the
  // sun, sky and water. Scene is the one that is always there, because there
  // is no scene object to select in order to reach it.
  bool is_world = have_obj && (otype == SceneObject::Sun ||
                               otype == SceneObject::Atmosphere ||
                               otype == SceneObject::Water);
  struct TabDef { int id; Icon icon; const char *label; const char *tip; bool shown; };
  const TabDef tabs[] = {
      {TAB_RENDER, Icon::Render, "Render",
       "Output engine, resolution and samples for this camera", is_camera},
      {TAB_SCENE, Icon::Scene, "Scene",
       "Resolution, world scale, layers, statistics", true},
      {TAB_WORLD, Icon::World, "World", "Sun, sky, clouds, fog, water", is_world},
      {TAB_OBJECT, is_camera ? Icon::Camera : Icon::Object,
       is_camera ? "Camera" : "Object",
       is_camera ? "Lens, exposure and film for this camera"
                 : "The selected object's properties",
       have_obj},
      {TAB_MATERIAL, Icon::Material, "Material",
       "Surface of the selected object", has_material},
      {TAB_NODE, Icon::Node, "Node", "The selected node's parameters", has_node},
  };
  const int TAB_N = 6;

  // only fall back when the current tab genuinely cannot be shown
  bool valid = false;
  for (const TabDef &t : tabs)
    if (t.id == a.prop_tab && t.shown) valid = true;
  if (!valid) a.prop_tab = have_obj ? TAB_OBJECT : TAB_SCENE;

  // The active tab is entirely sticky: selecting objects or nodes never
  // switches it and never steals focus, so working in the node editor is
  // not interrupted by clicking something in a viewport.
  if (!ImGui::Begin("Properties", &a.show_properties)) {
    ImGui::End();
    return;
  }

  // breadcrumb + search
  const char *tab_name = "Scene";
  for (const TabDef &t : tabs)
    if (t.id == a.prop_tab) tab_name = t.label;
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.53f, 0.50f, 1.f));
  if (have_obj)
    ImGui::Text("Scene  >  %s  >  %s", sc.objects[sc.selected].name.c_str(),
                tab_name);
  else
    ImGui::Text("Scene  >  %s", tab_name);
  ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##search", "search properties...", g_prop_search,
                           sizeof g_prop_search);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Filters the parameters of the active tab.");
  ImGui::Separator();

  // The tab column is icons now. Six words stacked vertically cost a fifth of
  // the panel's width for labels that never change; the name is in the
  // breadcrumb above and in the tooltip, where it is wanted only on demand.
  const float tab_w = ImGui::GetFontSize() +
                      ImGui::GetStyle().FramePadding.y * 2.f + 6.f;
  ImGui::BeginChild("##tabs", ImVec2(tab_w + 6.f, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  for (int i = 0; i < TAB_N; ++i) {
    const TabDef &t = tabs[i];
    if (!t.shown) continue;
    char id[16];
    snprintf(id, sizeof id, "##tab%d", i);
    char tip[192];
    snprintf(tip, sizeof tip, "%s\n%s", t.label, t.tip);
    if (IconButton(t.icon, id, tip, a.prop_tab == t.id, tab_w))
      a.prop_tab = t.id;
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_Borders);
  switch (a.prop_tab) {
    case TAB_RENDER:
      render_properties_ui(a);
      ai_assist_bar(a, AiDomain::Render,
                    "render 4k with mitsuba, 512 samples");
      break;
    case TAB_SCENE:
      scene_properties_ui(a);
      ai_assist_bar(a, AiDomain::Object,
                    "put the rock in front of the terrain");
      break;
    case TAB_WORLD:
      world_properties_ui(a);
      ai_assist_bar(a, AiDomain::World,
                    "low golden sunset, heavy haze, towering cumulonimbus");
      break;
    case TAB_OBJECT:
      object_properties_ui(a);
      ai_assist_bar(a, is_camera
                           ? AiDomain::Camera
                           : AiDomain::Object,
                    is_camera ? "35mm camera, 50mm lens, cinematic, Kodak film"
                              : "place this object on the ridge");
      break;
    case TAB_MATERIAL:
      material_properties_ui(a);
      break; // the Material tab has its own material-graph AI
    default:
      node_properties_ui(a);
      ai_assist_bar(a, AiDomain::Terrain,
                    "add ridged mountains with river erosion");
      break;
  }
  ImGui::EndChild();
  ImGui::End();
}

} // namespace studio




