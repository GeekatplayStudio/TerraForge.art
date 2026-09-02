// Geekatplay TerraForge - material subgraph and metanode-library
// serialization. Split from serialization.cpp for the 500-line module
// rule; the attribute JSON forms are shared through
// serialization_internal.hpp.
#include "gpx/serialization.hpp"
#include "serialization_internal.hpp"
#include "gpx/metanode.hpp"
#include <json.hpp>
#include <fstream>
#include <map>
#include <set>

using json = nlohmann::json;

namespace gpx {

// ---------------------------------------------------- material library
std::string material_to_json(const Graph &g, uint64_t material_node_id) {
  // upstream closure from the MaterialOutput node
  std::set<uint64_t> keep;
  std::vector<uint64_t> stack{material_node_id};
  while (!stack.empty()) {
    uint64_t id = stack.back();
    stack.pop_back();
    if (!keep.insert(id).second) continue;
    for (const Link &l : g.links)
      if (l.to_node == id) stack.push_back(l.from_node);
  }
  json j;
  j["format"] = "terraforge-material";
  j["version"] = 1;
  Node *mat = g.find_node(material_node_id);
  j["name"] = mat ? mat->attrs.get_s("name") : "Material";
  float min_x = 1e9f, min_y = 1e9f;
  for (uint64_t id : keep)
    if (Node *n = g.find_node(id)) {
      min_x = std::min(min_x, n->pos_x);
      min_y = std::min(min_y, n->pos_y);
    }
  json nodes = json::array();
  for (uint64_t id : keep) {
    Node *n = g.find_node(id);
    if (!n) continue;
    json jn;
    jn["id"] = n->id;
    jn["type"] = n->type;
    jn["pos"] = {n->pos_x - min_x, n->pos_y - min_y};
    jn["output"] = (n->id == material_node_id);
    json attrs;
    for (const auto &at : n->attrs.items) attrs[at.key] = attr_to_json(at);
    jn["attrs"] = attrs;
    if (!n->enabled) jn["enabled"] = false;
    nodes.push_back(jn);
  }
  j["nodes"] = nodes;
  json links = json::array();
  for (const Link &l : g.links)
    if (keep.count(l.from_node) && keep.count(l.to_node))
      links.push_back({{"from", l.from_node},
                       {"from_port", l.from_port},
                       {"to", l.to_node},
                       {"to_port", l.to_port}});
  j["links"] = links;
  return j.dump(2);
}

uint64_t material_from_json(Graph &g, const std::string &text, std::string &err,
                            float x, float y) {
  json j;
  try {
    j = json::parse(text);
  } catch (const std::exception &e) {
    err = e.what();
    return 0;
  }
  if (j.value("format", "") != "terraforge-material") {
    err = "not a TerraForge material file";
    return 0;
  }
  std::map<uint64_t, uint64_t> idmap;
  uint64_t out_id = 0;
  for (const auto &jn : j.value("nodes", json::array())) {
    Node *n = g.add_node(jn["type"].get<std::string>(),
                         x + jn["pos"][0].get<float>(),
                         y + jn["pos"][1].get<float>());
    if (!n) continue;
    idmap[jn["id"].get<uint64_t>()] = n->id;
    n->enabled = jn.value("enabled", true);
    if (jn.contains("attrs"))
      for (auto &at : n->attrs.items)
        if (jn["attrs"].contains(at.key)) attr_from_json(at, jn["attrs"][at.key]);
    if (jn.value("output", false)) out_id = n->id;
  }
  for (const auto &jl : j.value("links", json::array())) {
    auto f = idmap.find(jl["from"].get<uint64_t>());
    auto t = idmap.find(jl["to"].get<uint64_t>());
    if (f == idmap.end() || t == idmap.end()) continue;
    g.add_link(f->second, jl["from_port"].get<std::string>(), t->second,
               jl["to_port"].get<std::string>());
  }
  if (!out_id) err = "material file has no MaterialOutput node";
  g.mark_all_dirty();
  return out_id;
}

std::string registry_catalog_for_ai(const std::vector<std::string> &categories) {
  std::string out;
  Graph probe;
  probe.resolution = 8;
  for (const NodeDef *d : NodeRegistry::instance().all()) {
    if (!categories.empty() &&
        std::find(categories.begin(), categories.end(), d->category) ==
            categories.end())
      continue;
    Node *n = probe.add_node(d->type);
    if (!n) continue;
    out += d->type + " (" + d->category + "): " + d->description + "\n";
    out += "  in:[";
    bool first = true;
    for (auto &p : n->ports)
      if (p.dir == PortDir::In) {
        if (!first) out += ",";
        out += p.name + (p.optional ? "?" : "");
        first = false;
      }
    out += "] out:[";
    first = true;
    for (auto &p : n->ports)
      if (p.dir == PortDir::Out) {
        if (!first) out += ",";
        out += p.name;
        first = false;
      }
    out += "]\n  attrs: ";
    for (auto &a : n->attrs.items) {
      out += a.key;
      char buf[96];
      switch (a.type) {
        case AttrType::Float:
          snprintf(buf, sizeof buf, "(float %.3g..%.3g)", a.fmin, a.fmax);
          out += buf;
          break;
        case AttrType::Int:
          snprintf(buf, sizeof buf, "(int %d..%d)", a.imin, a.imax);
          out += buf;
          break;
        case AttrType::Bool: out += "(bool)"; break;
        case AttrType::Seed: out += "(seed)"; break;
        case AttrType::Choice: {
          out += "(choice:";
          for (size_t k = 0; k < a.labels.size(); ++k) {
            if (k) out += "|";
            out += a.labels[k];
          }
          out += ")";
        } break;
        case AttrType::Range:
        case AttrType::Vec2: out += "([a,b])"; break;
        default: out += "(text)"; break;
      }
      out += " ";
    }
    out += "\n";
  }
  return out;
}

} // namespace gpx
