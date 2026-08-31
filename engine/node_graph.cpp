#include "gpx/node_graph.hpp"
#include <algorithm>
#include <chrono>
#include <queue>
#include <set>

namespace gpx {

// ------------------------------------------------------------------- Node
Port *Node::port(const std::string &name) {
  for (auto &p : ports)
    if (p.name == name) return &p;
  return nullptr;
}

Port *Node::port(const std::string &name, PortDir dir) {
  for (auto &p : ports)
    if (p.dir == dir && p.name == name) return &p;
  return nullptr;
}

Port *Node::first_out(DataType t) {
  for (auto &p : ports)
    if (p.dir == PortDir::Out && p.type == t) return &p;
  return nullptr;
}

void Node::add_in(const std::string &name, DataType t, bool optional) {
  Port p;
  p.name = name;
  p.dir = PortDir::In;
  p.type = t;
  p.optional = optional;
  ports.push_back(std::move(p));
}

void Node::add_out(const std::string &name, DataType t) {
  Port p;
  p.name = name;
  p.dir = PortDir::Out;
  p.type = t;
  ports.push_back(std::move(p));
}

// ------------------------------------------------------------ field domain
void Node::add_field_in(const std::string &name, FieldType t, bool optional) {
  Port p;
  p.name = name;
  p.dir = PortDir::In;
  p.type = DataType::Field;
  p.field_type = t;
  p.optional = optional;
  ports.push_back(std::move(p));
}

void Node::add_field_out(const std::string &name, FieldType t, FieldEvalFn eval) {
  Port p;
  p.name = name;
  p.dir = PortDir::Out;
  p.type = DataType::Field;
  p.field_type = t;
  p.field_eval = std::move(eval);
  ports.push_back(std::move(p));
}

bool Node::field_connected(const std::string &name) const {
  return graph && graph->upstream(*this, name) != nullptr;
}

// Pull-based evaluation: walk up the link and ask the producing node for a
// value at this point. There is no cache here on purpose — a field is a
// function, and the caller (rasterizer, GLSL transpiler, picker) decides how
// often to sample it.
FieldValue Node::in_field(const std::string &name, const FieldContext &ctx,
                          FieldValue fallback) const {
  if (!graph) return fallback;
  const Node *src = graph->upstream_node(*this, name);
  const Port *up = graph->upstream(*this, name);
  if (!src || !up || !up->field_eval) return fallback;
  return up->field_eval(*src, ctx);
}

float Node::in_number(const std::string &name, const FieldContext &ctx,
                      float fallback) const {
  if (!field_connected(name)) return fallback;
  return in_field(name, ctx, FieldValue(fallback)).number();
}

FieldValue Node::eval_field(const std::string &name,
                            const FieldContext &ctx) const {
  for (const Port &p : ports)
    if (p.dir == PortDir::Out && p.name == name && p.field_eval)
      return p.field_eval(*this, ctx);
  return FieldValue(0.f);
}

const Heightmap *Node::in_hmap(const std::string &name) const {
  const Port *up = graph ? graph->upstream(*this, name) : nullptr;
  return up && up->hmap ? up->hmap.get() : nullptr;
}

const TextureRGBA *Node::in_tex(const std::string &name) const {
  const Port *up = graph ? graph->upstream(*this, name) : nullptr;
  return up && up->tex ? up->tex.get() : nullptr;
}

Heightmap &Node::out_hmap(const std::string &name) {
  Port *p = port(name, PortDir::Out);
  int res = graph ? graph->resolution : 512;
  if (!p->hmap || p->hmap->w != res)
    p->hmap = std::make_shared<Heightmap>(res, res);
  else
    std::fill(p->hmap->v.begin(), p->hmap->v.end(), 0.f);
  return *p->hmap;
}

TextureRGBA &Node::out_tex(const std::string &name) {
  Port *p = port(name, PortDir::Out);
  int res = graph ? graph->resolution : 512;
  if (!p->tex || p->tex->w != res) p->tex = std::make_shared<TextureRGBA>(res, res);
  return *p->tex;
}

// --------------------------------------------------------------- Registry
NodeRegistry &NodeRegistry::instance() {
  static NodeRegistry r;
  return r;
}

void NodeRegistry::reg(NodeDef def) { defs_[def.type] = std::move(def); }

const NodeDef *NodeRegistry::find(const std::string &type) const {
  auto it = defs_.find(type);
  return it == defs_.end() ? nullptr : &it->second;
}

std::vector<const NodeDef *> NodeRegistry::all() const {
  std::vector<const NodeDef *> out;
  for (auto &[k, d] : defs_) out.push_back(&d);
  std::sort(out.begin(), out.end(), [](const NodeDef *a, const NodeDef *b) {
    if (a->category != b->category) return a->category < b->category;
    return a->type < b->type;
  });
  return out;
}

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
  if (pa->type != pb->type) return false;
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

const Port *Graph::upstream(const Node &n, const std::string &in_port) const {
  for (const Link &l : links)
    if (l.to_node == n.id && l.to_port == in_port) {
      Node *up = find_node(l.from_node);
      if (!up) return nullptr;
      return up->port(l.from_port, PortDir::Out);
    }
  return nullptr;
}

Node *Graph::upstream_node(const Node &n, const std::string &in_port) const {
  for (const Link &l : links)
    if (l.to_node == n.id && l.to_port == in_port) return find_node(l.from_node);
  return nullptr;
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

bool Graph::evaluate() {
  auto order = topo_order();
  if (order.empty() && !nodes.empty()) return false;
  int idx = 0, total = 0;
  for (Node *n : order)
    if (n->dirty) total++;
  for (Node *n : order) {
    if (cancel.load()) return false;
    if (!n->dirty) continue;
    const NodeDef *def = NodeRegistry::instance().find(n->type);
    if (!def) continue;
    if (on_progress) on_progress(idx, total, n->type);
    n->error.clear();
    auto t0 = std::chrono::steady_clock::now();
    try {
      def->compute(*n);
    } catch (const std::exception &e) {
      n->error = e.what();
    }
    n->last_compute_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
            .count();
    n->dirty = false;
    idx++;
  }
  if (on_progress) on_progress(total, total, "");
  return true;
}

bool Graph::evaluate_at(int res) {
  int prev = resolution;
  resolution = res;
  mark_all_dirty();
  bool ok = evaluate();
  resolution = prev;
  mark_all_dirty();
  return ok;
}

} // namespace gpx

