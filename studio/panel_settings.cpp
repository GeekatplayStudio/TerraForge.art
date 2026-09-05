// Geekatplay TerraForge - the Settings window: one place for how the
// application looks and performs (Preferences) and what it connects to
// (Configuration): keyboard shortcuts you can remap, AI services and their
// keys, the ComfyUI installation, external applications, the folders the
// asset index watches.
//
// Tabs: General | Shortcuts | AI services | ComfyUI | Applications |
// Asset folders. The service and ComfyUI tabs are in
// panel_settings_services.cpp; this file is the frame, General and
// Shortcuts, Applications and Asset folders.
#include "app.hpp"
#include "asset_store.hpp"
#include "config.hpp"
#include "perf.hpp"
#include "prefs.hpp"
#include "shortcuts.hpp"
#include <cstring>
#include <imgui.h>
#include <string>

namespace studio {

void settings_services_tab(App &a);
void settings_comfy_tab(App &a);
std::string dialog_open_file(const char *filter, const char *def_ext);

namespace {

bool g_open = false;
std::string g_rebinding; // the command waiting for a key press
char g_app_name[64] = "";
char g_app_path[512] = "";
char g_root_path[512] = "";
int g_root_kind = 1;

void tab_general(App &a) {
  Prefs &p = prefs();
  ImGui::SeparatorText("Interface");
  ImGui::SetNextItemWidth(220);
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
  ImGui::SliderInt("Viewport rate", &p.viewport_fps, 10, 120, "%d fps");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Idle rate", &p.idle_fps, 2, 60, "%d fps");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Preview panel rate", &p.preview_fps, 1, 60, "%d fps");
  ImGui::SetNextItemWidth(220);
  ImGui::Combo("Preview panel quality", &p.preview_quality, "25%\0" "50%\0" "100%\0");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Preview res while dragging", &p.interactive_res, 64, 512);
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Graph memory ceiling", &p.graph_memory_mb, 0, 16384,
                   p.graph_memory_mb ? "%d MB" : "no limit");
  {
    double held = a.snapshot_bytes / (1024.0 * 1024.0);
    double freed = a.graph.released_bytes / (1024.0 * 1024.0);
    ImGui::TextDisabled("graph buffers: %.1f MB held, %.1f MB released so far", held, freed);
  }
  ImGui::SeparatorText("Performance governor");
  Config &c = config();
  studio::Checkbox("Keep the viewport responsive (governor)", &c.perf.governor);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("When the work per frame would drop the main view below the threshold,\n"
                      "secondary views, the preview panel, shadows, clouds and subdivision are\n"
                      "lightened a step at a time, and given back once the frame is comfortable.\n"
                      "Nothing it changes is written to the project.");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Main view threshold", &c.perf.fps_primary, 5, 120, "%d fps");
  ImGui::SetNextItemWidth(220);
  ImGui::SliderInt("Secondary views threshold", &c.perf.fps_secondary, 1, 120, "%d fps");
  ImGui::TextDisabled("now: %.0f fps possible, governor %s", perf_stats().potential_fps,
                      perf_stats().governor_note.empty() ? "full" : perf_stats().governor_note.c_str());
  ImGui::SeparatorText("Generated assets");
  studio::Checkbox("Notify when a generation finishes", &c.ai.notify_on_finish);
  ImGui::TextDisabled("textures: %s", config_output_dir("textures").c_str());
  ImGui::TextDisabled("skies:    %s", config_output_dir("skies").c_str());
  ImGui::TextDisabled("models:   %s", config_output_dir("models").c_str());
}

