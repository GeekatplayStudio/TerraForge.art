// Geekatplay TerraForge - the Plants workspace: the plant library window,
// and the tool row, left column and menu that go with it.
//
// Pictures of every plant, filtered by shelf and by where they came from,
// with the chosen one's details under them: how tall, how heavy, whose it is
// and on what terms. Double-click puts a plant in the scene under the view's
// pivot; Scatter covers the terrain with it. The free plants are one button
// away, and a person's own plant models are recorded where they keep them.
//
// The plant editor this workspace is named for - plants grown from rules,
// the way the dedicated plant tools build them - is planned
// (docs/roadmaps/plants.md); the graph under this window is where it will
// live, and the window says so rather than pretending otherwise.
#include "app.hpp"
#include "i18n.hpp"
#include "icons.hpp"
#include "mesh_thumbnail.hpp"
#include "panel_float.hpp"
#include "plant_library.hpp"
#include "plant_place.hpp"
#include "scene.hpp"
#include "stb_image.h"
#include "theme_colors.hpp"
#include "toolbar_internal.hpp"
#include <glad/gl.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace studio {

// panel_plant_editor.cpp: the species editor's menu entries and tool buttons
void plant_editor_menu_items(App &a);
void plant_editor_tool_buttons(App &a);

std::string dialog_open_file(const char *filter, const char *def_ext); // file_dialogs.cpp
void open_in_desktop(const std::string &path);                          // file_dialogs.cpp

namespace {

struct PanelState {
  char search[96] = "";
  int group = 0;  // index into plant_groups()
  int source = 0; // 0 every source, 1 built in, 2 free (Poly Haven), 3 my models
  std::string selected = "builtin/pine";
  int variant = 0;
  float size = 1.f;
  bool random_turn = true;
  int count = 300;
  bool rescan = false; // asked for this frame; done after the window is drawn
};
PanelState &ps() {
  static PanelState s;
  return s;
}

const char *const SOURCES[] = {"Every source", "Built in", "Free (Poly Haven, CC0)", "My models", "Grown from rules"};
const char *const SOURCE_KEYS[] = {"", "builtin", "polyhaven", "user", "species"};

unsigned upload(const unsigned char *px, int w, int h) {
  unsigned tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return tex;
}

// A plant's picture as a texture, made the first time its tile is drawn and
// kept: the file's for a downloaded plant, the built plant itself, drawn from
// three-quarters above, for a built-in. Zero is remembered too.
unsigned thumb_of(const PlantEntry &p) {
  static std::unordered_map<std::string, unsigned> cache;
  auto it = cache.find(p.id);
  if (it != cache.end()) return it->second;
  unsigned tex = 0;
  if (!p.thumb.empty()) {
    int w = 0, h = 0, c = 0;
    if (unsigned char *px = stbi_load(p.thumb.c_str(), &w, &h, &c, 4)) {
      tex = upload(px, w, h);
      stbi_image_free(px);
    }
  } else if (!p.kind.empty()) {
    SceneObject o;
    if (scene_primitive_build(p.kind, 24, o)) {
      MeshRasterOptions opt;
      opt.pitch_deg = -14.f;
      opt.plate = false;
      const std::vector<uint8_t> rgba = mesh_raster(o, 128, opt);
      if (rgba.size() == 128u * 128u * 4u) tex = upload(rgba.data(), 128, 128);
    }
  }
  cache[p.id] = tex;
  return tex;
}

std::string cap(std::string s) {
  if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
  return s;
}

// What a plant weighs on disk: its whole folder, measured once.
double folder_mb(const PlantEntry &p) {
  static std::unordered_map<std::string, double> sizes;
  if (p.folder.empty()) return 0.0;
  auto it = sizes.find(p.id);
  if (it != sizes.end()) return it->second;
  std::error_code ec;
  double bytes = 0;
  for (const auto &e : fs::recursive_directory_iterator(p.folder, ec))
    if (e.is_regular_file(ec)) bytes += (double)e.file_size(ec);
  return sizes[p.id] = bytes / (1024.0 * 1024.0);
}

float random_heading() {
  static uint32_t s = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
  s = s * 1664525u + 1013904223u;
  return (float)((s >> 8) % 3600u) / 10.f - 180.f;
}

void add_selected(App &a, const PlantEntry &p, bool scatter) {
  PanelState &s = ps();
  PlantPlace at;
  at.size = s.size;
  at.heading_deg = s.random_turn ? random_heading() : 0.f;
  at.scatter = scatter;
  at.count = s.count;
  if (!p.variants.empty())
    at.variant = p.variants[(size_t)std::clamp(s.variant, 0, (int)p.variants.size() - 1)].id;
  std::string err;
  if (!plant_add_async(a, p, at, err)) a.status = err;
}

bool have_free_plants() {
  for (const PlantEntry &p : plant_library())
    if (p.source == "polyhaven") return true;
  return false;
}

void start_fetch(App &a) {
  std::string err;
  if (!plant_fetch_start({}, "1k", err)) a.status = err;
  else a.status = "downloading the free CC0 plants into " + plant_library_dir();
}

void record_model(App &a) {
  const std::string path = dialog_open_file(
      "Plant models (*.gltf;*.glb;*.fbx;*.obj)\0*.gltf;*.glb;*.fbx;*.obj\0All files\0*.*\0", "gltf");
  if (path.empty()) return;
  std::string err;
  const std::string id = plant_record_model(plant_library_dir(), path, "", 0.f, err);
  if (id.empty()) {
    a.status = "could not record the model: " + err;
    return;
  }
  plant_library(true);
  ps().selected = id;
  ps().variant = 0;
  a.status = "recorded " + path + " in the plant library";
}

void tile(App &a, const PlantEntry &p, float cell) {
  PanelState &s = ps();
  ImGui::PushID(p.id.c_str());
  ImGui::BeginGroup();
  const unsigned tex = thumb_of(p);
  const bool chosen = s.selected == p.id;
  if (tex) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme::shade(theme::panel_bg(), 0.8f)));
    ImGui::ImageButton("##t", (ImTextureID)(intptr_t)tex, ImVec2(cell, cell));
    ImGui::PopStyleColor();
  } else {
    ImGui::Button("##t", ImVec2(cell, cell));
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    icon_draw(ImGui::GetWindowDrawList(), Icon::Plant, ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f),
              cell * 0.45f, theme::text_dim());
  }
  if (chosen)
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), theme::accent(), 3.f, 0, 2.f);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !chosen) {
    s.selected = p.id;
    s.variant = 0;
  }
  if (ImGui::IsItemHovered()) {
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) add_selected(a, p, false);
    char h[48] = "";
    if (p.height_m > 0.f) std::snprintf(h, sizeof h, ", %.2g m", p.height_m);
    ImGui::SetTooltip("%s\n%s%s%s\n\ndouble-click: add to the scene\nright-click: more", p.name.c_str(),
                      cap(p.group).c_str(), h,
                      p.variants.empty() ? "" : (", " + std::to_string(p.variants.size()) + " variants").c_str());
  }
  if (ImGui::BeginPopupContextItem("##ctx")) {
    s.selected = p.id;
    if (ImGui::MenuItem("Add to the scene")) add_selected(a, p, false);
    if (ImGui::MenuItem("Scatter over the terrain")) add_selected(a, p, true);
    if (!p.folder.empty() && ImGui::MenuItem("Open its folder")) open_in_desktop(p.folder);
    if (!p.url.empty() && ImGui::MenuItem("Open its page")) open_in_desktop(p.url);
    ImGui::EndPopup();
  }
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
  ImGui::TextUnformatted(p.name.c_str());
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  ImGui::PopID();
}

