# TerraForge

**A native node-based 3D terrain and environment studio.**
Geekatplay Studio.

TerraForge builds photoreal landscapes from a node graph: procedural and
simulated terrain, layered PBR materials, volumetric sky, water, and offline
path-traced rendering — all in a single native C++ application with a
real-time OpenGL viewport.

---

**[Node reference](docs/NODES.md)** — every node, port and parameter,
generated from the registry itself.

## Features

### The node graph
- **Two domains, one graph.** Alongside the raster graph — buffers, neighbour-
  aware, where erosion lives — is a **field domain**: nodes that evaluate a
  single point in 3D and are therefore resolution-independent. `Rasterize` and
  `Sample` bridge the two, so a procedural field can be baked for erosion and
  an eroded heightfield can be read back by a field graph.
- **Field graphs compile to GLSL.** A field network is transpiled to a shader
  and evaluated on the GPU, sharing its noise implementation with the planet
  renderer so the two agree by construction. CPU and GPU results are verified
  to agree to within 1.3e-5 over 20,480 samples.
- **Bypass any node** (`Ctrl+E`) — the graph resolves links straight through
  it, so it works down chains, in both domains, and for every future node.
- **MetaNodes** (`Ctrl+G`): group a subgraph into one node, publish the few
  parameters that matter, and save it to your library as a reusable node with
  its tuned values intact. Saved nodes appear under **My nodes**.
- **Universal blend.** Any node that turns terrain into terrain gets a mask
  input from the graph itself, so its effect can be limited anywhere without
  each node reimplementing masking.
- **Node List** — the same graph read as a tree from the result backwards,
  with per-row bypass, so a scene can be built and understood without
  untangling the network view.
- **Animation-ready:** every parameter can carry a keyframe track (linear,
  smooth or constant), sampled before evaluation. *The timeline UI is not
  built yet.*

### The studio
- **Node cards:** rounded nodes with a category-coloured title bar,
  connectors on the left and right edges coloured by data type (the wires
  carry the same colour), and three detail levels per node — expanded,
  compact, title bar only — from the header chevron or `H`. Text stays
  crisp at any zoom.
- **Several node editors:** `View > New node editor` opens another graph
  window pinned to a domain (terrain, materials, atmosphere, render, all),
  each with its own canvas and a side pane showing the selected node's
  parameters.
- **Windows that leave the window:** every panel has a corner button that
  floats it out of the main window — onto a second monitor when there is
  one — and docks it back.
- **Preview panel:** the chosen camera's view at the camera's aspect ratio,
  with its own sky/clouds/water/shadow switches and render scale, redrawn
  live as the graph changes even when the working viewports have all of
  that turned off; one button renders it with the camera's final engine
  and shows the passes as they arrive.

### Displacement
- **Redirect** moves where another field is evaluated. Warp, flow, swirl and
  domain distortion are all this one node, and it works on anything — noise,
  a sampled heightfield, another redirect.
- **Displace** turns a value into relief: along the surface normal, straight
  up, along a vector or a fixed direction; depth in real units or relative to
  a reference size; smoothing; a **quality boost** so relief can resolve finer
  than the geometry carrying it; and displace-outwards-only.
- **Compute Normal** recovers the surface direction *after* displacement, so
  "snow above this altitude on slopes below this angle" means the displaced
  terrain rather than the flat plane underneath it.
- **Zones** confine a field to a sphere or box with a smooth fade, and expose
  the region on its own as a mask.
- **Live on the GPU:** wire a field into a `TerrainDisplacement` node and the
  viewport compiles it to a shader and evaluates it per vertex, normals and
  shadows included. A field has no resolution, so the relief keeps resolving
  as the camera closes in. If a graph produces a shader that will not build,
  the viewport keeps the last good one instead of going black.
- **Adaptive subdivision.** The terrain is tessellated to whatever the camera
  needs rather than to a fixed grid — 64×64 patches subdivided per edge by
  that edge's length in pixels, from an effective 512 across (exactly the
  fixed grid it replaces, so it is never coarser) up to 2048 where the camera
  is close. Levels are chosen per edge from its two endpoints, so adjacent
  patches always agree and no crack can open; spacing is fractional, so a
  patch's level changes continuously instead of popping.
