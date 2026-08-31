// Geekatplay TerraForge â€” menu bar (File/Edit/View/Render/Help), tool strip
// with typed resolution, progress and resource usage.
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
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

static void menu_file(App &a) {
  if (!ImGui::BeginMenu("File")) return;
  if (ImGui::MenuItem("New project", "Ctrl+N")) {
    project_new(a);
    project_default_graph(a);
    a.graph_layout_serial++;
    a.request_eval();
  }
  if (ImGui::MenuItem("Open...", "Ctrl+O")) {
    std::string p = dialog_open_file(PROJECT_FILTER, "gpxt");
    if (!p.empty()) project_load(a, p);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Save", "Ctrl+S")) {
    std::string p = a.project_path;
    if (p.empty()) p = dialog_save_file(PROJECT_FILTER, "gpxt", "terrain.gpxt");
    if (!p.empty()) project_save(a, p);
  }
  if (ImGui::MenuItem("Save As...")) {
    std::string p = dialog_save_file(PROJECT_FILTER, "gpxt",
                                     a.project_path.empty() ? "terrain.gpxt"
                                                            : a.project_path.c_str());
    if (!p.empty()) project_save(a, p);
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Render image...")) {
    std::string p = dialog_save_file("PNG image\0*.png\0", "png", "render.png");
    if (!p.empty()) {
      a.status = renderer_render_to_file(p, 3840, 2160)
                     ? "rendered 4K image: " + p
                     : "RENDER FAILED";
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Exit", "Alt+F4"))
    glfwSetWindowShouldClose(a.window, GLFW_TRUE);
  ImGui::EndMenu();
}

static void menu_edit(App &a) {
  if (!ImGui::BeginMenu("Edit")) return;
  bool has_sel = a.selected_node != 0;
  if (ImGui::MenuItem("Delete node", "Del", false, has_sel)) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.remove_node(a.selected_node);
    a.selected_node = 0;
    a.request_eval();
  }
  if (ImGui::MenuItem("Duplicate node", "Ctrl+D", false, has_sel)) {
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
    if (ImGui::SliderFloat("Font size", &p.font_size, 12.f, 30.f, "%.0f px"))
      ImGui::GetStyle().FontSizeBase = p.font_size;
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("UI scale", &p.ui_scale, 0.7f, 2.f, "%.2f"))
      ImGui::GetStyle().FontScaleMain = p.ui_scale;
    ImGui::SeparatorText("Performance");
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Preview res while dragging", &p.interactive_res, 64, 512);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Slider drags recompute at this low resolution for\n"
                        "silky-smooth feedback; full quality on release.");
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
    ImGui::MenuItem("Views are dockable windows â€” drag to float", nullptr, false,
                    false);
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Reset layout")) a.request_layout_reset = true;
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
    menu_view(a);
    menu_help();

    // --- workspace screens (Terragen-style) ---
    ImGui::Separator();
    const char *ws_names[4] = {"Terrain", "Materials", "Atmosphere", "Render"};
    for (int w = 0; w < 4; ++w) {
      bool active = a.workspace == w;
      if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.92f, 0.86f, 1.f));
      }
      if (ImGui::Button(ws_names[w]) && a.workspace != w) {
        a.workspace = w;
        // a node from another domain must not linger in the inspector
        std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
        if (lk.owns_lock()) {
          gpx::Node *n = a.graph.find_node(a.selected_node);
          if (!n || domain_of_category(n->category) != w) {
            a.selected_node = 0;
            a.prop_tab = TAB_OBJECT;
          }
        }
        if (w == 2) { // Atmosphere workspace: bring its editors forward
          ImGui::SetWindowFocus("Environment");
        } else if (w == 3) {
          ImGui::SetWindowFocus("Render");
        }
      }
      if (active) ImGui::PopStyleColor(2);
    }

    // --- camera switcher ---
    ImGui::Separator();
    {
      SceneState &sc = scene();
      std::vector<int> cams = scene_camera_indices();
      int active = scene_active_camera();
      std::string label = "Free camera";
      if (active >= 0 && active < (int)sc.objects.size())
        label = sc.objects[active].name;
      ImGui::SetNextItemWidth(140);
      if (ImGui::BeginCombo("##camsel", label.c_str())) {
        if (ImGui::Selectable("Free camera", active < 0))
          scene_active_camera() = -1;
        for (int idx : cams) {
          bool sel = idx == active;
          if (ImGui::Selectable(sc.objects[idx].name.c_str(), sel)) {
            scene_active_camera() = idx;
            scene_last_used_camera() = idx;
          }
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Which camera the perspective views look through.");
      if (ImGui::SmallButton("+cam")) {
        int idx = scene_add_camera();
        scene_active_camera() = idx;
        sc.selected = idx;
        a.scene_selection_serial++;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a camera (inherits the last camera's settings)");
    }

    // --- tool strip ---
    ImGui::Separator();
    ImGui::TextDisabled("res");
    for (int res : {256, 512, 1024, 2048}) {
      char label[8];
      snprintf(label, sizeof label, "%s",
               res == 1024 ? "1k" : (res == 2048 ? "2k" : (res == 256 ? "256" : "512")));
      bool active = a.graph.resolution == res;
      if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
      if (ImGui::SmallButton(label)) {
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        a.graph.resolution = res;
        a.graph.mark_all_dirty();
        a.request_eval();
      }
      if (active) ImGui::PopStyleColor();
    }
    // typed custom resolution
    static int custom_res = 0;
    if (custom_res == 0) custom_res = a.graph.resolution;
    ImGui::SetNextItemWidth(64);
    if (ImGui::InputInt("##customres", &custom_res, 0, 0,
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
      custom_res = std::clamp(custom_res, 64, 8192);
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      a.graph.resolution = custom_res;
      a.graph.mark_all_dirty();
      a.request_eval();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("type any resolution 64..8192, Enter to apply");

    ImGui::Separator();
    if (ImGui::SmallButton("bake 4k exports")) {
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      for (auto &n : a.graph.nodes) {
        auto *auto_exp = n->attrs.find("auto_export");
        if (auto_exp) auto_exp->b = true;
      }
      a.graph.resolution = 4096;
      a.graph.mark_all_dirty();
      a.request_eval();
      a.status = "baking at 4096; export nodes write when done";
    }

    // --- progress / resources (right side) ---
    if (a.eval.running.load()) {
      int done = a.eval.progress_done.load(), total = a.eval.progress_total.load();
      std::string cur;
      {
        std::lock_guard<std::mutex> lk(a.eval.mtx);
        cur = a.eval.current_node;
      }
      ImGui::Separator();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
      ImGui::Text("computing %s (%d/%d)", cur.c_str(), done, total);
      ImGui::PopStyleColor();
    } else {
      ImGui::Separator();
      // resource usage: graph memory + total eval time (cached when busy)
      static double total_ms = 0;
      static size_t mem = 0, node_count = 0;
      {
        std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
        if (lk.owns_lock()) {
          mem = graph_memory_bytes(a);
          total_ms = 0;
          for (auto &n : a.graph.nodes) total_ms += n->last_compute_ms;
          node_count = a.graph.nodes.size();
        }
      }
      ImGui::TextDisabled("%zu nodes \xC2\xB7 %.0f MB \xC2\xB7 %.0f ms",
                          node_count, mem / (1024.0 * 1024.0), total_ms);
      if (!a.status.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", a.status.c_str());
      }
    }
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
}

} // namespace studio

