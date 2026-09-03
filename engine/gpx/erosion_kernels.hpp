// Geekatplay TerraForge — erosion kernels shared between nodes.
// The Hydraulic, Thermal and FlowAccumulation nodes own these simulations;
// ErosionLayers runs the same kernels in sequence and reads their side
// channels (erosion, deposition, talus transport, drainage) to derive
// material layer masks. One implementation, so a droplet in ErosionLayers
// carves exactly what a droplet in Hydraulic carves.
#pragma once
#include "gpx/heightmap.hpp"
#include <cstdint>
#include <vector>

namespace gpx {

struct DropletParams {
  int num_particles = 120000;
  int max_lifetime = 48;
  float inertia = 0.06f;
  float capacity = 5.5f;
  float min_capacity = 0.01f;
  float erode_rate = 0.4f;
  float deposit_rate = 0.25f;
  float evaporate = 0.015f;
  float gravity = 4.f;
  int brush_radius = 3;
};

struct PipeParams {
  int iterations = 120;
  float dt = 0.02f;
  float rain = 0.012f;
  float evaporation = 0.015f;
  float pipe_area = 20.f; // A*g/l lumped
  float capacity_k = 1.f; // Kc
  float erode_k = 0.5f;   // Ks
  float deposit_k = 0.5f; // Kd
  float min_tilt = 0.005f;
};

// Particle droplets on a map normalised to 0..1. Bit-identical for any
// thread count (counter-based particle RNG, fixed-point accumulation).
// erosion_out / deposition_out accumulate removed / dropped material.
void erode_droplets(Heightmap &map, const DropletParams &p, uint32_t seed,
                    Heightmap *erosion_out, Heightmap *deposit_out);

// Shallow-water pipe model (Mei et al. 2007) on a map normalised to 0..1.
// water_out, if given, receives the standing water depth at the end.
void erode_pipes(Heightmap &hmap, const PipeParams &pp, Heightmap *erosion_out,
                 Heightmap *deposit_out, Heightmap *water_out = nullptr);

// Talus relaxation to the angle of repose. `talus` is the height difference
// per texel above which material moves. deposit_out accumulates material
// that arrived at each cell — the scree apron below a cliff.
void thermal_relax(Heightmap &out, float talus, int iterations, float rate,
                   bool converge, Heightmap *deposit_out = nullptr,
                   Heightmap *eroded_out = nullptr);

// D8 upslope contributing area per cell, in cell counts; deterministic total
// order. recv_out receives each cell's receiver index (-1 at outlets).
std::vector<float> flow_accumulate(const Heightmap &in, std::vector<int> *recv_out,
                                   bool fill_pits);

} // namespace gpx
