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
#include "render_presets.hpp"
#include "wheel_widgets.hpp"
#include "anim_widgets.hpp"
#include "app.hpp"
#include "component_new.hpp"
#include "component_add.hpp"
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

void component_menu_items(App &a) {
  const char *group = "";
  for (const ComponentKind &k : component_kinds()) {
    if (std::string(group) != k.group) {
      if (*group) ImGui::Separator();
      ImGui::TextDisabled("%s", k.group);
      group = k.group;
    }
    if (ImGui::MenuItem(k.label)) {
      std::string path, err;
      if (std::string(k.kind) == "import_mesh") {
        path = dialog_open_file(
            "Meshes (*.obj;*.stl;*.ply;*.off;*.gltf;*.glb;*.fbx)\0"
            "*.obj;*.stl;*.ply;*.off;*.gltf;*.glb;*.fbx\0All files\0*.*\0",
            "obj");
        if (path.empty()) continue;
      }
      NewComponent nc;
      if (!component_add(a, k.kind, "", path, nc, err)) {
        log_error("scene", err);
        a.status = err;
      } else {
        a.status = std::string("added ") + k.label;
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k.tip);
  }
}

// ------------------------------------------------------------------ Objects
void menu_objects(App &a) {
  if (!ImGui::BeginMenu("Objects")) return;
  SceneState &sc = scene();
  if (ImGui::BeginMenu("Add component")) {
    component_menu_items(a);
    ImGui::EndMenu();
  }
  item_help("Every component the scene can take, grouped: the same list as\n"
            "the + tile in the tool row.");
  ImGui::Separator();
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
  studio::SliderFloatW("Altitude", &rs.sun_altitude, 1.f, 89.f, "%.0f\xC2\xB0");
  ImGui::SetNextItemWidth(150);
  studio::SliderFloatW("Azimuth", &rs.sun_azimuth, 0.f, 360.f, "%.0f\xC2\xB0");
  ImGui::SetNextItemWidth(150);
  studio::SliderFloatW("Intensity", &rs.sun_intensity, 0.f, 10.f, "x%.1f");
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
  // the presets: apply one, render with one, make one, drop one
  if (ImGui::BeginMenu("Render presets")) {
    SceneState &scp = scene();
    const int cam = scene_active_camera();
    const bool has_cam = cam >= 0 && cam < (int)scp.objects.size() &&
                         scp.objects[(size_t)cam].type == SceneObject::Camera;
    std::string to_delete;
    for (const RenderPreset &p : scp.render_presets) {
      char line[160];
      snprintf(line, sizeof line, "%s  (%dx%d, %d spp)", p.name.c_str(), p.assign.width,
               p.assign.height, p.assign.samples);
      if (ImGui::BeginMenu(line)) {
        if (ImGui::MenuItem("Apply to the active camera", nullptr, false, has_cam))
          render_preset_apply(p.name, scp.objects[(size_t)cam].cam.render);
        if (ImGui::MenuItem("Render the active camera with it", nullptr, false, has_cam)) {
          render_preset_apply(p.name, scp.objects[(size_t)cam].cam.render);
          a.request_camera_render = cam;
          go(a, WS_RENDER);
        }
        if (ImGui::MenuItem("Apply to every camera"))
          for (int i : scene_camera_indices()) render_preset_apply(p.name, scp.objects[(size_t)i].cam.render);
        if (ImGui::MenuItem("Render every camera with it (batch)")) {
          render_batch_queue(a, scene_camera_indices(), p.name);
          go(a, WS_RENDER);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete preset")) to_delete = p.name;
        ImGui::EndMenu();
      }
    }
    if (!to_delete.empty()) render_preset_delete(to_delete);
    if (scp.render_presets.empty()) ImGui::TextDisabled("no presets yet");
    ImGui::Separator();
    if (ImGui::MenuItem("Save the active camera's settings as a new preset", nullptr, false, has_cam)) {
      const std::string nm = render_preset_free_name();
      render_preset_upsert(nm, scp.objects[(size_t)cam].cam.render);
      scp.objects[(size_t)cam].cam.render.preset = nm;
      a.status = "render preset '" + nm + "' saved - rename it in the Render tab";
    }
    item_help("Presets are saved with the project. The Render tab names\n"
              "and edits them; a camera's properties apply one.");
    ImGui::EndMenu();
  }
  if (IconMenuItem(Icon::Render, "Render every camera (batch)")) {
    render_batch_queue(a, scene_camera_indices(), std::string());
    go(a, WS_RENDER);
  }
  item_help("Every camera in turn, each with its own assignment and\n"
            "its own file; a camera still on the default file gets one\n"
            "named after it.");
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
  for (int idx : scene_camera_indices()) {
    // Two cameras are allowed the same name; two widgets with the same label
    // in one window are the same widget, and ImGui marks that by drawing its
    // conflict highlight over both. The index is the identity here.
    const std::string label =
        sc.objects[idx].name + "##cam" + std::to_string(idx);
    if (ImGui::MenuItem(label.c_str(), nullptr, idx == active)) {
      scene_active_camera() = idx;
      scene_last_used_camera() = idx;
    }
  }
  ImGui::EndMenu();
}

} // namespace studio
