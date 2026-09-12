// Geekatplay TerraForge - the plant library. See plant_library.hpp.
#include "plant_library.hpp"
#include "gpx/mesh.hpp"
#include "gpx/mesh_io.hpp"
#include "paths.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace studio {

namespace {

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string str(const json &j, const char *key, const std::string &fallback = "") {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

float num(const json &j, const char *key, float fallback = 0.f) {
  auto it = j.find(key);
  return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

std::vector<std::string> words(const json &j, const char *key) {
  std::vector<std::string> out;
  auto it = j.find(key);
  if (it != j.end() && it->is_array())
    for (const json &w : *it)
      if (w.is_string()) out.push_back(w.get<std::string>());
  return out;
}

// A file the manifest names, relative to its folder unless it is absolute
// (a recorded model stays where the user keeps it).
std::string resolve(const std::string &folder, const std::string &name) {
  if (name.empty()) return "";
  const fs::path p(name);
  return p.is_absolute() ? p.string() : (fs::path(folder) / p).string();
}

bool exists(const std::string &path) {
  std::error_code ec;
  return !path.empty() && fs::is_regular_file(path, ec);
}

// The shelf a plant with no group of its own goes on, by its size.
std::string group_by_height(float h) {
  if (h >= 3.f) return "trees";
  if (h >= 0.8f) return "shrubs";
  return "ground cover";
}

} // namespace

const std::vector<std::string> &plant_groups() {
  static const std::vector<std::string> G = {"all",     "trees", "shrubs",   "ground cover",
                                             "flowers", "grass", "deadwood", "rocks"};
  return G;
}

std::string plant_library_dir() {
  return (data_dir() / "library" / "plants").string();
}

std::vector<PlantEntry> plant_builtins() {
  struct B {
    const char *kind, *name, *group, *note;
  };
  // the kinds scene_plants.cpp builds, with the words the Add menu uses
  static const B K[] = {
      {"pine", "Pine", "trees", "A conifer: tiers of ragged, drooping branches."},
      {"juniper", "Juniper", "trees", "A desert juniper or pinyon: a twisted trunk under a lumpy crown."},
      {"palm", "Palm", "trees", "A leaning ringed trunk under arching fronds."},
      {"bush", "Bush", "shrubs", "A shrub: a cluster of leafy lumps."},
      {"fern", "Fern", "ground cover", "A rosette of arching fronds."},
      {"grass", "Grass tuft", "grass", "A tuft of dry grass blades."},
      {"boulder", "Boulder", "rocks", "A fractured, flattened sandstone boulder."},
  };
  std::vector<PlantEntry> out;
  for (const B &b : K) {
    PlantEntry e;
    e.id = std::string("builtin/") + b.kind;
    e.name = b.name;
    e.source = "builtin";
    e.group = b.group;
    e.kind = b.kind;
    e.license = "built in";
    e.note = std::string(b.note) + " Built from its kind, so it needs no file; every seed "
                                   "grows another one.";
    e.height_m = scene_plant_size_m(b.kind);
    e.tags = {b.kind, "procedural"};
    out.push_back(std::move(e));
  }
  return out;
}

bool plant_manifest_parse(const std::string &text, const std::string &folder,
                          PlantEntry &out, std::string &err) {
  const json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    err = "plant.json is not a JSON object";
    return false;
  }
  const std::string id = str(j, "id"), model = str(j, "model");
  if (id.empty() || model.empty()) {
    err = "a plant manifest names its id and its model";
    return false;
  }
  PlantEntry e;
  e.source = str(j, "source", "user");
  e.id = e.source + "/" + id;
  e.name = str(j, "name", id);
  e.folder = folder;
  e.model = resolve(folder, model);
  const std::string thumb = resolve(folder, str(j, "thumbnail"));
  if (exists(thumb)) e.thumb = thumb;
  e.license = str(j, "license", e.source == "user" ? "your own" : "");
  e.url = str(j, "url");
  e.note = str(j, "note");
  e.authors = words(j, "authors");
  e.categories = words(j, "categories");
  e.tags = words(j, "tags");
  e.height_m = std::max(num(j, "height_m"), 0.f);
  e.unit_m = num(j, "unit_m", 1.f) > 0.f ? num(j, "unit_m", 1.f) : 1.f;
  e.polycount = (int)num(j, "polycount");
  e.group = str(j, "group", group_by_height(e.height_m));
  auto vs = j.find("variants");
  if (vs != j.end() && vs->is_array())
    for (const json &v : *vs) {
      if (!v.is_object() || str(v, "model").empty()) continue;
      PlantVariant pv;
      pv.id = str(v, "id");
      pv.name = str(v, "name", pv.id);
      pv.model = resolve(folder, str(v, "model"));
      pv.height_m = num(v, "height_m");
      pv.width_m = num(v, "width_m");
      pv.polycount = (int)num(v, "polycount");
      e.variants.push_back(std::move(pv));
    }
  // a, b, c rather than the order the scan happened to lay them out in
  std::stable_sort(e.variants.begin(), e.variants.end(),
                   [](const PlantVariant &x, const PlantVariant &y) { return lower(x.name) < lower(y.name); });
  out = std::move(e);
  return true;
}

std::vector<PlantEntry> plant_scan(const std::string &root) {
  std::vector<PlantEntry> out;
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return out;
  for (const fs::directory_entry &src : fs::directory_iterator(root, ec)) {
    if (!src.is_directory(ec)) continue;
    for (const fs::directory_entry &dir : fs::directory_iterator(src.path(), ec)) {
      const fs::path manifest = dir.path() / "plant.json";
      if (!fs::is_regular_file(manifest, ec)) continue;
      std::ifstream f(manifest, std::ios::binary);
      std::stringstream ss;
      ss << f.rdbuf();
      PlantEntry e;
      std::string err;
      if (!plant_manifest_parse(ss.str(), dir.path().string(), e, err)) continue;
      e.variants.erase(std::remove_if(e.variants.begin(), e.variants.end(),
                                      [](const PlantVariant &v) { return !exists(v.model); }),
                       e.variants.end());
      if (e.variants.empty() && !exists(e.model)) continue; // half a download
      out.push_back(std::move(e));
    }
  }
  std::sort(out.begin(), out.end(), [](const PlantEntry &a, const PlantEntry &b) {
    return lower(a.name) < lower(b.name);
  });
  return out;
}

const std::vector<PlantEntry> &plant_library(bool rescan) {
  static std::vector<PlantEntry> lib;
  static bool read = false;
  if (!read || rescan) {
    read = true;
    lib = plant_builtins();
    std::vector<PlantEntry> found = plant_scan(plant_library_dir());
    lib.insert(lib.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
  }
  return lib;
}

const PlantEntry *plant_find(const std::string &id) {
  const std::vector<PlantEntry> &lib = plant_library();
  const std::string want = lower(id);
  for (const PlantEntry &p : lib)
    if (lower(p.id) == want) return &p;
  for (const PlantEntry &p : lib) {
    const size_t slash = p.id.find('/');
    if (slash != std::string::npos && lower(p.id.substr(slash + 1)) == want) return &p;
  }
  for (const PlantEntry &p : lib)
    if (lower(p.name) == want) return &p;
  return nullptr;
}

const PlantVariant *plant_variant(const PlantEntry &p, const std::string &want) {
  if (p.variants.empty()) return nullptr;
  const std::string w = lower(want);
  for (const PlantVariant &v : p.variants)
    if (!w.empty() && (lower(v.id) == w || lower(v.name) == w)) return &v;
  return &p.variants.front();
}

bool plant_matches(const PlantEntry &p, const std::string &query) {
  std::string hay = lower(p.name + " " + p.id + " " + p.group);
  for (const std::string &t : p.tags) hay += " " + lower(t);
  for (const std::string &c : p.categories) hay += " " + lower(c);
  std::istringstream q(lower(query));
  std::string w;
  while (q >> w)
    if (hay.find(w) == std::string::npos) return false;
  return true;
}

std::string plant_record_model(const std::string &root, const std::string &model_path,
                               const std::string &name, float height_m, std::string &err) {
  if (!exists(model_path)) {
    err = "no such file: " + model_path;
    return "";
  }
  gpx::TriMesh m;
  if (!gpx::mesh_load(model_path, m, err)) return "";
  float lo[3], hi[3];
  const float file_h = gpx::mesh_bounds(m, lo, hi) ? hi[1] - lo[1] : 0.f;
  float unit_m = 1.f;
  if (height_m > 0.f && file_h > 0.f) unit_m = height_m / file_h;
  else if (file_h > 1500.f) unit_m = 0.001f; // millimetres
  else if (file_h > 150.f) unit_m = 0.01f;   // centimetres
  const std::string label = name.empty() ? fs::path(model_path).stem().string() : name;
  std::string slug;
  for (char c : lower(label)) {
    const bool keep = std::isalnum((unsigned char)c) != 0;
    if (keep) slug += c;
    else if (!slug.empty() && slug.back() != '_') slug += '_';
  }
  while (!slug.empty() && slug.back() == '_') slug.pop_back();
  if (slug.empty()) slug = "plant";
  const fs::path user = fs::path(root) / "user";
  std::string id = slug;
  std::error_code ec;
  for (int n = 2; fs::exists(user / id, ec); ++n) id = slug + "_" + std::to_string(n);
  fs::create_directories(user / id, ec);
  const float h = file_h * unit_m;
  json j = {{"id", id},
            {"name", label},
            {"source", "user"},
            {"license", "your own"},
            {"note", "Recorded from " + model_path + "; the file stays where it is."},
            {"group", group_by_height(h)},
            {"model", fs::absolute(model_path, ec).string()},
            {"unit_m", unit_m},
            {"height_m", h},
            {"polycount", (int)m.face_count()}};
  std::ofstream f(user / id / "plant.json", std::ios::binary);
  if (!(f << j.dump(1))) {
    err = "could not write " + (user / id / "plant.json").string();
    return "";
  }
  return "user/" + id;
}

} // namespace studio
