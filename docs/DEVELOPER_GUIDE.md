# Geekatplay TerraForge — Developer Guide

This is the guide for anyone who wants to work on TerraForge: what we are
building, why it is shaped the way it is, where the work is, and the rules
that keep it fast, deterministic and pleasant to use. Read this first, then
[AGENTS.md](../AGENTS.md) — the engineering rules, each one a bug we already
paid for — then [PROJECT_REFERENCE_MAP.md](../PROJECT_REFERENCE_MAP.md)
for the file-by-file map, and then the code.

TerraForge's source is public, readable and forkable, and it is free for
noncommercial use under the [PolyForm Noncommercial License 1.0.0](../LICENSE);
commercial use needs a paid licence ([COMMERCIAL.md](../COMMERCIAL.md)). That
one restriction is why we call it *source-available* rather than open source.
Contributions of any size are welcome: a node, a fix, a benchmark, a
screenshot of something that looks wrong. By contributing you agree that your
contribution ships under the same terms, including the commercial licence
Geekatplay Studio grants to paying users.

No GPL or LGPL code may be added ([the record, with what was checked and
when](LICENSING.md)) — copyleft requires granting downstream
users commercial freedom that this licence does not grant. Permissive only
(MIT, BSD, Apache-2.0, zlib, MPL-2.0, CC0), listed in
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) in the same commit.

---

## 1. Goal

**A native, node-based studio for terrains, planets and the environments
around them that matches or beats Terragen, Vue, Gaea and World Machine —
and that a script or an AI agent can drive as fully as a person can.**

Five things follow from that sentence, and every decision below traces
back to one of them:

1. **Native.** One C++20 application, OpenGL 4.3, no web stack, no cloud, no
   account. It starts in a second and runs offline.
2. **Node-based, in two domains.** Terrains are graphs. Some of the graph is
   *raster* (heightmaps, textures, point clouds — erosion needs neighbours);
   some of it is *field* (functions of position that compile to GPU code and
   have no resolution — planets and infinite terrain need that). Both
   domains live in one graph with bridges between them. This dual-domain
   model is the central architectural finding of the project; it is why
   Terragen's displacement and Vue's function graphs are reachable at all.
3. **Deterministic.** Same graph, same seeds → bit-identical output, on
   every run and every core count. Tests enforce it. An artist must be able
   to reopen a scene next year and get the same mountain.
4. **Performance is priority one.** A 60 fps viewport while editing; erosion
   in seconds, not minutes; nothing uploaded to the GPU that did not change;
   the application idles when the user thinks.
5. **Everything is automatable.** Every operation the UI can do is an action
   in a JSON document. The AI assistant, the scripting inbox, the Python
   client and the MCP server all speak that one document. If a feature
   cannot be reached from the API, it is not finished.

## 2. Where the project is

Version 1 of the studio shipped in late August 2026. Since then the work has
been phased (the README lists features; this is the map):

| Area | State |
| :--- | :--- |
| Node graph, two domains, GLSL transpiler | done; ~180 nodes |
| Erosion (hydraulic, thermal, stream power, wind, coastal, layers → materials) | done, deterministic |
| Fractals to Vue's parameter depth | done (raster); field versions pending |
| Materials (PBR graph, CC0 library, material stack, AI materials) | done |
| Planets (any radius, per-planet graphs) and infinite terrain | done; picking/sculpt on tiny planets is flat-approximate |
| Atmosphere, clouds, water, sun, lights, night | done; wind/rain/snow pending |
| Cameras with real optics, render engines (Mitsuba 3, Cycles, LuxCore), passes, panoramas | done |
| Points, paths, scattering (EcoSystems) | done |
| Timeline, keyframes, sequences | done |
| Studio: detachable editors, preview panel, undo across everything | done |
| AI assistant (local Ollama), actions API, MCP (~45 tools) | done |
| Transform gizmos + numerics, zones (local hi-res), spline editor, network rendering | **open** |

The README's feature sections are the authoritative list of what exists.

## 3. Roadmap and strategy

### Order of work

The plan of record orders phases by dependency, not by glamour:

1. **P0 universal framework** — the dual-domain graph, the transpiler, bypass
   and blend on every node, MetaNodes, animation hooks on every parameter.
   *(done)*
2. **P1 terrain and displacement** — per-point displacement on the GPU,
   adaptive subdivision. *(done)*
3. **P2 materials**, **P3 lighting**, **P4 atmosphere**, **P5 clouds**,
   **P6 render**, **P7 animation**, **P8 ecosystems**, **P9 objects and
   production**. *(v1 of each shipped; depth is the ongoing work)*

