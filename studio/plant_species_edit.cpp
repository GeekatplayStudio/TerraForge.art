// Geekatplay TerraForge - plant species: the settings a person turns, the
// individuals worth keeping, the parts they add, presets, the library and
// the mesh export (plant_species.hpp).
//
// Everything here edits the species root's own attributes, so it is the
// graph that remembers - a flagged seed, a preset or a new part saves with
// the project and travels in the species file like any other parameter.
#include "plant_species.hpp"
#include "app.hpp"
#include "console.hpp"
#include "mesh_thumbnail.hpp"
#include "plant_library.hpp"
#include "scene.hpp"
#include "stb_image_write.h"
#include "undo.hpp"
#include "plant/plant_schema.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <mutex>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

namespace {

using Lock = std::unique_lock<App::GraphMutex>;

bool lock(App &a, Lock &lk, std::string &err) {
  lk = Lock(a.graph_mtx, std::defer_lock);
  if (lk.try_lock_for(std::chrono::milliseconds(800))) return true;
  err = "the graph is busy, try again";
  return false;
}

gpx::Node *root_node(App &a, uint64_t root, std::string &err) {
  gpx::Node *n = a.graph.find_node(root);
  if (n && n->type == "PlantSpecies") return n;
  err = "no plant species " + std::to_string(root);
  return nullptr;
}

void setf(gpx::Node &n, const char *key, float v) {
  if (gpx::Attribute *at = n.attrs.find(key)) at->f = v;
}

bool port_free(const gpx::Graph &g, uint64_t node, const std::string &port) {
  for (const gpx::Link &l : g.links)
    if (l.to_node == node && l.to_port == port) return false;
  return true;
}

std::string slug_of(const std::string &name) {
  std::string s;
  for (char c : name) {
    const unsigned char u = (unsigned char)c;
    if (std::isalnum(u)) s += (char)std::tolower(u);
    else if (!s.empty() && s.back() != '_') s += '_';
  }
  while (!s.empty() && s.back() == '_') s.pop_back();
  return s.empty() ? "plant" : s;
}

json presets_of(const gpx::Node &root) {
  json j = json::parse(root.attrs.get_s("presets"), nullptr, false);
  return j.is_array() ? j : json::array();
}

uint32_t fresh_seed() {
  static uint32_t s = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ 0x5bd1e995u;
  s = s * 1664525u + 1013904223u;
  uint32_t x = s ^ (s >> 16);
  x *= 0x7feb352du;
  x ^= x >> 15;
  return x % 999983u + 1u;
}

} // namespace

bool species_set(App &a, uint64_t root, const SpeciesSettings &s, std::string &err) {
  Lock lk;
  if (!lock(a, lk, err)) return false;
  gpx::Node *n = root_node(a, root, err);
  if (!n) return false;
  undo_push_locked(a, "Plant settings");
  if (s.max_age >= 0.f) setf(*n, "max_age", std::max(s.max_age, 0.1f));
  const float max_age = n->attrs.get_f("max_age", 80.f);
  if (s.age >= 0.f) setf(*n, "age", std::clamp(s.age, 0.f, max_age));
  if (s.health >= 0.f) setf(*n, "health", std::clamp(s.health, 0.f, 1.f));
  // the year goes round: 1.25 is spring again
  if (s.season >= 0.f) setf(*n, "season", s.season - std::floor(s.season));
  if (s.wind_strength >= 0.f) setf(*n, "wind_strength", std::clamp(s.wind_strength, 0.f, 1.f));
  if (s.wind_direction > -999.f) setf(*n, "wind_direction", std::remainder(s.wind_direction, 360.f));
  if (s.detail > -999.f) setf(*n, "mesh_boost", std::clamp(s.detail, -3.f, 3.f));
  if (s.receive_wind >= 0)
    if (gpx::Attribute *at = n->attrs.find("receive_wind")) at->b = s.receive_wind != 0;
  if (s.seed >= 0)
    if (gpx::Attribute *at = n->attrs.find("seed")) at->seed = (uint32_t)s.seed;
  a.graph.mark_dirty(n->id);
  a.request_eval();
  return true;
}