void details(App &a, const PlantEntry &p) {
  PanelState &s = ps();
  ImGui::SeparatorText(p.name.c_str());
  std::string line = cap(p.group);
  if (p.height_m > 0.f) {
    char h[32];
    std::snprintf(h, sizeof h, " - %.2g m tall", p.height_m);
    line += h;
  }
  if (p.polycount > 0) line += " - " + std::to_string(p.polycount / 1000) + "k triangles";
  ImGui::TextUnformatted(line.c_str());
  ImGui::TextDisabled("Licence: %s", p.license.c_str());
  if (!p.authors.empty()) {
    std::string who;
    for (const std::string &w : p.authors) who += (who.empty() ? "" : ", ") + w;
    ImGui::TextDisabled("By %s", who.c_str());
  }
  if (!p.url.empty()) {
    ImGui::TextDisabled("%s", p.url.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to open the page");
    if (ImGui::IsItemClicked()) open_in_desktop(p.url);
  }
  if (!p.note.empty()) {
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextDisabled("%s", p.note.c_str());
    ImGui::PopTextWrapPos();
  }
  const double mb = folder_mb(p);
  if (mb > 50.0 || p.polycount > 250000)
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme::accent()),
                       "Heavy: %.0f MB on disk. It loads in the background; scatter it sparingly.", mb);

  if (!p.variants.empty()) {
    s.variant = std::clamp(s.variant, 0, (int)p.variants.size() - 1);
    const PlantVariant &cur = p.variants[(size_t)s.variant];
    char label[96];
    std::snprintf(label, sizeof label, "%s (%.2g m)", cur.name.c_str(), cur.height_m);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##variant", label)) {
      for (int i = 0; i < (int)p.variants.size(); ++i) {
        const PlantVariant &v = p.variants[(size_t)i];
        std::snprintf(label, sizeof label, "%s  (%.2g m, %dk triangles)", v.name.c_str(), v.height_m, v.polycount / 1000);
        if (ImGui::Selectable(label, i == s.variant)) s.variant = i;
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("This file holds %d plants; pick the one to place.", (int)p.variants.size());
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##size", &s.size, 0.25f, 4.f, "size x%.2f", ImGuiSliderFlags_Logarithmic);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Times the plant's own size - 1 is as it grew.");
  studio::Checkbox("Turn it at random", &s.random_turn);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("A random heading, so two of the same plant do not stand alike.");

  std::string loading;
  const bool busy = plant_place_busy(&loading);
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Add to the scene")) add_selected(a, p, false);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Stands the plant on the ground under the view's pivot, at its real size,\n"
                      "with a node in the graph that drives it.");
  ImGui::SameLine();
  if (ImGui::Button("Scatter")) add_selected(a, p, true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Copies of it over the whole terrain: a Scatter points node, bound to the\n"
                      "plant. Its density, mask and spacing are that node's.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::InputInt("##count", &s.count, 50, 500);
  s.count = std::clamp(s.count, 1, 50000);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many copies Scatter makes.");
  ImGui::EndDisabled();
  if (busy) ImGui::TextDisabled("loading %s...", loading.c_str());
}

void free_plants_section(App &a) {
  ImGui::SeparatorText("Free plants");
  const PlantFetchState f = plant_fetch_state();
  if (f.running) {
    ImGui::TextUnformatted(f.line.c_str());
    if (ImGui::SmallButton("Cancel")) plant_fetch_cancel();
    return;
  }
  ImGui::PushTextWrapPos(0.f);
  if (!have_free_plants())
    ImGui::TextDisabled("Photoscanned trees, shrubs, ferns, grass and flowers from Poly Haven, "
                        "free for any use (CC0). Not shipped with the application: about 320 MB, "
                        "downloaded once into %s.", plant_library_dir().c_str());
  else if (f.finished)
    ImGui::TextDisabled("%s", f.line.c_str());
  ImGui::PopTextWrapPos();
  if (ImGui::Button(have_free_plants() ? "Update the free plants" : "Get the free plants")) start_fetch(a);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Runs orchestrator/plant_fetch.py in the background. Files already here\n"
                      "are checked and kept; only what is missing is fetched.");
}

