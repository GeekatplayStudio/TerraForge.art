#include "node_library.hpp"
#include "app.hpp"
#include "gpx/metanode.hpp"
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace studio {

std::string node_library_dir() {
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path dir = base ? fs::path(base) : fs::temp_directory_path();
  dir /= "GeekatplayTerraForge";
  dir /= "nodes";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

static std::vector<SavedMetaNode> g_cache;
static bool g_scanned = false;

const std::vector<SavedMetaNode> &node_library(bool refresh) {
  if (g_scanned && !refresh) return g_cache;
  g_scanned = true;
  g_cache.clear();
  std::error_code ec;
  for (const auto &e : fs::directory_iterator(node_library_dir(), ec)) {
    if (e.path().extension() != ".gpxnode") continue;
    SavedMetaNode m;
    m.path = e.path().string();
    m.name = e.path().stem().string();
    try {
      std::ifstream f(e.path());
      json j;
      f >> j;
      m.note = j.value("note", "");
      m.name = j.value("name", m.name);
      m.inner_nodes = (int)j.value("inner", json::object())
                          .value("nodes", json::array())
                          .size();
      m.published = (int)j.value("published", json::array()).size();
    } catch (const std::exception &) {
      // a malformed file should not hide the rest of the library
      m.note = "(unreadable)";
    }
    g_cache.push_back(std::move(m));
  }
  std::sort(g_cache.begin(), g_cache.end(),
            [](const SavedMetaNode &a, const SavedMetaNode &b) {
              return a.name < b.name;
            });
  return g_cache;
}

// A saved MetaNode is its inner graph, its published table, and the shape of
// its boundary — enough to rebuild the node exactly, in any project.
bool node_library_save(App &a, unsigned long long metanode_id,
                       const std::string &name, const std::string &note,
                       std::string &err) {
  if (name.empty()) {
    err = "give it a name";
    return false;
  }
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  gpx::Node *meta = a.graph.find_node(metanode_id);
  if (!meta || meta->type != "MetaNode") {
    err = "select a MetaNode first";
    return false;
  }
  const gpx::Attribute *inner = meta->attrs.find("inner_graph");
  if (!inner || inner->s.empty()) {
    err = "this MetaNode is empty";
    return false;
  }
  json doc;
  doc["app"] = "Geekatplay TerraForge";
  doc["kind"] = "metanode";
  doc["version"] = 1;
  doc["name"] = name;
  doc["note"] = note;
  try {
    doc["inner"] = json::parse(inner->s);
  } catch (const std::exception &e) {
    err = e.what();
    return false;
  }
  const gpx::Attribute *pub = meta->attrs.find("published");
  if (pub && !pub->s.empty()) {
    try {
      doc["published"] = json::parse(pub->s);
    } catch (const std::exception &) {
    }
  }
  // the published parameters' current values live as mirrored attributes, so
  // a saved node remembers how it was tuned
  json vals = json::object();
  for (const gpx::Attribute &at : meta->attrs.items) {
    if (at.key.rfind("pub_", 0) != 0) continue;
    json v;
    v["type"] = (int)at.type;
    v["f"] = at.f;
    v["i"] = at.i;
    v["b"] = at.b;
    v["seed"] = at.seed;
    v["label"] = at.label;
    v["group"] = at.group;
    vals[at.key] = v;
  }
  doc["values"] = vals;
  // boundary port shapes, so the node can be rebuilt before it is wired
  json ports = json::array();
  for (const gpx::Port &p : meta->ports) {
    json jp;
    jp["name"] = p.name;
    jp["in"] = p.dir == gpx::PortDir::In;
    jp["type"] = (int)p.type;
    ports.push_back(jp);
  }
  doc["ports"] = ports;

  // sanitize: the name becomes a filename
  std::string file;
  for (char c : name)
    file += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_')
                ? c
                : '_';
  fs::path out = fs::path(node_library_dir()) / (file + ".gpxnode");
  std::ofstream f(out);
  if (!f) {
    err = "cannot write " + out.string();
    return false;
  }
  f << doc.dump(1);
  node_library(true);
  return true;
}

unsigned long long node_library_load(App &a, const std::string &path, float x,
                                     float y, std::string &err) {
  json doc;
  try {
    std::ifstream f(path);
    if (!f) {
      err = "cannot open " + path;
      return 0;
    }
    f >> doc;
  } catch (const std::exception &e) {
    err = e.what();
    return 0;
  }
  std::lock_guard<std::mutex> lk(a.graph_mtx);
  gpx::Node *meta = a.graph.add_node("MetaNode", x, y);
  if (!meta) {
    err = "MetaNode type unavailable";
    return 0;
  }
  if (gpx::Attribute *at = meta->attrs.find("inner_graph"))
    at->s = doc.value("inner", json::object()).dump();
  if (gpx::Attribute *at = meta->attrs.find("published"))
    at->s = doc.value("published", json::array()).dump();
  if (gpx::Attribute *at = meta->attrs.find("note"))
    at->s = doc.value("note", "");

  // rebuild the boundary ports
  for (const json &jp : doc.value("ports", json::array())) {
    std::string pname = jp.value("name", "");
    if (pname.empty()) continue;
    gpx::DataType t = (gpx::DataType)jp.value("type", 0);
    if (jp.value("in", true)) meta->add_in(pname, t, true);
    else meta->add_out(pname, t);
  }
  // and the mirrored published widgets, with the values they were saved at
  for (const gpx::PublishedParam &p : gpx::metanode_published(*meta)) {
    std::string key = "pub_" + std::to_string(p.inner_node) + "_" + p.attr_key;
    const json vals = doc.value("values", json::object());
    if (!vals.contains(key)) continue;
    const json &v = vals[key];
    gpx::Attribute at;
    at.type = (gpx::AttrType)v.value("type", 0);
    at.key = key;
    at.label = v.value("label", p.label);
    at.group = v.value("group", p.group);
    at.f = v.value("f", 0.f);
    at.i = v.value("i", 0);
    at.b = v.value("b", false);
    at.seed = v.value("seed", 0u);
    meta->attrs.add(at);
  }
  a.graph.mark_dirty(meta->id);
  a.graph_layout_serial++;
  a.request_eval();
  return meta->id;
}

bool node_library_delete(const std::string &path) {
  std::error_code ec;
  bool ok = fs::remove(path, ec);
  node_library(true);
  return ok;
}

} // namespace studio
