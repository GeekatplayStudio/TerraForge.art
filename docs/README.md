# TerraForge documentation

Start here. `index.html` is the public website; the Markdown is the manual.

## Start here
- [MANUAL.md](MANUAL.md) — **the user manual**: what the application is, the
  first five minutes, the window, the world, terrain, materials, air, water,
  cameras, rendering and what to do when something looks wrong. Read once,
  from the top; everything below goes deeper on one part of it.

## Orientation
- [AUDIT.md](AUDIT.md) — the system audit (10 Sep 2026): modularity, the three scripting surfaces, what an AI can build from the docs, what was fixed.
- [roadmaps/README.md](roadmaps/README.md) — one roadmap per module with the integration map: terrain, erosion, planets, space, materials, atmosphere, water, distribution, cameras, clouds, objects, lighting, render, viewports, animation.
- [AI_SCENE_GUIDE.md](AI_SCENE_GUIDE.md) — building complete scenes by language: the op grammar, the vocabulary per module, eight recipes with verification.
- [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) — what is being built and how to work on it; `../AGENTS.md` holds the engineering rules.
- [INSTALL.md](INSTALL.md), [LICENSING.md](LICENSING.md).

## The interface
- [INTERFACE.md](INTERFACE.md) — layout, the Objects panel, viewports, world shape, atmosphere height and deep space, languages.
- [ZOOM.md](ZOOM.md), [LOD.md](LOD.md), [TERRAIN_PERFORMANCE.md](TERRAIN_PERFORMANCE.md), [GPU_USE.md](GPU_USE.md) — what the viewport costs and why.

## Terrain and erosion
- [NODES.md](NODES.md) — every node, generated from the registry (`node_index.json` is the same for machines).
- [NODE_SEARCH.md](NODE_SEARCH.md), [NODE_PREVIEWS.md](NODE_PREVIEWS.md).
- [HEIGHT_PAINT.md](HEIGHT_PAINT.md), [SHAPES_AND_WATER.md](SHAPES_AND_WATER.md), [TERRAIN_ANALYSIS.md](TERRAIN_ANALYSIS.md), [DISPLACEMENT_LAYERS.md](DISPLACEMENT_LAYERS.md).

## Materials, populations, objects
- [MATERIAL_LAYERS.md](MATERIAL_LAYERS.md), [MATERIAL_REUSE.md](MATERIAL_REUSE.md).
- [ECOSYSTEM.md](ECOSYSTEM.md), [STONE_FIELDS.md](STONE_FIELDS.md), [GRASS.md](GRASS.md).

## Atmosphere, animation, services
- [VOLUMETRICS.md](VOLUMETRICS.md) — fog, clouds and volumetric materials: one model.
- [ANIMATION.md](ANIMATION.md) — specification, status and manual.
- [AI_SERVICES.md](AI_SERVICES.md) — describing a scene, painting textures and skies, building models.

## Examples
- `../examples/macros/*.json` — worked recipes (`run_macro`), `../examples/*.gpxt` — projects.
- [COMMUNITY_POSTS.md](COMMUNITY_POSTS.md) — the launch story.
