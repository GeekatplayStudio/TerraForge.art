# TerraForge system audit — modularity, integration, documentation

*10 September 2026. Read against the working tree at that date (phase 36 plus
the fixes made during this audit). Every claim names a file; where a claim was
verified by running something, the method is stated.*

This audit answers three questions:

1. Is the application modular — does each domain live in its own files with a
   clear contract to the rest?
2. Is every module reachable from the three scripting surfaces (the in-app
   assistant, the Python API, the MCP tools), and is that enforced?
3. Could an AI agent, given only the documentation, build a complete scene?

The short answer: **the engine is well modularised and the scripting surface
is nearly complete and now audited by tests; the documentation was strong per
node and weak per scene.** Section 6 lists what this audit changed; the
per-module roadmaps in [`roadmaps/`](roadmaps/README.md) list what remains.

---

## 1. Architecture in one page

```
engine/            gpx:: — GL-free, deterministic, tested in isolation
  gpx/*.hpp        heightmap, fields, planet maths, scatter contract, animation
  nodes/*.cpp      252 REGISTER_NODE across 65 files (one family per file)
  *.cpp            erosion kernels, hydrology, mesh tools, field→GLSL transpiler
studio/            the application: renderer, panels, ops, persistence
  render_settings  ONE RenderSettings singleton: sun, sky, fog, clouds, water,
                   world shape, placement, render defaults (154 saved fields)
  scene.hpp        SceneState: objects (Terrain, Water, Sun, Atmosphere, Mesh,
                   Group, Camera, Planet, InfiniteSurface, Light, Nebula),
                   layers, timeline, world_anim, render_presets
  ai_*.cpp, *_ops.cpp   the op dispatch: 160 ops, one JSON document in
  layout_store.cpp      → ai_apply_actions → every surface (assistant, API, MCP)
  scene_nodes*.cpp graph → settings / objects each evaluation (nodes drive)
mcp_server/        Python: Studio class (inbox/state files) + 150 MCP tools
orchestrator/      offline renderers (Mitsuba, Cycles, LuxCore) fed by scene.json
docs/              per-node reference (generated), per-topic guides, this audit
tests/             27 C++ suites + Python audits (settings, API, MCP)
```

**The single control path.** Every change the interface can make is an *op*
in a JSON document. The assistant emits ops; `mcp_server/studio_api.py`
writes them to an inbox file the application polls at 4 Hz; the console types
them as `op key=value`. There is one implementation, `ai_apply_actions`
(`studio/ai_actions.cpp`), so the three surfaces cannot drift from each other
— only from the code, which is what the audit tests below now check.

**Nodes drive, settings persist.** The graph is the source of truth Terragen-
style: `AtmosphereSettings`, `CloudLayer`, `WaterLayer`, `SunLight`, `Planet`,
`InfiniteTerrain`, `Nebula`, `SceneCamera`, `LightSource` nodes write into
`RenderSettings` / `SceneState` every evaluation (`studio/scene_nodes*.cpp`).
Fields with no node persist through the `env_fields` table alone.

---

## 2. Module matrix

For each module the user asked about: where it lives, how it is driven, how it
is documented and tested. ✔ complete · ◐ partial · ✘ missing. Details and
the plan per module are in `docs/roadmaps/<module>.md`.

