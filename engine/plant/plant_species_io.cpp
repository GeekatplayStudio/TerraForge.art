// Geekatplay TerraForge - a species as a folder on disk.
//
// A species is a subgraph, not a mesh. What a file has to carry is the
// nodes feeding the root, their attributes, the links between them and the
// pictures those attributes name - so the subtree is copied into a graph
// of its own and written with the project writer (graph_to_json). One
// reader serves both, which is why a species that gains a parameter
// tomorrow still loads the files written today: the attribute reader takes
// the keys it finds and leaves the defaults for the keys it does not.
//
// Around that graph sits the record a library shelf shows without opening
// anything - the name, the group, the note, the licence, the height, the
// seeds somebody flagged as worth keeping, the presets. It is written
// first and read on its own by plant_species_read_info, so listing a
// hundred species costs a hundred small parses rather than a hundred
// graphs.
//
// Pictures a material names by an absolute path are copied into `tex/`
// beside the file and the attribute becomes the relative name, so a
// species folder can be moved, zipped or handed to somebody else whole.
// Loading turns those names back into absolute paths against the folder
// the file came from, and gives every node a new id because the graph it
// lands in already has nodes of its own.
//
// Nothing here throws. A truncated file, a folder that cannot be made, a
// picture that will not copy: each comes back as false or an empty path
// with a sentence in `err`, because the caller is a person who chose
// Save or Load and wants to be told what went wrong.
#include "gpx/plant.hpp"
#include "gpx/serialization.hpp"
#include "plant/plant_schema.hpp"
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace gpx {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

const char *const FORMAT = "terraforge-species";

// The species.json, whether the caller named the folder or the file.
fs::path species_file(const std::string &path) {
  std::error_code ec;
  const fs::path p(path);
  if (fs::is_directory(p, ec)) return p / "species.json";
  return p;
}

bool read_text(const fs::path &p, std::string &out, std::string &err) {
  std::ifstream f(p, std::ios::binary);
  if (!f) {
    err = "cannot open " + p.string();
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  if (out.empty()) {
    err = p.string() + " is empty";
    return false;
  }
  return true;
}

// ------------------------------------------------------------- the record

json info_to_json(const PlantSpeciesInfo &i) {
  json j;
  j["id"] = i.id;
  j["name"] = i.name;
  j["group"] = i.group;
  j["note"] = i.note;
  j["thumb"] = i.thumb;
  j["license"] = i.license;
  j["authors"] = i.authors;
  j["height_m"] = i.height_m;
  j["seed"] = i.seed;
  j["polycount"] = i.polycount;
  j["flagged"] = i.flagged;
  json presets = json::array();
  for (const PlantPreset &p : i.presets) {
    json v = json::object();
    for (const auto &kv : p.values) v[kv.first] = kv.second;
    presets.push_back({{"name", p.name},
                       {"note", p.note},
                       {"thumb", p.thumb},
                       {"age", p.age},
                       {"health", p.health},
                       {"season", p.season},
                       {"values", v}});
  }
  j["presets"] = presets;
  return j;
}

float num(const json &j, const char *key, float def) {
  return j.contains(key) && j[key].is_number() ? j[key].get<float>() : def;
}
std::string str(const json &j, const char *key) {
  return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

void info_from_json(const json &j, PlantSpeciesInfo &i) {
  if (!j.is_object()) return;
  i.id = str(j, "id");
  i.name = str(j, "name");
  i.group = str(j, "group");
  i.note = str(j, "note");
  i.thumb = str(j, "thumb");
  i.license = str(j, "license");
  i.authors = str(j, "authors");
  i.height_m = num(j, "height_m", 0.f);
  i.seed = (uint32_t)num(j, "seed", 1.f);
  i.polycount = (int)num(j, "polycount", 0.f);
  i.flagged.clear();
  if (j.contains("flagged") && j["flagged"].is_array())
    for (const json &s : j["flagged"])
      if (s.is_number()) i.flagged.push_back((uint32_t)s.get<double>());
  i.presets.clear();
  if (!j.contains("presets") || !j["presets"].is_array()) return;
  for (const json &p : j["presets"]) {
    if (!p.is_object()) continue;
    PlantPreset pr;
    pr.name = str(p, "name");
    pr.note = str(p, "note");
    pr.thumb = str(p, "thumb");
    pr.age = num(p, "age", -1.f);
    pr.health = num(p, "health", -1.f);
    pr.season = num(p, "season", -1.f);
    if (p.contains("values") && p["values"].is_object())
      for (auto it = p["values"].begin(); it != p["values"].end(); ++it)
        if (it.value().is_number()) pr.values.push_back({it.key(), it.value().get<float>()});
    i.presets.push_back(pr);
  }
}

// ------------------------------------------------------------- pictures

// Whether `p` lies inside `dir`, and where, so a picture already beside
// the species is named relatively instead of being copied onto itself.
bool inside(const fs::path &dir, const fs::path &p, std::string &rel) {
  std::error_code ec;
  const fs::path d = fs::weakly_canonical(dir, ec);
  if (ec) return false;
  const fs::path f = fs::weakly_canonical(p, ec);
  if (ec) return false;
  const fs::path r = f.lexically_relative(d);
  if (r.empty() || r.native().rfind(fs::path("..").native(), 0) == 0) return false;
  rel = r.generic_string();
  return true;
}

// A file name nothing else in `tex/` has claimed yet.
std::string free_name(const fs::path &tex, const std::string &stem) {
  std::error_code ec;
  if (!fs::exists(tex / stem, ec)) return stem;
  const fs::path p(stem);
  const std::string base = p.stem().string(), ext = p.extension().string();
  for (int i = 2; i < 1000; ++i) {
    const std::string n = base + "_" + std::to_string(i) + ext;
    if (!fs::exists(tex / n, ec)) return n;
  }
  return stem;
}

// Every picture a material names by an absolute path is copied into
// `dir/tex/` and the attribute becomes the copy's relative name. Files
// that already lie inside the folder are only renamed relatively; a file
// that cannot be copied keeps its absolute path, because a species whose
// picture lives elsewhere is still a species on this machine.
void relocate_pictures(Graph &tmp, const fs::path &dir) {
  std::error_code ec;
  std::map<std::string, std::string> done; // source -> relative name
  for (const auto &np : tmp.nodes) {
    Node &n = *np;
    for (Attribute &a : n.attrs.items) {
      if (a.type != AttrType::Filename || a.s.empty()) continue;
      const fs::path src(a.s);
      if (src.is_relative()) continue; // already named beside the species
      std::string rel;
      if (inside(dir, src, rel)) {
        a.s = rel;
        continue;
      }
      if (n.type != "PlantMaterial") continue; // an imported mesh stays where it is
      auto it = done.find(a.s);
      if (it != done.end()) {
        a.s = it->second;
        continue;
      }
      if (!fs::is_regular_file(src, ec)) continue;
      const fs::path tex = dir / "tex";
      fs::create_directories(tex, ec);
      const std::string name = free_name(tex, src.filename().string());
      fs::copy_file(src, tex / name, fs::copy_options::overwrite_existing, ec);
      if (ec) continue;
      const std::string as = "tex/" + name;
      done[a.s] = as;
      a.s = as;
    }
  }
}

// The other direction: a relative name is a file beside the species.
void absolute_pictures(Node &n, const fs::path &dir) {
  for (Attribute &a : n.attrs.items) {
    if (a.type != AttrType::Filename || a.s.empty()) continue;
    const fs::path p(a.s);
    if (!p.is_relative()) continue;
    a.s = (dir / p).lexically_normal().string();
  }
}

} // namespace

// --------------------------------------------------------------- writing

std::string plant_species_save(const Graph &g, const Node &root, const PlantSpeciesInfo &info,
                               const std::string &dir, std::string &err) {
  err.clear();
  try {
    if (root.type != "PlantSpecies") {
      err = "a species is saved from its root, not from a " + root.type;
      return "";
    }
    std::error_code ec;
    const fs::path out(dir);
    fs::create_directories(out, ec);
    if (ec) {
      err = "cannot make " + out.string() + ": " + ec.message();
      return "";
    }
    std::vector<const Node *> sub = plant_subtree(g, root);
    if (sub.empty()) {
      err = "the species has no nodes";
      return "";
    }
    // Written in the order the graph made them rather than in walk order, so
    // that loading the file into an empty graph hands every node the id it
    // had. The walker hashes node ids into its random draws, so the same
    // seed then grows the same individual it grew before it was saved.
    std::sort(sub.begin(), sub.end(),
              [](const Node *a, const Node *b) { return a->id < b->id; });
    // the subtree as a graph of its own, so the project writer can write it
    Graph tmp;
    std::map<uint64_t, uint64_t> file_id; // live id -> file id
    for (const Node *n : sub) {
      Node *c = tmp.add_node(n->type, n->pos_x, n->pos_y);
      if (!c) continue;
      c->attrs = n->attrs;
      c->enabled = n->enabled;
      c->ui_collapse = n->ui_collapse;
      file_id[n->id] = c->id;
    }
    if (file_id.find(root.id) == file_id.end()) {
      err = "the species root could not be copied (is PlantSpecies registered?)";
      return "";
    }
    for (const Link &l : g.links) {
      const auto f = file_id.find(l.from_node), t = file_id.find(l.to_node);
      if (f == file_id.end() || t == file_id.end()) continue;
      tmp.add_link(f->second, l.from_port, t->second, l.to_port);
    }
    relocate_pictures(tmp, out);

    json graph = json::parse(graph_to_json(tmp), nullptr, false);
    if (graph.is_discarded()) {
      err = "the species graph could not be written";
      return "";
    }
    json j;
    j["format"] = FORMAT;
    j["version"] = 1;
    j["schema"] = plant::schema_version();
    j["info"] = info_to_json(info);
    j["root"] = file_id[root.id];
    j["graph"] = std::move(graph);

    const fs::path file = out / "species.json";
    std::ofstream f(file, std::ios::binary);
    if (!f) {
      err = "cannot write " + file.string();
      return "";
    }
    f << j.dump(1);
    if (!f) {
      err = "could not finish writing " + file.string();
      return "";
    }
    return file.string();
  } catch (const std::exception &e) {
    err = std::string("could not save the species: ") + e.what();
    return "";
  }
}

// --------------------------------------------------------------- reading

bool plant_species_read_info(const std::string &path, PlantSpeciesInfo &info, std::string &err) {
  err.clear();
  try {
    const fs::path file = species_file(path);
    std::string text;
    if (!read_text(file, text, err)) return false;
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", std::string()) != FORMAT) {
      err = file.string() + " is not a species file";
      return false;
    }
    info_from_json(j.contains("info") ? j["info"] : json::object(), info);
    if (info.id.empty()) info.id = file.parent_path().filename().string();
    return true;
  } catch (const std::exception &e) {
    err = std::string("could not read the species: ") + e.what();
    return false;
  }
}

uint64_t plant_species_load(Graph &g, const std::string &path, float x, float y,
                            PlantSpeciesInfo &info, std::string &err) {
  err.clear();
  try {
    const fs::path file = species_file(path);
    const fs::path dir = file.parent_path();
    std::string text;
    if (!read_text(file, text, err)) return 0;
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", std::string()) != FORMAT) {
      err = file.string() + " is not a species file";
      return 0;
    }
    info_from_json(j.contains("info") ? j["info"] : json::object(), info);
    if (info.id.empty()) info.id = dir.filename().string();
    if (!j.contains("graph") || !j["graph"].is_object()) {
      err = file.string() + " carries no species graph";
      return 0;
    }
    // Read it into a graph of its own first: the loader clears whatever it
    // is given, and the graph we are adding to is the user's.
    Graph tmp;
    std::map<uint64_t, uint64_t> from_file; // file id -> temporary id
    if (!graph_from_json(tmp, j["graph"].dump(), err, &from_file)) return 0;

    uint64_t root_tmp = 0;
    if (j.contains("root") && j["root"].is_number_unsigned()) {
      const auto it = from_file.find(j["root"].get<uint64_t>());
      if (it != from_file.end()) root_tmp = it->second;
    }
    if (!root_tmp)
      for (const auto &n : tmp.nodes)
        if (n->type == "PlantSpecies") {
          root_tmp = n->id;
          break;
        }
    const Node *rt = root_tmp ? tmp.find_node(root_tmp) : nullptr;
    if (!rt) {
      err = file.string() + " has no species root";
      return 0;
    }
    // laid out where the caller asked, keeping the shape it was saved in
    const float dx = x - rt->pos_x, dy = y - rt->pos_y;
    std::map<uint64_t, uint64_t> live; // temporary id -> the graph's id
    uint64_t root_live = 0;
    for (const auto &np : tmp.nodes) {
      Node *c = g.add_node(np->type, np->pos_x + dx, np->pos_y + dy);
      if (!c) continue;
      c->attrs = np->attrs;
      c->enabled = np->enabled;
      c->ui_collapse = np->ui_collapse;
      absolute_pictures(*c, dir);
      live[np->id] = c->id;
      if (np->id == root_tmp) root_live = c->id;
    }
    if (!root_live) {
      err = "the species root could not be added to the graph";
      return 0;
    }
    for (const Link &l : tmp.links) {
      const auto f = live.find(l.from_node), t = live.find(l.to_node);
      if (f == live.end() || t == live.end()) continue;
      g.add_link(f->second, l.from_port, t->second, l.to_port);
    }
    g.mark_dirty(root_live);
    return root_live;
  } catch (const std::exception &e) {
    err = std::string("could not load the species: ") + e.what();
    return 0;
  }
}

// ------------------------------------------------------------- published

std::vector<PlantPublished> plant_published(const Graph &g, const Node &root) {
  std::vector<PlantPublished> out;
  for (const Node *n : plant_subtree(g, root)) {
    if (!n) continue;
    for (const Attribute &a : n->attrs.items) {
      if (!a.published) continue;
      PlantPublished p;
      p.node = n->id;
      p.key = a.key;
      p.name = a.pub_name.empty() ? a.label : a.pub_name;
      p.group = a.pub_group;
      out.push_back(p);
    }
  }
  // Grouped for the preset sheet, and stable inside a group so the order a
  // person published them in is the order they read in.
  std::stable_sort(out.begin(), out.end(),
                   [](const PlantPublished &a, const PlantPublished &b) { return a.group < b.group; });
  return out;
}

} // namespace gpx