Within a phase we work from the **reference ledger**: the node catalogues
and manuals of the applications we measure against, mapped node by node
with a status of implemented / partial / gap. New work should close a gap
or deepen a partial, and say which.

### Strategy

- **Read the reference, then build ours.** When a feature exists in Vue or
  Terragen we read the manual's description of it — its parameters, its
  groups, its outputs — and ship *at least* that, with our own formulas
  where ours are better. Copying the shape of a feature is fine; copying
  code is not.
- **Ship whole vertical slices.** A node without a test, a doc entry, an
  action for the API and a preview is not done.
- **Never break determinism or performance to add a feature.** If a new
  solver is not bit-stable across thread counts it does not merge. If a
  change slows a benchmarked node past its ceiling, the PerfGuard fails.
- **Small modules.** Every source file stays under 500 lines. When a file
  grows past that, split it along a real seam and say so in the header
  comment.
- **Honesty in the log.** Regressions, measurement errors and abandoned
  approaches are recorded in the commit history, not hidden. A benchmark
  that cannot be explained by a mechanism is a measurement error until
  proven otherwise.

### Open items, in priority order

1. Field-domain fractals (so planets and the horizon get the new depth).
2. Transform gizmos with a numerics tab.
3. Vue "zones": local high-resolution regions inside the tile.
4. Atmosphere extras: wind, rain, snow, multi-layer clouds, cloud presets.
5. Spline editor for roads, rivers and camera moves.
6. Network rendering.
7. The remaining ~17 % of the reference node ledger.

## 4. Architecture

```
engine/            the graph and every node; no UI, no GL, no OS dependency
  gpx/             public headers: node_graph, field, noise_core, planet_math,
                   erosion_kernels, fractal_core, attribute, serialization
  nodes/           one file per node family (REGISTER_NODE blocks)
  field_glsl*.cpp  the field graph -> GLSL transpiler
studio/            the application: ImGui panels, renderer, scene, AI, API
  renderer*.cpp    OpenGL terrain/scene/planet passes, tessellation, culling
  shaders_*.cpp    GLSL sources (terrain, scene, planet)
  panel_*.cpp      one panel per file
  ai_*.cpp         the assistant and the action-document dispatcher
  studio_api.cpp   the file inbox + published scene state
orchestrator/      Python: render backends (Mitsuba, Cycles, LuxCore), parity
mcp_server/        Python: MCP tools over the same action documents
tests/             C++ test batteries + JSON manifests (the regression lock)
tools/             node docs generator, PerfGuard benchmark
docs/              node reference, guides, images
```

### The graph

- `gpx::Graph` owns `Node`s and `Link`s. A node is a registry type
  (`REGISTER_NODE(Name, "Category", "description", setup, compute)`), a set
  of declarative attributes (`add_float`, `add_choice`, `add_seed`,
  `add_range`, `add_vec2`, ...) and typed ports.
- **Port types:** `Heightmap`, `Texture`, `Points`, `Field`. Heightmap and
  Texture may be wired across each other (luminance ↔ grey image); the
  conversion happens on the wire (`Node::in_hmap` / `in_tex`) and is
  rebuilt before every compute.
- **Evaluation** is dirty-tracked and topological; every node compute is
  wrapped, timed and given per-output statistics (range, size, count) that
  the editor shows on the connectors. A memory ceiling releases buffers
  nothing downstream will read.
- **Field ports** carry a `FieldEvalFn` (CPU) and transpile to GLSL for the
  GPU. The CPU/GPU agreement test is the load-bearing test of the domain.
- **Bypass and universal blend** exist on every node without the node
  knowing (`add_universal_blend`, link resolution through disabled nodes).

### The studio

- Evaluation runs on a worker thread holding `App::graph_mtx`. **Panels
  never block on it**: they draw from `App::node_views` (a snapshot refreshed
  whenever the lock is free) and mirror edits (`NodeMirror`) that are flushed
  when the lock returns. See AGENTS.md UI rule 1 — and the crash it caused
  when violated (a `unique_lock::unlock()` on an un-owned try-lock).
- The node editor is imgui-node-editor with our own card drawing
  (`panel_graph_draw.cpp`); several editors can exist (`GraphEditor`), each
  pinned to a domain, each with a parameters side pane.
- The renderer draws each view into its own FBO slot (six views + the
  Preview panel). Terrain uses adaptive tessellation with per-patch frustum
  culling; the field graph's displacement and surface programs are spliced
  into the terrain shader; planets and the surround are GPU-procedural with
  one program pair per surface graph.