| Module | Engine | Studio | Ops | MCP | Nodes | Settings | Docs | Tests | Roadmap |
|---|---|---|---|---|---|---|---|---|---|
| Terrain shapes | ✔ 60+ nodes | ✔ editor, styles, sculpt, tiles | ✔ | ✔ | ✔ | ✔ | ✔ NODES + 6 guides | ✔ goldens | [terrain](roadmaps/terrain.md) |
| Erosion | ✔ 11 + 3 hydrology | ◐ (graph-only) | ✔ `terrain_effect` | ✔ | ✔ | — | ✔ | ◐ no conservation test | [erosion](roadmaps/erosion.md) |
| Planets & world shape | ✔ planet_math | ✔ renderer, rim, placement | ✔ | ✔ | ◐ Planet/InfiniteTerrain (no world-shape node) | ✔ | ✔ | ✔ maths; ◐ persistence | [planets](roadmaps/planets.md) |
| Space (stars, galaxy, nebulas, moons) | ✔ shaders_space | ✔ | ✔ | ✔ | ◐ Nebula only | ✔ | ✔ | ◐ round-trip only | [space](roadmaps/space.md) |
| Materials | ✔ 30 nodes | ✔ Studio, library, presets | ◐ channel/layer ops UI-only | ◐ | ✔ | ✔ | ✔ | ✔ | [materials](roadmaps/materials.md) |
| Atmosphere (sky, fog, height) | ✔ | ✔ | ✔ | ✔ `studio_set_world` | ✔ AtmosphereSettings | ✔ | ✔ VOLUMETRICS | ✘ no GLSL twin | [atmosphere](roadmaps/atmosphere.md) |
| Clouds | ✔ | ✔ | ◐ 6 fields not in `set_clouds` | ✔ | ✔ CloudLayer; 4 [Planned] | ✔ | ✔ | ✘ | [clouds](roadmaps/clouds.md) |
| Water | ✔ Lake/Flood/Rivers/Coast | ✔ shader shared tile/surround | ◐ 5 of 14 fields | ✔ | ✔ WaterLayer | ✔ | ◐ | ◐ | [water](roadmaps/water.md) |
| Distribution / ecosystems | ✔ 5-stage contract | ✔ instancing, LOD, camera-driven | ✔ | ✔ | ✔ | ✔ | ✔ ECOSYSTEM | ✔ deep | [distribution](roadmaps/distribution.md) |
| Cameras | ✔ camera_math | ✔ optics, per-camera render | ✔ + new view↔camera | ✔ | ◐ 4 [Planned] | — | ✔ | ✔ | [cameras](roadmaps/cameras.md) |
| Objects / modelling | ✔ Meshwright port, CSG | ✔ | ✔ | ✔ (2 added) | ◐ 3 [Planned] | — | ✔ | ✔ | [objects](roadmaps/objects.md) |
| Sun & lighting | ✔ | ✔ 8 lights | ◐ geographic mode not in `set_sun` | ✔ | ◐ 5 [Planned] | ✔ | ◐ | ✘ solar maths | [lighting](roadmaps/lighting.md) |
| Render | ✔ 12 AOVs | ✔ presets + batch (new) | ✔ | ✔ | ◐ 2 [Planned] | ✔ | ◐ | ◐ | [render](roadmaps/render.md) |
| Viewports & layouts | — | ✔ 8 views, layouts, restore (fixed) | ✔ (per-view added) | ✔ | — | prefs | ✔ | ✔ | [viewports](roadmaps/viewports.md) |
| Animation | ✔ tracks, modifiers, expressions | ✔ timeline, curves | ✔ 26 ops | ✔ | ◐ 3 [Planned] | — | ✔ ANIMATION | ✔ | [animation](roadmaps/animation.md) |

Counts verified 2026-09-10: 252 registered nodes (21 `[Planned]` placeholders,
none in terrain/erosion/distribution); 160 ops; 150 MCP tools (7 of them
Python-only prototypes in `mcp_server/server.py` that do not drive the app).

---

## 3. Integration surfaces — what was found

### 3.1 Ops ↔ MCP tools
- The audit test `tests/test_api_coverage.py` scanned only `studio/ai_*.cpp`.
  Sixty ops in `anim_ops.cpp`, `asset_ops.cpp`, `layout_store.cpp`,
  `material_ops.cpp` and `mesh_ops.cpp` were invisible to it, and five of
  them had no MCP tool: `mesh_retopo`, `mesh_solidify`, `remove_track`,
  `set_interp`, `mesh_analyze`. **Fixed**: the test now reads every file that
  compares `op`, and the five tools exist.
- `set_viewport` was written as `op != "…"` and escaped the regex. **Fixed.**

### 3.2 Ops ↔ the assistant's schema
- 27 ops had no syntax line in `studio/ai_schema.cpp` — including the whole
  node-editing family (`add_node`, `connect`, `set_attr`, `delete_node`,
  `bypass`, `view_node` …), `save_project`, `open_project`, both GPU
  verifiers. AGENTS.md demanded the schema be extended per op; nothing
  enforced it. **Fixed**: every op now has a `{"op":…}` line, and
  `test_every_op_is_in_the_assistant_schema` fails on the next omission.
- The Terrain and Material assistant domains received a 9-line schema; the
  graph verbs are now in the block every domain sees.