void tab_shortcuts(App &a) {
  (void)a;
  ImGui::TextDisabled("Click a chord to rebind it, then press the keys. Escape cancels; "
                      "Reset restores the default.");
  if (!g_rebinding.empty()) {
    KeyChord k = chord_captured();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      g_rebinding.clear();
    } else if (k.valid()) {
      shortcut_set(g_rebinding, chord_format(k));
      g_rebinding.clear();
    }
  }
  if (ImGui::BeginTable("##keys", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Category", 0, 0.6f);
    ImGui::TableSetupColumn("Command", 0, 1.6f);
    ImGui::TableSetupColumn("Shortcut", 0, 1.0f);
    ImGui::TableSetupColumn("", 0, 0.5f);
    ImGui::TableHeadersRow();
    for (const ShortcutCommand &cmd : shortcut_commands()) {
      ImGui::PushID(cmd.id);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", cmd.category);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(cmd.label);
      ImGui::TableNextColumn();
      std::string chord = shortcut_chord(cmd.id);
      bool waiting = g_rebinding == cmd.id;
      if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.33f, 0.13f, 1.f));
      if (ImGui::Button(waiting ? "press keys..." : chord.c_str(), ImVec2(150, 0)))
        g_rebinding = waiting ? "" : cmd.id;
      if (waiting) ImGui::PopStyleColor();
      std::vector<std::string> cl = shortcut_conflicts(chord_parse(chord), cmd.id);
      if (!cl.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.f), "conflict");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also bound to %s", cl[0].c_str());
      }
      ImGui::TableNextColumn();
      if (!shortcut_is_default(cmd.id) && ImGui::SmallButton("Reset")) shortcut_set(cmd.id, "");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void tab_applications(App &a) {
  (void)a;
  Config &c = config();
  ImGui::TextDisabled("External applications the studio can hand work to: a path per name.");
  if (ImGui::BeginTable("##apps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Name", 0, 0.6f);
    ImGui::TableSetupColumn("Path", 0, 2.0f);
    ImGui::TableSetupColumn("", 0, 0.4f);
    ImGui::TableHeadersRow();
    std::string remove;
    for (auto &kv : c.apps) {
      ImGui::PushID(kv.first.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(kv.first.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(kv.second.c_str());
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Remove")) remove = kv.first;
      ImGui::PopID();
    }
    ImGui::EndTable();
    if (!remove.empty()) {
      c.apps.erase(remove);
      config_save();
    }
  }
  ImGui::SetNextItemWidth(140);
  ImGui::InputTextWithHint("##an", "name (blender, python...)", g_app_name, sizeof g_app_name);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(360);
  ImGui::InputTextWithHint("##ap", "path to the executable", g_app_path, sizeof g_app_path);
  ImGui::SameLine();
  if (ImGui::SmallButton("Browse...")) {
    std::string p = dialog_open_file("Programs\0*.exe;*.bat;*.cmd;*.py\0All\0*.*\0", "exe");
    if (!p.empty()) snprintf(g_app_path, sizeof g_app_path, "%s", p.c_str());
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Add") && g_app_name[0] && g_app_path[0]) {
    c.apps[g_app_name] = g_app_path;
    config_save();
    g_app_name[0] = g_app_path[0] = 0;
  }
}

void tab_assets(App &a) {
  (void)a;
  gpx::AssetIndex &ix = asset_index();
  ImGui::TextDisabled("Folders the asset index watches. Materials, meshes, textures and layouts "
                      "found here are searchable in the Assets tab.");
  for (size_t i = 0; i < ix.roots.size(); ++i) {
    ImGui::PushID((int)i);
    ImGui::Text("%-9s %s", ix.roots[i].kind.c_str(), ix.roots[i].path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      asset_remove_root(ix.roots[i].path);
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  const char *kinds[] = {"material", "mesh", "texture", "layout", "macro", "other"};
  ImGui::SetNextItemWidth(360);
  ImGui::InputTextWithHint("##root", "folder to watch", g_root_path, sizeof g_root_path);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::Combo("##rk", &g_root_kind, kinds, 6);
  ImGui::SameLine();
  if (ImGui::SmallButton("Add")) {
    std::string err;
    if (asset_add_root(g_root_path, kinds[g_root_kind], err)) g_root_path[0] = 0;
    else a.status = err;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Rescan")) asset_rescan();
  ImGui::TextDisabled("%zu assets indexed in %s", ix.records.size(), asset_index_file().c_str());
}

} // namespace

void settings_open() { g_open = true; }

void draw_panel_settings(App &a) {
  if (!g_open) return;
  ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Settings", &g_open)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("##settings")) {
    struct Tab {
      const char *name;
      void (*fn)(App &);
    } tabs[] = {{"General", tab_general},
                {"Shortcuts", tab_shortcuts},
                {"AI services", settings_services_tab},
                {"ComfyUI", settings_comfy_tab},
                {"Applications", tab_applications},
                {"Asset folders", tab_assets}};
    for (const Tab &t : tabs)
      if (ImGui::BeginTabItem(t.name)) {
        ImGui::BeginChild("##t", ImVec2(0, -32));
        t.fn(a);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    ImGui::EndTabBar();
  }
  ImGui::Separator();
  if (ImGui::Button("Save", ImVec2(110, 0))) {
    prefs_save();
    config_save();
    a.status = "settings saved to " + config_file();
  }
  ImGui::SameLine();
  if (ImGui::Button("Close", ImVec2(110, 0))) {
    prefs_save();
    config_save();
    g_open = false;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", config_file().c_str());
  ImGui::End();
}

} // namespace studio