void my_models_section(App &a) {
  ImGui::SeparatorText("My plant models");
  ImGui::PushTextWrapPos(0.f);
  ImGui::TextDisabled("A plant you exported from any tool you own - glTF, FBX, OBJ - joins the "
                      "library where it is, without being copied. Keep content whose licence "
                      "forbids sharing it on your own machine.");
  ImGui::PopTextWrapPos();
  if (ImGui::Button("Record a plant model...")) record_model(a);
}

void editor_note() {
  if (ImGui::CollapsingHeader("Plant editor")) {
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextDisabled("Plants grown from rules rather than found as models: a trunk, the "
                        "branches it carries and how they are spread along it, leaves, age, "
                        "season and wind, each a node in this workspace's graph, with a new "
                        "individual from every seed. Open the Plant Editor and type a plant's name to grow one.");
    ImGui::PopTextWrapPos();
  }
}

} // namespace

void plants_service(App &a) {
  plant_fetch_poll();
  plant_place_service(a);
}

void draw_panel_plants(App &a) {
  if (!a.show_plants) return;
  ImGui::SetNextWindowSize(ImVec2(440, 720), ImGuiCond_FirstUseEver);
  panel_float_prepare(a, "Plants");
  if (!ImGui::Begin("Plants", &a.show_plants)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Plants");
  PanelState &s = ps();
  const std::vector<PlantEntry> &lib = plant_library();

  ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 110.f, 80.f));
  ImGui::InputTextWithHint("##plantsearch", "search plants...", s.search, sizeof s.search);
  ImGui::SameLine();
  if (ImGui::SmallButton("Rescan")) s.rescan = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Read the plant library folder again:\n%s", plant_library_dir().c_str());
  // the shelves, as a row of chips
  const std::vector<std::string> &groups = plant_groups();
  for (int i = 0; i < (int)groups.size(); ++i) {
    const std::string label = cap(groups[(size_t)i]);
    const float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
    if (i > 0 && ImGui::GetItemRectMax().x + w + 8.f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
      ImGui::SameLine();
    if (i == s.group) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme::fade(theme::accent(), 0.55f)));
    if (ImGui::SmallButton((label + "##g").c_str())) s.group = i;
    if (i == s.group) ImGui::PopStyleColor();
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::Combo("##source", &s.source, SOURCES, 5);

  // the pictures, over the chosen plant's details
  const float grid_h = std::max(ImGui::GetContentRegionAvail().y * 0.5f, 140.f);
  ImGui::BeginChild("##grid", ImVec2(0, grid_h), ImGuiChildFlags_Borders);
  const float cell = 84.f;
  const int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + ImGui::GetStyle().ItemSpacing.x + 2.f)));
  int shown = 0;
  const PlantEntry *chosen = nullptr;
  for (const PlantEntry &p : lib) {
    if (p.id == s.selected) chosen = &p;
    if (s.group > 0 && p.group != groups[(size_t)s.group]) continue;
    if (s.source > 0 && p.source != SOURCE_KEYS[s.source]) continue;
    if (!plant_matches(p, s.search)) continue;
    if (shown % cols != 0) ImGui::SameLine();
    tile(a, p, cell);
    ++shown;
  }
  if (!shown) ImGui::TextDisabled(s.source == 2 && !have_free_plants() ? "The free plants are not downloaded yet."
                                                                          : "No plant matches.");
  ImGui::EndChild();

  ImGui::BeginChild("##details", ImVec2(0, 0));
  if (!chosen && !lib.empty()) chosen = &lib.front();
  if (chosen) details(a, *chosen);
  free_plants_section(a);
  my_models_section(a);
  editor_note();
  ImGui::EndChild();
  ImGui::End();

  if (s.rescan) {
    s.rescan = false;
    plant_library(true);
  }
}

