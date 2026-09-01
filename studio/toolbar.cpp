// Geekatplay TerraForge — menu bar (File/Edit/View/Render/Help), tool strip
// with typed resolution, progress and resource usage.
#include "app.hpp"
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
  if (ImGui::MenuItem(tr("menu.file.new"), "Ctrl+N")) {
    project_new(a);
    project_default_graph(a);
    a.graph_layout_serial++;
    a.request_eval();
  }
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
  if (ImGui::MenuItem("Preferences...")) show_prefs = true;
  ImGui::EndMenu();
}

static void prefs_dialog() {
  if (show_prefs) {
    ImGui::OpenPopup("Preferences");
    show_prefs = false;
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Preferences", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    Prefs &p = prefs();
    ImGui::SeparatorText("Interface");
    ImGui::SetNextItemWidth(220);
    // Re-apply the theme on either change: padding, spacing and control sizes
    // are all derived from the font, so leaving them at the old size is what
    // made text clip inside its button the moment the font grew.
    if (ImGui::SliderFloat("Font size", &p.font_size, 12.f, 30.f, "%.0f px")) {
      ImGui::GetStyle().FontSizeBase = p.font_size;
      apply_theme();
    }
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("UI scale", &p.ui_scale, 0.7f, 2.f, "%.2f")) {
      ImGui::GetStyle().FontScaleMain = p.ui_scale;
      apply_theme();
    }
    ImGui::SeparatorText("Performance");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Preview res while dragging", &p.interactive_res, 64, 512);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Slider drags recompute at this low resolution for\n"
                        "silky-smooth feedback; full quality on release.");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Graph memory ceiling", &p.graph_memory_mb, 0, 16384,
                     p.graph_memory_mb ? "%d MB" : "no limit");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Every node keeps its output buffer, so a deep graph\n"
                        "at high resolution can hold gigabytes nothing will\n"
                        "read again. Past this, buffers no longer needed in\n"
                        "the current pass are released and rebuilt on demand.\n"
                        "Nothing is released while the graph fits. 0 = no cap.");
    {
      double held = app().snapshot_bytes / (1024.0 * 1024.0);
      double freed = app().graph.released_bytes / (1024.0 * 1024.0);
      ImGui::TextDisabled("graph buffers: %.1f MB held, %.1f MB released so far",
                          held, freed);
    }
    ImGui::SeparatorText("AI (local Ollama)");
    char url[256], tm[128], vm[128];
    auto copy_to = [](char *dst, size_t n, const std::string &s) {
      snprintf(dst, n, "%s", s.c_str());
    };
    copy_to(url, sizeof url, p.ollama_url);
    copy_to(tm, sizeof tm, p.text_model);
    copy_to(vm, sizeof vm, p.vision_model);
    ImGui::SetNextItemWidth(280);
    if (ImGui::InputText("Ollama URL", url, sizeof url)) p.ollama_url = url;
    ImGui::SetNextItemWidth(280);
    if (ImGui::InputText("Text model", tm, sizeof tm)) p.text_model = tm;
    ImGui::SetNextItemWidth(280);
    if (ImGui::InputText("Vision model (photos)", vm, sizeof vm)) p.vision_model = vm;
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
      prefs_save();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

