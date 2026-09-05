// Geekatplay TerraForge — menu bar (File/Edit/View/Render/Help), tool strip
// with typed resolution, progress and resource usage.
#include "app.hpp"
#include "ai_describe.hpp"
#include "ai_jobs.hpp"
#include "shortcuts.hpp"
#include "gizmo.hpp"
#include "icons.hpp"
#include "i18n.hpp"
#include "material_library.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <cstdio>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);
std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested);
bool renderer_render_to_file(const std::string &path, int w, int h);

static const char *PROJECT_FILTER =
    "TerraForge project (*.gpxt)\0*.gpxt\0All files\0*.*\0";

static bool show_about = false;
static bool show_prefs = false;

static size_t graph_memory_bytes(App &a) {
  size_t total = 0;
  for (auto &n : a.graph.nodes)
    for (auto &p : n->ports) {
      if (p.hmap) total += p.hmap->v.size() * sizeof(float);
      if (p.tex) total += p.tex->v.size() * sizeof(float);
    }
  return total;
}

// ---- recent projects (standard Windows "Open Recent" behaviour) ----------
static std::vector<std::string> &recent_list() {
  static std::vector<std::string> r;
  return r;
}

static std::string recent_file_path() {
  const char *base = std::getenv("LOCALAPPDATA");
  std::string dir = base ? std::string(base) : std::string(".");
  dir += "\\GeekatplayTerraForge";
  std::filesystem::create_directories(dir);
  return dir + "\\recent_projects.txt";
}

static void recent_load() {
  recent_list().clear();
  std::ifstream f(recent_file_path());
  std::string line;
  while (std::getline(f, line))
    if (!line.empty()) recent_list().push_back(line);
}

static void recent_save() {
  std::ofstream f(recent_file_path());
  for (const auto &p : recent_list()) f << p << "\n";
}

static void recent_add(const std::string &path) {
  if (path.empty()) return;
  auto &r = recent_list();
  r.erase(std::remove(r.begin(), r.end(), path), r.end());
  r.insert(r.begin(), path);
  if (r.size() > 10) r.resize(10);
  recent_save();
}