long long species_variation(App &a, uint64_t root, long long seed, std::string &err) {
  Lock lk;
  if (!lock(a, lk, err)) return -1;
  gpx::Node *n = root_node(a, root, err);
  if (!n) return -1;
  gpx::Attribute *at = n->attrs.find("seed");
  if (!at) {
    err = "the species has no seed";
    return -1;
  }
  undo_push_locked(a, "New plant variation");
  at->seed = seed >= 0 ? (uint32_t)seed : fresh_seed();
  a.graph.mark_dirty(n->id);
  a.request_eval();
  a.status = "individual " + std::to_string(at->seed);
  return (long long)at->seed;
}

std::vector<long long> species_flagged(const gpx::Node &root) {
  std::vector<long long> out;
  std::stringstream ss(root.attrs.get_s("flagged"));
  std::string part;
  while (std::getline(ss, part, ',')) {
    part.erase(std::remove_if(part.begin(), part.end(), [](char c) { return std::isspace((unsigned char)c) != 0; }), part.end());
    if (!part.empty() && std::all_of(part.begin(), part.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; }))
      out.push_back(std::stoll(part));
  }
  return out;
}

bool species_flag(App &a, uint64_t root, std::string &err) {
  Lock lk;
  if (!lock(a, lk, err)) return false;
  gpx::Node *n = root_node(a, root, err);
  if (!n) return false;
  const long long seed = (long long)n->attrs.get_seed("seed");
  std::vector<long long> f = species_flagged(*n);
  if (std::find(f.begin(), f.end(), seed) != f.end()) {
    a.status = "individual " + std::to_string(seed) + " is already flagged";
    return true;
  }
  undo_push_locked(a, "Flag this plant");
  f.push_back(seed);
  std::string s;
  for (long long v : f) s += (s.empty() ? "" : ",") + std::to_string(v);
  if (gpx::Attribute *at = n->attrs.find("flagged")) at->s = s;
  a.status = "flagged individual " + std::to_string(seed);
  return true;
}

gpx::Node *species_part_find(gpx::Graph &g, gpx::Node &root, const std::string &which) {
  const std::string w = which;
  if (w.empty() || w == "trunk") {
    gpx::Node *t = g.upstream_node(root, "trunk");
    return t ? t : (w.empty() ? &root : nullptr);
  }
  if (w == "root" || w == "species") return &root;
  const std::vector<const gpx::Node *> sub = gpx::plant_subtree(g, root);
  if (std::all_of(w.begin(), w.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; })) {
    const uint64_t id = std::stoull(w);
    for (const gpx::Node *n : sub)
      if (n->id == id) return g.find_node(id);
    return nullptr;
  }
  // a node type: the last one of that type, which is the outermost part
  gpx::Node *found = nullptr;
  for (const gpx::Node *n : sub)
    if (n->type == w || n->type == "Plant" + w) found = g.find_node(n->id);
  return found;
}

uint64_t species_add_part(App &a, uint64_t parent_id, const std::string &preset, int slot,
                          std::string &err) {
  const std::string type = gpx::plant_preset_type(preset);
  if (type.empty()) {
    err = "no plant preset '" + preset + "'";
    return 0;
  }
  Lock lk;
  if (!lock(a, lk, err)) return 0;
  gpx::Node *parent = a.graph.find_node(parent_id);
  if (!parent || parent->category != "Plant") {
    err = "no plant part " + std::to_string(parent_id);
    return 0;
  }
  // which input of the parent it goes into
  std::string port;
  auto has_in = [&](const std::string &p) { return parent->port(p, gpx::PortDir::In) != nullptr; };
  if (type == "PlantMaterial") {
    const bool leafy = preset.find("leaf") != std::string::npos;
    const char *const order_seg[] = {leafy ? "blade material" : "material", "material", "material 2",
                                     "material 3", "material 4", "cap material", "blade material 2"};
    for (const char *p : order_seg)
      if (has_in(p) && port_free(a.graph, parent->id, p)) {
        port = p;
        break;
      }
  } else if (parent->type == "PlantSpecies") {
    if (port_free(a.graph, parent->id, "trunk")) port = "trunk";
  } else if (slot > 0) {
    port = "child " + std::to_string(slot);
    if (!has_in(port)) port.clear();
  } else {
    for (int i = 1; i <= 6; ++i) {
      const std::string p = "child " + std::to_string(i);
      if (has_in(p) && port_free(a.graph, parent->id, p)) {
        port = p;
        break;
      }
    }
  }
  if (port.empty()) {
    err = "the " + parent->type.substr(5) + " has no free input for a " + preset;
    return 0;
  }
  undo_push_locked(a, "Add " + preset);
  int siblings = 0;
  for (const gpx::Link &l : a.graph.links)
    if (l.to_node == parent->id) ++siblings;
  gpx::Node *child = a.graph.add_node(type, parent->pos_x - 280.f, parent->pos_y + 150.f * (float)siblings);
  if (!child) {
    err = "could not make a " + type;
    return 0;
  }
  gpx::plant_preset_apply(*child, preset);
  if (!a.graph.add_link(child->id, "plant", parent->id, port)) {
    a.graph.remove_node(child->id);
    err = "could not connect the " + preset + " to " + port;
    return 0;
  }
  a.selected_node = child->id;
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "added a " + preset + " to " + port;
  return child->id;
}

