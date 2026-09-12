# Module roadmaps

One file per module, written 10 September 2026 from the audit in
[`../AUDIT.md`](../AUDIT.md). Each has the same shape:

1. **Purpose** — the goal in one paragraph, the user's words where they exist.
2. **Where it stands** — what ships, with files.
3. **Integrations** — what the module reads from and writes to other modules.
4. **Scripting surface** — ops, MCP tools, nodes, settings, state.
5. **Gaps** — measured or verified, not guessed.
6. **Roadmap** — Phase 1 (next), 2, 3, each with acceptance tests.

| Module | File | Phase 1 headline |
|---|---|---|
| Terrain shapes | [terrain.md](terrain.md) | Real-map import with true elevation and georeferencing; per-window evaluation |
| Erosion & time | [erosion.md](erosion.md) | Physical (unnormalised) side channels; mass conservation; one flow solver |
| Planets | [planets.md](planets.md) | Per-planet environment (air, water, sun) — the planet as a collection |
| Space | [space.md](space.md) | Star catalogue realism, nebula gallery, moons with phases and orbits |
| Materials | [materials.md](materials.md) | Layer/channel ops scriptable; metallic/AO consumed; environment drivers unified |
| Atmosphere | [atmosphere.md](atmosphere.md) | Physically based sky; weather (rain, snow, particles); CPU twins for the GLSL |
| Water | [water.md](water.md) | Spectral ocean; every dial scriptable; flow-driven rivers and lakes |
| Distribution | [distribution.md](distribution.md) | Unbounded affinity/repulsion; painting; simulation over time |
| Cameras | [cameras.md](cameras.md) | Camera-switch track; DoF/motion blur nodes; render passes per camera in the UI |
| Clouds | [clouds.md](clouds.md) | Extra layers in the panel; shadows from every layer; weather field |
| Objects & modelling | [objects.md](objects.md) | Booleans as nodes; groups; displacement painting |
| Sun & lighting | [lighting.md](lighting.md) | More than one sun; skylight, area lights, god rays; shadows from point lights |
| Render | [render.md](render.md) | Parity on Cycles/LuxCore; presets with passes; sequences through the engines |
| Viewports | [viewports.md](viewports.md) | Per-view orbit; per-view engine; layouts per project |
| Animation | [animation.md](animation.md) | Every numeric field keyable through one table; clips; sequence rendering |
| Plants | [plants.md](plants.md) | The plant as a graph of growth rules, with its parts and real UVs (added 11 September) |

## Integration map

Arrows read "writes into" / "is read by". The `RenderSettings` singleton and
the `SceneState` object list are the two hubs; the graph writes into both.

```
                  ┌────────────────────────────┐
   Terrain ──────►│ TerrainOutput heightmap    │◄────── Erosion (nodes in the chain)
   (nodes)        │ + masks (ErosionLayers)    │
                  └──────┬─────────────┬───────┘
                         │             │ masks / wetness / flow
             placement   │             ▼
   Planets ◄─────────────┘        Materials ───► Distribution (presence → points)
   (world shape,                  (MaterialStack,       │
    surround, rim)                 MaterialLayer)       ▼ instances
        │                                          Objects (meshes, LOD, imprint)
        │ curvature, altitude (pl_world_alt)             │ ground imprint → Terrain
        ▼
   Atmosphere ───► Clouds (band on the surface) ───► cloud shadows on Terrain
   (height, fog)         │
        │                ▼
        ▼          Space (drawn where the air lets it through)
   Water (level on the tile's face; surround ocean; far-shell water)
        │
   Lighting (sun direction; sun inside a world; 8 point lights) ──► every pass
        │
   Cameras ──► Viewports (look-through, curvature per view) ──► Render (per-camera
        │                                                        assignment, presets,
        └────────────── Animation (tracks on objects, world, nodes) ─── batch, passes)
```

Contracts to respect when a module changes (each is a rule in AGENTS.md):

- Anything placed on the world goes through `pl_sphere_place` / `world_alt`
  with the face's `Shape` — terrain, water, clouds, fog, objects, the rim.
- A component is an object plus the node that drives it plus a material
  (`component_new.cpp`).
- A setting is saved and scriptable by being in `env_fields`; an op is
  reachable by having a `studio_<op>` tool and a schema line; both are tested.
- Every march has a ceiling and an early exit; fog, clouds and volumetric
  materials are one model.

## Integration work that belongs to no single module

- **The MCP server has no transport** (`mcp_server/server.py`): give it a
  stdio JSON-RPC loop and drop the seven `terrain_*` Python prototypes, so
  `python -m mcp_server` is a complete server.
- **Settings descriptions**: extend `EnvField` with a label, a unit and a
  range so `list_settings` explains itself; generate `docs/SETTINGS.md`
  from it the way `NODES.md` is generated.
- **Regenerate `docs/NODES.md` and `node_index.json`** on every build that
  adds a node (a CMake custom target, run by the tests).
- **A scene-level golden**: `tests/manifest/goldens.txt` hashes heightmap
  ports only; add a `capture`-based golden of the default scene per view
  (tolerance by mean absolute difference) so sky, water, clouds and space
  are under the regression lock.
- **One time base**: erosion iterations, weather, ecosystem growth and the
  animation clock have no common notion of elapsed time; the roadmaps for
  erosion, atmosphere and distribution each propose a `time` input that the
  Timeline drives.