static void menu_file(App &a) {
  static bool loaded = false;
  if (!loaded) {
    recent_load();
    loaded = true;
  }
  if (!ImGui::BeginMenu(tr("menu.file"))) return;
  // "New" asks for the size of the ground, the height range and the
  // resolution before it throws the current project away - see dialog_new.cpp
  if (ImGui::MenuItem(tr("menu.file.new"), "Ctrl+N")) new_terrain_request();
  if (ImGui::MenuItem(tr("menu.file.open"), "Ctrl+O")) {
    std::string p = dialog_open_file(PROJECT_FILTER, "gpxt");
    if (!p.empty() && project_load(a, p)) recent_add(p);
  }
  if (ImGui::BeginMenu(tr("menu.file.open_recent"))) {
    if (recent_list().empty()) {
      ImGui::MenuItem(tr("menu.file.recent_empty"), nullptr, false, false);
    } else {
      int n = 1;
      for (const std::string &p : std::vector<std::string>(recent_list())) {
        char label[600];
        snprintf(label, sizeof label, "&%d  %s", n++, p.c_str());
        if (ImGui::MenuItem(label)) {
          if (project_load(a, p)) recent_add(p);
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem(tr("menu.file.clear_recent"))) {
        recent_list().clear();
        recent_save();
      }
    }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::MenuItem(tr("menu.file.save"), "Ctrl+S")) {
    std::string p = a.project_path;
    if (p.empty()) p = dialog_save_file(PROJECT_FILTER, "gpxt", "terrain.gpxt");
    if (!p.empty() && project_save(a, p)) recent_add(p);
  }
  if (ImGui::MenuItem(tr("menu.file.save_as"), "Ctrl+Shift+S")) {
    std::string p = dialog_save_file(PROJECT_FILTER, "gpxt",
                                     a.project_path.empty() ? "terrain.gpxt"
                                                            : a.project_path.c_str());
    if (!p.empty() && project_save(a, p)) recent_add(p);
  }
  ImGui::Separator();
  if (ImGui::BeginMenu(tr("menu.file.import"))) {
    if (ImGui::MenuItem(tr("menu.file.import_obj"))) {
      std::string p = dialog_open_file("Wavefront OBJ\0*.obj\0", "obj");
      if (!p.empty()) {
        std::string err;
        int idx = scene_import_obj(p, err);
        a.status = idx >= 0 ? "imported " + p : "IMPORT FAILED: " + err;
      }
    }
    if (ImGui::MenuItem(tr("menu.file.import_heightmap"))) {
      std::string p = dialog_open_file(
          "Heightfield\0*.png;*.tif;*.jpg;*.raw\0", nullptr);
      if (!p.empty()) {
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        gpx::Node *n = a.graph.add_node("HeightmapFile", 0, 400);
        if (n) {
          if (gpx::Attribute *at = n->attrs.find("path")) at->s = p;
          a.graph_layout_serial++;
          a.request_eval();
          a.status = "added HeightmapFile node";
        }
      }
    }
    if (ImGui::MenuItem(tr("menu.file.import_material"))) {
      std::string p = dialog_open_file(
          "Textures\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0", nullptr);
      if (!p.empty()) {
        std::string err;
        unsigned long long id = material_import_texture_set(a, p, err);
        a.status = id ? "imported texture set" : "IMPORT FAILED: " + err;
      }
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu(tr("menu.file.export"))) {
    auto add_export = [&](const char *label, const char *type) {
      if (!ImGui::MenuItem(label)) return;
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      gpx::Node *n = a.graph.add_node(type, 1200, 400);
      if (n) {
        if (gpx::Attribute *at = n->attrs.find("auto_export")) at->b = true;
        a.graph_layout_serial++;
        a.request_eval();
        a.status = std::string("added ") + type + " node - set its file path";
      }
    };
    add_export(tr("menu.file.export_heightmap"), "ExportHeightmap");
    add_export(tr("menu.file.export_mesh"), "ExportMesh");
    add_export(tr("menu.file.export_texture"), "ExportTexture");
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::MenuItem(tr("menu.file.render_image"), "F12")) {
    std::string p = dialog_save_file("PNG image\0*.png\0", "png", "render.png");
    if (!p.empty()) {
      a.status = renderer_render_to_file(p, 3840, 2160)
                     ? "rendered 4K image: " + p
                     : "RENDER FAILED";
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem(tr("menu.file.exit"), "Alt+F4"))
    glfwSetWindowShouldClose(a.window, GLFW_TRUE);
  ImGui::EndMenu();
}

static void menu_edit(App &a) {
  if (!ImGui::BeginMenu(tr("menu.edit"))) return;
  {
    std::string ulabel = tr("menu.edit.undo");
    std::string rlabel = tr("menu.edit.redo");
    if (undo_can_undo()) ulabel += " " + undo_next_label();
    if (undo_can_redo()) rlabel += " " + undo_redo_label();
    if (ImGui::MenuItem(ulabel.c_str(), "Ctrl+Z", false, undo_can_undo())) {
      undo_perform(a);
      a.status = "undo";
    }
    if (ImGui::MenuItem(rlabel.c_str(), "Ctrl+Y", false, undo_can_redo())) {
      redo_perform(a);
      a.status = "redo";
    }
    if (ImGui::BeginMenu(tr("menu.edit.history"),
                         undo_can_undo() || undo_can_redo())) {
      const std::string *labels = nullptr;
      int n = undo_history(&labels);
      int pos = undo_history_position();
      for (int i = n - 1; i >= 0; --i) {
        bool current = i == pos;
        if (ImGui::MenuItem(labels[i].c_str(), nullptr, current) && !current)
          undo_jump_to(a, i);
      }
      ImGui::EndMenu();
    }
  }
  ImGui::Separator();
  bool has_sel = a.selected_node != 0;
  if (ImGui::MenuItem(tr("menu.edit.delete"), "Del", false, has_sel)) {
    undo_push(a, "Delete node");
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.remove_node(a.selected_node);
    a.selected_node = 0;
    a.request_eval();
  }
  if (ImGui::MenuItem(tr("menu.edit.duplicate"), "Ctrl+D", false, has_sel)) {
    undo_push(a, "Duplicate node");
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    gpx::Node *src = a.graph.find_node(a.selected_node);
    if (src) {
      gpx::Node *dup = a.graph.add_node(src->type, src->pos_x + 40, src->pos_y + 40);
      if (dup) {
        dup->attrs = src->attrs;
        a.selected_node = dup->id;
        a.graph_layout_serial++;
        a.request_eval();
      }
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Recompute all", "F5")) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Settings...", shortcut_chord("app.settings").c_str())) show_prefs = true;
  ImGui::EndMenu();
}

// The Preferences dialog grew into the Settings window (panel_settings.cpp):
// general, shortcuts, AI services, ComfyUI, applications, asset folders.
static void prefs_dialog() {
  if (show_prefs) {
    settings_open();
    show_prefs = false;
  }
}

// toolbar_layout.cpp: the Viewports and Layouts menus, and the dialog that
// names a layout being saved.
void menu_view_viewports(App &a);
void menu_view_layouts(App &a);
void layout_dialogs(App &a);

static void menu_view(App &a) {
  if (!ImGui::BeginMenu("View")) return;
  ImGui::MenuItem("Library", nullptr, &a.show_library);
  ImGui::MenuItem("Node List", nullptr, &a.show_nodelist);
  ImGui::MenuItem("Properties", nullptr, &a.show_properties);
  ImGui::MenuItem("Viewport", nullptr, &a.show_viewport);
  ImGui::MenuItem("Timeline", nullptr, &a.show_timeline);
  ImGui::MenuItem("Preview", nullptr, &a.show_preview);
  ImGui::MenuItem("Material Editor", nullptr, &a.show_material_editor);
  ImGui::Separator();
  if (ImGui::BeginMenu("New node editor")) {
    // Another graph window, pinned to one domain, with its own canvas and
    // a side pane for the selected node - so the material graph can live on
    // the second monitor while the terrain graph stays here.
    for (int oi = 0; oi < 8; ++oi) {
      int d = WORKSPACE_ORDER[oi];
      std::string label = std::string(workspace_name(d)) + " nodes";
      if (ImGui::MenuItem(label.c_str())) graph_editor_add(a, d);
    }
    if (ImGui::MenuItem(workspace_name(WS_ALL))) graph_editor_add(a, WS_ALL);
    ImGui::Separator();
    ImGui::MenuItem("Each editor floats out with its corner button", nullptr,
                    false, false);
    ImGui::EndMenu();
  }
  // Viewports and saved layouts live in toolbar_layout.cpp - this file is at
  // the 500-line limit and those two menus have real logic behind them.
  menu_view_viewports(a);
  menu_view_layouts(a);
  ImGui::EndMenu();
}

// the Terrain menu lives in terrain_styles.cpp
void menu_terrain(App &a);

// The AI menu: generate an image, a texture or a skydome, a 3D model; build
// a scene, a terrain or an atmosphere from words; the jobs list; settings.
static void menu_ai(App &a) {
  (void)a;
  if (!ImGui::BeginMenu("AI")) return;
  if (ImGui::MenuItem("Describe a scene...")) ai_describe_open(DESCRIBE_SCENE);
  if (ImGui::MenuItem("Describe the terrain...")) ai_describe_open(DESCRIBE_TERRAIN);
  if (ImGui::MenuItem("Describe the atmosphere...")) ai_describe_open(DESCRIBE_ATMOSPHERE);
  ImGui::Separator();
  if (ImGui::MenuItem("Generate an image...", shortcut_chord("ai.generate_image").c_str())) ai_generate_open_image(JOB_IMAGE);
  if (ImGui::MenuItem("Generate a tileable texture...")) ai_generate_open_image(JOB_TEXTURE);
  if (ImGui::MenuItem("Generate a 360 skydome...")) ai_generate_open_image(JOB_SKYDOME);
  if (ImGui::MenuItem("Generate a 3D model...", shortcut_chord("ai.generate_model").c_str())) ai_generate_open_model();
  ImGui::Separator();
  if (ImGui::MenuItem("Jobs...")) ai_generate_open_jobs();
  if (ImGui::MenuItem("AI services settings...")) settings_open();
  ImGui::EndMenu();
}

static void menu_help() {
  if (!ImGui::BeginMenu("Help")) return;
  if (ImGui::MenuItem("About Geekatplay TerraForge...")) show_about = true;
  ImGui::EndMenu();
}

static void about_dialog() {
  if (show_about) {
    ImGui::OpenPopup("About TerraForge");
    show_about = false;
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("About TerraForge", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::Text("Geekatplay TerraForge");
    ImGui::PopStyleColor();
    ImGui::Text("Node-based terrain studio, version 2.0.0");
    ImGui::Separator();
    ImGui::Text("Created by Vladimir Shopine");
    ImGui::Text("Geekatplay Studio");
    ImGui::Separator();
    ImGui::TextDisabled("Native C++ engine \xC2\xB7 OpenGL renderer");
    ImGui::TextDisabled("%d node types registered",
                        (int)gpx::NodeRegistry::instance().all().size());
    ImGui::Spacing();
    // the licence belongs where a user can find it without the repository:
    // free for noncommercial work, paid for production
    ImGui::TextDisabled("Free for noncommercial use");
    ImGui::TextDisabled("PolyForm Noncommercial 1.0.0 \xC2\xB7 see LICENSE");
    ImGui::TextDisabled("Commercial licence: COMMERCIAL.md");
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void draw_toolbar(App &a) {
  if (ImGui::BeginMenuBar()) {
    menu_file(a);
    menu_edit(a);
    menu_terrain(a);
    menu_view(a);
    menu_ai(a);
    menu_help();

    // The global tools, on the row the hand is already on. They used to be a
    // 56 px column down the left edge: a whole column of window spent on seven
    // buttons, with undo somewhere no other application keeps it.
    ImGui::Spacing();
    ImGui::SameLine(0, 14);
    draw_global_tools(a);

    ImGui::EndMenuBar();
  }
  about_dialog();
  layout_dialogs(a); // "save layout as" (toolbar_layout.cpp)
  prefs_dialog();
  new_terrain_dialog(a);

  // keyboard shortcuts
  ImGuiIO &io = ImGui::GetIO();
  // W/E/R pick the transform tool, as they do in Maya, Unity and Unreal.
  // Guarded on text input so typing a node name does not swap the gadget.
  // every chord comes from the shortcut table (shortcuts.cpp), which the
  // Settings window can rebind
  if (shortcut_pressed("tool.move")) gizmo_mode() = GizmoMode::Move;
  if (shortcut_pressed("tool.rotate")) gizmo_mode() = GizmoMode::Rotate;
  if (shortcut_pressed("tool.scale")) gizmo_mode() = GizmoMode::Scale;
  if (shortcut_pressed("app.settings")) settings_open();
  if (shortcut_pressed("ai.generate_image")) ai_generate_open_image(JOB_TEXTURE);
  if (shortcut_pressed("ai.generate_model")) ai_generate_open_model();
  if (shortcut_pressed("file.save")) {
    std::string p = a.project_path;
    if (p.empty()) p = dialog_save_file(PROJECT_FILTER, "gpxt", "terrain.gpxt");
    if (!p.empty()) project_save(a, p);
  }
  if (shortcut_pressed("file.open")) {
    std::string p = dialog_open_file(PROJECT_FILTER, "gpxt");
    if (!p.empty()) project_load(a, p);
  }
  if (shortcut_pressed("graph.recompute")) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  // undo / redo, with the usual Windows and Blender-style shortcuts
  if (shortcut_pressed("edit.undo")) {
    if (undo_perform(a)) a.status = "undo";
  }
  if (shortcut_pressed("edit.redo") || shortcut_pressed("edit.redo_alt")) {
    if (redo_perform(a)) a.status = "redo";
  }
}

} // namespace studio



