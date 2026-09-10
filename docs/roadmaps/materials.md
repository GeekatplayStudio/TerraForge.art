# Materials — roadmap

## Purpose

A complete editor for ultra-realistic materials, by studio and by nodes;
libraries and presets; distribution of materials driven by terrain shape
(altitude, slope, orientation, curvature, erosion) and by the environment
(wetness, snow, sun, wind), so one material dresses a whole world.

## Where it stands (2026-09-10)

- **Graph**: `MaterialOutput` with eight channels and the full Vue-style
  parameter block (`engine/gpx/material_params.hpp`); channel nodes
  (`Levels`, `GradientMap`, `NormalBlend`, `TextureTransform`, AO and
  curvature from height, `ChannelMix`), textures (`TextureFile`, `PBRMaterial`
  from ambientCG sets, `Splatmap`), layers (`MaterialLayer` with altitude/
  slope/orientation presence, `FractalColor`, `NaturalGrain`),
  `MaterialStack` (six mask/albedo pairs, environment influences),
  `DistributionLayer`, `EffectorLayer`, `EcosystemLayer`, `MaterialSource`
  (reuse by object), field-domain `FieldDistribution`/`FieldColorMix`.
- **Studio**: the Material Studio (tabs, preview sphere/shapes, snapshots,
  hierarchy, follow-selection lock), the browser, the library
  (`.gpxmat` + thumbnail), 22 base presets, 14 gradients, AI textures.
- **Types read from the graph, never stored** (`material_type_of`).
- **Environment drivers**: `ErosionLayers` → `MaterialStack` (bedrock,
  scree, soil, grass, sediment, riverbed, snow, wetness, flow);
  `MaterialLayer` altitude/slope/orientation; `TerrainOutput albedo`.
- **Volumetrics**: a material with `vol_density > 0` is a medium (one model
  with fog and clouds); exported to Mitsuba as a homogeneous medium.
- Tests: channel ops, stack ops, editor, source, source-scene.

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Terrain / Erosion | heightmap, masks, wetness, flow |
| in ← | Atmosphere (partial) | snow line only; no wetness from rain, no sun bleaching |
| out → | Distribution | `DistributionLayer`/`EcosystemLayer` presence → points |
| out → | Objects | `material_node` per object; `MaterialSource` follows the object |
| out → | Render | `MATERIAL_*_PLACEHOLDER` shared by the terrain, meshes and the preview sphere; exported to Mitsuba/Cycles |
| out → | Animation | every `MaterialOutput` attribute is a node track |

## Scripting surface

Ops: `open_material`, `list_materials`, `set_material_type`, `set_material`,
`save_material`, `load_material`, `preset_material`, `assign_material`,
`ai_generate_texture`, plus `add_node`/`set_attr` for layers. MCP: one tool
each. Guide: recipes 1, 4.

## Gaps (verified)

1. The channel and layer surgery (`material_channel_ops`, `material_stack_ops`:
   channel mode, add/remove/reorder layers, randomize, snapshots) is
   UI-only — no op, no MCP tool.
2. Metallic and AO channels exist on `MaterialOutput` and are not consumed
   by the viewport.
3. Presence is evaluated before displacement; orientation has a
   discontinuity at due south (`docs/MATERIAL_LAYERS.md` known limits).
4. Environment drivers are split three ways (`MaterialLayer`,
   `MaterialStack env_on`, `FieldDistribution`) with different dials.
5. No shared/instanced layers, no painted presence, no filter curve editor
   (documented Vue features not adopted).
6. Reoriented normal mapping and tri-planar contact blending are not
   implemented (`NormalBlend` is whiteout).
7. No material response to weather: wet look, puddles, frost, dust, sun
   bleaching.

## Roadmap

### Phase 1 — everything the studio does, scripting does
- Ops `material_layer` (add/remove/move/set, by index or name),
  `material_channel` (mode, source, randomize), `material_snapshot`
  (store/restore), each with an MCP tool and a schema line; the audit tests
  keep them covered. Accept: a macro rebuilds a five-layer material from
  nothing and the studio shows it unchanged.
- Metallic and AO in the viewport shading; a `ContactBlend` on object bases
  (tri-planar terrain material with a depth mask).
- One `EnvironmentDrivers` block shared by `MaterialLayer`, `MaterialStack`
  and `FieldDistribution`: altitude, slope, orientation, curvature,
  wetness, flow, snow, exposure, each with fuzz.

### Phase 2 — weather-aware materials
- Inputs from the atmosphere: rain → wetness (darkening, puddles in
  `FillBasins`), snow depth → cover, sun exposure → bleaching, wind →
  dust; keyable through the world tracks so a storm changes the ground.
- Reoriented normal mapping; height-based splat blending; a painted
  presence layer (brush into a `MaskPaint`).

### Phase 3 — the library as a world
- Biome presets: a material stack plus ecosystem per climate, chosen by a
  planet's environment (planets.md Phase 3).
- Shared/instanced layers; a filter-curve editor; procedural micro-detail
  (`NaturalGrain`) tied to camera distance.
- Renderer parity for layered materials in Cycles/LuxCore (render.md).

Owner files: `engine/nodes/nodes_material*.cpp`, `studio/material_*.cpp`,
`studio/panel_material_*.cpp`, `studio/renderer_matparams.cpp`.
