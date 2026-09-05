// Geekatplay TerraForge - the Assets tab of the Material Browser: every
// indexed asset - materials, meshes, textures, layouts - found by typing what
// it is, not where it was put.
//
// The search box is the whole interface: type, and the tiles reorder by
// how well they match; a kind filter narrows; a tile hovers into a card
// with its words and tags, double-clicks open, right-clicks tag, note,
// trash or restore. Roots are managed from the small gear at the end of
// the row. Nothing here deletes a file.
#include "app.hpp"
#include "asset_store.hpp"
#include "i18n.hpp"
#include "material_ui.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <string>

namespace studio {

namespace {

const char *KINDS[] = {"all", "material", "mesh", "texture", "layout", "macro", "other"};

struct AssetsUi {
  char query[256] = "";
  int kind = 0;
  bool show_trash = false;
  char tag[96] = "";
  char note[512] = "";
  char root_path[512] = "";
  int root_kind = 1;
  // the results, recomputed when the query or the index changes
  std::vector<gpx::AssetHit> hits;
  std::string last_query;
  int last_kind = -1;
  bool last_trash = false;
  size_t last_count = 0;
};
AssetsUi g_ui;

void refresh_hits() {
  gpx::AssetIndex &ix = asset_index();
  std::string kind = g_ui.kind == 0 ? "" : KINDS[g_ui.kind];
  if (g_ui.last_query == g_ui.query && g_ui.last_kind == g_ui.kind &&
      g_ui.last_trash == g_ui.show_trash && g_ui.last_count == ix.records.size())
    return;
  g_ui.hits = ix.search(g_ui.query, 400, kind, g_ui.show_trash);
  g_ui.last_query = g_ui.query;
  g_ui.last_kind = g_ui.kind;
  g_ui.last_trash = g_ui.show_trash;
  g_ui.last_count = ix.records.size();
}

void invalidate() { g_ui.last_count = (size_t)-1; }
bool g_focus = false;

} // namespace

void assets_tab_show(const std::string &query) {
  std::strncpy(g_ui.query, query.c_str(), sizeof g_ui.query - 1);
  g_ui.query[sizeof g_ui.query - 1] = 0;
  g_focus = true;
}
bool assets_tab_take_focus() {
  bool f = g_focus;
  g_focus = false;
  return f;
}

namespace {

void hover_card(const gpx::AssetRecord &r, unsigned tex) {
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(r.name.c_str());
  ImGui::TextDisabled("%s%s", r.kind.c_str(), r.trashed ? tr("  (in trash)") : "");
  if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(192, 192), ImVec2(0, 1), ImVec2(1, 0));
  if (!r.tags.empty()) {
    std::string t;
    for (const std::string &s : r.tags) t += (t.empty() ? "" : ", ") + s;
    ImGui::Text(tr("tags: %s"), t.c_str());
  }
  if (!r.description.empty()) ImGui::TextWrapped("%s", r.description.c_str());
  ImGui::TextDisabled("%s", r.path.c_str());
  ImGui::TextDisabled("%s", tr("double-click: open\nright-click: tag, note, trash"));
  ImGui::EndTooltip();
}

void context_menu(App &a, const gpx::AssetRecord &rec) {
  gpx::AssetIndex &ix = asset_index();
  const std::string id = rec.id;
  if (ImGui::MenuItem(tr("Open"))) {
    std::string err;
    if (!asset_open(a, id, err)) a.status = err;
  }
  ImGui::Separator();
  ImGui::SetNextItemWidth(160);
  if (ImGui::InputTextWithHint("##tag", tr("add a tag"), g_ui.tag, sizeof g_ui.tag,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    ix.add_tag(id, g_ui.tag);
    g_ui.tag[0] = 0;
    asset_save();
    invalidate();
  }
  for (const std::string &t : std::vector<std::string>(rec.tags))
    if (ImGui::MenuItem((std::string(tr("remove tag")) + " '" + t + "'").c_str())) {
      ix.remove_tag(id, t);
      asset_save();
      invalidate();
    }
  ImGui::SetNextItemWidth(260);
  if (ImGui::IsWindowAppearing())
    std::strncpy(g_ui.note, rec.description.c_str(), sizeof g_ui.note - 1);
  if (ImGui::InputTextWithHint("##note", tr("a note about it"), g_ui.note, sizeof g_ui.note,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    ix.set_description(id, g_ui.note);
    asset_save();
    invalidate();
  }
  ImGui::Separator();
  if (!rec.trashed && ImGui::MenuItem(tr("Move to trash"))) {
    ix.trash(id);
    asset_save();
    invalidate();
  }
  if (rec.trashed && ImGui::MenuItem(tr("Restore from trash"))) {
    ix.restore(id);
    asset_save();
    invalidate();
  }
}

void roots_popup() {
  gpx::AssetIndex &ix = asset_index();
  ImGui::TextDisabled("%s", tr("Folders the index watches"));
  for (size_t i = 0; i < ix.roots.size(); ++i) {
    ImGui::PushID((int)i);
    ImGui::Text("%-9s %s", ix.roots[i].kind.c_str(), ix.roots[i].path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("remove"))) {
      asset_remove_root(ix.roots[i].path);
      invalidate();
      ImGui::PopID();
      break;
    }
    ImGui::PopID();
  }
  ImGui::Separator();
  ImGui::SetNextItemWidth(360);
  ImGui::InputTextWithHint("##root", tr("folder to add"), g_ui.root_path, sizeof g_ui.root_path);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::Combo("##rkind", &g_ui.root_kind, tr_combo(KINDS + 1, (int)(sizeof KINDS / sizeof *KINDS) - 1).c_str());
  ImGui::SameLine();
  if (ImGui::Button(tr("Add"))) {
    std::string err;
    if (asset_add_root(g_ui.root_path, KINDS[g_ui.root_kind + 1], err)) g_ui.root_path[0] = 0;
    invalidate();
  }
  if (ImGui::Button(tr("Rescan all"))) {
    asset_rescan();
    invalidate();
  }
  ImGui::SameLine();
  ImGui::TextDisabled(tr("%zu assets, %s"), ix.records.size(), asset_index_file().c_str());
}

} // namespace

void draw_assets_tab(App &a, float cell) {
  gpx::AssetIndex &ix = asset_index();
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##q", tr("find by name, folder, tag or note..."), g_ui.query,
                           sizeof g_ui.query);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::Combo("##kind", &g_ui.kind, tr_combo(KINDS, (int)(sizeof KINDS / sizeof *KINDS)).c_str());
  ImGui::SameLine();
  ImGui::Checkbox(tr("trash"), &g_ui.show_trash);
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Rescan"))) {
    asset_rescan();
    invalidate();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(tr("Folders..."))) ImGui::OpenPopup("##roots");
  if (ImGui::BeginPopup("##roots")) {
    roots_popup();
    ImGui::EndPopup();
  }
  refresh_hits();
  if (g_ui.hits.empty()) {
    ImGui::TextDisabled("%s", ix.records.empty()
                            ? tr("Nothing indexed yet. Folders... adds a place to look.")
                            : tr("Nothing matches."));
    return;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%zu", g_ui.hits.size());
  float avail = ImGui::GetContentRegionAvail().x;
  int cols = std::max(1, (int)(avail / (cell + 10)));
  int i = 0;
  // the hits hold indices, and a context action can reorder the records;
  // work from a copy of the ids so a menu cannot invalidate its own tile
  std::vector<gpx::AssetHit> hits = g_ui.hits;
  for (const gpx::AssetHit &h : hits) {
    if (h.index >= ix.records.size()) continue;
    const gpx::AssetRecord &r = ix.records[h.index];
    if (i % cols != 0) ImGui::SameLine();
    ImGui::PushID((int)h.index);
    ImGui::BeginGroup();
    unsigned tex = asset_thumb_texture(r);
    if (r.trashed) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
    if (tex)
      ImGui::ImageButton("##t", (ImTextureID)(intptr_t)tex, ImVec2(cell, cell),
                         ImVec2(0, 1), ImVec2(1, 0));
    else
      ImGui::Button(tr(r.kind.c_str()), ImVec2(cell + 8, cell + 8));
    if (r.trashed) ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) {
      hover_card(r, tex);
      if (ImGui::IsMouseDoubleClicked(0)) {
        std::string err;
        if (!asset_open(a, r.id, err)) a.status = err;
      }
    }
    if (ImGui::BeginPopupContextItem("##ctx")) {
      context_menu(a, r);
      ImGui::EndPopup();
    }
    std::string label = r.name.size() > 14 ? r.name.substr(0, 13) + "~" : r.name;
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndGroup();
    ImGui::PopID();
    ++i;
  }
}

} // namespace studio