static void menu_view(App &a) {
  if (!ImGui::BeginMenu("View")) return;
  ImGui::MenuItem("Library", nullptr, &a.show_library);
  ImGui::MenuItem("Node List", nullptr, &a.show_nodelist);
  ImGui::MenuItem("Properties", nullptr, &a.show_properties);
  ImGui::MenuItem("Viewport", nullptr, &a.show_viewport);
  ImGui::Separator();
  if (ImGui::BeginMenu("Viewport windows")) {
    for (int n = 1; n <= 6; ++n) {
      char label[32];
      snprintf(label, sizeof label, "%d view%s", n, n > 1 ? "s" : "");
      if (ImGui::MenuItem(label, nullptr, prefs().view_count == n)) {
        prefs().view_count = n;
        prefs_save();
        a.request_layout_reset = true;
      }
    }
    ImGui::Separator();
    ImGui::MenuItem("Views are dockable windows — drag to float", nullptr, false,
                    false);
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Reset layout")) a.request_layout_reset = true;
  ImGui::EndMenu();
}

// ---- terrain style presets -------------------------------------------------
// One-click starting points (Vue's "predefined terrain styles"): each builds a
// small node chain so the result stays fully editable — open the graph and
// every stage of the style is right there to retune.
struct StyleNode {
  const char *type;
  std::vector<std::pair<const char *, float>> floats;
  std::vector<std::pair<const char *, int>> ints;
};

static void apply_terrain_style(App &a, const char *name,
                                const std::vector<StyleNode> &chain) {
  undo_push(a, std::string("Terrain style: ") + name);
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  // fresh seed on every click, the way Vue randomizes the fractal origin
  static uint32_t style_serial = 1;
  uint32_t seed = style_serial++ * 2654435761u;

  gpx::Node *prev = nullptr;
  float x = 60, y = 60;
  // place the chain below the existing graph so nothing overlaps
  for (auto &n : a.graph.nodes) y = std::max(y, n->pos_y + 260);
  gpx::Node *first = nullptr;
  for (const StyleNode &sn : chain) {
    gpx::Node *n = a.graph.add_node(sn.type, x, y);
    if (!n) continue;
    if (!first) first = n;
    for (auto &kv : sn.floats)
      if (gpx::Attribute *at = n->attrs.find(kv.first)) at->f = kv.second;
    for (auto &kv : sn.ints)
      if (gpx::Attribute *at = n->attrs.find(kv.first)) {
        at->i = kv.second;
        at->seed = (uint32_t)kv.second;
      }
    if (gpx::Attribute *sd = n->attrs.find("seed")) sd->seed = seed;
    if (prev) {
      gpx::Port *po = prev->first_out(gpx::DataType::Heightmap);
      if (po) a.graph.add_link(prev->id, po->name, n->id, "input");
    }
    prev = n;
    x += 190;
  }
  if (!prev) return;

  // route the style into the TerrainOutput (creating one if needed), so the
  // new terrain shows up immediately
  gpx::Node *out_node = nullptr;
  for (auto &n : a.graph.nodes)
    if (n->type == "TerrainOutput") out_node = n.get();
  if (!out_node) out_node = a.graph.add_node("TerrainOutput", x, y);
  if (out_node) {
    for (const gpx::Link &l : a.graph.links)
      if (l.to_node == out_node->id && l.to_port == "heightmap") {
        a.graph.remove_link(l.id);
        break;
      }
    gpx::Port *po = prev->first_out(gpx::DataType::Heightmap);
    if (po) a.graph.add_link(prev->id, po->name, out_node->id, "heightmap");
  }
  a.selected_node = prev->id;
  a.view_node = 0; // follow the TerrainOutput again
  a.graph_layout_serial++;
  a.request_eval();
  a.status = std::string("terrain style: ") + name;
}

static void menu_terrain(App &a) {
  if (!ImGui::BeginMenu("Terrain")) return;
  ImGui::TextDisabled("Style presets (build editable node chains)");
  ImGui::Separator();
  if (ImGui::MenuItem("Mountain"))
    apply_terrain_style(a, "Mountain",
                        {{"Noise", {}, {{"octaves", 10}}}});
  if (ImGui::MenuItem("Ridged peaks"))
    apply_terrain_style(a, "Ridged peaks",
                        {{"Noise", {}, {{"type", 1}, {"octaves", 11}}},
                         {"Peaks", {{"strength", 0.45f}}, {}}});
  if (ImGui::MenuItem("Eroded mountain"))
    apply_terrain_style(a, "Eroded mountain",
                        {{"Noise", {}, {{"octaves", 10}}},
                         {"Hydraulic", {}, {}},
                         {"Thermal", {}, {}}});
  if (ImGui::MenuItem("Canyon"))
    apply_terrain_style(a, "Canyon",
                        {{"Noise", {}, {{"octaves", 9}}},
                         {"Terrace", {{"shape", 6.f}}, {{"levels", 7}}},
                         {"Dissolve", {{"amount", 0.4f}}, {}}});
  if (ImGui::MenuItem("Dunes"))
    apply_terrain_style(a, "Dunes", {{"Dunes", {}, {}}});
  if (ImGui::MenuItem("Iceberg"))
    apply_terrain_style(
        a, "Iceberg",
        {{"Noise", {}, {{"octaves", 8}}},
         {"TerrainClip", {{"softness", 0.04f}}, {{"high_mode", 1}}},
         {"Glaciation", {{"strength", 0.7f}}, {}}});
  if (ImGui::MenuItem("Lunar"))
    apply_terrain_style(a, "Lunar",
                        {{"Noise", {{"gain", 0.42f}}, {{"octaves", 8}}},
                         {"Crater", {}, {{"profile", 1}}},
                         {"Grit", {{"amount", 0.015f}}, {}}});
  ImGui::Separator();
  ImGui::TextDisabled("Each style drops a fresh chain into the graph\n"
                      "and wires it to the Terrain Output — the old\n"
                      "chain stays in the graph, and Ctrl+Z undoes it.");
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
  prefs_dialog();

  // keyboard shortcuts
  ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
    std::string p = a.project_path;
    if (p.empty()) p = dialog_save_file(PROJECT_FILTER, "gpxt", "terrain.gpxt");
    if (!p.empty()) project_save(a, p);
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
    std::string p = dialog_open_file(PROJECT_FILTER, "gpxt");
    if (!p.empty()) project_load(a, p);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  // undo / redo, with the usual Windows and Blender-style shortcuts
  if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (undo_perform(a)) a.status = "undo";
  }
  if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ||
      (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))) {
    if (redo_perform(a)) a.status = "redo";
  }
}

} // namespace studio



