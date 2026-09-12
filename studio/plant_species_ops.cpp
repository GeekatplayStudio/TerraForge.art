// Geekatplay TerraForge - plant species as operations, so the assistant, the
// Python API and MCP can grow, vary, save and export plants the way the
// Plant Editor does (plant_species.hpp does the work; this reads the
// actions and writes the replies).
//
// A species is named by "species": its root's id, its species name, or the
// name of the object its plant is; omitted, the Plant Editor's current one.
#include "app.hpp"
#include "config.hpp"
#include "plant_library.hpp"
#include "plant_species.hpp"
#include "scene.hpp"
#include <algorithm>
#include <chrono>
#include <json.hpp>
#include <mutex>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

SpeciesMake make_from(const json &act) {
  SpeciesMake mk;
  mk.place = act.value("place", true);
  if (act.contains("position") && act["position"].is_array() && act["position"].size() >= 3) {
    mk.at.at_view = false;
    for (int k = 0; k < 3; ++k) mk.at.pos[k] = act["position"][(size_t)k].get<float>();
  }
  mk.at.size = act.value("size", 1.f);
  mk.at.heading_deg = act.value("heading_deg", 0.f);
  mk.at.scatter = act.value("scatter", false);
  mk.at.count = std::clamp(act.value("count", 300), 1, 50000);
  mk.at.name = act.value("name", std::string());
  mk.individuals = std::clamp(act.value("individuals", 1), 1, 16);
  return mk;
}

std::string species_arg(const json &act) {
  if (!act.contains("species")) return "";
  const json &s = act["species"];
  if (s.is_number_unsigned() || s.is_number_integer()) return std::to_string(s.get<long long>());
  return s.is_string() ? s.get<std::string>() : "";
}

json description_json(const gpx::PlantDescription &d) {
  const json j = json::parse(gpx::plant_description_json(d), nullptr, false);
  return j.is_discarded() ? json::object() : j;
}

json grown(uint64_t root) {
  json j = {{"species", root}};
  if (const auto m = gpx::plant_mesh_for(root)) {
    j["height_m"] = m->height_m;
    j["triangles"] = m->triangle_count();
    j["vertices"] = m->vertex_count();
    j["primitives"] = m->primitives;
    j["leaves"] = m->leaves;
    if (!m->warnings.empty()) j["warnings"] = m->warnings;
  }
  const int obj = species_object(root);
  if (obj >= 0) j["object"] = scene().objects[(size_t)obj].name;
  return j;
}

} // namespace

