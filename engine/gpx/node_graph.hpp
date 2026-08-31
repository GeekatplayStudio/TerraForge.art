// Geekatplay Studio â€” registry-based dataflow graph.
// Nodes self-register (REGISTER_NODE) with a setup fn (declares ports +
// attributes) and a compute fn. Evaluation is topological with dirty
// propagation and per-node output caching.
#pragma once
#include "gpx/attribute.hpp"
#include "gpx/field.hpp"
#include "gpx/heightmap.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gpx {

// Heightmap/Texture are the raster domain (buffers at the graph resolution).
// Field is the second domain: a function evaluated per point, with its own
// value type carried alongside. See gpx/field.hpp for why both exist.
enum class DataType { Heightmap, Texture, Field };
enum class PortDir { In, Out };

class Node;

// A field port carries no buffer â€” it carries the promise that the node can be
// asked for a value at any point. Evaluation is pull-based: asking an output
// port for a value walks upstream through the links.
using FieldEvalFn = std::function<FieldValue(const Node &, const FieldContext &)>;

struct Port {
  std::string name;
  PortDir dir = PortDir::In;
  DataType type = DataType::Heightmap;
  bool optional = false;
  // output storage (raster domain)
  std::shared_ptr<Heightmap> hmap;
  std::shared_ptr<TextureRGBA> tex;
  // field domain: what kind of value this port carries, and (on outputs) how
  // to produce it
  FieldType field_type = FieldType::Number;
  FieldEvalFn field_eval;
};

class Graph;

class Node {
public:
  uint64_t id = 0;
  std::string type;     // registry key, e.g. "NoiseFBM"
  std::string category; // e.g. "Primitive"
  float pos_x = 0, pos_y = 0;
  AttrSet attrs;
  std::vector<Port> ports;
  bool dirty = true;
  // Bypass. A disabled node is not computed and does not appear in the data
  // flow: anything reading its output reads straight through to its own input
  // instead, so the graph behaves as though the node were not there. Same
  // semantic as Terragen's Enable checkbox, and it costs each node nothing
  // because the graph implements it during link resolution.
  bool enabled = true;
  double last_compute_ms = 0;
  std::string error;

  Graph *graph = nullptr;

  Port *port(const std::string &name);
  // direction-aware lookup â€” required when a node names an input and an
  // output identically (e.g. Levels' "texture" in and out)
  Port *port(const std::string &name, PortDir dir);
  Port *first_out(DataType t);

  void add_in(const std::string &name, DataType t = DataType::Heightmap,
              bool optional = false);
  void add_out(const std::string &name, DataType t = DataType::Heightmap);

  // ---- field domain -------------------------------------------------------
  // A field input declares the value type it expects; a field output declares
  // what it produces and how. `eval` receives the node and the point being
  // asked about, so field nodes are stateless and re-entrant â€” which is what
  // lets them be evaluated in parallel and transpiled to GLSL.
  void add_field_in(const std::string &name, FieldType t = FieldType::Number,
                    bool optional = false);
  void add_field_out(const std::string &name, FieldType t, FieldEvalFn eval);

  // Pull a value from a connected field input. If nothing is connected,
  // `fallback` is returned, so every field node has a defined result even in a
  // half-built graph â€” a partially wired graph must still preview.
  FieldValue in_field(const std::string &name, const FieldContext &ctx,
                      FieldValue fallback = FieldValue(0.f)) const;
  float in_number(const std::string &name, const FieldContext &ctx,
                  float fallback = 0.f) const;
  bool field_connected(const std::string &name) const;
  // Evaluate one of this node's own field outputs.
  FieldValue eval_field(const std::string &name, const FieldContext &ctx) const;

  // resolved input (follows link to upstream out-port); null if unconnected
  const Heightmap *in_hmap(const std::string &name) const;
  const TextureRGBA *in_tex(const std::string &name) const;

  // output accessors, allocate on demand at graph resolution
  Heightmap &out_hmap(const std::string &name);
  TextureRGBA &out_tex(const std::string &name);
};

struct Link {
  uint64_t id = 0;
  uint64_t from_node = 0;
  std::string from_port;
  uint64_t to_node = 0;
  std::string to_port;
};

struct NodeDef {
  std::string type, category, description;
  std::function<void(Node &)> setup;
  std::function<void(Node &)> compute;
};

