#include "gpx/node_graph.hpp"
#include "node_graph_internal.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <queue>
#include <set>

namespace gpx {

// ------------------------------------------------------------------ Graph
uint64_t Graph::next_id() { return id_counter_++; }

void Graph::adopt(Graph &other) {
  nodes = std::move(other.nodes);
  links = std::move(other.links);
  resolution = other.resolution;
  uint64_t max_id = 1;
  for (auto &n : nodes) {
    n->graph = this;
    max_id = std::max(max_id, n->id);
  }
  for (auto &l : links) max_id = std::max(max_id, l.id);
  id_counter_ = max_id + 1;
  other.nodes.clear();
  other.links.clear();
  mark_all_dirty();
}

Node *Graph::add_node(const std::string &type, float x, float y) {
  const NodeDef *def = NodeRegistry::instance().find(type);
  if (!def) return nullptr;
  auto n = std::make_unique<Node>();
  n->id = next_id();
  n->type = def->type;
  n->category = def->category;
  n->pos_x = x;
  n->pos_y = y;
  n->graph = this;
  def->setup(*n);
  add_universal_blend(*n);
  Node *raw = n.get();
  nodes.push_back(std::move(n));
  return raw;
}

void Graph::remove_node(uint64_t id) {
  links.erase(std::remove_if(links.begin(), links.end(),
                             [&](const Link &l) {
                               return l.from_node == id || l.to_node == id;
                             }),
              links.end());
  for (auto &n : nodes)
    if (n->id != id) {
      // downstream of removed node becomes dirty via link removal below
    }
  nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                             [&](auto &n) { return n->id == id; }),
              nodes.end());
  mark_all_dirty();
}

Node *Graph::find_node(uint64_t id) const {
  for (auto &n : nodes)
    if (n->id == id) return n.get();
  return nullptr;
}

bool Graph::add_link(uint64_t from_node, const std::string &from_port,
                     uint64_t to_node, const std::string &to_port) {
  Node *a = find_node(from_node), *b = find_node(to_node);
  if (!a || !b || a == b) return false;
  Port *pa = a->port(from_port, PortDir::Out);
  Port *pb = b->port(to_port, PortDir::In);
  if (!pa || !pb) return false;
  if (!ports_compatible(pa->type, pb->type)) return false;
  // one link per input: replace existing
  links.erase(std::remove_if(links.begin(), links.end(),
                             [&](const Link &l) {
                               return l.to_node == to_node && l.to_port == to_port;
                             }),
              links.end());
  Link l;
  l.id = next_id();
  l.from_node = from_node;
  l.from_port = from_port;
  l.to_node = to_node;
  l.to_port = to_port;
  links.push_back(l);
  mark_dirty(to_node);
  // cycle check: reject if it made a cycle
  if (topo_order().empty() && !nodes.empty()) {
    links.pop_back();
    return false;
  }
  return true;
}

void Graph::remove_link(uint64_t id) {
  auto it = std::find_if(links.begin(), links.end(),
                         [&](const Link &l) { return l.id == id; });
  if (it == links.end()) return;
  uint64_t to = it->to_node;
  links.erase(it);
  mark_dirty(to);
}

void Graph::clear() {
  nodes.clear();
  links.clear();
}

// Which of a bypassed node's inputs stands in for the output being asked for.
// Prefer a port literally named "input", then the first input of the same data
// type — that is the pass-through channel for essentially every filter we have.
static const Link *bypass_source(const Graph &g, const Node &n,
                                 const Port *out_port) {
  const Link *typed = nullptr;
  for (const Link &l : g.links) {
    if (l.to_node != n.id) continue;
    const Port *in = const_cast<Node &>(n).port(l.to_port, PortDir::In);
    if (!in) continue;
    if (l.to_port == "input") return &l;
    if (!typed && out_port && in->type == out_port->type) typed = &l;
  }
  return typed;
}

// Resolve a link, walking through any bypassed nodes on the way. Recursion is
// depth-limited rather than cycle-tracked because add_link already rejects
// cycles; the limit is a belt-and-braces guard for a corrupt file.
static const Port *resolve_upstream(const Graph &g, const Node &n,
                                    const std::string &in_port, Node **out_node,
                                    int depth) {
  if (depth > 64) return nullptr;
  for (const Link &l : g.links) {
    if (l.to_node != n.id || l.to_port != in_port) continue;
    Node *up = g.find_node(l.from_node);
    if (!up) return nullptr;
    Port *p = up->port(l.from_port, PortDir::Out);
    if (up->enabled) {
      if (out_node) *out_node = up;
      return p;
    }
    // bypassed: read through it to whatever feeds its own input
    const Link *src = bypass_source(g, *up, p);
    if (!src) return nullptr;
    return resolve_upstream(g, *up, src->to_port, out_node, depth + 1);
  }
  return nullptr;
}

const Port *Graph::upstream(const Node &n, const std::string &in_port) const {
  return resolve_upstream(*this, n, in_port, nullptr, 0);
}

Node *Graph::upstream_node(const Node &n, const std::string &in_port) const {
  Node *found = nullptr;
  resolve_upstream(*this, n, in_port, &found, 0);
  return found;
}

void Graph::mark_dirty(uint64_t node_id) {
  std::set<uint64_t> visited;
  std::queue<uint64_t> q;
  q.push(node_id);
  while (!q.empty()) {
    uint64_t id = q.front();
    q.pop();
    if (!visited.insert(id).second) continue;
    if (Node *n = find_node(id)) n->dirty = true;
    for (const Link &l : links)
      if (l.from_node == id) q.push(l.to_node);
  }
}

void Graph::mark_all_dirty() {
  for (auto &n : nodes) n->dirty = true;
}

std::vector<Node *> Graph::topo_order() const {
  std::map<uint64_t, int> indeg;
  for (auto &n : nodes) indeg[n->id] = 0;
  for (const Link &l : links) indeg[l.to_node]++;
  std::queue<Node *> q;
  for (auto &n : nodes)
    if (indeg[n->id] == 0) q.push(n.get());
  std::vector<Node *> order;
  while (!q.empty()) {
    Node *n = q.front();
    q.pop();
    order.push_back(n);
    for (const Link &l : links)
      if (l.from_node == n->id && --indeg[l.to_node] == 0)
        q.push(find_node(l.to_node));
  }
  if (order.size() != nodes.size()) return {}; // cycle
  return order;
}

} // namespace gpx