- The scene (`SceneState`) holds objects — terrain, water, sun, atmosphere,
  cameras, lights, meshes, planets, infinite surfaces — and is serialized
  with the graph into one `.gpxt` JSON project.

### AI, API, MCP — one document

Every operation is a JSON action: `{"op":"add_node","type":"NoiseFractal",
"alias":"f"}`, `{"op":"connect",...}`, `{"op":"set_planet",...}`,
`{"op":"render"}`. The dispatcher (`ai_actions.cpp`, `ai_ops_graph.cpp`,
`ai_ops_view.cpp`) is the only place an operation is implemented; the
assistant, the file inbox (`%LOCALAPPDATA%/GeekatplayTerraForge/api/
actions_inbox.json`), the Python client and the MCP tools all produce that
document. The schema the AI is shown lives in `ai_schema.cpp`; when you add
an op, add it there and to `mcp_server/` in the same commit. Undo pushes
one step per document, so an agent's whole batch reverts as one edit.

### Rendering

- **Viewport:** rasterized PBR, shadows, volumetric clouds, water with foam,
  atmosphere, lights, instanced scatter — timed on the GPU with
  `GpuTimer::Scope` (never with wall-clock frame time; vsync lies).
- **Final frames:** `render_scene_export.cpp` writes `scene.json`;
  `orchestrator/render_engines.py` runs Mitsuba 3, Cycles or LuxCore with
  the same meshes, instances, lights and camera, refining a preview the
  studio watches. Parity between engines is a test concern, not a hope.

### Determinism, concretely

- Counter-based RNG keyed on (seed, index), never on the worker id.
- Parallel solvers write per-cell or reduce in fixed order; fixed-point
  atomics where float addition order would leak the thread count.
- Read-then-write passes never read what another chunk is rewriting (the
  pipe-model race we found and fixed is the cautionary tale).

## 5. How we work

### The loop for a feature

0. Read what the last session left: the console's first line counts the
   open crash reports, hang reports and killed sessions in `logs/`;
   `{"op":"crash_reports"}` lists them. Fix what it names, then
   `crash_mark_fixed` with a note. A hang report with no *recovered* line
   is a deadlock, and the graph mutex taken twice on one thread is the
   usual one (see AGENTS.md "Clicks under a lease act after it").
1. Read the reference material for it, if any. Note the gap it closes.
2. Implement in the engine (a node) or the studio (a panel/op), keeping the
   file under 500 lines.
3. Write the test: a `test_*` in `tests/cpp/test_engine.cpp` (or the undo /
   render batteries). Determinism and range checks are the minimum.
4. Build, then `.\test.ps1` (Windows) or `./test.sh` (macOS, Linux) — seven
   suites, all of which must pass.
5. `regression_tests --update` to record new nodes/attributes in the
   manifest; `node_docs_gen` to regenerate `docs/NODES.md`.
6. If it touches the UI, launch the app and verify it *live*: send an action
   document to the inbox, capture with the `capture` op (the viewport) or
   `save_node_preview` (one node's card, which is usually the faster answer
   to "did that node do what I meant"), look at it. Screenshots go in the PR.
7. Add the op to the AI schema and MCP — `tests/test_api_coverage.py`
   fails if an op has no MCP tool, and `tests/test_settings_coverage.py`
   fails if a render setting is not in the saved-settings table (which is
   what `set_setting` and `list_settings` reach) or a panel has no
   `show_panel` name. Update the README section.
8. Commit with a message that says what changed and why — the history is
   documentation. Push to `main`.

### The four auditors

Suites say a change did not break what was already there. These ask whether
what is there was ever right. All four report rather than fail, because a gate
switched on over hundreds of pre-existing findings is a gate someone turns off
— the checks that survive a clean sweep get promoted into the contract battery
afterwards, where they cannot regress.

| Tool | Asks |
| :--- | :--- |
| `build/node_audit` | Does every node explain itself — a description worth reading, a tooltip on every parameter, a name a person would choose, an output the editor can draw? |
| `build/param_audit` | Does moving each slider change anything? It builds every node with real upstream nodes, moves one parameter, and looks. When nothing moves it retries under each mode switch, toggle and zeroed amount on the node before reporting, because most of what looks dead is merely switched off. |
| `python scripts/mutate.py` | Would the tests notice if the code were wrong? It breaks the source on purpose, one plausible mistake at a time, and reports the mutants that survive. |
| `python docs_private/mark_refs.py` | Does the reference ledger still match the registry? It reported ten gaps we had filled months earlier. |
| The hang watchdog (`studio/hang_watch.cpp`) and the ledger (`studio/crash_ledger.cpp`) | Did the application ever stop? A frame that takes longer than `perf.hang_seconds` gets the main thread's stack written to `logs/hang_<stamp>.txt`; the ledger lists it beside the crash reports and the sessions killed without a clean exit, until someone marks it fixed. |
| The performance watcher (`studio/perf_watch.cpp`) | Did the last ten seconds do work for nothing? Counts lease misses, evaluations, uploads and redraws per frame, and writes findings in words to `logs/perf_watch.json` — "56 of 149 idle frames did over 4 ms of work". `perf_report` returns the same to a script. |

