#include "../include/geekatplay/mcp_bridge.hpp"
#include "../include/geekatplay/dag.hpp"
#include "../include/geekatplay/noise.hpp"
#include <sstream>

namespace Geekatplay {

std::string MCPBridge::HandleJSONRPC(const std::string& requestJson) {
    if (requestJson.find("tools/list") != std::string::npos) {
        return "{\"jsonrpc\":\"2.0\",\"result\":{\"tools\":["
               "{\"name\":\"terrain_evaluate\",\"description\":\"Evaluate C++ terrain graph\"},"
               "{\"name\":\"terrain_simulate_erosion\",\"description\":\"Run hydraulic & thermal erosion\"},"
               "{\"name\":\"terrain_bake_pbr\",\"description\":\"Bake PBR splatmaps and normal maps\"}"
               "]},\"id\":1}";
    }

    if (requestJson.find("terrain_evaluate") != std::string::npos) {
        TerrainDAG dag;
        dag.AddNode({"n1", "ridged_multifractal", {{"octaves", 6.0f}, {"frequency", 2.5f}}});
        dag.AddNode({"n2", "hydraulic_erosion", {{"iterations", 15.0f}}});
        dag.AddEdge({"n1", "height", "n2", "height_in"});

        auto outputs = dag.Evaluate(64);
        std::ostringstream oss;
        oss << "{\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"success\",\"evaluated_nodes_count\":"
            << outputs.size() << "},\"id\":1}";
        return oss.str();
    }

    return "{\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"ok\"},\"id\":1}";
}

} // namespace Geekatplay
