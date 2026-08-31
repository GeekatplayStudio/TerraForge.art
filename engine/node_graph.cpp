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

// A buffer parked directly on an input port wins over the link. This is how a
// MetaNode feeds values across its boundary into the inner graph: the inner
// node has no upstream link to follow, so the value is injected instead.
const Heightmap *Node::in_hmap(const std::string &name) const {
  if (const Port *local = const_cast<Node *>(this)->port(name, PortDir::In))
    if (local->hmap) return local->hmap.get();
  const Port *up = graph ? graph->upstream(*this, name) : nullptr;
  return up && up->hmap ? up->hmap.get() : nullptr;
}

const TextureRGBA *Node::in_tex(const std::string &name) const {
  if (const Port *local = const_cast<Node *>(this)->port(name, PortDir::In))
    if (local->tex) return local->tex.get();
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

// The node's own input, captured before it computes, so the blend has
// something to mix back toward. Only the primary heightmap input is used —
// that is the channel a filter is transforming.
static const Heightmap *primary_input_of(Node &n) {
  // prefer a port literally called "input", else the first heightmap input
  if (const Heightmap *h = n.in_hmap("input")) return h;
  for (const Port &p : n.ports)
    if (p.dir == PortDir::In && p.type == DataType::Heightmap &&
        p.name != "blend")
      if (const Heightmap *h = n.in_hmap(p.name)) return h;
  return nullptr;
}

// Snapshot the input before compute overwrites the outputs.
static thread_local std::vector<float> g_blend_before;
static void apply_universal_blend_pre(Node &n) {
  g_blend_before.clear();
  if (!n.attrs.find("blend_invert")) return; // node has no universal blend
  if (!n.graph || !n.graph->upstream(n, "blend")) return;
  if (const Heightmap *in = primary_input_of(n)) g_blend_before = in->v;
}

// After compute: mix each heightmap output back toward the untouched input,
// wherever the blend mask says this node should not apply.
static void apply_universal_blend_post(Node &n) {
  if (g_blend_before.empty()) return;
  const Heightmap *mask = n.in_hmap("blend");
  if (!mask || mask->empty()) return;
  bool invert = n.attrs.get_b("blend_invert");
  // the mask may be any range; normalize so "bright applies the effect" holds
  float mn, mx;
  mask->minmax(mn, mx);
  float span = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
  for (Port &p : n.ports) {
    if (p.dir != PortDir::Out || !p.hmap) continue;
    Heightmap &out = *p.hmap;
    if (out.v.size() != g_blend_before.size()) continue;
    for (size_t i = 0; i < out.v.size(); ++i) {
      float m = (mask->v[i] - mn) / span;
      if (invert) m = 1.f - m;
      m = m < 0.f ? 0.f : (m > 1.f ? 1.f : m);
      out.v[i] = g_blend_before[i] + (out.v[i] - g_blend_before[i]) * m;
    }
  }
  g_blend_before.clear();
}

// Terragen puts blend controls at the bottom of most nodes (guide p16): any
// node that transforms a heightmap can have its effect confined to part of the
// terrain. Rather than making every node author remember, the graph gives one
// to any node that transforms a heightmap and does not already declare its own
// mask input. The port is optional, so an unused one costs nothing.
static void add_universal_blend(Node &n) {
  // Only nodes that turn a terrain into a terrain: blending a selector's mask
  // or a router's passthrough toward a heightmap would be meaningless, and
  // exporters are sinks. The "input"/"output" naming is the convention every
  // such filter already follows.
  if (n.category == "Logic" || n.category == "Export" || n.category == "Mask" ||
      n.category == "Group")
    return;
  bool has_in = false, has_out = false, has_mask = false;
  for (const Port &p : n.ports) {
    if (p.type != DataType::Heightmap) continue;
    if (p.dir == PortDir::In) {
      if (p.name == "input") has_in = true;
      if (p.name == "mask" || p.name == "blend" || p.name == "envelope")
        has_mask = true;
    } else if (p.name == "output") {
      has_out = true;
    }
  }
  if (!has_in || !has_out || has_mask) return;
  n.add_in("blend", DataType::Heightmap, true);
  add_bool(n.attrs, "blend_invert", "Invert blend", false, "Blend")
      .tooltip = "Applies this node where the blend input is dark instead of\n"
                 "where it is bright.";
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

bool Graph::evaluate() {
  auto order = topo_order();
  if (order.empty() && !nodes.empty()) return false;
  int idx = 0, total = 0;
  for (Node *n : order)
    if (n->dirty && n->enabled) total++;
  for (Node *n : order) {
    if (cancel.load()) return false;
    if (!n->dirty) continue;
    // a bypassed node costs nothing: readers already resolve through it
    if (!n->enabled) {
      n->dirty = false;
      n->error.clear();
      n->last_compute_ms = 0;
      continue;
    }
    const NodeDef *def = NodeRegistry::instance().find(n->type);
    if (!def) continue;
    apply_universal_blend_pre(*n);
    if (on_progress) on_progress(idx, total, n->type);
    n->error.clear();
    auto t0 = std::chrono::steady_clock::now();
    try {
      def->compute(*n);
      apply_universal_blend_post(*n);
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

