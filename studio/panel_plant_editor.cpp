// Geekatplay TerraForge - the Plant Editor: where a plant species is grown,
// looked at and changed.
//
// Read top to bottom the way the plant tool's window is read (the manual's
// interface overview, root node and toolbars): the species with New, Load,
// Save and Export; a line to type a plant's name - "old oak", "date palm",
// "lavender" - and grow it; the node presets that add a trunk, a branch, a
// leaf or a flower to the selected part; the root's general parameters - the
// individual's seed with New variation and Flag, age, health and the season
// of the year - then its wind and meshing and what grew; the parts as a
// tree, the presets and the published parameters
// (panel_plant_editor_parts.cpp); and the assistant. Every control edits a
// node: the graph is the truth, and the node editor shows the same species.
//
// Drawn under a GraphLease. Whatever takes the graph lock itself - making a
// species, adding a part, saving, a file dialog - is queued and run after
// the lease is released; run under it, it would wait on the lock this very
// frame holds (AGENTS.md, "Clicks under a lease act after it").
#include "ai_assist.hpp"
#include "app.hpp"
#include "config.hpp"
#include "graph_lease.hpp"
#include "icons.hpp"
#include "panel_float.hpp"
#include "plant_library.hpp"
#include "plant_species.hpp"
#include "scene.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <imgui.h>
#include <string>
#include <vector>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);
std::string dialog_save_file(const char *filter, const char *def_ext, const char *suggested);
// panel_plant_editor_parts.cpp
bool plant_editor_parts(App &a, gpx::Node &root, std::vector<std::function<void()>> &later);
void plant_editor_presets(App &a, gpx::Node &root, std::vector<std::function<void()>> &later);

