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
  if (!up) return nullptr;
  if (up->hmap) return up->hmap.get();
  // A texture wired into a heightmap input: its luminance is the height.
  // Cached on the input port, rebuilt whenever the source differs.
  if (up->type == DataType::Texture && up->tex && !up->tex->empty()) {
    Port *local = const_cast<Node *>(this)->port(name, PortDir::In);
    if (!local) return nullptr;
    const TextureRGBA &t = *up->tex;
    auto hm = std::make_shared<Heightmap>(t.w, t.h);
    for (int y = 0; y < t.h; ++y)
      for (int x = 0; x < t.w; ++x) {
        const float *p = t.px(x, y);
        hm->at(x, y) = p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f;
      }
    local->hmap = hm; // the local value until the next compute
    local->converted = true;
    return local->hmap.get();
  }
  return nullptr;
}

const TextureRGBA *Node::in_tex(const std::string &name) const {
  if (const Port *local = const_cast<Node *>(this)->port(name, PortDir::In))
    if (local->tex) return local->tex.get();
  const Port *up = graph ? graph->upstream(*this, name) : nullptr;
  if (!up) return nullptr;
  if (up->tex) return up->tex.get();
  // a heightmap wired into a texture input: a grey image of it, 0..1
  if (up->type == DataType::Heightmap && up->hmap && !up->hmap->empty()) {
    Port *local = const_cast<Node *>(this)->port(name, PortDir::In);
    if (!local) return nullptr;
    const Heightmap &h = *up->hmap;
    float mn, mx;
    h.minmax(mn, mx);
    float d = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
    auto tx = std::make_shared<TextureRGBA>(h.w, h.h);
    for (int y = 0; y < h.h; ++y)
      for (int x = 0; x < h.w; ++x) {
        float v = (h.at(x, y) - mn) / d;
        float *p = tx->px(x, y);
        p[0] = p[1] = p[2] = v;
        p[3] = 1.f;
      }
    local->tex = tx;
    local->converted = true;
    return local->tex.get();
  }
  return nullptr;
}

const PointCloud *Node::in_points(const std::string &name) const {
  if (const Port *local = const_cast<Node *>(this)->port(name, PortDir::In))
    if (local->pts) return local->pts.get();
  const Port *up = graph ? graph->upstream(*this, name) : nullptr;
  return up && up->pts ? up->pts.get() : nullptr;
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

PointCloud &Node::out_points(const std::string &name) {
  Port *p = port(name, PortDir::Out);
  if (!p->pts) p->pts = std::make_shared<PointCloud>();
  p->pts->clear();
  return *p->pts;
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
