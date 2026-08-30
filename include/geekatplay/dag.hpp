#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>

namespace Geekatplay {

struct GraphNode {
    std::string id;
    std::string type;
    std::map<std::string, float> params;
};

struct GraphEdge {
    std::string sourceNode;
    std::string sourcePort;
    std::string targetNode;
    std::string targetPort;
};

struct NodeOutput {
    std::vector<float> heightfield;
    std::vector<float> flow;
    std::vector<float> talus;
    std::vector<float> splatmapRGBA;
};

class TerrainDAG {
public:
    void AddNode(const GraphNode& node);
    void AddEdge(const GraphEdge& edge);
    void Clear();

    // Validates graph for cycles and returns topological order
    bool GetTopologicalOrder(std::vector<std::string>& order) const;

    // Evaluates full DAG and returns map of node outputs
    std::map<std::string, NodeOutput> Evaluate(int res = 128);

private:
    std::vector<GraphNode> m_nodes;
    std::vector<GraphEdge> m_edges;
};

} // namespace Geekatplay