- **Both domains, both directions.** A graph that samples a buffer can drive
  the viewport too, so an *eroded* heightfield can shade or displace the
  surface — bake a field for erosion, then take the result back out.

### Environment-sensitive materials
- **Distribution** answers *where a material belongs* — by altitude, steepness
  and which way the ground faces, each with a soft band. The criteria multiply,
  so a material sits where every condition holds, the way you would say it out
  loud: "rock, on the steep bits, below the snow line."
- Because a distribution reads the surface *after* displacement, "snow above
  this altitude on slopes below this angle" means the displaced terrain rather
  than the flat plane underneath it.
- **Colour blending** in the field domain — mix, add, multiply, screen,
  overlay, darken, lighten — with the factor meaning the same thing in every
  mode.
- **Live on the GPU:** wire a colour field into a `TerrainSurface` node and
  the viewport shades the terrain with it per pixel. Slope and facing come
  from the shaded normal, so the distribution follows detail finer than the
  heightmap carrying it.
- **One graph, several channels.** The same node also takes **roughness** and
  **bump**, each its own field. The distribution that decides where the grass
  goes can say that the grass is rougher than the rock and has a finer grain.
  An unconnected channel leaves that part of the shading alone.

### Points, paths and scattering
- **A third domain: point clouds.** ScatterPoints (random, jittered grid, or
  spaced), relaxation, mask filtering, merging, shuffling, values sampled
  from any heightmap; stamp clouds into rasters or exact distance fields.
- **Paths are ordered clouds.** Thread a scatter into a tour, resample it
  evenly, spline through it (Catmull-Rom), fractalize it into a coastline
  wander, or route it across the terrain with a slope-penalty least-cost
  search — then hand it to PathCarve for the riverbed.
- **EcoSystem-style scattering.** Bind any imported mesh to a Points node and
  instanced copies stand on the terrain — hashed yaw, size and brightness
  jitter per copy, live in the viewport and identical in every offline
  render engine. CSV/PLY export and CSV import round-trip the clouds.

### Terrain analysis
- **Flow accumulation** (D8): how much water passes through each point.
- **Wetness index** — `ln(a/tan b)`, the standard measure of where water
  collects. High in flat hollows fed from above, low on steep ground, so a
  vegetation or moss mask sits where you would expect it.
- **Resample** at half, quarter, double or a custom sampling — detail control
  without changing the buffer size.

### Terrain
- **Node graph engine** with dirty-tracking evaluation, multithreaded solvers,
  per-node previews and timings, and any resolution from 64 to 8192.
- **Erosion that models the physics:** particle-droplet hydraulic erosion, a
  shallow-water pipe-model solver, explicit and implicit (Braun-Willett)
  stream-power fluvial incision with tectonic uplift and rock hardness,
  thermal talus weathering, aeolian dunes and sediment deposition.
- **Erosion that decides what grows where:** `ErosionLayers` runs thermal +
  hydraulic erosion and turns the simulation's side channels (scour, silt,
  talus, drainage, standing water) into a stack of material masks — bedrock,
  scree, soil, grass, sediment, riverbed, snow — plus wetness and flow. The
  layers are priority-ordered and always sum to one, and come packed as two
  splat textures too. One graph serves both editors: the erosion node shows
  in the Materials workspace as soon as its masks are wired into a material.
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
- **The home planet is a sphere:** the terrain tile lies on a planet whose
  radius is a real length from 1 m up (Objects ▸ Planet surface, or the
  Atmosphere tab). Large radii give the curved horizon; below the tile's own
  circumference the tile wraps the whole globe, equirectangular — a 1 m
  planet made from the heightmap, with the same nodes, materials and
  erosion as any terrain.
- **Every world its own graph:** each planet (and the home surround) names
  the `SurfaceDisplacement` node whose field graph displaces it, as a
  Terragen planet has its own terrain network. "New graph for this world" in
  the planet's properties, or `"surface_node":"new"` in `add_planet`.
