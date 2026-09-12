// Geekatplay TerraForge - the plant library as operations, so the assistant,
// the Python API and MCP can list, place, fetch and record plants the way the
// Plants workspace does.
#include "app.hpp"
#include "plant_library.hpp"
#include "plant_place.hpp"
#include <algorithm>
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

json plant_json(const PlantEntry &p) {
  json j = {{"id", p.id},         {"name", p.name},       {"source", p.source},
            {"group", p.group},   {"license", p.license}, {"height_m", p.height_m},
            {"polycount", p.polycount}};
  if (!p.url.empty()) j["url"] = p.url;
  if (!p.variants.empty()) {
    json vs = json::array();
    for (const PlantVariant &v : p.variants)
      vs.push_back({{"id", v.id}, {"name", v.name}, {"height_m", v.height_m}});
    j["variants"] = vs;
  }
  return j;
}

} // namespace

int ai_plant_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "plant_library") {
    const std::vector<PlantEntry> &lib = plant_library(act.value("rescan", false));
    const std::string query = act.value("query", std::string());
    const std::string group = act.value("group", std::string());
    const std::string source = act.value("source", std::string());
    const size_t limit = (size_t)std::max(act.value("limit", 200), 1);
    json list = json::array();
    std::string line;
    for (const PlantEntry &p : lib) {
      if (!plant_matches(p, query)) continue;
      if (!group.empty() && group != "all" && p.group != group) continue;
      if (!source.empty() && p.source != source) continue;
      if (list.size() >= limit) break;
      list.push_back(plant_json(p));
      line += (line.empty() ? "" : "; ") + p.id;
    }
    a.api_reply = json{{"plants", list}, {"library", plant_library_dir()}}.dump();
    a.status = list.empty() ? "no plant matches '" + query + "'"
                            : std::to_string(list.size()) + " plant(s): " + line;
    return 1;
  }

  if (op == "plant_add") {
    const std::string want = act.value("plant", act.value("id", std::string()));
    const PlantEntry *p = plant_find(want);
    if (!p) {
      err = "plant_add: no plant '" + want + "' (plant_library lists them)";
      return 0;
    }
    PlantPlace at;
    if (act.contains("position") && act["position"].is_array() && act["position"].size() >= 3) {
      at.at_view = false;
      for (int k = 0; k < 3; ++k) at.pos[k] = act["position"][(size_t)k].get<float>();
    }
    at.size = act.value("size", 1.f);
    at.heading_deg = act.value("heading_deg", 0.f);
    at.scatter = act.value("scatter", false);
    at.count = act.value("count", 300);
    at.variant = act.value("variant", std::string());
    at.name = act.value("name", std::string());
    return plant_add(a, *p, at, err) >= 0 ? 1 : 0;
  }

  if (op == "plant_fetch") {
    if (act.value("cancel", false)) {
      plant_fetch_cancel();
      a.status = "plant download cancelled";
      return 1;
    }
    std::vector<std::string> ids;
    if (act.contains("ids") && act["ids"].is_array())
      for (const json &id : act["ids"])
        if (id.is_string()) ids.push_back(id.get<std::string>());
    if (!plant_fetch_start(ids, act.value("res", std::string("1k")), err)) return 0;
    a.status = "downloading " + (ids.empty() ? std::string("the free CC0 plants") : std::to_string(ids.size()) + " plant(s)") +
               " into " + plant_library_dir();
    return 1;
  }

  if (op == "plant_record") {
    const std::string path = act.value("path", std::string());
    if (path.empty()) {
      err = "plant_record needs the model's 'path'";
      return 0;
    }
    const std::string id = plant_record_model(plant_library_dir(), path, act.value("name", std::string()),
                                              act.value("height_m", 0.f), err);
    if (id.empty()) return 0;
    plant_library(true);
    a.status = "recorded " + id + " in the plant library";
    return 1;
  }

  return -1;
}

} // namespace studio
