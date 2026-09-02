// Geekatplay TerraForge - Node and the node registry: ports, field
// evaluation, buffer accessors, registration. Split from
// node_graph.cpp for the 500-line module rule; Graph stays there.
#include "gpx/node_graph.hpp"
#include <algorithm>
#include <chrono>
#include <map>
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

} // namespace gpx
