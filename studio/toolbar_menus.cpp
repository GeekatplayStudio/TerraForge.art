// Geekatplay TerraForge — the workflow menus on the top row.
//
// The commands of each workflow existed only as icons in the left column and
// on the tool row. That is fine once you know the application and useless
// before then: an icon cannot be scanned for "what can this thing do", it can
// only be recognised once you already know. Every serious application answers
// that question with text menus, and so does this one now — Objects,
// Materials, Atmosphere, Lighting, Cameras, Animation and Render alongside
// File, Edit, Terrain, View, AI and Help.
//
// Two rules held throughout:
//
//  * A menu entry and the icon that does the same thing run the same code.
//    Nothing here is a second implementation of a command; where the logic is
//    more than a line it stays where it was and is called.
//  * The entries are grouped the way the work is: what you make, then what you
//    change about it, then what you look at it with. Separators mark the
//    groups, and the workflow's own menu opens the workspace when picked from
//    another one, because a command that needs its workspace should switch to
//    it rather than quietly do nothing.
#include "ai_describe.hpp"
#include "anim_widgets.hpp"
#include "app.hpp"
#include "component_new.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "icons.hpp"
#include "mesh_object.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "sculpt.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include <imgui.h>
#include <string>

namespace studio {

// Elsewhere in the studio.
void settings_open();
void ai_generate_open_model();
std::string dialog_open_file(const char *filter, const char *def_ext);

namespace {

// Switching to the workspace a command belongs to. Picking "Add cube" from the
// Terrain workspace should leave you where the cube is, not where it isn't.
void go(App &a, int ws) {
  if (a.workspace != ws) a.workspace = ws;
}

void item_help(const char *tip) {
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
}

} // namespace

// ------------------------------------------------------------------ Objects
void menu_objects(App &a) {
  if (!ImGui::BeginMenu("Objects")) return;
  SceneState &sc = scene();
  struct P { const char *kind, *label; Icon icon; };
  static const P prims[] = {{"cube", "Cube", Icon::Object},
                            {"sphere", "Sphere", Icon::Sphere},
                            {"plane", "Plane", Icon::Plane},
                            {"cylinder", "Cylinder", Icon::Cylinder},
                            {"cone", "Cone", Icon::Cone}};
  for (const P &p : prims)
    if (IconMenuItem(p.icon, p.label)) {
      // component_new.cpp: the object, the node that drives it, and a
      // material - the same call the left column's tile makes.
      const NewComponent nc = component_add_primitive(a, p.kind);
      if (nc.object >= 0) {
        sc.selected = nc.object;
        a.scene_selection_serial++;
      }
      go(a, WS_OBJECTS);
    }
  item_help("Adds the whole component: the object, its node in the graph,\n"
            "and a material - the last one used, or a plain grey one.");
  ImGui::Separator();
  if (IconMenuItem(Icon::Planet, "Planet")) {
    undo_push(a, "Add planet");
    sc.selected = scene_add_planet();
    a.scene_selection_serial++;
    go(a, WS_OBJECTS);
  }
  if (IconMenuItem(Icon::Terrain, "Infinite terrain surface")) {
    undo_push(a, "Add infinite terrain");
    sc.selected = scene_add_infinite_surface(-1);
    a.scene_selection_serial++;
    go(a, WS_OBJECTS);
  }
  if (IconMenuItem(Icon::Light, "Light")) {
    undo_push(a, "Add light");
    sc.selected = scene_add_light("");
    a.scene_selection_serial++;
  }
  if (IconMenuItem(Icon::Camera, "Camera")) {
    int idx = scene_add_camera();
    scene_active_camera() = idx;
    sc.selected = idx;
    a.scene_selection_serial++;
  }
  ImGui::Separator();
  if (IconMenuItem(Icon::Mesh, "Import a mesh...")) {
    std::string p = dialog_open_file(
        "Meshes (*.obj;*.stl;*.ply;*.off;*.gltf;*.glb;*.fbx)\0"
        "*.obj;*.stl;*.ply;*.off;*.gltf;*.glb;*.fbx\0All files\0*.*\0",
        "obj");
    if (!p.empty()) {
      undo_push(a, "Import mesh");
      std::string err;
      int idx = scene_import_mesh(p, err);
      if (idx >= 0) {
        sc.selected = idx;
        a.scene_selection_serial++;
        go(a, WS_OBJECTS);
      } else {
        log_error("io", err.empty() ? ("could not import " + p) : err);
      }
    }
  }
  item_help("OBJ, STL, PLY, OFF, glTF and FBX. Mesh Tools then reports\n"
            "what is wrong with it and can repair it.");
  if (IconMenuItem(Icon::Modifier, "Mesh Tools", a.show_mesh_tools))
    a.show_mesh_tools = !a.show_mesh_tools;
  item_help("Analyse, repair, reduce, split and export the selected mesh.");
  if (IconMenuItem(Icon::Object, "Generate a 3D model...")) ai_generate_open_model();
  ImGui::Separator();
  ImGui::TextDisabled("%d objects in the scene", (int)sc.objects.size());
  ImGui::TextDisabled("Delete and reorder in the Objects tree");
  ImGui::EndMenu();
}

// ---------------------------------------------------------------- Materials
void menu_materials(App &a) {
  if (!ImGui::BeginMenu("Materials")) return;
  RenderSettings &rs = render_settings();
  if (IconMenuItem(Icon::Material, "Material Editor", a.show_material_editor)) {
    a.show_material_editor = !a.show_material_editor;
    if (a.show_material_editor) go(a, WS_MATERIALS);
  }
  item_help("The material graph: channels, layers and the preview sphere.");
  if (IconMenuItem(Icon::Scene, "Material Studio", a.show_material_studio)) {
    a.show_material_studio = !a.show_material_studio;
    if (a.show_material_studio) go(a, WS_MATERIALS);
  }
  item_help("Every property of the material being edited, with its preview.");
  if (IconMenuItem(Icon::Folder, "Material Browser", a.show_material_browser)) {
    a.show_material_browser = !a.show_material_browser;
    if (a.show_material_browser) go(a, WS_MATERIALS);
  }
  item_help("The project's materials, the library, and the asset index.");
  ImGui::Separator();
  if (IconMenuItem(Icon::Textured, "Show materials on the terrain", rs.use_albedo))
    rs.use_albedo = !rs.use_albedo;
  item_help("The material's colour on the terrain instead of height shading.");
  ImGui::SetNextItemWidth(210);
  static const char *const K[] = {"Auto (last texture)", "Procedural", "Chosen node"};
  ImGui::Combo("Terrain colour from", &rs.terrain_material_mode, tr_combo(K, 3).c_str());
  item_help("Where the terrain's colour comes from when no MaterialOutput\n"
            "is assigned to it.");
  ImGui::EndMenu();
}

// --------------------------------------------------------------- Atmosphere
void menu_atmosphere(App &a) {
  if (!ImGui::BeginMenu("Atmosphere")) return;
  RenderSettings &rs = render_settings();
  if (IconMenuItem(Icon::Cloud, "Clouds", rs.clouds_on)) rs.clouds_on = !rs.clouds_on;
  if (IconMenuItem(Icon::Water, "Water", rs.show_water)) rs.show_water = !rs.show_water;
  if (IconMenuItem(Icon::Sun, "Shadows", rs.shadows)) rs.shadows = !rs.shadows;
  ImGui::Separator();
  ImGui::SetNextItemWidth(150);
  static const char *const F[] = {"Off", "Haze", "Fog", "Pollution"};
  ImGui::Combo("Fog", &rs.fog_type, tr_combo(F, 4).c_str());
  ImGui::Separator();
  ImGui::TextDisabled("Sun");
  ImGui::SetNextItemWidth(150);
  ImGui::SliderFloat("Altitude", &rs.sun_altitude, 1.f, 89.f, "%.0f\xC2\xB0");
  ImGui::SetNextItemWidth(150);
  ImGui::SliderFloat("Azimuth", &rs.sun_azimuth, 0.f, 360.f, "%.0f\xC2\xB0");
  ImGui::SetNextItemWidth(150);
  ImGui::SliderFloat("Intensity", &rs.sun_intensity, 0.f, 10.f, "x%.1f");
  ImGui::Separator();
  if (ImGui::MenuItem("Describe the atmosphere...")) {
    ai_describe_open(DESCRIBE_ATMOSPHERE);
    go(a, WS_ATMOSPHERE);
  }
  ImGui::EndMenu();
}

// --------------------------------------------------------------- Animation
void menu_animation(App &a) {
  if (!ImGui::BeginMenu("Animation")) return;
  gpx::Timeline &tl = scene().timeline;
  if (IconMenuItem(Icon::KeyAdd, "Key the selection")) anim_key_selection_transform(a);
  item_help("Sets a key on the selected object's transform at the current\n"
            "frame. K does the same.");
  if (IconMenuItem(Icon::Autokey, "Autokey", tl.autokey)) tl.autokey = !tl.autokey;
  item_help("Every change to an animatable value sets a key.");
  ImGui::Separator();
  if (IconMenuItem(a.anim_playing ? Icon::Pause : Icon::Play,
                   a.anim_playing ? "Pause" : "Play"))
    a.anim_playing = !a.anim_playing;
  if (IconMenuItem(Icon::Stop, "Stop")) {
    a.anim_playing = false;
    anim_set_time(a, a.anim_start);
  }
  if (IconMenuItem(Icon::Loop, "Loop", a.anim_loop)) a.anim_loop = !a.anim_loop;
  ImGui::Separator();
  if (IconMenuItem(Icon::Timeline, "Timeline", a.show_timeline)) {
    a.show_timeline = !a.show_timeline;
    if (a.show_timeline) go(a, WS_ANIMATION);
  }
  if (IconMenuItem(Icon::Curve, "Curve Editor", a.show_curve_editor)) {
    a.show_curve_editor = !a.show_curve_editor;
    if (a.show_curve_editor) go(a, WS_ANIMATION);
  }
  item_help("The F-curves: what every keyed value does between its keys.");
  ImGui::EndMenu();
}

// ------------------------------------------------------------------- Render
void menu_render(App &a) {
  if (!ImGui::BeginMenu("Render")) return;
  RenderSettings &rs = render_settings();
  if (IconMenuItem(Icon::Render, "Render the active camera")) {
    a.request_camera_render = scene_active_camera();
    go(a, WS_RENDER);
  }
  item_help("Renders through the active camera with its own engine,\n"
            "resolution and sample settings.");
  if (IconMenuItem(Icon::Scene, "Preview render panel", a.show_preview))
    a.show_preview = !a.show_preview;
  item_help("The progressive render, updating as the scene changes.");
  ImGui::Separator();
  ImGui::SetNextItemWidth(180);
  static const char *const E[] = {"Rasterized PBR", "Cinematic raymarch"};
  ImGui::Combo("Viewport engine", &rs.viewport_engine, tr_combo(E, 2).c_str());
  item_help("How the viewport itself draws. Offline engines are chosen\n"
            "per camera in the Render properties.");
  ImGui::Separator();
  ImGui::TextDisabled("Camera");
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (ImGui::MenuItem("Free camera", nullptr, active < 0)) scene_active_camera() = -1;
  for (int idx : scene_camera_indices())
    if (ImGui::MenuItem(sc.objects[idx].name.c_str(), nullptr, idx == active)) {
      scene_active_camera() = idx;
      scene_last_used_camera() = idx;
    }
  ImGui::EndMenu();
}

} // namespace studio
