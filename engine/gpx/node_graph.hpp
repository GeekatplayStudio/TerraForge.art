// Geekatplay Studio — registry-based dataflow graph.
// Nodes self-register (REGISTER_NODE) with a setup fn (declares ports +
// attributes) and a compute fn. Evaluation is topological with dirty
// propagation and per-node output caching.
#pragma once
#include "gpx/attribute.hpp"
#include "gpx/heightmap.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gpx {

enum class DataType { Heightmap, Texture };
enum class PortDir { In, Out };

struct Port {
  std::string name;
  PortDir dir = PortDir::In;
  DataType type = DataType::Heightmap;
  bool optional = false;
  // output storage
  std::shared_ptr<Heightmap> hmap;
  std::shared_ptr<TextureRGBA> tex;
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
  double last_compute_ms = 0;
  std::string error;

  Graph *graph = nullptr;

  Port *port(const std::string &name);
  Port *first_out(DataType t);

  void add_in(const std::string &name, DataType t = DataType::Heightmap,
              bool optional = false);
  void add_out(const std::string &name, DataType t = DataType::Heightmap);

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
  std::vector<Node *> topo_order() const;
  // returns false if a cycle or error occurred; computes only dirty nodes
  bool evaluate();
  // evaluate the whole graph at an explicit resolution (bake), restores after
  bool evaluate_at(int res);

  uint64_t next_id();
  // take over another graph's nodes and links (replaces current content)
  void adopt(Graph &other);

private:
  uint64_t id_counter_ = 1;
};

} // namespace gpx
