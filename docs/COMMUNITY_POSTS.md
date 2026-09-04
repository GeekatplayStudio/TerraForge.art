# Geekatplay TerraForge — community posts

Three lengths of the same story. The long one is written for the top of the
GitHub README; the medium one for forums, Reddit and Discord; the short one
for Facebook, X and group posts.

---

## Long (README)

**Geekatplay TerraForge** is a node-based studio — free for noncommercial use,
source in the open — for building 3D terrains, planets and the environments
around them — the kind of work people do in Terragen, Vue, Gaea or World
Machine — as a single native
C++ application with no web stack, no cloud account and nothing that phones
home.

**What it does today**

- A node graph with ~180 nodes across two evaluation domains: a *raster*
  domain (heightmaps, textures, point clouds — the home of erosion, flow,
  masks and everything that needs to look at its neighbours) and a *field*
  domain (resolution-independent functions that compile straight to GPU
  code and run on the terrain, on planets and on the endless horizon).
  Bridges let you move between the two.
- Real erosion: hydraulic (particles and the shallow-water pipe model),
  thermal, stream power with tectonic uplift, wind, coastal — deterministic,
  bit-identical across runs and CPU core counts. One node runs erosion and
  hands back material masks (bedrock, scree, soil, grass, sediment, riverbed,
  snow) so what grows where follows what the water did.
- Vue-class fractals with the full parameter sets, Terragen-style planets
  from a 1 m globe to Earth, infinite terrain to the horizon, a PBR material
  graph with photoscanned CC0 materials, water, volumetric clouds, atmosphere
  with a physical sun, cameras with real optics and film stocks, lights,
  scattering of meshes on point clouds, a timeline with keyframes and image
  sequences.
- Rendering: a fast rasterized PBR viewport (adaptive tessellation, GPU
  displacement, shadows) for working, and final frames through **Mitsuba 3**,
  **Blender Cycles** or **LuxCore** with scene parity, render passes and
  360° panoramas. A live preview panel shows the shot as you edit nodes.
- A studio built for working: several detachable node editors (drag one to
  your second monitor), a Blender-style properties editor, undo across
  everything — including undoing what the AI just did.

**AI and API**

Every panel has a natural-language assistant that runs on a **local Ollama**
model — text, or a photo to describe the scene you want — and turns the
request into the same JSON action document that scripts use. That document
is the whole API: a file-based inbox any language can write to, a Python
client, and an **MCP server** exposing ~45 tools so agents like Claude can
build terrains, assign materials, place cameras and render. Nothing is sent
anywhere unless you point it at your own model.

**What we are working on next**

- Field-domain versions of the new fractals, so the GPU/planet path gets
  the same parameter depth.
- Transform gizmos with a numerics tab; Vue-style "zones" of local
  high-resolution detail.
- Atmosphere extras (wind, rain, snow, multi-layer clouds), a spline editor
  for roads and rivers, network rendering.
- More Terragen/Vue reference coverage — the private ledger says ~83% of the
  reference node catalogue is matched; the rest is queued.

**Technology:** C++20, OpenGL 4.3, Dear ImGui with a custom node editor,
GLSL transpiler for the field graph, deterministic multi-threaded kernels,
JSON project files, a regression lock and node-contract test battery with a
performance guard on every commit. Windows first; the engine is portable.

**Who is building it:** Vladimir Chopine of Geekatplay Studio — co-author of the
official Vue guide *Vue 7: From the Ground Up* (Focal Press) and *3D Art
Essentials*, and the maker of 3,000+ landscape tutorials on Vue, Terragen and
World Machine over 35 years in film and VFX. TerraForge is the workflow he has
taught for twenty years, built as the tool.

Try it, break it, tell us what a terrain artist actually needs:
https://github.com/GeekatplayStudio/TerraForge.art

---

## Medium (forums / Reddit / Discord)

I've been building **Geekatplay TerraForge**, a node-based terrain and
environment studio, free for noncommercial use with the source in the open —
think Terragen/Vue/Gaea territory, as one native C++ app, no cloud, no
account.

What's in it now: ~180 nodes over two domains (heightmap/texture/points for
erosion and masks, plus resolution-independent "field" functions that
compile to GPU code and drive terrain, planets and the infinite horizon).
Deterministic hydraulic/thermal/stream-power/wind/coastal erosion — with a
node that outputs material masks from the erosion itself (bedrock, scree,
soil, grass, sediment, snow). Vue-class fractals with full parameter sets.
Planets from 1 m to Earth-size, each with its own node graph. PBR material
graph with CC0 photoscanned sets, water, volumetric clouds, physical sun and
cameras, scattering, a timeline.

Rendering: a fast PBR viewport for working, final frames via Mitsuba 3,
Blender Cycles or LuxCore, plus a live preview panel of the shot while you
edit. Several detachable node editors, undo across everything.

AI/API: every panel has a natural-language assistant on a local Ollama model
(text or a reference photo). It emits the same JSON action document scripts
use — file inbox, Python client, and an MCP server (~45 tools) so agents can
drive the whole app.

Next up: field-domain fractals, transform gizmos, local high-res zones,
atmosphere extras, spline editor, network rendering.

I've taught this craft for twenty years (co-author of the official Vue 7
guide, 3,000+ Vue/Terragen/World Machine tutorials at Geekatplay), so this
is built from the workflow, not from a spec.

Looking for people who actually make terrains to tell me what's missing or
wrong. Repo: https://github.com/GeekatplayStudio/TerraForge.art

---

## Short (Facebook / X / groups)

Geekatplay TerraForge — a node-based terrain & planet studio
(Terragen/Vue-style), native C++, no cloud, free for noncommercial use.
Deterministic erosion that also outputs material masks, Vue-class fractals,
planets from 1 m to
Earth, PBR materials, clouds, cameras, scattering, timeline. Fast PBR
viewport + final renders in Mitsuba 3, Cycles or LuxCore. Local-AI
assistant in every panel and a full API/MCP so scripts and agents can build
scenes. By the co-author of the official Vue 7 guide and 3,000+ landscape tutorials
(Geekatplay Studio). Early, ambitious, and looking for terrain artists to try it and say
what's missing. https://github.com/GeekatplayStudio/TerraForge.art
