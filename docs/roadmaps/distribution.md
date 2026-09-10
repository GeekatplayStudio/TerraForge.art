# Distribution — roadmap

## Purpose

Intelligent placement of objects and elements in designated areas: by
density, environment (altitude, slope, orientation, wetness, sun), by
affinity and repulsion between populations, by hand where wanted, at any
scale including unbounded ground, deterministic, and — over time — growing.

## Where it stands (2026-09-10)

- **The five-stage contract** (`engine/gpx/scatter.hpp`): candidates on a
  world-anchored lattice with stable ids → presence filter → interaction
  (affinity/repulsion/overlap) → species → transform. No RNG state, thread
  count or time reaches a decision; raising density only adds.
- **Nodes**: `EcosystemLayer` (Vue's EcoSystem material, in metres),
  `ScatterArea`/`PointsInteract`/`PointsTransform`, the Points family
  (`ScatterPoints`, `PointsRelax`, `PointsFilter`, `PointsToMask`,
  `PointsSDF`, `PointsMerge`, `PointsShuffle`, `PointsSetValues`,
  `PointsFromCsv`, `ExportPoints`), `DistributionLayer` (one rule shades and
  places), `EffectorLayer`, `FieldStones`/`FieldGrass` (functions, GPU),
  `FakeStones`/`GrassDisplacement`/`Pebbles`/`FirTrees` (raster).
- **Studio**: `set_scatter` binds a mesh to a cloud per species; instanced
  rendering with LOD chains and billboards; camera-driven population on
  infinite ground (`eco_dynamic.cpp`); blue-noise; `points_stats`;
  `export_instances`; the Material Studio's Ecosystem tabs.
- Tests: `test_ecosystem.cpp` (density superset, bands, affinity/repulsion,
  species, thread-count identity, world-cell stability at 1000 km).

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Terrain / Erosion | slope, altitude, masks, wetness, flow, `TerrainImprint objects` |
| in ← | Materials | presence from `DistributionLayer`/`EcosystemLayer` |
| in ← | Planets | populations on the surround follow the camera |
| in ← | Water | water masks as exclusion |
| out → | Objects | instances of meshes (LOD, billboards, shadows) |
| out → | Render | instances exported to Mitsuba/Cycles |
| out → | Animation | `scatter.scale/jitter/sway` tracks |
| in ← | Atmosphere (future) | wind sway, seasons |

## Scripting surface

Ops: `set_scatter`, `place_on_terrain`, `set_ground`, `points_stats`,
`export_instances`, `save_node_preview`, `add_component scatter|ecosystem`,
graph ops for the chain. MCP: one tool each. Macros:
`examples/macros/ecosystem_layers.json`, `scatter_forest.json`,
`rocks_grass_layers.json`. Guide: recipe 4.

## Gaps (verified)

1. Affinity and repulsion do nothing on unbounded (camera-driven)
   populations; masks need a tile; the population follows one camera.
2. Nine parameters move nothing under the audit harness
   (`docs_private/PARAM_AUDIT.txt`): the unbounded-mode dials — no test
   separates "inert by design" from "broken".
3. No painting: no brush to add/remove/override instances (Vue's EcoSystem
   painter, design E6).
4. No simulation: no growth, competition, self-thinning, age classes,
   seasons (E7).
5. No stable 128-bit identity / counter-based RNG; node ids are renumbered
   on load (a prerequisite named in the design for persistent hand edits).
6. `PointCloud` buffers escape the graph memory ceiling and the golden
   hasher (heightmap ports only).
7. No USD PointInstancer / glTF instancing export; CSV/PLY only.
8. Contact blend at object bases (WG-06) not built.

## Roadmap

### Phase 1 — unbounded done right
- Interaction on unbounded populations: evaluate affinity/repulsion per
  world cell against the neighbouring cells of the layer below (the lattice
  already exists); masks on the surround through a world-space mask
  sampler. Accept: the ecosystem test's affinity/repulsion cases pass with
  `unbounded` on.
- Points in the memory ceiling and in the golden hasher; a scatter golden
  project.
- USD PointInstancer and glTF `EXT_mesh_gpu_instancing` export.

### Phase 2 — painting
- Stable ids (Philox RNG, `gpx::Identity`), then an `EcosystemPaint` layer:
  brushes to add/remove instances and override species/scale/rotation,
  stored as deltas against ids so a density change keeps hand edits.
- "Convert to objects": selected instances become editable scene objects.
- Contact blend on bases; wind sway from the weather block.

### Phase 3 — living populations
- Simulation over the time base: seeding, growth curves per species, light
  competition (sun exposure), self-thinning, death and regrowth, seasons
  (colour, leaf density) — cached per keyframe so scrubbing is instant.
- Roads/paths/fields as distribution constraints (`PathSDF` already);
  settlements and structures placed by rules.
- Biomes from the planet's environment (materials.md / planets.md Phase 3).

Owner files: `engine/gpx/scatter.hpp`, `engine/scatter_*.cpp`,
`engine/nodes/nodes_ecosystem.cpp`, `nodes_points.cpp`, `studio/eco_dynamic.cpp`,
`studio/renderer_instances.cpp`.
