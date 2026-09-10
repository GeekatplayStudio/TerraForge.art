# Erosion and effects over time — roadmap

## Purpose

Natural effects on terrain shapes over time and under influences: every
terrestrial process (water, wind, ice, gravity, coast, dissolution) and the
non-terrestrial ones (impacts, low gravity, no atmosphere, exotic fluids),
as physics that conserves what it moves, at any scale, driven by a clock.

## Where it stands (2026-09-10)

- **Solvers** (`engine/nodes/nodes_erosion*.cpp`, `nodes_hydro.cpp`,
  `nodes_lake.cpp`, `nodes_terrain_fx.cpp`): `Hydraulic` (deterministic
  droplet, and the Mei/Decaudin/Hu pipe model), `Thermal` (angle of repose),
  `StreamPower` (explicit and implicit Braun–Willett with uplift and
  diffusion), `Wind`, `SedimentDeposit`, `Glaciation`, `Dissolve`, `Coast`,
  `Rivers`, `FillBasins` (Priority-Flood), `Flood`, `Lake`, `HydraulicBlur`;
  `ErosionLayers` runs the kernels in sequence and derives seven material
  masks plus wetness and flow.
- **Editor**: eight Vue erosions with one hardness dial
  (`studio/terrain_editor.cpp`), `terrain_effect` op, styles that chain
  Hydraulic → StreamPower → ErosionLayers.
- **Determinism**: identical results across thread counts (tested); a
  96 MB memory cap on the droplet solver; a shared kernel header so the
  same droplet carves the same channel everywhere.
- **Tests**: `test_erosion`, flow-accumulation oracle on an analytic ramp,
  `fill_depressions` battery, goldens `erosion_all`, `hydro`, `effects_all`.

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Terrain | any heightmap in the chain; hardness from `GeologicalStrata`/`Stratify` (StreamPower only) |
| out → | Materials | `ErosionLayers` masks → `MaterialStack`; `erosion_map`, `deposition_map`, `water_map`, `wetness`, `flow` |
| out → | Water | `Rivers`/`Lake`/`Flood` water depth and masks; the `water_level` setting is not driven by them |
| out → | Distribution | wetness and sediment as presence masks |
| in ← | Atmosphere (future) | rain amount, wind direction and speed should drive hydraulic and aeolian rates |
| in ← | Animation | iterations are not keyable; there is no elapsed-time input |

## Scripting surface

Ops: `terrain_effect` (eight erosions, twelve effects), `terrain_style`,
`add_node`/`set_attr` for every parameter, `find_nodes` ("wear the mountains
down"). MCP: `studio_terrain_effect` plus the graph tools. Nodes: 11 Erosion,
3 Hydrology. Guide: recipe 1.

## Gaps (measured in `docs_private/roadmap/erosion.md`, confirmed present)

1. Every side channel is `remap(0,1)` per buffer — a picture, not a field.
   `erosion_map`'s mean moved 35% between compiler flag sets while the
   height moved 0.012%.
2. The pipe model does not conserve mass: a closed cone loses 90.7% at the
   shipped 120 iterations; fractal input gains 36.8%. No conservation test.
3. StreamPower's implicit mode leaves 70% relief at the default 40
   iterations and 19% at the range's top.
4. Four D8 receiver walks with three tie-break rules; StreamPower's 83% is a
   comparison sort a radix sort replaces bit-identically.
5. Every declared output port allocates a full buffer whether wired or not.
6. Missing physics: `Wind` has no avalanching/saltation; `Coast` cannot see
   a neighbour; `Glaciation` is a blur below an ice line; `Snow` has no
   temperature or wind input; no regolith/strata column.
7. No GPU path (deliberately: cross-vendor float equality would break the
   determinism guarantee); erosion is where the seconds go (16.6 s at 2048²).
8. No notion of time: iterations are counts, not years; nothing links the
   Timeline to an eroding terrain.

## Roadmap

### Phase 1 — honest fields
- `Node::out_connected()`: allocate only wired outputs.
- Side channels in physical units (metres of erosion/deposition, metres of
  water, m³/s of flow) with a documented range; `ErosionLayers` masks stay
  0..1 but derive from those. Accept: sum(erosion) − sum(deposition) equals
  the height change to 0.1%.
- Pipe-model conservation: MacCormack/BFECC advection, no `min_tilt` floor
  on flat ground, outflow boundary. Accept: a closed cone keeps ≥ 99.9% of
  its mass at 300 iterations; gate WG-03.
- StreamPower relief guard and lake routing; radix sort; one
  `gpx::hydro::flow_solve` shared by the four users.

### Phase 2 — the missing processes
- Wind with avalanching and saltation, a hardness input on `Hydraulic`,
  a coast that sees its shoreline (fetch, wave energy, longshore drift),
  glaciers with thickness and flow, snow with temperature and wind.
- A regolith column (`LayerStack` data type): bedrock/soil/sediment depth,
  so erosion exposes strata and materials read real depth.
- Non-terrestrial presets: low gravity (repose angle), no atmosphere
  (impacts only, no fluvial), lava, methane; craters as an erosion process
  with age.

### Phase 3 — time
- An `elapsed years` input on every solver, driven by the Timeline: scrub a
  million years; keyable rates; a `TerrainEvolution` node that caches states
  per keyframe so playback is instant.
- Rain and wind from the atmosphere module feed the rates (see
  atmosphere.md Phase 2).
- GPU preview path (not authoritative) mirroring `verify_field_gpu`'s check.

Owner files: `engine/nodes/nodes_erosion*.cpp`, `engine/gpx/erosion_kernels.hpp`,
`engine/hydrology.cpp`, `studio/terrain_editor.cpp`.