namespace {

using Later = std::vector<std::function<void()>>;

struct EditorState {
  char words[256] = "";
  char image[512] = "";
  bool use_ai = false;
  int individuals = 1;
  bool scatter = false;
  int count = 300;
  char save_name[96] = "";
  char save_note[256] = "";
  int save_group = 1;
};
EditorState &es() {
  static EditorState s;
  return s;
}

std::string pretty(std::string s) {
  for (char &c : s)
    if (c == '_') c = ' ';
  if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
  return s;
}

const char *season_name(float s) {
  s -= std::floor(s);
  if (s < 0.125f || s >= 0.875f) return "winter";
  if (s < 0.375f) return "spring";
  if (s < 0.625f) return "summer";
  return "autumn";
}

const char *health_name(float h) {
  if (h < 0.15f) return "dying";
  if (h < 0.4f) return "dry";
  if (h < 0.75f) return "tired";
  return "thriving";
}

// A labelled slider row on a root attribute, with the attribute's own
// tooltip and one undo step per drag. True while the value changes.
bool row_float(App &a, gpx::Node &n, const char *key, const char *label, float mn, float mx,
               const char *fmt = "%.2f") {
  gpx::Attribute *at = n.attrs.find(key);
  if (!at) return false;
  ImGui::PushID(key);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (ImGui::IsItemHovered() && !at->tooltip.empty()) ImGui::SetTooltip("%s", at->tooltip.c_str());
  ImGui::SameLine(130.f);
  ImGui::SetNextItemWidth(-1);
  const bool changed = ImGui::SliderFloat("##v", &at->f, mn, mx, fmt);
  if (ImGui::IsItemActivated()) undo_push_locked(a, std::string("Change ") + label);
  if (ImGui::IsItemHovered() && !at->tooltip.empty()) ImGui::SetTooltip("%s", at->tooltip.c_str());
  ImGui::PopID();
  return changed;
}

bool row_bool(App &a, gpx::Node &n, const char *key, const char *label) {
  gpx::Attribute *at = n.attrs.find(key);
  if (!at) return false;
  bool v = at->b;
  if (!ImGui::Checkbox(label, &v)) {
    if (ImGui::IsItemHovered() && !at->tooltip.empty()) ImGui::SetTooltip("%s", at->tooltip.c_str());
    return false;
  }
  undo_push_locked(a, std::string("Change ") + label);
  at->b = v;
  return true;
}

void header(App &a, gpx::Node *root, Later &later) {
  EditorState &s = es();
  const std::vector<SpeciesRef> all = species_in_graph(a.graph);
  std::string label = "(no species yet)";
  for (const SpeciesRef &r : all)
    if (root && r.root == root->id) label = r.name;
  ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 230.f, 110.f));
  if (ImGui::BeginCombo("##species", label.c_str())) {
    for (const SpeciesRef &r : all)
      if (ImGui::Selectable((r.name + "##" + std::to_string(r.root)).c_str(), root && r.root == root->id)) {
        species_current() = r.root;
        a.selected_node = r.root;
      }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The species this editor shows. Selecting any part of a species in the node editor shows it here too.");
  ImGui::SameLine();
  if (ImGui::Button("New")) ImGui::OpenPopup("##newspecies");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new species of a kind: a broadleaf tree, a conifer, a palm, a fern...\nEach is a graph of parts you can change.");
  if (ImGui::BeginPopup("##newspecies")) {
    ImGui::TextDisabled("Grow a new species of kind");
    for (const std::string &arch : gpx::plant_archetypes())
      if (ImGui::MenuItem(pretty(arch).c_str()))
        later.push_back([&a, arch]() {
          gpx::PlantDescription d;
          gpx::plant_describe_from_words(pretty(arch), d); // the kind's usual size and look
          d.archetype = arch;
          d.name = pretty(arch);
          std::string err;
          SpeciesMake mk;
          if (!species_create(a, d, mk, err)) a.status = "New species: " + err;
        });
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load..."))
    later.push_back([&a]() {
      const std::string p = dialog_open_file("Plant species (species.json)\0species.json;*.json\0All files\0*.*\0", "json");
      if (p.empty()) return;
      std::string err;
      SpeciesMake mk;
      if (!species_load(a, p, mk, err)) a.status = "Load species: " + err;
    });
  if (!root) return;
  const uint64_t id = root->id;
  ImGui::SameLine();
  if (ImGui::Button("Save...")) {
    std::snprintf(s.save_name, sizeof s.save_name, "%s", root->attrs.get_s("name").c_str());
    ImGui::OpenPopup("##savespecies");
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Into the plant library, where Add and Scatter place it like any plant.");
  if (ImGui::BeginPopup("##savespecies")) {
    ImGui::TextDisabled("Save the species to the plant library");
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("Name", s.save_name, sizeof s.save_name);
    const std::vector<std::string> &groups = plant_groups();
    std::string items;
    for (size_t i = 1; i < groups.size(); ++i) items += pretty(groups[i]) + '\0';
    ImGui::SetNextItemWidth(240);
    ImGui::Combo("Shelf", &s.save_group, items.c_str());
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("Note", s.save_note, sizeof s.save_note);
    if (ImGui::Button("Save", ImVec2(110, 0))) {
      const std::string name = s.save_name, note = s.save_note;
      const std::string group = groups[(size_t)std::clamp(s.save_group + 1, 1, (int)groups.size() - 1)];
      later.push_back([&a, id, name, group, note]() {
        std::string err;
        if (species_save_to_library(a, id, name, group, note, err).empty()) a.status = "Save species: " + err;
      });
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Export..."))
    later.push_back([&a, id]() {
      const std::string p = dialog_save_file("glTF binary (*.glb)\0*.glb\0Wavefront OBJ (*.obj)\0*.obj\0", "glb", "plant.glb");
      if (p.empty()) return;
      std::string err;
      if (!species_export(a, id, p, err)) a.status = "Export plant: " + err;
    });
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The grown plant as a mesh with its pictures: .glb (embedded) or .obj + .mtl.");
}

void describe_box(App &a, Later &later) {
  EditorState &s = es();
  ImGui::SeparatorText("Grow a plant");
  ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 70.f, 100.f));
  const bool enter = ImGui::InputTextWithHint("##plantwords", "type a plant: old oak, date palm, lavender, saguaro...",
                                              s.words, sizeof s.words, ImGuiInputTextFlags_EnterReturnsTrue);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A plant's name with anything you want of it: old, young, dead, dry, lush,\n"
                      "in spring or autumn, tall, weeping, windswept. Well over a hundred plants\n"
                      "and every kind are known without a model; tick Ask the AI model for the rest.");
  ImGui::SameLine();
  const bool go = ImGui::Button("Grow", ImVec2(-1, 0)) || enter;
  const std::string provider = config().ai.text_provider;
  const bool ready = service_ready(provider);
  ImGui::BeginDisabled(!ready);
  ImGui::Checkbox("Ask the AI model", &s.use_ai);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(ready ? "The text model (%s) describes the plant first: any species, a cultivar, a photograph."
                            : "No text model is ready (%s): Settings > AI services. The plant is grown from its name meanwhile.",
                      provider.c_str());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::SliderInt("##individuals", &s.individuals, 1, 8, "%d plant(s)");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many individuals of the species: each its own seed, standing side by side.");
  ImGui::SameLine();
  ImGui::Checkbox("Scatter", &s.scatter);
  if (s.scatter) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputInt("##scattercount", &s.count, 0, 0);
    s.count = std::clamp(s.count, 1, 50000);
  }
  if (s.use_ai && ready) {
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 70.f, 100.f));
    ImGui::InputTextWithHint("##plantimg", "a photograph of the plant (optional)", s.image, sizeof s.image);
    ImGui::SameLine();
    if (ImGui::Button("...##img", ImVec2(-1, 0)))
      later.push_back([]() {
        const std::string p = dialog_open_file("Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0", nullptr);
        if (!p.empty()) std::snprintf(es().image, sizeof es().image, "%s", p.c_str());
      });
  }
  if (go && s.words[0]) {
    const std::string words = s.words, image = s.image;
    const bool ai = s.use_ai && ready;
    SpeciesMake mk;
    mk.individuals = s.individuals;
    mk.at.scatter = s.scatter;
    mk.at.count = s.count;
    later.push_back([&a, words, image, ai, mk, provider]() {
      std::string err;
      if (ai) {
        const uint64_t job = species_ai_submit(words, image, mk);
        a.status = "asking " + provider + " to describe '" + words + "' (job " + std::to_string(job) + ")";
        return;
      }
      bool matched = false;
      if (!species_create_from_words(a, words, mk, matched, err)) a.status = "Grow: " + err;
    });
  }
}

