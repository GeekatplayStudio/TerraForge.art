// Geekatplay TerraForge - the Plant Editor's parts, presets and published
// parameters. Split from panel_plant_editor.cpp for the 500-line rule.
//
// The parts are the species' own graph read as the tree it grows into: the
// root, the trunk on it, the branches on the trunk, the leaves on the
// branches, each with the materials it wears. It is the same shape the
// plant tool draws its graph in, and a plant is easier to think about as a
// tree than as wires. Clicking a part selects its node, so the Properties
// editor shows every parameter of it; the menu on a part adds a part to it,
// stops it growing (bypass) or deletes it.
#include "app.hpp"
#include "plant_species.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <imgui.h>
#include <string>
#include <vector>

namespace studio {

namespace {

using Later = std::vector<std::function<void()>>;

struct Feed {
  gpx::Node *node = nullptr;
  std::string port;
};

// The plant parts feeding `n`, in the order of its inputs.
std::vector<Feed> feeds(gpx::Graph &g, const gpx::Node &n) {
  std::vector<Feed> out;
  for (const gpx::Port &p : n.ports) {
    if (p.dir != gpx::PortDir::In || p.type != gpx::DataType::Plant) continue;
    for (const gpx::Link &l : g.links)
      if (l.to_node == n.id && l.to_port == p.name)
        if (gpx::Node *src = g.find_node(l.from_node)) out.push_back({src, p.name});
  }
  return out;
}

std::string pretty(std::string s) {
  for (char &c : s)
    if (c == '_') c = ' ';
  if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
  return s;
}

std::string label_of(const gpx::Node &n) {
  // The name the person gave the part is the row; only a part still called
  // what it was born as falls back to the kind, so a tree of "Segment,
  // Segment, Segment" becomes "Trunk, Lower branches, Twigs".
  std::string label = gpx::node_display_name(n.type);
  if (label.rfind("Plant ", 0) == 0) label = pretty(label.substr(6));
  const std::string given = n.attrs.get_s("name");
  if (!given.empty() && n.type != "PlantMaterial" && n.type != "PlantSpecies") label = given;
  char extra[64] = "";
  if (n.type == "PlantMaterial") {
    label += ": " + n.attrs.get_s("name");
  } else if (n.type == "PlantSegment" || n.type == "PlantLeaf" || n.type == "PlantCutoutLeaf" ||
             n.type == "PlantFlower" || n.type == "PlantWarpboard") {
    if (const gpx::Attribute *len = n.attrs.find("length"))
      std::snprintf(extra, sizeof extra, len->f >= 1.f ? "  %.1f m" : "  %.0f cm", len->f >= 1.f ? len->f : len->f * 100.f);
  } else if (n.type == "PlantSpecies") {
    label = "Species: " + n.attrs.get_s("name");
  }
  if (const gpx::Attribute *c = n.attrs.find("count"))
    if (n.type != "PlantSpecies") {
      char k[32];
      std::snprintf(k, sizeof k, "  x%.0f", c->f);
      std::strncat(extra, k, sizeof extra - std::strlen(extra) - 1);
    }
  return label + extra;
}

void part_row(App &a, gpx::Node &n, const std::string &port, int depth, uint64_t root_id,
              uint64_t &remove, Later &later) {
  if (depth > 24) return;
  const std::vector<Feed> kids = feeds(a.graph, n);
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (kids.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  if (depth < 3) flags |= ImGuiTreeNodeFlags_DefaultOpen;
  if (a.selected_node == n.id) flags |= ImGuiTreeNodeFlags_Selected;
  std::string shown = label_of(n);
  if (!port.empty() && port.rfind("child", 0) != 0 && port != "trunk") shown = port + ": " + shown;
  if (!n.enabled) shown += "  (not growing)";
  if (!n.error.empty()) shown += "  (!)";
  ImGui::PushID((int)(n.id & 0x7fffffff));
  const bool open = ImGui::TreeNodeEx("##part", flags, "%s", shown.c_str());
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    a.selected_node = n.id;
    a.prop_tab = TAB_NODE;
  }
  if (ImGui::IsItemHovered() && !n.error.empty()) ImGui::SetTooltip("%s", n.error.c_str());
  if (ImGui::BeginPopupContextItem("##partmenu")) {
    a.selected_node = n.id;
    if (ImGui::BeginMenu("Add a part to it")) {
      const uint64_t id = n.id;
      for (const std::string &p : gpx::plant_presets())
        if (ImGui::MenuItem(pretty(p).c_str()))
          later.push_back([&a, id, p]() {
            std::string err;
            if (!species_add_part(a, id, p, 0, err)) a.status = err;
          });
      ImGui::EndMenu();
    }
    if (n.type != "PlantSpecies") {
      bool grows = n.enabled;
      if (ImGui::MenuItem("Grows", nullptr, &grows)) {
        undo_push_locked(a, grows ? "Grow part" : "Stop part growing");
        n.enabled = grows;
        a.graph.mark_dirty(root_id);
        a.request_eval();
      }
    }
    if (ImGui::MenuItem(n.type == "PlantSpecies" ? "Delete the species root" : "Delete this part")) remove = n.id;
    ImGui::EndPopup();
  }
  if (open) {
    for (const Feed &f : kids) part_row(a, *f.node, f.port, depth + 1, root_id, remove, later);
    ImGui::TreePop();
  }
  ImGui::PopID();
}

} // namespace

// True when the root itself was deleted, so the caller stops drawing it.
bool plant_editor_parts(App &a, gpx::Node &root, Later &later) {
  ImGui::SeparatorText("Parts");
  uint64_t remove = 0;
  const uint64_t root_id = root.id;
  part_row(a, root, "", 0, root_id, remove, later);
  if (!remove) return false;
  undo_push_locked(a, "Delete plant part");
  a.graph.remove_node(remove);
  if (a.selected_node == remove) a.selected_node = remove == root_id ? 0 : root_id;
  a.graph_layout_serial++;
  a.request_eval();
  return remove == root_id;
}

void plant_editor_presets(App &a, gpx::Node &root, Later &later) {
  const uint64_t id = root.id;
  ImGui::SeparatorText("Presets");
  static char name[96] = "";
  for (const std::string &p : species_preset_names(root)) {
    ImGui::PushID(p.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(p.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.f);
    if (ImGui::SmallButton("Apply"))
      later.push_back([&a, id, p]() {
        std::string out, err;
        if (!species_preset(a, id, "apply", p, out, err)) a.status = err;
      });
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete"))
      later.push_back([&a, id, p]() {
        std::string out, err;
        if (!species_preset(a, id, "delete", p, out, err)) a.status = err;
      });
    ImGui::PopID();
  }
  ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 70.f, 80.f));
  ImGui::InputTextWithHint("##presetname", "a name: sapling, winter veteran...", name, sizeof name);
  ImGui::SameLine();
  ImGui::BeginDisabled(name[0] == 0);
  if (ImGui::Button("Store", ImVec2(-1, 0))) {
    const std::string n = name;
    later.push_back([&a, id, n]() {
      std::string out, err;
      if (!species_preset(a, id, "store", n, out, err)) a.status = err;
    });
    name[0] = 0;
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Keeps this age, health, season, seed and the published parameters under a name.");

  ImGui::SeparatorText("Published parameters");
  const std::vector<gpx::PlantPublished> pubs = gpx::plant_published(a.graph, root);
  if (pubs.empty()) {
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextDisabled("Publish a part's number from its menu (the v beside it in Properties) to "
                        "gather the parameters that matter here, where presets keep them.");
    ImGui::PopTextWrapPos();
    return;
  }
  std::string last_group;
  for (const gpx::PlantPublished &pp : pubs) {
    gpx::Node *pn = a.graph.find_node(pp.node);
    gpx::Attribute *at = pn ? pn->attrs.find(pp.key) : nullptr;
    if (!at) continue;
    if (pp.group != last_group) {
      last_group = pp.group;
      if (!last_group.empty()) ImGui::TextDisabled("%s", last_group.c_str());
    }
    ImGui::PushID((int)((pp.node * 31u) & 0x7fffffff));
    gpx::Attribute shown = *at;
    shown.label = pp.name.empty() ? at->label : pp.name;
    if (draw_attribute(shown)) {
      undo_push_locked(a, "Change " + shown.label);
      const std::string keep = at->label;
      *at = shown;
      at->label = keep;
      a.graph.mark_dirty(pn->id);
      a.request_eval();
    }
    ImGui::PopID();
  }
}

} // namespace studio
