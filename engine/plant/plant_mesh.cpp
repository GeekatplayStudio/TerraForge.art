// Geekatplay TerraForge - the plant mesh's own methods, the store the studio
// picks finished meshes from, and the fingerprint that says whether a
// rebuild would change anything (gpx/plant.hpp).
//
// The store is the one piece of shared state in the plant engine, and it
// is a mailbox, not a cache: the root's compute drops its mesh in under
// the root's id, the studio picks it up after the evaluation, and a
// removed root's slot is forgotten. A mutex guards it because graph
// evaluation and the studio's scene rebuild are on different threads; the
// meshes themselves are immutable once stored (shared_ptr<const>).
//
// The fingerprint hashes what a build reads and nothing else: the subtree's
// attribute values (every field a Random, Curve, Gradient, Colour, Text or
// number carries), the Plant links between those nodes, the field links
// into their driven ports, and the options. Node positions, names, errors
// and UI state are left out, so moving a node in the editor is not a
// rebuild. Equal fingerprints mean equal bytes because the build reads
// nothing else - that is the promise the walker keeps.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

namespace gpx {

// ------------------------------------------------------------------- mesh

void PlantMesh::clear() {
  pos.clear(); nrm.clear(); uv.clear(); wind.clear(); tint.clear(); idx.clear();
  parts.clear(); materials.clear();
  bmin[0] = bmin[1] = bmin[2] = bmax[0] = bmax[1] = bmax[2] = 0.f;
  height_m = 0.f;
  primitives = leaves = cut = 0;
  warnings.clear();
}

void PlantMesh::append(const PlantMesh &m, const float offset[3]) {
  // materials merge by the node they came from (a built-in has node 0 and
  // is matched by name), so two individuals of one species share theirs
  std::vector<int> remap(m.materials.size(), 0);
  for (size_t i = 0; i < m.materials.size(); ++i) {
    const PlantMaterial &src = m.materials[i];
    int found = -1;
    for (size_t j = 0; j < materials.size(); ++j)
      if (materials[j].node == src.node && materials[j].name == src.name) { found = (int)j; break; }
    if (found < 0) {
      materials.push_back(src);
      found = (int)materials.size() - 1;
    }
    remap[i] = found;
  }
  const uint32_t v0 = (uint32_t)vertex_count();
  const uint32_t i0 = (uint32_t)idx.size();
  pos.reserve(pos.size() + m.pos.size());
  for (size_t i = 0; i < m.pos.size(); i += 3) {
    pos.push_back(m.pos[i] + offset[0]);
    pos.push_back(m.pos[i + 1] + offset[1]);
    pos.push_back(m.pos[i + 2] + offset[2]);
  }
  nrm.insert(nrm.end(), m.nrm.begin(), m.nrm.end());
  uv.insert(uv.end(), m.uv.begin(), m.uv.end());
  wind.insert(wind.end(), m.wind.begin(), m.wind.end());
  tint.insert(tint.end(), m.tint.begin(), m.tint.end());
  idx.reserve(idx.size() + m.idx.size());
  for (uint32_t k : m.idx) idx.push_back(k + v0);
  for (PlantPart p : m.parts) {
    p.first_index += i0;
    p.material = p.material >= 0 && p.material < (int)remap.size() ? remap[(size_t)p.material] : 0;
    parts.push_back(p);
  }
  primitives += m.primitives;
  leaves += m.leaves;
  cut += m.cut;
  if (!m.warnings.empty()) warnings += m.warnings;
  compute_bounds();
}

void PlantMesh::compute_bounds() {
  if (pos.empty()) {
    bmin[0] = bmin[1] = bmin[2] = bmax[0] = bmax[1] = bmax[2] = 0.f;
    height_m = 0.f;
    return;
  }
  for (int c = 0; c < 3; ++c) bmin[c] = bmax[c] = pos[(size_t)c];
  for (size_t i = 0; i < pos.size(); i += 3)
    for (int c = 0; c < 3; ++c) {
      bmin[c] = std::min(bmin[c], pos[i + (size_t)c]);
      bmax[c] = std::max(bmax[c], pos[i + (size_t)c]);
    }
  height_m = bmax[1];
}

// ------------------------------------------------------------------ store

namespace {
std::mutex &store_mutex() {
  static std::mutex m;
  return m;
}
std::map<uint64_t, std::shared_ptr<const PlantMesh>> &store() {
  static std::map<uint64_t, std::shared_ptr<const PlantMesh>> s;
  return s;
}
} // namespace

std::shared_ptr<const PlantMesh> plant_mesh_for(uint64_t root_id) {
  std::lock_guard<std::mutex> lock(store_mutex());
  auto it = store().find(root_id);
  return it == store().end() ? nullptr : it->second;
}

void plant_mesh_store(uint64_t root_id, std::shared_ptr<const PlantMesh> mesh) {
  std::lock_guard<std::mutex> lock(store_mutex());
  store()[root_id] = std::move(mesh);
}

void plant_mesh_forget(uint64_t root_id) {
  std::lock_guard<std::mutex> lock(store_mutex());
  store().erase(root_id);
}

// ------------------------------------------------------------ fingerprint

namespace {

struct Fnv {
  uint64_t h = 1469598103934665603ull;
  void bytes(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ull;
    }
  }
  void f(float v) { bytes(&v, sizeof v); }
  void i(int64_t v) { bytes(&v, sizeof v); }
  void s(const std::string &v) {
    i((int64_t)v.size());
    bytes(v.data(), v.size());
  }
  void curve(const Curve &c) {
    i(c.interp);
    f(c.xmin); f(c.xmax); f(c.ymin); f(c.ymax);
    i((int64_t)c.keys.size());
    for (const CurveKey &k : c.keys) { f(k.x); f(k.y); f(k.sl); f(k.sr); }
  }
  void curves(const CurveSet &cs) {
    i((int64_t)cs.curves.size());
    for (const Curve &c : cs.curves) curve(c);
    for (float w : cs.weights) f(w);
  }
  void attr(const Attribute &a) {
    s(a.key);
    i((int64_t)a.type);
    switch (a.type) {
      case AttrType::Float: f(a.f); break;
      case AttrType::Int: case AttrType::Choice: i(a.i); break;
      case AttrType::Bool: i(a.b ? 1 : 0); break;
      case AttrType::Seed: i(a.seed); break;
      case AttrType::Range: case AttrType::Vec2: f(a.v2[0]); f(a.v2[1]); break;
      case AttrType::Color: for (float c : a.col) f(c); break;
      case AttrType::Gradient:
        i((int64_t)a.stops.size());
        for (const GradientStop &g : a.stops) { f(g.t); f(g.r); f(g.g); f(g.b); f(g.a); }
        break;
      case AttrType::Filename: case AttrType::Text: s(a.s); break;
      case AttrType::Field: i(a.fw); i(a.fh); bytes(a.field.data(), a.field.size() * sizeof(float)); break;
      case AttrType::Curve: curves(a.curves); break;
      case AttrType::Random:
        f(a.f); f(a.spread); i(a.spread_mode); i(a.scope); i(a.hier_level); i(a.hier_cascade ? 1 : 0);
        curves(a.curve_along); curves(a.curve_hier);
        break;
    }
  }
};

} // namespace