// ---- the workspace's tool row, left column and menu -------------------------
void tools_plants(App &a) {
  if (tool_icon(Icon::Plant, "##plantlib", tr("Plant library\n\nEvery plant the scene can take: built in,\nthe free CC0 set, and your own models."),
                a.show_plants))
    a.show_plants = !a.show_plants;
  plant_editor_tool_buttons(a);
  tool_sep();
  const PlantFetchState f = plant_fetch_state();
  if (f.running) {
    tool_label("%s", f.line.c_str());
    if (tool_text(tr("Cancel"), tr("Stop the plant download"))) plant_fetch_cancel();
  } else {
    if (tool_text(have_free_plants() ? tr("Update free plants") : tr("Get free plants"),
                  tr("Download the free CC0 plants from Poly Haven (about 320 MB),\nor fetch whatever of them is missing.")))
      start_fetch(a);
  }
  if (tool_text(tr("Record a model..."), tr("A plant model you own joins the library where it is.")))
    record_model(a);
  std::string loading;
  if (plant_place_busy(&loading)) tool_label(tr("loading %s..."), loading.c_str());
  else tool_label(tr("%d plants"), (int)plant_library().size());
}

void column_plants(App &a) {
  const PlantEntry *p = plant_find(ps().selected);
  if (tool_icon(Icon::Plant, "##plantlib2", tr("Plant library"), a.show_plants)) a.show_plants = !a.show_plants;
  ImGui::BeginDisabled(!p || plant_place_busy());
  if (tool_icon(Icon::Plus, "##plantadd", tr("Add the chosen plant\n\nOn the ground under the view's pivot.")) && p)
    add_selected(a, *p, false);
  if (tool_icon(Icon::Group, "##plantscatter", tr("Scatter the chosen plant\n\nCopies of it over the whole terrain.")) && p)
    add_selected(a, *p, true);
  ImGui::EndDisabled();
}

void menu_plants(App &a) {
  if (!ImGui::BeginMenu("Plants")) return;
  if (IconMenuItem(Icon::Plant, "Plant library", a.show_plants)) {
    a.show_plants = !a.show_plants;
    if (a.show_plants) a.workspace = WS_PLANTS;
  }
  if (ImGui::BeginMenu("Add a built-in plant")) {
    for (const PlantEntry &p : plant_builtins())
      if (ImGui::MenuItem(p.name.c_str())) {
        std::string err;
        PlantPlace at;
        at.heading_deg = random_heading();
        if (!plant_add_async(a, p, at, err)) a.status = err;
      }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  const PlantFetchState f = plant_fetch_state();
  if (f.running) {
    if (ImGui::MenuItem("Cancel the plant download")) plant_fetch_cancel();
  } else if (ImGui::MenuItem(have_free_plants() ? "Update the free plants" : "Get the free plants")) {
    start_fetch(a);
  }
  if (ImGui::MenuItem("Record a plant model...")) record_model(a);
  if (ImGui::MenuItem("Open the plant library folder")) open_in_desktop(plant_library_dir());
  ImGui::Separator();
  plant_editor_menu_items(a);
  ImGui::EndMenu();
}

} // namespace studio
