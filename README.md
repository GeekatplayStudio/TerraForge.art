# TerraForge

**A native node-based 3D terrain and environment studio.**
Geekatplay Studio.

TerraForge builds photoreal landscapes from a node graph: procedural and
simulated terrain, layered PBR materials, volumetric sky, water, and offline
path-traced rendering — all in a single native C++ application with a
real-time OpenGL viewport.

---

## Features

### Terrain
- **Node graph engine** with dirty-tracking evaluation, multithreaded solvers,
  per-node previews and timings, and any resolution from 64 to 8192.
- **Erosion that models the physics:** particle-droplet hydraulic erosion, a
  shallow-water pipe-model solver, explicit and implicit (Braun-Willett)
  stream-power fluvial incision with tectonic uplift and rock hardness,
  thermal talus weathering, aeolian dunes and sediment deposition.
- **Generators:** multi-type coherent noise (Perlin/ridged/billow/Swiss/value/
  Worley), diamond-square and fault fractals, geometric shapes, geological
  strata, craters, dunes.
- **Surface realism:** multi-scale power-fractal displacement, procedural
  boulders, tilted stratification, shear folding, slope-targeted craggy
  detail, snow with settle-thaw, rivers and coastlines.
- **Modeling:** import heightfield images and stamp them onto the terrain
  with blending and falloff.
- **Sculpting:** brush directly on the terrain in any viewport — raise,
  flatten, smooth, terrace, noise and erase, with radius/flow/falloff, an
  on-surface brush ring and mouse-wheel resizing. Strokes live in a
  `TerrainSculpt` node in the graph as their own layer, so retuning the
  procedural terrain underneath never destroys your hand edits, and each
  stroke is one undo step.
- **Effects:** one-click, deterministic finishing nodes — Grit, Gravel
  (slope-seeking debris), Peaks, Sharpen, Cracks, Glaciation (U-valleys
  below an ice line), Dissolve (stream-carving with a flow-map output), and
  altitude clipping with flat tops, holes and a clip mask.
- **Style presets:** the Terrain menu drops complete editable node chains —
  Mountain, Ridged peaks, Eroded mountain, Canyon, Dunes, Iceberg, Lunar —
  with a fresh seed every click.

### Planets and infinite terrains
- **Unlimited planets.** Each is a pure parameter block — radius, relief,
  seed, sea level, snow line, rock and water colors, atmosphere — generated
  on the GPU every frame. No textures, no meshes, no caches, so a hundred
  planets cost the same memory as one. Add them from the Outliner or by
  asking the AI.
- **Infinite procedural terrains.** Stack any number of endless terrain
  layers (rolling hills / ridged mountains / billow dunes, each with its own
  scale, amplitude, coverage and seed). Parented to a planet they shape its
  surface; at the root they extend the home terrain tile past its edges to
  the horizon, blending seamlessly out of the tile's own heightmap.
- **Continuous zoom.** Pull back to see a whole planetary neighbourhood —
  the sky thins to a starfield as you leave the atmosphere — then fly to any
  world (double-click it in the Outliner) and keep zooming until individual
  ridges resolve. Detail is a *continuous* function of on-screen size, so
  nothing pops as you travel.
- **Progressive quality.** Everything scales with how much a thing is
  actually worth: sub-pixel planets are skipped entirely, sphere meshes swap
  through three LODs with hysteresis so they never flicker at a threshold,
  and shadow maps, volumetric clouds and 4K material maps switch off once
  the camera leaves the ground. Surface shading always evaluates two octaves
  finer than the geometry, so detail below mesh resolution still shows.

### Materials
- **PBR terrain shading:** roughness, metallic, specular, sky reflections,
  translucency, transparency and true displacement mapping.
- **Photoscanned materials:** browse and download CC0 PBR sets (albedo,
  normal, roughness, AO) from ambientCG at up to 8K, cached locally.
- **Multilayer compositing:** splat maps from terrain masks, layer blending,
  color grading, and albedo-to-PBR derivation.

### Environment and rendering
- **Volumetric clouds** raymarched with Perlin-Worley noise, cloud types
  (stratus / cumulus / cumulonimbus), coverage, wind and self-shadowing.
