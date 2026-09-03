// Geekatplay TerraForge — graph evaluation: the universal blend port, animation
// sampling, the topological evaluate() walk and the buffer memory ceiling.
// Split from node_graph.cpp for the 500-line module rule.
#include "gpx/node_graph.hpp"
#include "node_graph_internal.hpp"
#include <algorithm>
#include <chrono>
#include <map>

namespace gpx {

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
void add_universal_blend(Node &n) {
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

// Write every animated attribute's value for the current time. Doing it here,
// once, before the topological walk means no node has to know that animation
// exists: it simply reads the attribute it always read.
void Graph::apply_animation() {
  for (auto &n : nodes) {
    bool changed = false;
    for (Attribute &a : n->attrs.items) {
      if (a.anim.empty()) continue;
      float v = a.anim.sample(time);
      switch (a.type) {
        case AttrType::Float:
          if (a.f != v) { a.f = v; changed = true; }
          break;
        case AttrType::Int:
        case AttrType::Choice: {
          int iv = (int)std::lround(v);
          if (a.i != iv) { a.i = iv; changed = true; }
        } break;
        case AttrType::Bool: {
          bool bv = v >= 0.5f;
          if (a.b != bv) { a.b = bv; changed = true; }
        } break;
        case AttrType::Seed: {
          uint32_t sv = (uint32_t)std::max(0.f, v);
          if (a.seed != sv) { a.seed = sv; changed = true; }
        } break;
        default: break; // vectors, colours and gradients animate in P7
      }
    }
    if (changed) mark_dirty(n->id);
  }
}

bool Graph::evaluate() {
  apply_animation();
  auto order = topo_order();
  if (order.empty() && !nodes.empty()) return false;

  // Liveness, for the memory ceiling. `last_use[i]` is the furthest point in
  // this walk at which node i's output is still read. Past that the buffer is
  // dead *for this pass* and can be released if memory is tight.
  //
  // Read through bypassed nodes rather than off the raw links: with A -> B
  // (bypassed) -> C, the links say A is last used at B, but C actually reads
  // A's buffer. Releasing it at B would hand C nothing.
  //
  // A node nothing consumes is an output of the graph and is never released,
  // which is why -1 means "keep" rather than "dead immediately".
  std::vector<int> last_use(order.size(), -1);
  if (buffer_budget) {
    std::map<uint64_t, int> pos;
    for (size_t i = 0; i < order.size(); ++i) pos[order[i]->id] = (int)i;
    for (size_t i = 0; i < order.size(); ++i)
      for (const Port &p : order[i]->ports) {
        if (p.dir != PortDir::In) continue;
        Node *up = upstream_node(*order[i], p.name);
        if (!up) continue;
        auto it = pos.find(up->id);
        if (it != pos.end())
          last_use[it->second] = std::max(last_use[it->second], (int)i);
      }
  }

  int idx = 0, total = 0;
  for (Node *n : order)
    if (n->dirty && n->enabled) total++;
  for (size_t step = 0; step < order.size(); ++step) {
    Node *n = order[step];
    if (cancel.load()) return false;
    const NodeDef *def =
        n->dirty && n->enabled ? NodeRegistry::instance().find(n->type) : nullptr;
    if (n->dirty && !n->enabled) {
      // a bypassed node costs nothing: readers already resolve through it
      n->dirty = false;
      n->error.clear();
      n->last_compute_ms = 0;
    } else if (def) {
      apply_universal_blend_pre(*n);
      if (on_progress) on_progress(idx, total, n->type);
      n->error.clear();
      auto t0 = std::chrono::steady_clock::now();
      // inputs that hold a buffer converted from the other raster type are
      // rebuilt from the live upstream every time
      for (Port &p : n->ports)
        if (p.dir == PortDir::In && p.converted) {
          p.hmap.reset();
          p.tex.reset();
          p.converted = false;
        }
      try {
        def->compute(*n);
        apply_universal_blend_post(*n);
      } catch (const std::exception &e) {
        n->error = e.what();
      }
      // what each output now holds, for the connector readouts
      for (Port &p : n->ports) {
        if (p.dir != PortDir::Out) continue;
        p.has_stat = false;
        if (p.hmap && !p.hmap->empty()) {
          p.hmap->minmax(p.stat_min, p.stat_max);
          p.stat_count = (int)p.hmap->w;
          p.has_stat = true;
        } else if (p.tex && !p.tex->empty()) {
          p.stat_count = p.tex->w;
          p.stat_min = 0.f;
          p.stat_max = 1.f;
          p.has_stat = true;
        } else if (p.pts) {
          p.stat_count = (int)p.pts->x.size();
          p.has_stat = true;
        }
      }
      n->last_compute_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - t0)
              .count();
      n->dirty = false;
      idx++;
    }
    // The memory ceiling. Only under pressure: while the graph fits, nothing
    // is released and evaluation behaves exactly as it did before this
    // existed. Over budget, buffers that nothing further in this pass will
    // read are freed, which bounds *peak* memory — the number that decides
    // whether a deep graph at 4096 finishes or runs the machine out.
    //
    // A released node is marked dirty so a later partial edit rebuilds it
    // before anything reads it. That is the trade being made: memory now for
    // recompute later, and it is only made when memory is actually short.
    if (buffer_budget && buffer_bytes() > buffer_budget)
      release_dead_buffers(order, last_use, (int)step);
  }
  if (on_progress) on_progress(total, total, "");
  return true;
}

// Free the outputs of nodes whose last reader in this walk is already behind
// us. Stops as soon as the total is back under budget, so the buffers most
// recently finished with survive longest.
void Graph::release_dead_buffers(const std::vector<Node *> &order,
                                 const std::vector<int> &last_use, int step) {
  size_t total = buffer_bytes();
  for (size_t j = 0; j < order.size() && total > buffer_budget; ++j) {
    if (last_use[j] < 0 || last_use[j] >= step) continue; // still needed
    Node *n = order[j];
    if (is_protected_node(n->id)) continue;
    size_t freed = 0;
    for (Port &p : n->ports) {
      if (p.dir != PortDir::Out) continue; // input buffers are injected values
      if (p.hmap) freed += p.hmap->v.size() * sizeof(float);
      if (p.tex) freed += p.tex->v.size() * sizeof(float);
      p.hmap.reset();
      p.tex.reset();
    }
    if (!freed) continue;
    n->dirty = true;
    total -= freed;
    released_bytes += freed;
  }
}

bool Graph::is_protected_node(uint64_t id) const {
  for (uint64_t p : protected_nodes)
    if (p == id) return true;
  return false;
}

// ---------------------------------------------------------- memory ceiling
size_t Graph::buffer_bytes() const {
  size_t bytes = 0;
  for (const auto &n : nodes)
    for (const Port &p : n->ports) {
      if (p.hmap) bytes += p.hmap->v.size() * sizeof(float);
      if (p.tex) bytes += p.tex->v.size() * sizeof(float);
    }
  return bytes;
}

size_t Graph::evict_buffers(const std::vector<uint64_t> &protect) {
  if (buffer_budget == 0) return 0;
  size_t total = buffer_bytes();
  if (total <= buffer_budget) return 0;

  auto is_protected = [&](uint64_t id) {
    if (is_protected_node(id)) return true;
    for (uint64_t p : protect)
      if (p == id) return true;
    return false;
  };

  // Everything releasable, cheapest to rebuild first. A node that has never
  // been computed has a cost of zero and goes first, which is right: it is
  // holding a buffer nothing produced.
  struct Cand {
    Node *n;
    size_t bytes;
    double ms;
  };
  std::vector<Cand> cands;
  for (auto &n : nodes) {
    if (is_protected(n->id)) continue;
    size_t bytes = 0;
    for (const Port &p : n->ports) {
      if (p.dir != PortDir::Out) continue; // input buffers are injected values
      if (p.hmap) bytes += p.hmap->v.size() * sizeof(float);
      if (p.tex) bytes += p.tex->v.size() * sizeof(float);
    }
    if (bytes) cands.push_back({n.get(), bytes, n->last_compute_ms});
  }
  std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
    if (a.ms != b.ms) return a.ms < b.ms;
    return a.n->id < b.n->id; // deterministic tie-break
  });

  size_t freed = 0;
  for (const Cand &c : cands) {
    if (total - freed <= buffer_budget) break;
    for (Port &p : c.n->ports) {
      if (p.dir != PortDir::Out) continue;
      p.hmap.reset();
      p.tex.reset();
    }
    // Only this node: downstream buffers are still valid, and marking them
    // dirty as well would throw away exactly what the budget is trying to
    // keep. Evaluation walks in topological order, so a dirty node is always
    // rebuilt before its consumers read it.
    c.n->dirty = true;
    freed += c.bytes;
  }
  released_bytes += freed;
  return freed;
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
