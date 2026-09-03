// Geekatplay TerraForge — the port catalog. See gpx/port_catalog.hpp.
#include "gpx/port_catalog.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>

namespace gpx {

namespace {

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

// Built once, on first use, and never invalidated: the registry is populated
// by static initialisers before main and nothing adds to it afterwards.
struct Catalog {
  std::map<std::string, std::vector<PortInfo>> by_type;
  std::once_flag once;

  void build() {
    // Built through Graph::add_node, not by calling NodeDef::setup directly.
    // The graph adds ports of its own after setup - the universal blend input
    // that any heightmap filter gets for free - and a catalog that skipped
    // them would hide exactly the input a drag is most often looking for.
    // Going through the real construction path is the only way the catalog
    // cannot drift from what a node actually has.
    Graph scratch;
    for (const NodeDef *d : NodeRegistry::instance().all()) {
      Node *n = scratch.add_node(d->type, 0, 0);
      if (!n) continue;
      std::vector<PortInfo> ports;
      ports.reserve(n->ports.size());
      for (const Port &p : n->ports)
        ports.push_back({p.name, p.dir, p.type, p.field_type, p.optional});
      by_type.emplace(d->type, std::move(ports));
    }
  }
};

Catalog &catalog() {
  static Catalog c;
  std::call_once(c.once, [&] { c.build(); });
  return c;
}

} // namespace

const std::vector<PortInfo> &port_catalog(const std::string &node_type) {
  static const std::vector<PortInfo> none;
  Catalog &c = catalog();
  auto it = c.by_type.find(node_type);
  return it == c.by_type.end() ? none : it->second;
}

bool node_offers(const std::string &node_type, DataType type,
                 PortDir want_dir) {
  Catalog &c = catalog();
  auto it = c.by_type.find(node_type);
  if (it == c.by_type.end()) return true; // unknown type: fail open
  for (const PortInfo &p : it->second)
    if (p.dir == want_dir && ports_compatible(type, p.type)) return true;
  return false;
}

std::string select_port(const Node &live, DataType type, PortDir want_dir,
                        FieldType prefer_field) {
  const bool want_in = want_dir == PortDir::In;
  std::string conventional, field_match, first, compatible;
  for (const Port &p : live.ports) {
    if (p.dir != want_dir) continue;
    if (p.type != type) {
      // a heightmap may take a texture and vice versa: the fallback when
      // nothing of the exact type is offered (ports_compatible)
      if (compatible.empty() && ports_compatible(type, p.type)) compatible = p.name;
      continue;
    }
    if (first.empty()) first = p.name;
    const std::string n = lower(p.name);
    if (conventional.empty() &&
        (want_in ? (n == "input" || n == "in")
                 : (n == "output" || n == "out")))
      conventional = p.name;
    if (field_match.empty() && type == DataType::Field &&
        p.field_type == prefer_field)
      field_match = p.name;
  }
  if (!conventional.empty()) return conventional;
  if (!field_match.empty()) return field_match;
  if (!first.empty()) return first;
  return compatible;
}

} // namespace gpx