class NodeRegistry {
public:
  static NodeRegistry &instance();
  void reg(NodeDef def);
  const NodeDef *find(const std::string &type) const;
  std::vector<const NodeDef *> all() const; // sorted by category, then name
private:
  std::map<std::string, NodeDef> defs_;
};

struct Registrar {
  Registrar(NodeDef def) { NodeRegistry::instance().reg(std::move(def)); }
};

// variadic so commas inside lambda bodies survive preprocessing
#define REGISTER_NODE(TYPE, ...)                                                \
  static ::gpx::Registrar reg_##TYPE{{#TYPE, __VA_ARGS__}};

class Graph {
public:
  int resolution = 512; // preview resolution; bake overrides per-run
  // Scene time in seconds. Animated attributes are sampled from their track at
  // this time before each evaluation, and it reaches field nodes through
  // FieldContext::time. Setting it is what "scrubbing the timeline" means.
  float time = 0.f;
  std::vector<std::unique_ptr<Node>> nodes;
  std::vector<Link> links;
  std::atomic<bool> cancel{false};
  // progress callback (node index, count, node type)
  std::function<void(int, int, const std::string &)> on_progress;

  Node *add_node(const std::string &type, float x = 0, float y = 0);
  void remove_node(uint64_t id);
  Node *find_node(uint64_t id) const;
  bool add_link(uint64_t from_node, const std::string &from_port,
                uint64_t to_node, const std::string &to_port);
  void remove_link(uint64_t id);
  void clear();

  // find upstream out-port feeding (node,in_port); null if none
  const Port *upstream(const Node &n, const std::string &in_port) const;
  Node *upstream_node(const Node &n, const std::string &in_port) const;

  void mark_dirty(uint64_t node_id); // node + all downstream
  void mark_all_dirty();
  // sample every animated attribute at Graph::time (called by evaluate)
  void apply_animation();
  std::vector<Node *> topo_order() const;
  // returns false if a cycle or error occurred; computes only dirty nodes
  bool evaluate();
  // evaluate the whole graph at an explicit resolution (bake), restores after
  bool evaluate_at(int res);

  uint64_t next_id();
  // take over another graph's nodes and links (replaces current content)
  void adopt(Graph &other);

  // ------------------------------------------------------- memory ceiling
  // Cached node outputs are by far the graph's largest memory cost. Every
  // output port holds its buffer for the graph's lifetime, so the total is
  // one buffer per output port, not one per live value: at 4096 a heightmap
  // output is 64 MB and an RGBA texture output is 256 MB, and a deep graph
  // holds gigabytes that nothing will read again.
  //
  // They are kept because a partial re-evaluation reads them: change a node
  // near the end of a chain and everything upstream stays clean and is not
  // recomputed. Freeing one therefore trades memory for recompute time, which
  // is the trade a budget exists to make deliberately.
  //
  // 0 means unlimited, which is the behaviour that shipped before this.
  size_t buffer_budget = 0;
  // Nodes never released whatever the budget says: what the viewport is
  // showing, and what the user has selected.
  std::vector<uint64_t> protected_nodes;
  // Total released since the graph was created. A counter rather than a flag
  // so "the ceiling never actually did anything" is distinguishable from "the
  // ceiling is working", which is the difference between a feature and a
  // claim.
  size_t released_bytes = 0;

  // What the cached buffers currently hold, in bytes. Counts input ports too
  // (a MetaNode parks values there), because they are equally resident.
  size_t buffer_bytes() const;

  // Release cached outputs until the total fits the budget, cheapest to
  // rebuild first — the graph already records what each node cost, so there
  // is no reason to throw away an erosion pass before a noise pass holding
  // the same bytes. Ties break on node id, so eviction is deterministic.
  //
  // A released node is marked dirty, and evaluation walks in topological
  // order, so it is rebuilt before anything reads it. Buffers parked on input
  // ports are never touched: those are values injected across a MetaNode
  // boundary and marking a node dirty does not bring them back.
  //
  // `protect` names nodes to leave alone whatever the budget says — what the
  // viewport is showing, and what the user has selected.
  // Returns the bytes released.
  size_t evict_buffers(const std::vector<uint64_t> &protect = {});

private:
  uint64_t id_counter_ = 1;
  // Called from evaluate() when the budget is exceeded: frees outputs whose
  // last reader in this walk has already been passed.
  void release_dead_buffers(const std::vector<Node *> &order,
                            const std::vector<int> &last_use, int step);
  bool is_protected_node(uint64_t id) const;
};

} // namespace gpx