### 3.3 Ops ↔ MCP parameter lists
Parameters the ops accept but the tool descriptions did not list:
`atmosphere_height` and the per-view switches on `studio_set_viewport`;
`type`, `cone`, `heading_deg`, `pitch_deg` on `studio_set_light`;
`camera`/`preset` on `studio_render`. **Fixed** for the first and third; the
light params remain a roadmap line.

### 3.4 Settings
`env_fields` (`studio/scene_io.cpp`) has 154 entries, and is both the
project's environment section and what `list_settings`/`set_setting` reach —
so "saved with the project" and "scriptable" are the same property by
construction. The entries carry **no description, range or unit**, so an
agent reading `list_settings` sees names and numbers only. Roadmap:
[render](roadmaps/render.md) §"Settings descriptions".

### 3.5 Published state
`scene_state.json` now carries `world` (shape, width, thickness, outline,
inside, sun_inside, planet_radius, atmosphere_height), the full `space`
block, `render_presets` and the `render_queue` length, so an agent can read
back what it set.

### 3.6 Fields an op cannot reach (only `set_setting`, the node, or animation)
- Sun: geographic mode (`latitude`, `longitude`, `utc_offset`, `month`,
  `day`, `hour`) — `set_sun` forces manual mode.
- Clouds: `detail`, `anvil`, `wind_dir`, `color`, `ambient`, `quality`.
- Water: `clarity`, `opacity`, the three wave dials, the four foam dials.
- Planet: `spin`, `visible`; nebula: `visible`; infinite terrain:
  `mask_scale`, `height_scale`.
Each is a one-line addition and is listed in its module's roadmap.

### 3.7 MCP server
`mcp_server/server.py` exposes the 150 studio tools and still carries seven
`terrain_*` prototypes that operate on an in-process Python DAG, never the
application. It has no transport of its own (no stdio loop); a host process
feeds it JSON-RPC. Roadmap: [`roadmaps/README.md`](roadmaps/README.md)
§"Integration work".

---

## 4. Documentation — can an AI build a scene from it?

**Per node, yes.** `docs/NODES.md` / `docs/node_index.json` are generated
from the registry: every node's ports, every attribute's kind, range,
default and tooltip (95% tooltipped at the attribute level; the remaining
gaps are shared `seed`/`phase` helpers). `find_nodes` searches them by
meaning.

**Per scene, no — until now.** There was no recipe or end-to-end page in
`docs/`; the eight worked macros live in `examples/macros/` and only one was
referenced anywhere. The assistant's schema listed ops without the order to
use them in. **Added**: [`docs/AI_SCENE_GUIDE.md`](AI_SCENE_GUIDE.md) — the
op grammar, the scene model in plain language, the module vocabulary, and
eight complete scene recipes (an alpine valley at dawn, a ring world, a
night sky with a moon and nebulas, a desert with a scattered forest, a
photoreal coast from a DEM, a flat world, a render batch, a day-to-night
animation), each ending with how to verify the result through `capture`,
`probe_height`, `points_stats` and the state file.

**Stale statements found** (corrected in this pass where the fix was a
sentence; otherwise carried to the roadmap): `docs/VOLUMETRICS.md` said
planet-wrapped cloud shells were not started (they are); README said the
same; `docs/ANIMATION.md` and README describe a `Render ▸ Sequence` menu
item that does not exist (`render_sequence` is an op only);
`docs/INTERFACE.md` named the layout files `workspace-*.json` (they are
`workspace2-*.json`) and "1-6 viewports" (eight); `docs/NODES.md` and
`node_index.json` predate the `Nebula`, `FirTrees`, `Invert`, `Pebbles`
and `TileRotate` nodes and must be regenerated (`node_docs_gen`,
`node_index_gen`); `docs_private/ECOSYSTEM_DESIGN.md` §2.2 and
`docs_private/roadmap/scatter.md` describe as missing a system that has
shipped.

---

## 5. Cross-cutting defects worth stating once

1. **Viewports reset on every launch.** The arrangement captured at exit was
   written to `layouts/workspace2-<name>.json` and never read back: the only
   restore path ran on a workspace *switch*, and the first frame has no
   previous workspace. The free orbit camera and the ortho pan/zoom were
   never saved at all. **Fixed** (`workspace_layout_restore`, `LayoutRecord::orbit`,
   the ortho fields); verified by capturing a view, restarting the
   application and capturing again (mean difference 1.3 grey levels, cloud
   motion).