- **Sky and light:** configurable atmosphere, height fog with absorption and
  sun scattering, and a sun that can be positioned manually or from a real
  latitude, longitude, date and time.
- **Water:** depth-graded color, waves, and shoreline and crest foam.
- **Viewport:** 1-6 dockable view windows (perspective / top / front / right),
  shading modes, shadow mapping, scale bar, metric or imperial units.
- **Offline rendering:** path-traced output through Mitsuba 3, Blender Cycles
  or LuxCoreRender. The render reuses the viewport's own sky and clouds as an
  HDR environment, plus the same material, sun, water, fog and tone mapping,
  so the result matches the preview.

### AI assistance
Describe a landscape in plain language — or drop in a photograph — and a local
[Ollama](https://ollama.com) model builds the node graph, parameters,
materials and lighting for you. Everything runs on your machine.

---

## Requirements

- Windows 10/11 (the codebase is portable; Windows is what ships today)
- A GPU supporting **OpenGL 4.3**
- **CMake 3.20+**, **Ninja**, and a C++20 compiler (MinGW-w64 GCC or MSVC)
- **Python 3.9+** — optional, for AI assistance and offline rendering

## Build

```powershell
git clone https://github.com/GeekatplayStudio/TerraForm.git
cd TerraForm
powershell -ExecutionPolicy Bypass -File scripts\get_deps.ps1   # one time
.\build.ps1
.\start.ps1
```

`get_deps.ps1` fetches Dear ImGui, GLFW, imgui-node-editor, GLM, GLAD, miniz,
nlohmann/json and stb into `external/`. They are not committed to this
repository.

### Optional extras

```powershell
pip install mitsuba          # path-traced offline rendering
pip install -r requirements.txt
ollama pull llama3.1         # AI terrain from text
ollama pull llava            # AI terrain from a photograph
```

## Run the tests

```powershell
.\test.ps1
```

Runs the engine suite (node registry, evaluation and caching, cycle
rejection, determinism, erosion, materials, serialization, AI graph
building), the undo/redo suite and the Python suite.

---

## Project layout

| Path | Contents |
|---|---|
| `engine/` | Terrain engine: heightmaps, node graph, attributes, node packs |
| `studio/` | Desktop application: viewports, renderer, panels, AI, scene |
| `orchestrator/` | Python layer: render backends, AI helpers |
| `mcp_server/` | Tool server exposing the engine for automation |
| `src/`, `include/` | Original C++ solver library and CLI |
| `tests/` | C++ and Python test suites |
| `scripts/` | Dependency fetcher |

## Usage notes

- **Workspaces** across the top switch the editor between Terrain,
  Materials, Atmosphere and Render.
- **Properties** is a tabbed editor (Render, Scene, World, Object, Material,
  Node) that follows what you select and has a search box.
- **Right-click a viewport** for view options; the same menu sets how many
  view windows you want. Layouts persist between sessions.
- **Undo/redo** (`Ctrl+Z` / `Ctrl+Y`, also `Ctrl+Shift+Z`) covers the node
  graph, scene objects and world settings as one history. Every step is
  named, and **Edit > History** lists them so you can jump straight back to
  any earlier point. Changes made by the AI assistant, the Python API and
  MCP are ordinary history steps, so they can be taken back the same way.
- Projects are saved as `.gpxt` JSON files.

### Undo/redo from automation

```python
from mcp_server.studio_api import Studio

s = Studio()
s.set_sun(azimuth=250, altitude=12)   # one history step, named "AI: set_sun"
s.undo()                              # take it back
s.redo()                              # and put it back
```

The same is available over MCP as `studio_undo` / `studio_redo`, and to the
in-app assistant as `{"op": "undo", "steps": 1}`. All four surfaces run the
same action schema, so a change is undoable no matter which one made it.

## License

Copyright © Geekatplay Studio. All rights reserved.

Third-party dependencies are fetched at build time and remain under their own
licenses (Dear ImGui, GLFW, GLM, imgui-node-editor, GLAD, miniz,
nlohmann/json, stb). Downloaded material libraries are CC0.