`param_audit` earns its place by having found what a green suite cannot: three
selectors carrying an "Edge softness" control that was declared, tooltipped,
saved, loaded and read by nothing at all.

### Conventions

- C++20, `namespace gpx` for the engine, `namespace studio` for the app.
- Headers explain *why*; commit messages explain what and why.
- Every parameter gets a label, a range and a tooltip.
- Every user-visible string through `tr()` where the panel already uses it.
- No personal data in the repository; `docs_private/` and `_temp_hesiod/`
  are gitignored reference material and never pushed.
- Logs and crash reports land in `logs/` (gitignored). A crash writes a
  `crash_<stamp>.txt` with a resolvable stack; `scripts/resolve_crash.py`
  turns it into file:line.

### Building

Windows (MinGW-w64 or MSVC), macOS (Clang) and Linux (GCC or Clang), all from
the same CMake project. The fastest way in is the one-click installer with
`-Dev` / `--dev`, which builds without installing anything:

```powershell
.\install.bat            # or: scripts\install.ps1 -Dev
```
```bash
./scripts/install.sh --dev
```

By hand:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\get_deps.ps1
.\build.ps1 ; .\test.ps1 ; .\start.ps1
```
```bash
./scripts/get_deps.sh
./build.sh && ./test.sh && ./start.sh
```

Optional: `pip install mitsuba`, Blender on PATH, `pip install pyluxcore`;
Ollama for the assistant; `pip install minidump` to read Windows crash dumps.

**Windows is the reference platform.** The golden hashes were recorded with
MinGW-w64 GCC, so a change that moves them on MSVC or Clang is expected;
re-record only on the reference toolchain.

**Portability rules that will bite you** (the full set is in `AGENTS.md`):

- macOS caps OpenGL at **4.1**. `studio/glsl_version.hpp` rewrites the
  `#version 430 core` line to 410 there, and every `glShaderSource` call site
  must go through it. Use nothing above 4.1 — no compute, no SSBO, no
  `layout(binding=)`.
- Every `#ifdef _WIN32` needs a `__APPLE__` branch and an `#else`. A branch
  that silently returns nothing is how the file dialogs worked on Windows only.
- Anything the app writes for itself goes through `studio/paths.cpp`
  (`settings_path`, `data_dir`). A bare relative path writes to `/` inside a
  macOS bundle and fails without saying so.

### Packaging

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\make_installer.ps1
```
```bash
./packaging/macos/make_dmg.sh --build
```

Writes to `dist/`: a per-user Inno Setup `.exe` and a portable `.zip` on
Windows, a `.dmg` holding an ad-hoc signed `.app` on macOS. Shipped binaries
are stripped and the debug symbols are kept in `dist/symbols`, because a crash
report's addresses are module offsets that resolve against the unstripped
build. `packaging/windows/stage.ps1` is the single definition of what an
installed TerraForge consists of; `tests/test_packaging.py` checks the two
dependency fetchers agree, the shell scripts are LF and executable, and the
installer stages only directories that exist.

Full user-facing instructions: **[docs/INSTALL.md](INSTALL.md)**.

### Sending a change

- Open a PR against `main`. One topic per PR; tests green; screenshots for
  anything visible.
- Say which reference gap it closes (or which rule it strengthens).
- Expect review on determinism, performance and module size before style.

## 6. Good first contributions

- A node from the reference ledger's gap list (ask in an issue for the
  list of the smallest ones).
- A field-domain version of a raster node (the transpiler pattern is in
  `engine/nodes/nodes_field*.cpp`).
- A render-parity check between two engines for a feature you use.
- A `test_*` for something that has none.
- Translations: the `tr()` dictionary in `studio/i18n.cpp`.
- Run the macOS or Linux build and report what breaks — those ports are newer
  than the Windows one and have had far fewer eyes on them.
- Try to break the crash handler, the undo stack, or the API fuzzer
  (`scripts/fuzz_ops.py`) and file what you find.

Welcome aboard.
