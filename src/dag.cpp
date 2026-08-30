#include "../include/geekatplay/dag.hpp"
#include "../include/geekatplay/noise.hpp"
#include "../include/geekatplay/erosion.hpp"
#include "../include/geekatplay/geology.hpp"
#include "../include/geekatplay/materials.hpp"
#include <queue>

namespace Geekatplay {

void TerrainDAG::AddNode(const GraphNode& node) {
    m_nodes.push_back(node);
}

void TerrainDAG::AddEdge(const GraphEdge& edge) {
    m_edges.push_back(edge);
}

void TerrainDAG::Clear() {
    m_nodes.clear();
    m_edges.clear();
}

bool TerrainDAG::GetTopologicalOrder(std::vector<std::string>& order) const {
    order.clear();
    std::map<std::string, int> inDegrees;
    std::map<std::string, std::vector<std::string>> adj;

    for (const auto& n : m_nodes) {
        inDegrees[n.id] = 0;
        adj[n.id] = {};
    }

    for (const auto& e : m_edges) {
        if (inDegrees.find(e.targetNode) != inDegrees.end()) {
            inDegrees[e.targetNode]++;
            adj[e.sourceNode].push_back(e.targetNode);
        }
    }

    std::queue<std::string> q;
    for (const auto& [id, deg] : inDegrees) {
        if (deg == 0) q.push(id);
    }

    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();
        order.push_back(curr);

        for (const auto& neighbor : adj[curr]) {
            inDegrees[neighbor]--;
            if (inDegrees[neighbor] == 0) {
                q.push(neighbor);
            }
        }
    }

    return order.size() == m_nodes.size();
}

std::map<std::string, NodeOutput> TerrainDAG::Evaluate(int res) {
    std::map<std::string, NodeOutput> outputs;
    std::vector<std::string> order;

    if (!GetTopologicalOrder(order)) {
        return outputs; // Cycle detected
    }

    std::map<std::string, GraphNode> nodeMap;
    for (const auto& n : m_nodes) nodeMap[n.id] = n;

    for (const auto& nodeId : order) {
        const auto& node = nodeMap[nodeId];
        NodeOutput out;

        // Collect primary input heightfield from incoming edge
        std::vector<float> inH(res * res, 0.0f);
        for (const auto& e : m_edges) {
            if (e.targetNode == nodeId && outputs.find(e.sourceNode) != outputs.end()) {
                inH = outputs[e.sourceNode].heightfield;
                break;
            }
        }

        if (node.type == "perlin_noise") {
            float freq = node.params.count("frequency") ? node.params.at("frequency") : 4.0f;
            out.heightfield = NoiseGenerator::Perlin2D(res, freq);
        } else if (node.type == "ridged_multifractal") {
            float oct = node.params.count("octaves") ? node.params.at("octaves") : 8.0f;
            float freq = node.params.count("frequency") ? node.params.at("frequency") : 2.0f;
            out.heightfield = NoiseGenerator::RidgedMultifractal(res, static_cast<int>(oct), freq);
        } else if (node.type == "voronoi_cellular") {
            float cells = node.params.count("cells") ? node.params.at("cells") : 16.0f;
            out.heightfield = NoiseGenerator::VoronoiCellular(res, static_cast<int>(cells));
        } else if (node.type == "geological_strata") {
            float layers = node.params.count("layers") ? node.params.at("layers") : 12.0f;
            out.heightfield = GeologySolver::GeologicalStrata(inH, res, static_cast<int>(layers));
        } else if (node.type == "tectonic_fault") {
            float disp = node.params.count("displacement") ? node.params.at("displacement") : 0.25f;
            out.heightfield = GeologySolver::TectonicFault(inH, res, 45.0f, disp);
        } else if (node.type == "hydraulic_erosion") {
            float iters = node.params.count("iterations") ? node.params.at("iterations") : 20.0f;
            auto resE = ErosionSolver::SimulateHydraulic(inH, res, static_cast<int>(iters));
            out.heightfield = resE.heightfield;
            out.flow = resE.flowAccumulation;
        } else if (node.type == "thermal_weathering") {
            auto resT = ErosionSolver::SimulateThermal(inH, res);
            out.heightfield = resT.heightfield;
            out.talus = resT.talusMap;
        } else if (node.type == "pbr_splatmap") {
            auto resM = MaterialSolver::GeneratePBRSplatmap(inH, res);
            out.heightfield = inH;
            out.splatmapRGBA = resM.splatmapRGBA;
        } else {
            out.heightfield = inH;
        }

        outputs[nodeId] = out;
    }

    return outputs;
}

} // namespace Geekatplay
