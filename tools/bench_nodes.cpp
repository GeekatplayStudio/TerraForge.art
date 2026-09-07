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
// 2026-09-07: SelectAspect 6, SkyExposure 30, SunExposure 15. SkyExposure is
// sixteen horizon passes over a megapixel; the convex-hull sweep is what
// makes that 30 ms instead of the second a marched ray would cost.
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
    {"SelectAspect", 35},     {"SkyExposure", 160},
    {"SunExposure", 80},
    {"NoiseFractal", 600},    {"TerrainFractal", 600},
    {"TerrainFractal2", 1500}, {"RockyMountains", 1500},
};

int main(int argc, char **argv) {
  bool record = false;
  int res = 1024;      // the ceilings below are recorded at this resolution
  bool only_slow = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--record")) record = true;
    else if (!std::strcmp(argv[i], "--slow")) only_slow = true;
    else if (!std::strncmp(argv[i], "--res=", 6)) res = std::atoi(argv[i] + 6);
  }
  // A resolution sweep answers a question a single number cannot: whether a
  // node is linear in the pixels or worse. An O(n^2) that looks fine at 1024
  // is what makes a 4k bake take minutes.
  const bool sweeping = res != 1024;
  int failures = 0;
  for (const Entry &e : ENTRIES) {
    if (only_slow && e.ceiling_ms < 300) continue;
    Graph g;
    g.resolution = res;
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
    // A ceiling recorded at 1024 says nothing about another resolution, so a
    // sweep reports rather than judges - and reports the per-megapixel rate,
    // which is the number that says whether a node is linear in its pixels.
    if (sweeping) {
      const double mpix = (double)res * res / 1.0e6;
      std::printf("%-18s %9.1f ms   %8.1f ms/Mpix\n", e.type, ms, ms / mpix);
      continue;
    }
    bool ok = ms <= e.ceiling_ms;
    if (!ok) ++failures;
    if (record)
      std::printf("    {\"%s\", %.0f},\n", e.type, ms * 5.0);
    else
      std::printf("%-18s %8.1f ms  (ceiling %6.0f)  %s\n", e.type, ms,
                  e.ceiling_ms, ok ? "ok" : "TOO SLOW");
  }
  if (sweeping) {
    std::printf("(no ceilings applied: they are recorded at 1024)\n");
    return 0;
  }
  if (failures == 0) {
    std::printf("PERF GUARD PASSED\n");
    return 0;
  }
  std::printf("%d PERF FAILURES\n", failures);
  return 1;
}