std::vector<std::string> species_preset_names(const gpx::Node &root) {
  std::vector<std::string> out;
  for (const json &p : presets_of(root))
    if (p.is_object()) out.push_back(p.value("name", std::string()));
  return out;
}

bool species_preset(App &a, uint64_t root, const std::string &action, const std::string &name,
                    std::string &out, std::string &err) {
  Lock lk;
  if (!lock(a, lk, err)) return false;
  gpx::Node *n = root_node(a, root, err);
  if (!n) return false;
  json list = presets_of(*n);
  if (action == "list") {
    for (const std::string &s : species_preset_names(*n)) out += s + "\n";
    return true;
  }
  if (name.empty()) {
    err = "a preset needs a name";
    return false;
  }
  auto find = [&]() -> json * {
    for (json &p : list)
      if (p.is_object() && p.value("name", std::string()) == name) return &p;
    return nullptr;
  };
  const std::vector<const gpx::Node *> sub = gpx::plant_subtree(a.graph, *n);
  auto index_of = [&](uint64_t id) {
    for (size_t i = 0; i < sub.size(); ++i)
      if (sub[i]->id == id) return (int)i;
    return -1;
  };
  if (action == "store") {
    json p = {{"name", name}, {"age", n->attrs.get_f("age")}, {"health", n->attrs.get_f("health")},
              {"season", n->attrs.get_f("season")}, {"seed", n->attrs.get_seed("seed")}};
    json values = json::object();
    for (const gpx::PlantPublished &pp : gpx::plant_published(a.graph, *n)) {
      const gpx::Node *pn = a.graph.find_node(pp.node);
      const gpx::Attribute *at = pn ? pn->attrs.find(pp.key) : nullptr;
      const int idx = index_of(pp.node);
      if (!at || idx < 0) continue;
      const float v = at->type == gpx::AttrType::Int || at->type == gpx::AttrType::Choice ? (float)at->i
                      : at->type == gpx::AttrType::Bool                                   ? (at->b ? 1.f : 0.f)
                                                                                          : at->f;
      values[std::to_string(idx) + "/" + pp.key] = v;
    }
    p["values"] = values;
    undo_push_locked(a, "Store plant preset");
    if (json *old = find()) *old = p;
    else list.push_back(p);
  } else if (action == "apply") {
    json *p = find();
    if (!p) {
      err = "no preset called '" + name + "'";
      return false;
    }
    undo_push_locked(a, "Apply plant preset");
    setf(*n, "age", p->value("age", n->attrs.get_f("age")));
    setf(*n, "health", p->value("health", n->attrs.get_f("health")));
    setf(*n, "season", p->value("season", n->attrs.get_f("season")));
    if (gpx::Attribute *s = n->attrs.find("seed")) s->seed = p->value("seed", s->seed);
    if (p->contains("values") && (*p)["values"].is_object())
      for (auto it = (*p)["values"].begin(); it != (*p)["values"].end(); ++it) {
        const std::string k = it.key();
        const size_t slash = k.find('/');
        if (slash == std::string::npos || !it.value().is_number()) continue;
        const int idx = std::atoi(k.substr(0, slash).c_str());
        if (idx < 0 || idx >= (int)sub.size()) continue;
        gpx::Node *pn = a.graph.find_node(sub[(size_t)idx]->id);
        gpx::Attribute *at = pn ? pn->attrs.find(k.substr(slash + 1)) : nullptr;
        if (!at) continue;
        const float v = it.value().get<float>();
        if (at->type == gpx::AttrType::Int || at->type == gpx::AttrType::Choice) at->i = (int)std::lround(v);
        else if (at->type == gpx::AttrType::Bool) at->b = v > 0.5f;
        else at->f = v;
        a.graph.mark_dirty(pn->id);
      }
  } else if (action == "delete") {
    const size_t before = list.size();
    json kept = json::array();
    for (const json &p : list)
      if (!(p.is_object() && p.value("name", std::string()) == name)) kept.push_back(p);
    if (kept.size() == before) {
      err = "no preset called '" + name + "'";
      return false;
    }
    undo_push_locked(a, "Delete plant preset");
    list = kept;
  } else {
    err = "preset action is store, apply, delete or list";
    return false;
  }
  if (gpx::Attribute *at = n->attrs.find("presets")) at->s = list.dump();
  a.graph.mark_dirty(n->id);
  a.request_eval();
  a.status = "preset '" + name + "': " + action;
  return true;
}

