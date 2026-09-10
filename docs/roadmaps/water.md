# Water — roadmap

## Purpose

The fluid form of the terrain: oceans, lakes, rivers, shores, with their own
properties (level, colour, clarity, waves, foam, flow), placed by the terrain
and the planet's shape, and driven by the same time base as erosion and
weather.

## Where it stands (2026-09-10)

- **Global water** (`RenderSettings`): level, deep/shallow colours,
  clarity, opacity, three wave dials, foam (colour, amount, crests, scale);
  `WaterLayer` node; Environment ▸ Water.
- **One shader** (`WATER_FN_GLSL`, `studio/shaders_scene.cpp`) for the
  tile's plane and the surround, so a lake crossing the border is one lake;
  three sine waves, Fresnel, depth colour, foam bands; the plane lies on the
  world's face (`pl_sphere_place`); the surround ocean and the far shell
  draw water where the relief is below the level.
- **Bodies from the terrain**: `Lake` (a body at a place, wandering shore,
  carved bed, beach mask), `Flood`, `FillBasins` (Priority-Flood),
  `Rivers` (traced from headwaters, carved, masked), `Coast`,
  `TerrainShape`; a negative placed feature becomes a basin the water fills.
- Tests: `test_shape_lake` (35 checks), `test_flood`, water level
  persistence.

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Terrain / Erosion | bed height; `Rivers`/`Lake` masks; `water_map`/`wetness` |
| in ← | Planets | the plane on the face; surround ocean; far-shell water colour |
| in ← | Atmosphere | sky reflection, fog on the water |
| out → | Materials | shore/beach masks; wetness |
| out → | Distribution | water masks as exclusion/presence |
| out → | Render | Mitsuba/Cycles get a plane with the deep colour only |
| out → | Animation | level, wave dials, colours are world tracks |

## Scripting surface

Ops: `set_water` (enabled, level, deep, shallow, foam); the rest by
`set_setting` or the `WaterLayer` node. MCP: `studio_set_world`. Guide:
recipes 1, 5.

## Gaps (verified)

1. `set_water` reaches 5 of 14 settings.
2. Waves are three sines: no spectral (Tessendorf) ocean, no wind-driven
   direction, no shore refraction or breaking.
3. Rivers, lakes and floods are masks and carved beds; the drawn water is
   always the global plane — a mountain lake above sea level is not drawn
   as water unless the tile's water level is raised for the whole tile.
4. No flow on the surface (no current, no river surface motion), no
   waterfalls, no wet shore band that follows the tide.
5. The far shell's water is a flat colour; the surround water has no foam.
6. `docs/SHAPES_AND_WATER.md` covers `TerrainShape`/`Lake` only.
7. Renderers get colour and roughness only; no waves, foam or clarity.

## Roadmap

### Phase 1 — every dial, every body
- `set_water` gains every field; a `Water` doc page for the global layer
  and the shader.
- **Water bodies as objects**: `Lake`/`Rivers`/`Flood` outputs become
  `WaterBody` objects (level per body, colour per body) drawn as separate
  planes/meshes at their own height, so a tarn at 2000 m and the sea coexist.
  Accept: the `routed_river` example shows water in the channel above sea
  level.
- Surround foam at the shore; far-shell water with a normal from the
  relief.

### Phase 2 — moving water
- Tessendorf spectral ocean (FFT on the GPU) with wind speed/direction from
  the weather block; shore refraction from the depth map; breaking foam.
- River surface flow from `FlowAccumulation` (a flow-map UV advection),
  waterfalls where the bed drops, mist particles at their foot.
- Tide and level animation with the wet-shore band following.

### Phase 3 — water as physics
- Shallow-water simulation on the tile driven by rain and the erosion
  solver's water depth (the same field), cached per keyframe.
- Ice: freezing by temperature (atmosphere Phase 2), ice material on
  lakes, glaciers feeding rivers.
- Renderer parity: displaced ocean mesh and foam masks exported.

Owner files: `studio/shaders_scene.cpp` (VS_WATER, WATER_FN_GLSL), `engine/nodes/nodes_hydro.cpp`,
`nodes_lake.cpp`, `studio/renderer_passes.cpp` (pass_water).
