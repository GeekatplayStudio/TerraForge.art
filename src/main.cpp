#include <iostream>
#include <string>
#include <chrono>
#include "../include/geekatplay/noise.hpp"
#include "../include/geekatplay/erosion.hpp"
#include "../include/geekatplay/geology.hpp"
#include "../include/geekatplay/materials.hpp"
#include "../include/geekatplay/ecosystem.hpp"
#include "../include/geekatplay/atmosphere.hpp"
#include "../include/geekatplay/dag.hpp"
#include "../include/geekatplay/persistence.hpp"

using namespace Geekatplay;

int main(int argc, char* argv[]) {
    std::cout << "======================================================================\n";
    std::cout << " 🏔️  GEEKATPLAY STUDIOS — NODETERRAIN NATIVE C++20 ENGINE\n";
    std::cout << "======================================================================\n\n";

    std::string prompt = "Alpine Glacial Ridge with Hydraulic Erosion & Pine EcoSystem";
    int resolution = 256;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--prompt" && i + 1 < argc) prompt = argv[++i];
        if (arg == "--resolution" && i + 1 < argc) resolution = std::stoi(argv[++i]);
    }

    std::cout << "🎯 [Target Goal]: " << prompt << "\n";
    std::cout << "📐 [Resolution]: " << resolution << "x" << resolution << "\n\n";

    auto tStart = std::chrono::high_resolution_clock::now();

    // 1. Construct DAG
    TerrainDAG dag;
    dag.AddNode({"node_base_ridge", "ridged_multifractal", {{"octaves", 8.0f}, {"frequency", 2.1f}}});
    dag.AddNode({"node_strata", "geological_strata", {{"layers", 12.0f}}});
    dag.AddNode({"node_erosion", "hydraulic_erosion", {{"iterations", 25.0f}}});
    dag.AddNode({"node_talus", "thermal_weathering", {}});
    dag.AddNode({"node_pbr", "pbr_splatmap", {}});

    dag.AddEdge({"node_base_ridge", "height", "node_strata", "height_in"});
    dag.AddEdge({"node_strata", "height", "node_erosion", "height_in"});
    dag.AddEdge({"node_erosion", "height", "node_talus", "height_in"});
    dag.AddEdge({"node_talus", "height", "node_pbr", "height_in"});

    std::vector<std::string> order;
    if (dag.GetTopologicalOrder(order)) {
        std::cout << "⚡ [DAG Solver]: Topological execution order:\n   ";
        for (size_t i = 0; i < order.size(); ++i) {
            std::cout << order[i] << (i + 1 < order.size() ? " -> " : "\n\n");
        }
    }

    // 2. Evaluate
    std::cout << "🚀 Evaluating Procedural C++ Kernels...\n";
    auto outputs = dag.Evaluate(resolution);

    // 3. Populate EcoSystem
    std::vector<EcoSpecies> species = {
        {1, "Alpine Pine", 0.1f, 0.65f, 35.0f, 0.06f, 0.8f, 1.3f},
        {2, "Granite Boulders", 0.0f, 0.90f, 50.0f, 0.02f, 0.5f, 1.1f}
    };
    auto instances = EcoSystemSolver::Populate(outputs["node_talus"].heightfield, resolution, species);

    auto tEnd = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    std::cout << "✨ [Evaluation Complete in " << ms << " ms]:\n";
    std::cout << "   • Evaluated Nodes: " << outputs.size() << "\n";
    std::cout << "   • Heightfield Vertices: " << resolution * resolution << "\n";
    std::cout << "   • EcoSystem Instances: " << instances.size() << "\n";
    std::cout << "   • PBR Splatmap Channels: 4 (Rock, Gravel, Soil, Snow)\n\n";

    // 4. Persistence Journal Check
    WALJournal wal("project_journal.wal");
    wal.LogMutation("SYNTHESIS_C++", prompt);
    std::cout << "🛡️  Write-Ahead Log (WAL) Journaled successfully.\n";
    std::cout << "✅ Geekatplay NodeTerrain Native C++ Engine Finished.\n";

    return 0;
}