std::string species_library_dir() { return (fs::path(plant_library_dir()) / "species").string(); }

std::string species_save_to_library(App &a, uint64_t root, const std::string &name,
                                    const std::string &group, const std::string &note,
                                    std::string &err) {
  Lock lk;
  if (!lock(a, lk, err)) return "";
  gpx::Node *n = root_node(a, root, err);
  if (!n) return "";
  const std::string nm = !name.empty() ? name : !n->attrs.get_s("name").empty() ? n->attrs.get_s("name") : "Plant";
  const std::string slug = slug_of(nm);
  const fs::path dir = fs::path(species_library_dir()) / slug;
  const auto mesh = gpx::plant_mesh_for(root);
  const float height = mesh ? mesh->height_m : 0.f;
  gpx::PlantSpeciesInfo info;
  info.id = slug;
  info.name = nm;
  info.group = !group.empty() ? group : height >= 3.f ? "trees" : height >= 0.8f ? "shrubs" : "ground cover";
  info.note = note;
  info.license = "your own";
  info.height_m = height;
  info.seed = n->attrs.get_seed("seed");
  info.polycount = mesh ? (int)mesh->triangle_count() : 0;
  for (long long s : species_flagged(*n)) info.flagged.push_back((uint32_t)s);
  for (const json &p : presets_of(*n)) {
    if (!p.is_object()) continue;
    gpx::PlantPreset pr;
    pr.name = p.value("name", std::string());
    pr.age = p.value("age", -1.f);
    pr.health = p.value("health", -1.f);
    pr.season = p.value("season", -1.f);
    if (p.contains("values") && p["values"].is_object())
      for (auto it = p["values"].begin(); it != p["values"].end(); ++it)
        if (it.value().is_number()) pr.values.push_back({it.key(), it.value().get<float>()});
    info.presets.push_back(pr);
  }
  const std::string path = gpx::plant_species_save(a.graph, *n, info, dir.string(), err);
  if (path.empty()) return "";
  // the manifest the plant library reads, and the picture its tile shows
  json m = {{"id", slug},          {"name", nm},        {"source", "species"},
            {"group", info.group}, {"model", "species.json"},
            {"license", "your own"}, {"height_m", height}, {"polycount", info.polycount},
            {"note", note.empty() ? std::string("Grown from rules: every seed grows another individual.") : note},
            {"tags", json::array({"species", "procedural"})}};
  const int obj = species_object(root);
  if (obj >= 0 && scene().objects[(size_t)obj].vert_count > 0) {
    const std::vector<uint8_t> rgba = mesh_thumbnail(scene().objects[(size_t)obj], 256);
    if (rgba.size() == 256u * 256u * 4u &&
        stbi_write_png((dir / "thumb.png").string().c_str(), 256, 256, 4, rgba.data(), 256 * 4))
      m["thumbnail"] = "thumb.png";
  }
  std::ofstream(dir / "plant.json", std::ios::binary) << m.dump(1);
  lk.unlock();
  plant_library(true);
  a.status = "saved " + nm + " to the plant library: " + dir.string();
  log_info("plants", a.status);
  return path;
}

bool species_export(App &a, uint64_t root, const std::string &path, std::string &err) {
  const auto mesh = gpx::plant_mesh_for(root);
  if (!mesh || mesh->vertex_count() == 0) {
    err = "the species has not grown yet: evaluate the graph first";
    return false;
  }
  if (!gpx::plant_mesh_write(*mesh, path, err)) return false;
  a.status = "exported the plant to " + path + " (" + std::to_string(mesh->triangle_count()) + " triangles)";
  return true;
}

} // namespace studio
