// Geekatplay TerraForge — the performance guard (test strategy tier 3.6).
// Times a representative node set at 1024x1024 against recorded ceilings.
// Ceilings carry ~5x headroom over a warm 2026 dev machine, so this fails on
// a real regression (an accidental O(n^2), a lost parallel_rows) and not on
// scheduler noise. Run with --record to print a fresh table.
#include "gpx/node_graph.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>

using namespace gpx;

struct Entry {
  const char *type;
  double ceiling_ms; // ~5x a warm baseline
};

// baseline 2026-09-02: Scatter 0, Relax 0.4, ToMask 9, SDF 5, Morph 10,
// AreaRemove 4, Skeleton 61, Flood 10, Kuwahara 10, DirBlur 38, Detrend 5,
// RelElev 11, SmoothFill 15, KMeans 52, Tileable 7, Gabor 63, DLA 52,
// PathFind 186, PathSDF 8, White 2, Landform 73, Quilt (~40), Wavelet (~50)
static const Entry ENTRIES[] = {
    {"ScatterPoints", 50},    {"PointsRelax", 50},
    {"PointsToMask", 80},     {"PointsSDF", 60},
    {"Morphology", 80},       {"AreaRemove", 60},
    {"Skeleton", 400},        {"Flood", 80},
    {"Kuwahara", 80},         {"DirectionalBlur", 250},
    {"Detrend", 60},          {"RelativeElevation", 90},
    {"SmoothFill", 120},      {"KMeans", 350},
    {"MakeTileable", 60},     {"GaborNoise", 400},
    {"DiffusionLimited", 350},{"PathFind", 1200},
    {"PathSDF", 80},          {"WhiteNoise", 40},
    {"Landform", 450},        {"Quilt", 400},
    {"WaveletNoise", 400},    {"DetailEqualizer", 250},
    {"LineNoise", 600},       {"HydraulicBlur", 150},
    {"FlowWarp", 400},        {"ErosionLayers", 8000},
};

int main(int argc, char **argv) {
  bool record = argc > 1 && !std::strcmp(argv[1], "--record");
  int failures = 0;
  for (const Entry &e : ENTRIES) {
    Graph g;
    g.resolution = 1024;
    Node *src = g.add_node("Noise", 0, 0);
    Node *n = g.add_node(e.type, 0, 0);
    if (!n) {
      std::printf("%-18s MISSING\n", e.type);
      ++failures;
      continue;
    }
    for (Port &p : n->ports)
      if (p.dir == PortDir::In && p.type == DataType::Heightmap)
        g.add_link(src->id, "output", n->id, p.name);
    Node *sc = nullptr;
    for (Port &p : n->ports)
      if (p.dir == PortDir::In && p.type == DataType::Points) {
        if (!sc) sc = g.add_node("ScatterPoints", 0, 0);
        g.add_link(sc->id, "points", n->id, p.name);
      }
    g.evaluate(); // warm: allocations, first-touch, caches
    n->dirty = true;
    auto t0 = std::chrono::steady_clock::now();
    g.evaluate();
    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
    bool ok = ms <= e.ceiling_ms;
    if (!ok) ++failures;
    if (record)
      std::printf("    {\"%s\", %.0f},\n", e.type, ms * 5.0);
    else
      std::printf("%-18s %8.1f ms  (ceiling %6.0f)  %s\n", e.type, ms,
                  e.ceiling_ms, ok ? "ok" : "TOO SLOW");
  }
  if (failures == 0) {
    std::printf("PERF GUARD PASSED\n");
    return 0;
  }
  std::printf("%d PERF FAILURES\n", failures);
  return 1;
}