uint64_t plant_fingerprint(const Graph &g, const Node &root, const PlantBuildOptions &o) {
  Fnv h;
  std::vector<const Node *> nodes = plant_subtree(g, root);
  std::vector<uint64_t> ids;
  for (const Node *n : nodes) ids.push_back(n->id);
  for (const Node *n : nodes) {
    h.i((int64_t)n->id);
    h.s(n->type);
    h.i(n->enabled ? 1 : 0);
    for (const Attribute &a : n->attrs.items) h.attr(a);
  }
  // links into the subtree: plant links and driven field ports alike; a
  // field subgraph's own attributes are hashed too, since its value is read
  for (const Link &l : g.links) {
    if (std::find(ids.begin(), ids.end(), l.to_node) == ids.end()) continue;
    h.i((int64_t)l.from_node); h.s(l.from_port); h.i((int64_t)l.to_node); h.s(l.to_port);
    if (std::find(ids.begin(), ids.end(), l.from_node) != ids.end()) continue;
    // an upstream field node: walk it and what feeds it (bounded)
    std::vector<uint64_t> stack{l.from_node};
    std::vector<uint64_t> seen;
    while (!stack.empty() && seen.size() < 512) {
      const uint64_t id = stack.back();
      stack.pop_back();
      if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
      seen.push_back(id);
      const Node *f = g.find_node(id);
      if (!f) continue;
      h.i((int64_t)f->id);
      h.s(f->type);
      for (const Attribute &a : f->attrs.items) h.attr(a);
      for (const Link &m : g.links)
        if (m.to_node == id) {
          h.i((int64_t)m.from_node); h.s(m.from_port); h.s(m.to_port);
          stack.push_back(m.from_node);
        }
    }
  }
  h.i(o.seed);
  h.f(o.age); h.f(o.health); h.f(o.season); h.f(o.time);
  h.i(o.lod); h.f(o.detail);
  h.i(o.wind_weights ? 1 : 0); h.i(o.tints ? 1 : 0);
  h.s(o.asset_dir);
  for (float v : o.facing) h.f(v);
  return h.h;
}

} // namespace gpx
