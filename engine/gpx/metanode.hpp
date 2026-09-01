// Geekatplay TerraForge — MetaNodes (P0.3).
//
// A MetaNode encapsulates part of a graph behind a single node. Vue is explicit
// about why this is more than grouping (manual p976): because a MetaNode can be
// saved, reloaded and given its own small interface, it becomes a building
// block for larger graphs rather than just a tidier way to look at one.
//
// Our implementation keeps the inner graph as JSON on the node itself, so it
// travels with the project, survives undo (which snapshots graph JSON), and can
// be written to a library file unchanged.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gpx {

class Graph;
class Node;

// Collapse `node_ids` into a single MetaNode placed at their centre. Links that
// crossed the selection boundary are reconnected to the MetaNode, so the graph
// keeps working. Returns the new node, or null with `err` set.
Node *metanode_group(Graph &g, const std::vector<uint64_t> &node_ids,
                     std::string &err);

// Expand a MetaNode back into its constituent nodes, restoring the links that
// crossed its boundary. Returns the ids of the restored nodes.
std::vector<uint64_t> metanode_ungroup(Graph &g, uint64_t metanode_id,
                                       std::string &err);

// The inner graph, for editing. Load it into a scratch Graph, edit, then store.
bool metanode_open(const Node &meta, Graph &inner, std::string &err);
bool metanode_store(Node &meta, const Graph &inner, std::string &err);

// Loading renumbers node ids, but the boundary and the published-parameter
// table refer to the ids the nodes had when they were grouped. This maps those
// stored ids onto the live nodes of an opened inner graph. Everything that
// reaches into a MetaNode's interior must go through it.
std::map<uint64_t, Node *> metanode_id_map(const Node &meta, Graph &inner);

// ---- published parameters -------------------------------------------------
// Publishing lifts a parameter of an inner node up onto the MetaNode, so the
// MetaNode gets a small purpose-built interface instead of exposing everything.
struct PublishedParam {
  std::string label;      // what the user sees on the MetaNode
  uint64_t inner_node = 0; // which node inside owns it
  std::string attr_key;   // which of its attributes
  std::string group;      // optional grouping in the UI
};

std::vector<PublishedParam> metanode_published(const Node &meta);
bool metanode_publish(Node &meta, uint64_t inner_node, const std::string &key,
                      const std::string &label, const std::string &group = "");
bool metanode_unpublish(Node &meta, uint64_t inner_node, const std::string &key);
// Push the MetaNode's published values into its inner graph before evaluating.
void metanode_apply_published(const Node &meta, Graph &inner);

} // namespace gpx


