#include "gpx/metanode.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <algorithm>
#include <json.hpp>
#include <map>
#include <set>
#include <string>

using json = nlohmann::json;

namespace gpx {

// The MetaNode's state lives in two Text attributes: the inner graph as JSON,
// and the published-parameter table. Keeping them as ordinary attributes means
// serialization, undo and the library all work on them with no special cases.
static const char *K_INNER = "inner_graph";
static const char *K_PUBLISHED = "published";

// ------------------------------------------------------------------ grouping
Node *metanode_group(Graph &g, const std::vector<uint64_t> &node_ids,
                     std::string &err) {
  if (node_ids.empty()) {
    err = "nothing selected";
    return nullptr;
  }
  std::set<uint64_t> inside(node_ids.begin(), node_ids.end());
  // refuse to swallow a MetaNode's own machinery by accident
  for (uint64_t id : inside) {
    Node *n = g.find_node(id);
    if (!n) {
      err = "selection refers to a missing node";
      return nullptr;
    }
  }

  // Work out the boundary: links entering the selection become MetaNode inputs,
  // links leaving it become outputs. Ports are named after the inner node and
  // port so the MetaNode's interface is readable rather than "in1, in2".
  struct Crossing {
    uint64_t outside_node = 0;
    std::string outside_port;
    uint64_t inside_node = 0;
    std::string inside_port;
    std::string port_name;
    DataType type = DataType::Heightmap;
  };
  std::vector<Crossing> ins, outs;
  float cx = 0, cy = 0;
  for (uint64_t id : inside) {
    Node *n = g.find_node(id);
    cx += n->pos_x;
    cy += n->pos_y;
  }
  cx /= (float)inside.size();
  cy /= (float)inside.size();

  for (const Link &l : g.links) {
    bool from_in = inside.count(l.from_node) > 0;
    bool to_in = inside.count(l.to_node) > 0;
    if (from_in == to_in) continue; // wholly inside or wholly outside
    Node *inner = g.find_node(from_in ? l.from_node : l.to_node);
    Port *p = inner->port(from_in ? l.from_port : l.to_port,
                          from_in ? PortDir::Out : PortDir::In);
    if (!p) continue;
    Crossing c;
    c.type = p->type;
    if (to_in) { // entering the selection
      c.outside_node = l.from_node;
      c.outside_port = l.from_port;
      c.inside_node = l.to_node;
      c.inside_port = l.to_port;
      c.port_name = inner->type + " " + l.to_port;
      ins.push_back(c);
    } else { // leaving it
      c.outside_node = l.to_node;
      c.outside_port = l.to_port;
      c.inside_node = l.from_node;
      c.inside_port = l.from_port;
      c.port_name = inner->type + " " + l.from_port;
      outs.push_back(c);
    }
  }

  // Build the inner graph as a standalone document. Node ids are preserved so
  // published parameters can keep referring to them.
  json inner;
  inner["resolution"] = g.resolution;
  json jnodes = json::array(), jlinks = json::array();
  // Serialize the whole graph once, then lift out the records for the selected
  // nodes. (Binding the parse to a named value matters: iterating
  // `json::parse(...)["nodes"]` directly walks a subobject of a temporary that
  // is already destroyed by the time the loop body runs.)
  const json whole = json::parse(graph_to_json(g));
  for (const json &jn : whole["nodes"])
    if (inside.count(jn["id"].get<uint64_t>())) jnodes.push_back(jn);
  for (const Link &l : g.links)
    if (inside.count(l.from_node) && inside.count(l.to_node))
      jlinks.push_back({{"from", l.from_node},
                        {"from_port", l.from_port},
                        {"to", l.to_node},
                        {"to_port", l.to_port}});
  inner["nodes"] = jnodes;
  inner["links"] = jlinks;
  // the boundary, so ungrouping can put everything back
  json jbound = json::array();
  auto record = [&](const Crossing &c, const char *dir) {
    jbound.push_back({{"dir", dir},
                      {"port", c.port_name},
                      {"inner_node", c.inside_node},
                      {"inner_port", c.inside_port},
                      {"type", (int)c.type}});
  };
  for (const Crossing &c : ins) record(c, "in");
  for (const Crossing &c : outs) record(c, "out");
  inner["boundary"] = jbound;

  Node *meta = g.add_node("MetaNode", cx, cy);
  if (!meta) {
    err = "MetaNode type is not registered";
    return nullptr;
  }
  // declare the boundary ports before anything is reconnected to them
  for (const Crossing &c : ins) meta->add_in(c.port_name, c.type, true);
  for (const Crossing &c : outs) meta->add_out(c.port_name, c.type);
  if (Attribute *a = meta->attrs.find(K_INNER)) a->s = inner.dump();

  // Rewire: outside -> meta, meta -> outside. Remove the inner nodes last so
  // link removal does not disturb the ids we are still reading.
  std::vector<Crossing> re_in = ins, re_out = outs;
  for (uint64_t id : inside) g.remove_node(id);
  for (const Crossing &c : re_in)
    g.add_link(c.outside_node, c.outside_port, meta->id, c.port_name);
  for (const Crossing &c : re_out)
    g.add_link(meta->id, c.port_name, c.outside_node, c.outside_port);

  g.mark_dirty(meta->id);
  return meta;
}

std::vector<uint64_t> metanode_ungroup(Graph &g, uint64_t metanode_id,
                                       std::string &err) {
  std::vector<uint64_t> restored;
  Node *meta = g.find_node(metanode_id);
  if (!meta || meta->type != "MetaNode") {
    err = "not a MetaNode";
    return restored;
  }
  const Attribute *a = meta->attrs.find(K_INNER);
  if (!a || a->s.empty()) {
    err = "MetaNode has no inner graph";
    return restored;
  }
  json inner;
  try {
    inner = json::parse(a->s);
  } catch (const std::exception &e) {
    err = e.what();
    return restored;
  }

  // remember what the MetaNode was wired to, before it goes away
  struct Outside {
    std::string meta_port;
    uint64_t node = 0;
    std::string port;
    bool incoming = false;
  };
  std::vector<Outside> around;
  for (const Link &l : g.links) {
    if (l.to_node == metanode_id)
      around.push_back({l.to_port, l.from_node, l.from_port, true});
    else if (l.from_node == metanode_id)
      around.push_back({l.from_port, l.to_node, l.to_port, false});
  }

  // recreate the inner nodes, offset onto the MetaNode's position
  std::map<uint64_t, uint64_t> idmap;
  float ox = meta->pos_x, oy = meta->pos_y;
  bool first = true;
  float bx = 0, by = 0;
  for (const json &jn : inner.value("nodes", json::array())) {
    float px = jn["pos"][0].get<float>(), py = jn["pos"][1].get<float>();
    if (first) { bx = px; by = py; first = false; }
    Node *n = g.add_node(jn["type"].get<std::string>(), ox + (px - bx),
                         oy + (py - by));
    if (!n) continue;
    idmap[jn["id"].get<uint64_t>()] = n->id;
    n->enabled = jn.value("enabled", true);
    if (jn.contains("attrs")) {
      // reuse the loader by round-tripping this single node through a document
      json one;
      one["resolution"] = g.resolution;
      one["nodes"] = json::array({jn});
      one["links"] = json::array();
      Graph tmp;
      std::string e2;
      if (graph_from_json(tmp, one.dump(), e2) && !tmp.nodes.empty())
        n->attrs = tmp.nodes[0]->attrs;
    }
    restored.push_back(n->id);
  }
  for (const json &jl : inner.value("links", json::array())) {
    auto f = idmap.find(jl["from"].get<uint64_t>());
    auto t = idmap.find(jl["to"].get<uint64_t>());
    if (f == idmap.end() || t == idmap.end()) continue;
    g.add_link(f->second, jl["from_port"].get<std::string>(), t->second,
               jl["to_port"].get<std::string>());
  }

  // reconnect the boundary to the restored inner nodes
  std::map<std::string, std::pair<uint64_t, std::string>> in_map, out_map;
  for (const json &jb : inner.value("boundary", json::array())) {
    auto it = idmap.find(jb["inner_node"].get<uint64_t>());
    if (it == idmap.end()) continue;
    auto entry = std::make_pair(it->second, jb["inner_port"].get<std::string>());
    if (jb["dir"].get<std::string>() == "in")
      in_map[jb["port"].get<std::string>()] = entry;
    else
      out_map[jb["port"].get<std::string>()] = entry;
  }
  g.remove_node(metanode_id);
  for (const Outside &o : around) {
    if (o.incoming) {
      auto it = in_map.find(o.meta_port);
      if (it != in_map.end())
        g.add_link(o.node, o.port, it->second.first, it->second.second);
    } else {
      auto it = out_map.find(o.meta_port);
      if (it != out_map.end())
        g.add_link(it->second.first, it->second.second, o.node, o.port);
    }
  }
  for (uint64_t id : restored) g.mark_dirty(id);
  return restored;
}

// -------------------------------------------------------------- inner access
bool metanode_open(const Node &meta, Graph &inner, std::string &err) {
  const Attribute *a = meta.attrs.find(K_INNER);
  if (!a || a->s.empty()) {
    err = "MetaNode has no inner graph";
    return false;
  }
  return graph_from_json(inner, a->s, err);
}

std::map<uint64_t, Node *> metanode_id_map(const Node &meta, Graph &inner) {
  std::map<uint64_t, Node *> out;
  const Attribute *a = meta.attrs.find(K_INNER);
  if (!a || a->s.empty()) return out;
  try {
    json doc = json::parse(a->s);
    const json &jn = doc.at("nodes");
    // graph_from_json creates nodes in document order and skips only unknown
    // types, so position is the reliable correspondence
    for (size_t i = 0; i < jn.size() && i < inner.nodes.size(); ++i)
      out[jn[i].at("id").get<uint64_t>()] = inner.nodes[i].get();
  } catch (const std::exception &) {
  }
  return out;
}

bool metanode_store(Node &meta, const Graph &inner, std::string &err) {
  Attribute *a = meta.attrs.find(K_INNER);
  if (!a) {
    err = "not a MetaNode";
    return false;
  }
  // keep the boundary description the group step wrote; only the nodes and
  // links are replaced by the edit
  json doc;
  try {
    doc = a->s.empty() ? json::object() : json::parse(a->s);
  } catch (const std::exception &) {
    doc = json::object();
  }
  json fresh = json::parse(graph_to_json(inner));
  doc["nodes"] = fresh["nodes"];
  doc["links"] = fresh["links"];
  doc["resolution"] = fresh["resolution"];
  a->s = doc.dump();
  return true;
}

// -------------------------------------------------------- published params
std::vector<PublishedParam> metanode_published(const Node &meta) {
  std::vector<PublishedParam> out;
  const Attribute *a = meta.attrs.find(K_PUBLISHED);
  if (!a || a->s.empty()) return out;
  try {
    for (const json &j : json::parse(a->s)) {
      PublishedParam p;
      p.label = j.value("label", "");
      p.inner_node = j.value("node", 0ull);
      p.attr_key = j.value("key", "");
      p.group = j.value("group", "");
      if (!p.attr_key.empty()) out.push_back(p);
    }
  } catch (const std::exception &) {
  }
  return out;
}

static void write_published(Node &meta, const std::vector<PublishedParam> &v) {
  json arr = json::array();
  for (const PublishedParam &p : v) {
    json e;
    e["label"] = p.label;
    e["node"] = p.inner_node;
    e["key"] = p.attr_key;
    e["group"] = p.group;
    arr.push_back(e);
  }
  if (Attribute *a = meta.attrs.find(K_PUBLISHED)) a->s = arr.dump();
}

bool metanode_publish(Node &meta, uint64_t inner_node, const std::string &key,
                      const std::string &label, const std::string &group) {
  std::vector<PublishedParam> v = metanode_published(meta);
  for (const PublishedParam &p : v)
    if (p.inner_node == inner_node && p.attr_key == key) return false;

  // mirror the inner attribute onto the MetaNode so it gets a real widget with
  // the right type and range, rather than a generic text box
  Graph inner;
  std::string err;
  if (!metanode_open(meta, inner, err)) return false;
  std::map<uint64_t, Node *> live = metanode_id_map(meta, inner);
  auto it = live.find(inner_node);
  if (it == live.end()) return false;
  const Attribute *src = it->second->attrs.find(key);
  if (!src) return false;
  Attribute copy = *src;
  copy.key = "pub_" + std::to_string(inner_node) + "_" + key;
  copy.label = label.empty() ? src->label : label;
  copy.group = group;
  meta.attrs.add(copy);

  PublishedParam p;
  p.label = copy.label;
  p.inner_node = inner_node;
  p.attr_key = key;
  p.group = group;
  v.push_back(p);
  write_published(meta, v);
  return true;
}

bool metanode_unpublish(Node &meta, uint64_t inner_node, const std::string &key) {
  std::vector<PublishedParam> v = metanode_published(meta);
  size_t before = v.size();
  v.erase(std::remove_if(v.begin(), v.end(),
                         [&](const PublishedParam &p) {
                           return p.inner_node == inner_node && p.attr_key == key;
                         }),
          v.end());
  if (v.size() == before) return false;
  std::string mirror = "pub_" + std::to_string(inner_node) + "_" + key;
  meta.attrs.items.erase(
      std::remove_if(meta.attrs.items.begin(), meta.attrs.items.end(),
                     [&](const Attribute &a) { return a.key == mirror; }),
      meta.attrs.items.end());
  write_published(meta, v);
  return true;
}

void metanode_apply_published(const Node &meta, Graph &inner) {
  std::map<uint64_t, Node *> live = metanode_id_map(meta, inner);
  for (const PublishedParam &p : metanode_published(meta)) {
    std::string mirror = "pub_" + std::to_string(p.inner_node) + "_" + p.attr_key;
    const Attribute *from = meta.attrs.find(mirror);
    if (!from) continue;
    auto it = live.find(p.inner_node);
    if (it == live.end()) continue;
    if (Attribute *to = it->second->attrs.find(p.attr_key)) {
      // copy the value, keep the target's own metadata
      std::string k = to->key, l = to->label, grp = to->group;
      *to = *from;
      to->key = k;
      to->label = l;
      to->group = grp;
      inner.mark_dirty(it->second->id);
    }
  }
}

} // namespace gpx