2. **A capture through a camera drew a flat world** (phase 36 finding): a
   view with `scene_camera == -2` was not treated as a camera view. Fixed.
3. **`render` with no active camera did nothing and reported success.**
   Fixed: it errors and says how to name a camera.
4. **Named render presets and batch rendering did not exist.** Added
   (`studio/render_presets.cpp`): presets saved with the project, applied per
   camera or to all, a render queue, the Render menu, the Render tab and each
   camera's properties.
5. **Real-map import** normalises every DEM to 0..1: absolute elevation is
   lost, there is no georeferencing, compressed GeoTIFFs are refused, and the
   file dialog offers `*.exr;*.r16;*.raw` that the loader cannot read.
   Roadmap: [terrain](roadmaps/terrain.md) Phase 1.
6. **Erosion side channels are per-buffer normalised pictures**, the pipe
   model does not conserve mass (a closed cone loses 90% at defaults), and
   StreamPower's implicit mode destroys relief inside its own range. All
   measured in `docs_private/roadmap/erosion.md` and confirmed present.
   Roadmap: [erosion](roadmaps/erosion.md) Phase 1.
7. **One sun, one atmosphere, one water level per scene** — `RenderSettings`
   is a singleton. Multiple suns, per-planet atmospheres and per-planet water
   are the structural item behind three of the user's module goals.
   Roadmap: [lighting](roadmaps/lighting.md), [planets](roadmaps/planets.md).
8. **Offline renderer parity** is real only for Mitsuba. Cycles has no fog;
   LuxCore has no HDR sky, water, fog or point lights; appleseed is a stub
   still offered in every engine combo; planets, world shape and deep space
   reach the renderers only as a baked environment map.
   Roadmap: [render](roadmaps/render.md).
9. **No GLSL has a CPU twin outside the planet maths**: the sky, fog, cloud
   and water shaders are unverified by any test. Roadmap:
   [atmosphere](roadmaps/atmosphere.md) Phase 1.
10. **Nothing coordinates the modules through time** except the animation
    tracks: erosion has no notion of elapsed time, weather has no state,
    ecosystems do not grow. Roadmap: the "Time" sections of erosion,
    atmosphere and distribution.

---

## 6. What this audit changed in the code

| Area | Change | Where |
|---|---|---|
| Viewports | Layout restored at startup; orbit camera and ortho views persisted | `layout_workspace.cpp`, `layout_record.*`, `layout_store.cpp`, `app.cpp` |
| Viewports | View → camera (new or existing) and camera → view (link or copy) from the gear menu and ops `view_to_camera` / `camera_to_view` | `layout_store.cpp`, `panel_viewport.cpp` |
| Viewports | `set_viewport` takes per-view `scene_camera`, `projection`, `curved`, `atmosphere`, `water`, `grid`, `outlines`; `view` is 1-based everywhere | `ai_ops_view.cpp` |
| Render | Named presets saved with the project; per-camera preset combo; Render menu lists, applies, renders and deletes them; batch queue over cameras; `render` takes `camera` and `preset`, errors without a camera; ops `render_preset`, `render_batch` | `render_presets.*`, `panel_render.cpp`, `panel_camera.cpp`, `toolbar_menus.cpp`, `ai_actions.cpp`, `scene_io.cpp` |
| Animation | Nebula fields and the space settings keyable | `anim_targets.cpp` |
| Integration | Coverage test reads every op file; five missing MCP tools added; every op has a schema line and a test keeps it so; MCP params for the new fields; published state carries world, space, presets, queue | `tests/test_api_coverage.py`, `mcp_server/*.py`, `ai_schema.cpp`, `studio_api.cpp` |
| Docs | This audit, the per-module roadmaps, the AI scene guide, the docs index | `docs/` |

---

## 7. How to keep it true

- `python -m pytest tests/test_settings_coverage.py tests/test_api_coverage.py`
  after any op, setting or node change: it fails on an unsaved setting, an
  op with no tool, an op the assistant is not shown, or a node attribute
  with no tooltip.
- `cmake --build build --target node_docs_gen node_index_gen` and run both
  after adding a node, or `docs/NODES.md` lags the registry (it does today by
  five nodes).
- A new module gets a roadmap file in `docs/roadmaps/` with the integration
  table filled in, and a line in this matrix.