int ai_plant_species_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "plant_species_new") {
    const SpeciesMake mk = make_from(act);
    uint64_t root = 0;
    if (act.contains("description") && act["description"].is_object()) {
      gpx::PlantDescription d;
      if (!gpx::plant_description_parse(act["description"].dump(), d, err)) return 0;
      if (!mk.at.name.empty()) d.name = mk.at.name;
      root = species_create(a, d, mk, err);
    } else if (act.contains("archetype")) {
      gpx::PlantDescription d;
      d.archetype = act.value("archetype", std::string("broadleaf_tree"));
      d.name = mk.at.name.empty() ? d.archetype : mk.at.name;
      const auto &known = gpx::plant_archetypes();
      if (std::find(known.begin(), known.end(), d.archetype) == known.end()) {
        err = "plant_species_new: no archetype '" + d.archetype + "'";
        return 0;
      }
      if (act.contains("height_m")) d.height_m = act.value("height_m", d.height_m);
      root = species_create(a, d, mk, err);
    } else {
      const std::string words = act.value("words", act.value("prompt", std::string()));
      if (words.empty()) {
        err = "plant_species_new needs 'words' (a plant's name), a 'description' or an 'archetype'";
        return 0;
      }
      if (act.value("ai", false)) {
        const std::string provider = config().ai.text_provider;
        if (!service_ready(provider)) {
          err = "plant_species_new: the text model '" + provider + "' is not ready (Settings > AI services); "
                "leave out \"ai\" to grow the plant from its name";
          return 0;
        }
        const uint64_t job = species_ai_submit(words, act.value("image", std::string()), mk);
        a.status = "asking " + provider + " to describe '" + words + "' (job " + std::to_string(job) + ")";
        a.api_reply = json{{"job", job}}.dump();
        return 1;
      }
      bool matched = false;
      root = species_create_from_words(a, words, mk, matched, err);
      if (root) {
        json r = grown(root);
        r["known_plant"] = matched;
        a.api_reply = r.dump();
        return 1;
      }
    }
    if (!root) return 0;
    a.api_reply = grown(root).dump();
    return 1;
  }

  if (op == "plant_species_describe") {
    const std::string words = act.value("words", act.value("prompt", std::string()));
    gpx::PlantDescription d;
    const bool matched = gpx::plant_describe_from_words(words, d);
    a.api_reply = json{{"description", description_json(d)}, {"known_plant", matched}}.dump();
    a.status = (matched ? "described " : "guessed ") + d.name + " as a " + d.archetype;
    return 1;
  }

  if (op == "plant_forest") {
    ForestPlan plan;
    plan.words = act.value("words", act.value("prompt", std::string()));
    if (act.contains("species")) {
      std::string e;
      plan.species = species_resolve(a, species_arg(act), e);
      if (!plan.species && plan.words.empty()) {
        err = e.empty() ? "plant_forest: no such species" : e;
        return 0;
      }
    }
    if (!plan.species && plan.words.empty()) {
      err = "plant_forest needs 'words' (a plant's name) or 'species'";
      return 0;
    }
    plan.individuals = std::clamp(act.value("individuals", 5), 1, 8);
    plan.area_m = std::clamp(act.value("area_m", 1200.f), 20.f, 100000.f);
    plan.unbounded = act.value("unbounded", true);
    plan.density_ha = std::max(0.f, act.value("density_ha", 0.f));
    plan.spacing_m = std::max(0.f, act.value("spacing_m", 0.f));
    plan.clumping = std::clamp(act.value("clumping", 0.55f), 0.f, 1.f);
    plan.clump_size_m = std::max(0.f, act.value("clump_size_m", 0.f));
    plan.altitude_lo = std::clamp(act.value("altitude_lo", 0.02f), 0.f, 1.f);
    plan.altitude_hi = std::clamp(act.value("altitude_hi", 0.75f), 0.f, 1.f);
    plan.max_slope_deg = std::clamp(act.value("max_slope_deg", 34.f), 0.f, 90.f);
    plan.size_variation = std::clamp(act.value("size_variation", 0.35f), 0.f, 1.f);
    ForestResult res;
    if (!plant_forest(a, plan, res, err)) return 0;
    json ids = json::array();
    for (uint64_t id : res.individuals) ids.push_back(id);
    a.api_reply = json{{"layer", res.layer}, {"individuals", ids}, {"kinds", res.kinds},
                       {"known_plant", res.known}}
                      .dump();
    return 1;
  }

  if (op == "plant_species_list") {
    json in_graph = json::array();
    {
      std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
      if (!lk.try_lock_for(std::chrono::milliseconds(800))) {
        err = "the graph is busy, try again";
        return 0;
      }
      for (const SpeciesRef &r : species_in_graph(a.graph)) {
        json j = grown(r.root);
        j["name"] = r.name;
        in_graph.push_back(j);
      }
    }
    json library = json::array();
    for (const PlantEntry &p : plant_library())
      if (p.source == "species")
        library.push_back({{"plant", p.id}, {"name", p.name}, {"group", p.group}, {"height_m", p.height_m}});
    a.api_reply = json{{"graph", in_graph}, {"library", library}, {"archetypes", gpx::plant_archetypes()},
                       {"presets", gpx::plant_presets()}}.dump();
    a.status = std::to_string(in_graph.size()) + " species in the graph, " + std::to_string(library.size()) +
               " in the library";
    return 1;
  }

  if (op == "plant_species_load") {
    std::string path = act.value("path", std::string());
    if (path.empty()) {
      const std::string want = act.value("plant", std::string());
      const PlantEntry *p = plant_find(want);
      if (!p || p->source != "species") {
        err = "plant_species_load: no species '" + want + "' in the library (plant_species_list lists them)";
        return 0;
      }
      path = p->model;
    }
    const uint64_t root = species_load(a, path, make_from(act), err);
    if (!root) return 0;
    a.api_reply = grown(root).dump();
    return 1;
  }

  // everything below edits a species that exists
  const bool edits = op == "plant_species_set" || op == "plant_species_variation" ||
                     op == "plant_species_individuals" || op == "plant_part_add" ||
                     op == "plant_species_preset" || op == "plant_species_save" ||
                     op == "plant_species_export";
  if (!edits) return -1;
  const uint64_t root = species_resolve(a, species_arg(act), err);
  if (!root) return 0;

  if (op == "plant_species_set") {
    SpeciesSettings s;
    if (act.contains("age")) s.age = act.value("age", 0.f);
    if (act.contains("max_age")) s.max_age = act.value("max_age", 80.f);
    if (act.contains("health")) s.health = act.value("health", 1.f);
    if (act.contains("season")) s.season = act.value("season", 0.5f);
    if (act.contains("seed")) s.seed = act.value("seed", (long long)1);
    if (act.contains("wind_strength")) s.wind_strength = act.value("wind_strength", 0.3f);
    if (act.contains("wind_direction")) s.wind_direction = act.value("wind_direction", 0.f);
    if (act.contains("detail")) s.detail = act.value("detail", 0.f);
    if (act.contains("receive_wind")) s.receive_wind = act.value("receive_wind", true) ? 1 : 0;
    if (!species_set(a, root, s, err)) return 0;
    a.status = "plant settings changed";
    return 1;
  }
  if (op == "plant_species_variation") {
    if (act.value("flag", false)) return species_flag(a, root, err) ? 1 : 0;
    const long long seed = species_variation(a, root, act.value("seed", (long long)-1), err);
    if (seed < 0) return 0;
    a.api_reply = json{{"species", root}, {"seed", seed}}.dump();
    return 1;
  }
  if (op == "plant_species_individuals") {
    SpeciesMake mk = make_from(act);
    const int made = species_add_individuals(a, root, std::clamp(act.value("count", 2), 1, 32), mk, err);
    return made > 0 ? 1 : 0;
  }
  if (op == "plant_part_add") {
    uint64_t parent = 0;
    {
      std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
      if (!lk.try_lock_for(std::chrono::milliseconds(800))) {
        err = "the graph is busy, try again";
        return 0;
      }
      gpx::Node *r = a.graph.find_node(root);
      std::string which;
      if (act.contains("parent"))
        which = act["parent"].is_string() ? act["parent"].get<std::string>()
                                          : std::to_string(act["parent"].get<long long>());
      gpx::Node *p = r ? species_part_find(a.graph, *r, which) : nullptr;
      if (!p) {
        err = "plant_part_add: no part '" + which + "' in the species";
        return 0;
      }
      parent = p->id;
    }
    const uint64_t id = species_add_part(a, parent, act.value("preset", std::string()), act.value("slot", 0), err);
    if (!id) return 0;
    a.api_reply = json{{"node", id}, {"parent", parent}}.dump();
    return 1;
  }
  if (op == "plant_species_preset") {
    std::string out;
    if (!species_preset(a, root, act.value("action", std::string("list")), act.value("name", std::string()), out, err))
      return 0;
    if (!out.empty()) a.api_reply = json{{"presets", out}}.dump();
    return 1;
  }
  if (op == "plant_species_save") {
    const std::string path = species_save_to_library(a, root, act.value("name", std::string()),
                                                     act.value("group", std::string()),
                                                     act.value("note", std::string()), err);
    if (path.empty()) return 0;
    a.api_reply = json{{"path", path}}.dump();
    return 1;
  }
  if (op == "plant_species_export") {
    const std::string path = act.value("path", std::string());
    if (path.empty()) {
      err = "plant_species_export needs a 'path' ending .glb, .gltf or .obj";
      return 0;
    }
    return species_export(a, root, path, err) ? 1 : 0;
  }
  return -1;
}

} // namespace studio
