// Geekatplay TerraForge - the Material Browser: the thin strip under the
// Material Studio with the project's materials on one tab and the library
// on the other.
//
// Hover a tile and a larger preview pops up with what the material is made
// of; double-click and it opens in the studio (asking first if the one there
// has unsaved changes); right-click for assign / save / remove. Removing
// from the library moves the files to a trash folder rather than deleting
// them - a browser that can lose work in one mis-click is not a browser.
#include "app.hpp"
#include "asset_store.hpp"
#include "material_library.hpp"
#include "material_ui.hpp"
#include "panel_float.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace studio {

namespace {

// Project thumbnails, one texture per material, redrawn only when the
// material's evaluation moved.
struct Thumb {
  unsigned tex = 0;
  uint64_t version = 0;
};
std::unordered_map<uint64_t, Thumb> g_thumbs;

unsigned project_thumb(App &a, gpx::Node *mat, int size) {
  Thumb &t = g_thumbs[mat->id];
  if (t.tex && t.version == a.eval_serial) return t.tex;
  MaterialPreviewSpec spec = material_preview_spec(a, mat);
  t.tex = renderer_material_thumbnail(spec, size, t.tex);
  t.version = a.eval_serial;
  return t.tex;
}

void hover_card(const char *name, unsigned tex, const std::string &detail) {
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(name);
  if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(192, 192), ImVec2(0, 1), ImVec2(1, 0));
  if (!detail.empty()) ImGui::TextDisabled("%s", detail.c_str());
  ImGui::TextDisabled("double-click: open in the studio\nright-click: more");
  ImGui::EndTooltip();
}

std::string project_detail(App &a, gpx::Node *mat) {
  int type = material_type_of(a.graph, mat);
  std::string s = material_type_name(type);
  int channels = 0;
  for (const char *p : {"base color", "normal", "roughness", "metallic",
                        "height", "ambient occlusion"})
    if (a.graph.upstream_node(*mat, p)) ++channels;
  s += " - " + std::to_string(channels) + " of 6 channels";
  int used = 0;
  for (const SceneObject &o : scene().objects)
    if (o.material_node == mat->id) ++used;
  if (used) s += " - on " + std::to_string(used) + (used == 1 ? " object" : " objects");
  return s;
}

void assign_to_selected(App &a, uint64_t mat_id) {
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    a.status = "select an object to assign the material to";
    return;
  }
  undo_push_locked(a, "assign material");
  sc.objects[(size_t)sc.selected].material_node = mat_id;
  a.uploaded_serial = 0;
  a.status = "assigned to " + sc.objects[(size_t)sc.selected].name;
}

