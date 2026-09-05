// Geekatplay TerraForge - the asset manager as operations, so the assistant,
// the Python API and MCP can search, tag and open what the Assets tab can.
#include "app.hpp"
#include "asset_store.hpp"
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

int ai_asset_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "asset_search") {
    gpx::AssetIndex &ix = asset_index();
    std::string q = act.value("query", std::string());
    std::string kind = act.value("kind", std::string());
    if (kind == "all") kind.clear();
    size_t limit = (size_t)act.value("limit", 20);
    bool trash = act.value("include_trashed", false);
    std::vector<gpx::AssetHit> hits = ix.search(q, limit, kind, trash);
    std::string line;
    for (const gpx::AssetHit &h : hits) {
      const gpx::AssetRecord &r = ix.records[h.index];
      char buf[64];
      snprintf(buf, sizeof buf, " %.2f", h.score);
      line += (line.empty() ? "" : "; ") + r.id + " (" + r.name + buf + ")";
    }
    a.status = hits.empty() ? "no asset matches '" + q + "'"
                            : std::to_string(hits.size()) + " asset(s): " + line;
    a.show_material_browser = true;
    assets_tab_show(q);
    return 1;
  }

  if (op == "asset_open") {
    std::string id = act.value("id", std::string());
    if (id.empty()) {
      err = "asset_open needs the asset 'id' (kind/relative path, from asset_search)";
      return 0;
    }
    return asset_open(a, id, err) ? 1 : 0;
  }

  if (op == "asset_tag" || op == "asset_untag" || op == "asset_note") {
    std::string id = act.value("id", std::string());
    gpx::AssetIndex &ix = asset_index();
    bool ok = false;
    if (op == "asset_tag") ok = ix.add_tag(id, act.value("tag", std::string()));
    else if (op == "asset_untag") ok = ix.remove_tag(id, act.value("tag", std::string()));
    else ok = ix.set_description(id, act.value("text", std::string()));
    if (!ok) {
      err = op + ": no asset '" + id + "' (or nothing to change)";
      return 0;
    }
    asset_save();
    a.status = op + " " + id;
    return 1;
  }

  if (op == "asset_trash" || op == "asset_restore") {
    std::string id = act.value("id", std::string());
    gpx::AssetIndex &ix = asset_index();
    bool ok = op == "asset_trash" ? ix.trash(id) : ix.restore(id);
    if (!ok) {
      err = op + ": no asset '" + id + "' in the state that allows it";
      return 0;
    }
    asset_save();
    a.status = (op == "asset_trash" ? "moved to trash: " : "restored: ") + id;
    return 1;
  }

  if (op == "asset_rescan") {
    size_t n = asset_rescan();
    a.status = "asset index: " + std::to_string(n) + " record(s)";
    return 1;
  }

  if (op == "asset_add_root") {
    std::string path = act.value("path", std::string());
    std::string kind = act.value("kind", std::string("other"));
    if (!asset_add_root(path, kind, err)) return 0;
    a.status = "watching " + path + " as " + kind;
    return 1;
  }

  if (op == "asset_remove_root") {
    std::string path = act.value("path", std::string());
    if (!asset_remove_root(path)) {
      err = "no root '" + path + "'";
      return 0;
    }
    a.status = "no longer watching " + path;
    return 1;
  }

  if (op == "asset_roots") {
    std::string line;
    for (const gpx::AssetRoot &r : asset_index().roots)
      line += (line.empty() ? "" : "; ") + r.kind + ": " + r.path;
    a.status = line.empty() ? "no roots" : line;
    return 1;
  }

  return -1;
}

} // namespace studio