- **Unlimited planets.** Each is a pure parameter block — radius, relief,
  seed, sea level, snow line, rock and water colors, atmosphere — generated
  on the GPU every frame. No textures, no meshes, no caches, so a hundred
  planets cost the same memory as one. Add them from the Objects panel or by
  asking the AI.
- **Infinite procedural terrains.** Stack any number of endless terrain
  layers (rolling hills / ridged mountains / billow dunes, each with its own
  scale, amplitude, coverage and seed). Parented to a planet they shape its
  surface; at the root they extend the home terrain tile past its edges to
  the horizon, blending seamlessly out of the tile's own heightmap.
- **Your work survives a crash.** The application autosaves every couple of
  minutes whenever the undo history has moved - three files in rotation, the
  user's own project path never touched - and a session that ends without
  closing properly is offered back on the next start, newest autosave first.
- **Lakes, and flow that reaches the sea.** `FillBasins` floods every closed
  basin to the height of its outlet (Priority-Flood). Read one way it is the
  water standing in the hollow - depth and mask outputs, ready to drive a
  material or a blend. Read the other way it is the surface flow routing needs:
  without it D8 routing dead-ends at every pit, and on plain fractal terrain
  81 such cells were swallowing the drainage of 41% of the map.
- **Two noise bases, not one.** fBm for ridges and rolling ground, and
  cellular (Worley) noise for everything fBm cannot make: cracked mud, basalt
  columns, scree, crater fields and tectonic plates. `FieldVoronoi` gives the
  distance to the nearest cell point, to the second, to the seam between them,
  or one flat height per cell - through round, diamond or square cells. Being
  a field node it compiles to GLSL, so it shapes planets and infinite terrain
  at any scale as well as the finite tile.
- **Shaped by a node graph.** A planet's surface and the endless ground plane
  have no heightmap - they are functions, evaluated on the GPU at whatever
  detail the camera has earned. So they are authored as functions: wire a
  field graph into a `SurfaceDisplacement` node and it is transpiled to GLSL
  and added to every procedural surface in the scene, at every scale, for no
  memory at all. The same field nodes run on the CPU for picking and baking,
  and a test holds the two definitions together.
- **Continuous zoom.** Pull back to see a whole planetary neighbourhood —
  the sky thins to a starfield as you leave the atmosphere — then fly to any
  world (double-click it in the Objects tree) and keep zooming until individual
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
- **Material stack:** `MaterialStack` blends up to six mask + albedo layers
  with height-aware blending (silt fills the cracks of the rock before it
  covers the ridges) into albedo and per-layer roughness for `MaterialOutput`.
  `examples/macros/erosion_materials.json` builds the whole
  erosion → layers → material chain and assigns it to the terrain.

### Scene and lighting
- **Built-in primitives** — cube, sphere, plane, cylinder, cone — plus OBJ
  import; every mesh transforms, takes materials, and scatters.
- **Point lights** (up to eight) with color, intensity and reach, agreed on
  by the viewport and every offline engine.
- **Night**: let the sun set and stars come out over a sky, haze and
  surround that darken together — a campfire in the dark is one AI prompt.
- **Real-world terrain**: SRTM `.hgt` tiles and uncompressed GeoTIFF DEMs
  import directly, alongside 8/16-bit PNG heightmaps.
- **Spot lights** too: cone angle, heading and pitch, in the viewport and
  every engine.

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
  so the result matches the preview — scattered meshes and point lights
  included.
- **Render passes**: depth and world-normal EXRs beside the beauty image.
- **360° panoramas**: one equirectangular frame from any camera position,
  for skyboxes and VR stills.

### Animation
- **Timeline** (View menu): play/pause/stop, a loop range, a scrub bar, and
  per-attribute keyframing on the selected node — key markers on miniature
  track bars, constant/linear/smooth interpolation per track. Tracks save
  with the project.
- **PNG sequence capture**: the loop range becomes numbered frames through
  the viewport engine, one animation frame per completed evaluation.
- **Fly-throughs**: hand the sequence any path node and the camera rides it
  at terrain height — a spline through scattered waypoints is a camera dolly.
- Scattered meshes sway in the wind, each copy on its own phase.