void project_tab(App &a, float cell) {
  std::vector<MatEntry> mats = collect_materials(a);
  if (mats.empty()) {
    ImGui::TextDisabled("No materials in this project. New in the studio above, "
                        "or double-click one in the library.");
    return;
  }
  float avail = ImGui::GetContentRegionAvail().x;
  int cols = std::max(1, (int)(avail / (cell + 10)));
  int i = 0;
  for (const MatEntry &m : mats) {
    gpx::Node *mat = a.graph.find_node(m.id);
    if (!mat) continue;
    if (i % cols != 0) ImGui::SameLine();
    ImGui::PushID((int)m.id);
    ImGui::BeginGroup();
    unsigned tex = project_thumb(a, mat, (int)cell);
    const bool open = material_studio().material == m.id;
    if (open) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
    ImGui::ImageButton("##t", (ImTextureID)(intptr_t)tex, ImVec2(cell, cell),
                       ImVec2(0, 1), ImVec2(1, 0));
    if (open) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      hover_card(m.name.c_str(), tex, project_detail(a, mat));
      if (ImGui::IsMouseDoubleClicked(0)) material_studio_open(a, m.id);
    }
    if (ImGui::BeginPopupContextItem("##ctx")) {
      if (ImGui::MenuItem("Open in the studio")) material_studio_open(a, m.id);
      if (ImGui::MenuItem("Assign to the selected object")) assign_to_selected(a, m.id);
      if (ImGui::MenuItem("Save to the library")) {
        std::string err;
        std::string path = material_library_save(a, m.id, err);
        a.status = path.empty() ? "SAVE FAILED: " + err : "saved " + path;
      }
      if (ImGui::MenuItem("Show its node")) graph_focus_node(a, m.id);
      ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
    ImGui::TextUnformatted(m.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::PopID();
    ++i;
  }
}

void trash_library_material(LibraryMaterial &m) {
  namespace fs = std::filesystem;
  fs::path trash = fs::path(material_library_dir()) / "trash";
  std::error_code ec;
  fs::create_directories(trash, ec);
  fs::rename(m.path, trash / fs::path(m.path).filename(), ec);
  if (!m.thumb.empty())
    fs::rename(m.thumb, trash / fs::path(m.thumb).filename(), ec);
  material_library_rescan();
}

void library_tab(App &a, float cell) {
  auto &lib = material_library();
  if (ImGui::SmallButton("Rescan")) material_library_rescan();
  ImGui::SameLine();
  ImGui::TextDisabled("%zu in %s", lib.size(), material_library_dir().c_str());
  if (lib.empty()) {
    ImGui::TextDisabled("The library is empty - Save in the studio starts it.");
    return;
  }
  float avail = ImGui::GetContentRegionAvail().x;
  int cols = std::max(1, (int)(avail / (cell + 10)));
  int i = 0;
  int trash_idx = -1;
  for (auto &m : lib) {
    if (i % cols != 0) ImGui::SameLine();
    ImGui::PushID(i);
    ImGui::BeginGroup();
    unsigned tex = material_thumb_texture(m);
    if (tex)
      ImGui::ImageButton("##t", (ImTextureID)(intptr_t)tex, ImVec2(cell, cell));
    else
      ImGui::Button(m.name.c_str(), ImVec2(cell, cell));
    if (ImGui::IsItemHovered()) {
      hover_card(m.name.c_str(), tex, "library");
      if (ImGui::IsMouseDoubleClicked(0)) {
        std::string err;
        unsigned long long id = material_library_load(a, m, err);
        if (id) {
          a.graph_layout_serial++;
          a.request_eval();
          material_studio_open(a, id);
          a.status = "loaded " + m.name;
        } else {
          a.status = "LOAD FAILED: " + err;
        }
      }
    }
    if (ImGui::BeginPopupContextItem("##ctx")) {
      if (ImGui::MenuItem("Load into the project and open")) {
        std::string err;
        unsigned long long id = material_library_load(a, m, err);
        if (id) {
          a.graph_layout_serial++;
          a.request_eval();
          material_studio_open(a, id);
        } else {
          a.status = "LOAD FAILED: " + err;
        }
      }
      if (ImGui::MenuItem("Load and assign to the selected object")) {
        std::string err;
        unsigned long long id = material_library_load(a, m, err);
        if (id) {
          a.graph_layout_serial++;
          a.request_eval();
          assign_to_selected(a, id);
        }
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Move to trash")) trash_idx = i;
      ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
    ImGui::TextUnformatted(m.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::PopID();
    ++i;
  }
  if (trash_idx >= 0 && trash_idx < (int)lib.size()) {
    a.status = "moved '" + lib[(size_t)trash_idx].name + "' to the library trash";
    trash_library_material(lib[(size_t)trash_idx]);
  }
}

} // namespace

void draw_panel_material_browser(App &a) {
  if (!a.show_material_browser) return;
  ImGui::SetNextWindowSize(ImVec2(820, 200), ImGuiCond_FirstUseEver);
  panel_float_prepare(a, "Material Browser");
  if (!ImGui::Begin("Material Browser", &a.show_material_browser)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Material Browser");
  std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) {
    ImGui::TextDisabled("computing...");
    ImGui::End();
    return;
  }
  static float cell = 84.f;
  if (ImGui::BeginTabBar("##tabs")) {
    if (ImGui::BeginTabItem("Project")) {
      ImGui::BeginChild("##p", ImVec2(0, 0));
      project_tab(a, cell);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Library")) {
      ImGui::BeginChild("##l", ImVec2(0, 0));
      library_tab(a, cell);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Assets", nullptr,
                            assets_tab_take_focus() ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::BeginChild("##a", ImVec2(0, 0));
      draw_assets_tab(a, cell);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

} // namespace studio