// The node presets, onto the selected part of this species (the trunk when
// nothing of it is selected, the root when it has no trunk yet).
void preset_row(App &a, gpx::Node &root, Later &later) {
  gpx::Node *target = nullptr;
  if (gpx::Node *sel = a.graph.find_node(a.selected_node))
    if (sel->category == "Plant") {
      if (sel == &root) target = sel;
      else if (const gpx::Node *r = gpx::plant_root_of(a.graph, *sel))
        if (r->id == root.id) target = sel;
    }
  if (!target) target = species_part_find(a.graph, root, "");
  if (!target) return;
  std::string on = target == &root ? std::string("the species root") : gpx::node_display_name(target->type);
  ImGui::SeparatorText(("Add to " + on).c_str());
  const uint64_t id = target->id;
  float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  bool first = true;
  for (const std::string &p : gpx::plant_presets()) {
    const std::string label = pretty(p);
    const float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.f;
    if (!first && ImGui::GetItemRectMax().x + w + 8.f < right) ImGui::SameLine();
    first = false;
    if (ImGui::SmallButton((label + "##preset").c_str()))
      later.push_back([&a, id, p]() {
        std::string err;
        if (!species_add_part(a, id, p, 0, err)) a.status = err;
      });
  }
}

void individual(App &a, gpx::Node &root, Later &later) {
  bool changed = false;
  const uint64_t id = root.id;
  ImGui::SeparatorText("Individual");
  if (gpx::Attribute *seed = root.attrs.find("seed")) {
    int v = (int)seed->seed;
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputInt("##seed", &v)) {
      undo_push_locked(a, "Plant seed");
      seed->seed = (uint32_t)std::max(v, 0);
      changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", seed->tooltip.c_str());
    ImGui::SameLine();
    if (ImGui::Button("New variation"))
      later.push_back([&a, id]() {
        std::string err;
        if (species_variation(a, id, -1, err) < 0) a.status = err;
      });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Another individual of the same species: every random value drawn again.");
    ImGui::SameLine();
    if (ImGui::Button("Flag this plant"))
      later.push_back([&a, id]() {
        std::string err;
        if (!species_flag(a, id, err)) a.status = err;
      });
    const std::vector<long long> flagged = species_flagged(root);
    if (!flagged.empty()) {
      ImGui::TextDisabled("Flagged:");
      for (long long f : flagged) {
        ImGui::SameLine();
        if (ImGui::SmallButton(std::to_string(f).c_str())) {
          undo_push_locked(a, "Plant seed");
          seed->seed = (uint32_t)f;
          changed = true;
        }
      }
    }
  }
  ImGui::SeparatorText("Age, health and season");
  const float max_age = root.attrs.get_f("max_age", 80.f);
  changed |= row_float(a, root, "max_age", "Max age (years)", 1.f, std::max(max_age * 2.f, 100.f), "%.0f");
  changed |= row_float(a, root, "age", "Age (years)", 0.f, std::max(max_age, 0.1f), "%.1f");
  char hl[48];
  std::snprintf(hl, sizeof hl, "%%.2f  %s", health_name(root.attrs.get_f("health", 1.f)));
  changed |= row_float(a, root, "health", "Health", 0.f, 1.f, hl);
  char sl[48];
  std::snprintf(sl, sizeof sl, "%%.2f  %s", season_name(root.attrs.get_f("season", 0.5f)));
  changed |= row_float(a, root, "season", "Season", 0.f, 1.f, sl);
  if (changed) {
    a.graph.mark_dirty(root.id);
    a.request_eval();
  }
}