### AI assistance
Describe a landscape in plain language — or drop in a photograph — and a local
[Ollama](https://ollama.com) model builds the node graph, parameters,
materials and lighting for you. Everything runs on your machine.

---

## Examples

- `examples/eroded_island.gpxt` — an island landform through hydraulic
  erosion, with a midslope mask feeding a point scatter.
- `examples/routed_river.gpxt` — PathFind routes a least-cost line across
  the noise and PathCarve cuts the riverbed along it.
- `examples/basalt_steps.gpxt` — columnar basalt softened by thermal
  weathering.
- `examples/showcase_valley.gpxt` — a forested island: eroded mountain,
  midrange scatter mask, 900 instanced pines with wind sway, hero camera.

Open them with File > Open (or the `open_project` scripting op) and press
around.

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

Six suites, all of which must pass before a commit:

| Suite | Covers |
|---|---|
| `nodeterrain_tests` | The original solver library and CLI |
| `engine_tests` | Registry, evaluation and caching, cycle rejection, determinism, erosion, materials, serialization, field domain, GLSL transpiler, bypass, MetaNodes, blend, animation |
| `undo_tests` | Restore correctness, redo branching, history jumps, node library round-trip |
| `node_tests` | **Universal node contract** — one data-driven battery over all 118 node types: metadata, ports, determinism, bypass, serialization, extremes, and that every field node has a GLSL emitter. Adding a node automatically tests it. |
| `regression_tests` | **Regression lock** — a node may never be removed or change category, an attribute may never be removed or be retyped, 14 committed projects must still evaluate to the same hash, and every entry in the feature manifest must still name a test that exists. |
| `pytest` | Render backends and AI helpers |

The contract and regression suites are the reason features do not quietly
disappear: 6,056 contract assertions and 1,726 regression checks over 118
node types, 540 attributes and 61 manifest features. If a change to a golden
is intentional, `regression_tests --update` re-records it — review that diff
rather than trusting it.

---

## Logs and crash reports

Every console message is also appended, flushed line by line, to
`logs/terraforge_<stamp>.log` under the project root (or `$TERRAFORGE_LOG_DIR`),
so a crash cannot take the log with it. `std::terminate`, `abort()`, a UCRT
invalid-parameter and unhandled SEH exceptions each write
`logs/crash_<stamp>.txt` with the reason (an uncaught exception's `what()`
included) and a stack as `module+RVA`. The build carries `-g`, so
`python scripts/resolve_crash.py` turns those frames into `file:line`;
`python scripts/dump_stack.py <file.dmp>` reads a Windows minidump the same
way. `{"op":"debug_crash"}` on the actions API exercises the whole pipeline.
The `logs/` folder is local and never committed.

## Project layout

| Path | Contents |
|---|---|
| `engine/` | Terrain engine: heightmaps, node graph, attributes, node packs |
| `studio/` | Desktop application: viewports, renderer, panels, AI, scene |
| `orchestrator/` | Python layer: render backends, AI helpers |
| `mcp_server/` | Tool server exposing the engine for automation |
| `src/`, `include/` | Original C++ solver library and CLI |
| `tests/` | C++ and Python test suites |
| `tests/manifest/` | The regression lock's records: node, attribute, golden and feature censuses |
| `tests/projects/` | Golden `.gpxt` projects, re-evaluated to a hash on every run |
| `scripts/` | Dependency fetcher |

## Usage notes

- **Workspaces** across the top switch the editor between Terrain,
  Materials, Atmosphere and Render.
- **Properties** is a tabbed editor (Render, Scene, World, Object, Material,
  Node) that follows what you select and has a search box.
- **Right-click a viewport** for view options; the same menu sets how many
  view windows you want. Layouts persist between sessions.
- **In the node graph:** `Ctrl+E` bypasses the selected nodes, `Ctrl+G` groups
  them into a MetaNode and `Ctrl+Shift+G` expands one back out. Bypassed nodes
  are dimmed and tagged.
- **Node List** (tabbed with the Library) shows the same graph as a tree from
  the terrain result backwards, with each node's inputs indented beneath it.
  Nodes that do not reach the result are listed separately rather than being
  silently ignored.
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
s.set_sun(azimuth_deg=250, altitude_deg=12)  # one history step, "AI: set_sun"
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