void wind_and_meshing(App &a, gpx::Node &root) {
  bool changed = false;
  ImGui::SeparatorText("Wind");
  changed |= row_bool(a, root, "receive_wind", "Receive wind");
  ImGui::SameLine();
  ImGui::Checkbox("Preview in the views", &a.plant_wind_preview);
  changed |= row_float(a, root, "wind_strength", "Strength", 0.f, 1.f);
  changed |= row_float(a, root, "wind_direction", "Direction", -180.f, 180.f, "%.0f deg");
  changed |= row_float(a, root, "gust_amplitude", "Gusts", 0.f, 1.f);
  changed |= row_float(a, root, "leaf_flutter_influence", "Leaf flutter", 0.f, 2.f);
  ImGui::SeparatorText("Meshing");
  changed |= row_float(a, root, "mesh_boost", "Detail", -3.f, 3.f, "%+.1f");
  if (changed) {
    a.graph.mark_dirty(root.id);
    a.request_eval();
  }
  const auto mesh = gpx::plant_mesh_for(root.id);
  if (!mesh) {
    ImGui::TextDisabled(a.graph.upstream_node(root, "trunk")
                            ? "Growing..."
                            : "Nothing grows yet: add a trunk above, or type a plant's name.");
    return;
  }
  ImGui::Text("%.2f m tall, %zu triangles, %zu vertices", mesh->height_m, mesh->triangle_count(),
              mesh->vertex_count());
  ImGui::Text("%d parts grown, %d leaves, %d cut", mesh->primitives, mesh->leaves, mesh->cut);
  if (!mesh->warnings.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.2f, 1.f));
    ImGui::TextWrapped("%s", mesh->warnings.c_str());
    ImGui::PopStyleColor();
  }
  if (ImGui::SmallButton("Show the root's parameters")) {
    a.selected_node = root.id;
    a.prop_tab = TAB_NODE;
    a.show_properties = true;
  }
  ImGui::SameLine();
  const int obj = species_object(root.id);
  ImGui::BeginDisabled(obj < 0);
  if (ImGui::SmallButton("Select the plant in the scene") && obj >= 0) {
    scene().selected = obj;
    a.scene_selection_serial++;
  }
  ImGui::EndDisabled();
}

} // namespace

void draw_panel_plant_editor(App &a) {
  plant_wind_preview_set(a.plant_wind_preview);
  if (!a.show_plant_editor) return;
  ImGui::SetNextWindowSize(ImVec2(460, 820), ImGuiCond_FirstUseEver);
  panel_float_prepare(a, "Plant Editor");
  if (!ImGui::Begin("Plant Editor", &a.show_plant_editor)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Plant Editor");
  Later later;
  {
    GraphLease lk(a);
    if (!lk.owns_lock()) {
      ImGui::TextDisabled("The graph is computing; the editor is back in a moment.");
    } else {
      // a part selected anywhere brings its species here
      if (gpx::Node *sel = a.graph.find_node(a.selected_node)) {
        if (sel->type == "PlantSpecies") species_current() = sel->id;
        else if (sel->category == "Plant")
          if (const gpx::Node *r = gpx::plant_root_of(a.graph, *sel)) species_current() = r->id;
      }
      gpx::Node *root = species_find(a.graph, "");
      if (root) species_current() = root->id;
      header(a, root, later);
      describe_box(a, later);
      if (root) {
        preset_row(a, *root, later);
        individual(a, *root, later);
        wind_and_meshing(a, *root);
        if (!plant_editor_parts(a, *root, later)) plant_editor_presets(a, *root, later);
      }
    }
  }
  for (auto &f : later) f();
  // the assistant applies actions that take the graph lock: outside the lease
  if (ImGui::CollapsingHeader("Ask the assistant"))
    ai_assist_bar(a, AiDomain::Plant, "e.g. make it a young birch in spring with a sparse crown");
  ImGui::End();
}

void plant_editor_menu_items(App &a) {
  if (IconMenuItem(Icon::Plant, "Plant Editor", a.show_plant_editor)) {
    a.show_plant_editor = !a.show_plant_editor;
    if (a.show_plant_editor) a.workspace = WS_PLANTS;
  }
  if (ImGui::BeginMenu("New plant species")) {
    for (const std::string &arch : gpx::plant_archetypes())
      if (ImGui::MenuItem(pretty(arch).c_str())) {
        gpx::PlantDescription d;
        gpx::plant_describe_from_words(pretty(arch), d);
        d.archetype = arch;
        d.name = pretty(arch);
        std::string err;
        SpeciesMake mk;
        if (!species_create(a, d, mk, err)) a.status = "New species: " + err;
      }
    ImGui::EndMenu();
  }
}

void plant_editor_tool_buttons(App &a) {
  if (tool_icon(Icon::Node, "##planteditor",
                "Plant Editor\n\nGrow a plant from rules: type its name, then change its\n"
                "age, season, health, wind and every part of it.",
                a.show_plant_editor))
    a.show_plant_editor = !a.show_plant_editor;
}

} // namespace studio
