# TerraForge — engineering rules

Rules for anyone (human or AI) changing this codebase. They exist because
each one was a bug we already paid for. Do not regress them.

## UI invariants

1. **Panels must never blank out.** Evaluation holds `App::graph_mtx` for its
   whole run. No panel may block on that lock or replace its contents with a
   placeholder when the lock is busy — that made the graph and the properties
   disappear while scrolling a value.
   - The graph draws from `App::node_views` / `App::link_views`, refreshed by
     `App::refresh_snapshot()` whenever the lock happens to be free.
   - The Properties panel mirrors the selected node's attributes
     (`NodeMirror`), edits the mirror, and flushes to the real node on the
     next frame that can take the lock.
   - New panels follow the same pattern: `try_to_lock`, and if it fails, draw
     from cached data.
2. **No flicker on refresh.** Never rebuild UI state from scratch per frame in
   a way that changes layout. Cache, then diff.
3. **Selection never steals the active Properties tab.** Clicking an object in
   a viewport or the Outliner, or a node in the graph, must not change
   `App::prop_tab`. It changes only when the user clicks a tab, presses an
   explicit button, or the tab cannot apply to the selection.
4. **Every user-visible string goes through `tr("tag")`** (see `studio/i18n.*`).
   Add the tag to the English dictionary in the same commit.
5. **Flat visual language.** Zero rounding, dark grey with a dim orange
   accent, `studio::Checkbox` (no check marks) instead of `ImGui::Checkbox`.
6. **Numeric rows** are `[-] [slider-look drag] [+]`: drag to scrub, click to
   type, mouse wheel to step while hovering.

## Performance rules

1. **Never upload a GPU texture per frame.** Uploads are versioned
   (`renderer_set_material_maps(..., version)`); skip when nothing changed.
   Re-uploading the material maps every frame once cost more than the entire
   rest of the frame.
2. **Interactive edits evaluate at reduced resolution** (`prefs().interactive_res`)
   and run one full-resolution pass on release.
3. **Node previews are not regenerated during interactive drags.**
4. Measure before and after. Node timings are on every node in the graph and
   the totals are in the toolbar.

## Engine rules

1. **Every step must be deterministic.** Same graph and seeds must produce
   bit-identical output, every run, on every thread count. Parallel solvers
   use per-worker buffers reduced in a fixed order — never unsynchronised
   writes to shared data. `test_workflow_determinism` enforces this.
2. **Erosion must not destroy relief.** Clamp per-round changes; a solver that
   produces spikes will be normalised into a flat terrain.
   `test_erosion` enforces this.
3. **Port lookups are direction-aware.** Nodes may name an input and an output
   identically (`Levels` has `texture` in and out).
4. Node parameters are declarative (`add_float`, `add_choice`, ...), so the
   properties UI, serialization, and the AI catalog all come for free. Give
   every parameter a tooltip.

## Node framework

The graph has **two domains**. The *raster* domain passes buffers and can look
at neighbours — that is where erosion lives. The *field* domain evaluates one
point in 3D, is resolution-independent, and compiles to a shader. `Rasterize`
and `Sample` are the only bridges. Put a node in the domain its maths actually
belongs to; do not fake a field node with a 1×1 buffer.

1. **Every field node must have a GLSL emitter** in `engine/field_glsl.cpp`.
   `node_tests` enforces it. A field node that evaluates on the CPU but has no
   emitter silently does nothing on the GPU — the worst possible failure, so
   it is a build-breaking omission instead.
2. **The CPU and GLSL paths must agree.** They share `gpx::planet::pl_*` rather
   than reimplementing noise. `verify_field_gpu` measures the agreement against
   a real GL context; the threshold is 2e-4 and the measured worst case is
   1.3e-5. Re-run it after touching either side.
3. **Bypass is resolved in link resolution**, not per node
   (`resolve_upstream` / `bypass_source`). Never add a per-node "enabled"
   check — the graph already walks through bypassed nodes in both domains, for
   every future node, for free. Bypass serializes only when false, so old
   projects load enabled.
4. **Universal blend is graph-provided.** `add_universal_blend` gives a `blend`
   input to any node that turns terrain into terrain (has ports named exactly
   `input` and `output`, and is not Logic/Mask/Export/Group). Do not hand-roll
   masking on a new filter that fits that shape, and do not widen the rule —
   blending a selector's mask or a router's passthrough toward a heightmap is
   meaningless, and an exporter is a sink. It must stay a bit-exact no-op when
   unconnected.
5. **A MetaNode injects values across its boundary by parking a buffer on the
   input port**, so `in_hmap` / `in_tex` prefer a port's own buffer over the
   link. Keep that precedence.
6. **Animation tracks live on `Attribute` itself**, never in a side table —
   that is what makes copy, serialize, undo and publish carry the animation
   automatically. `Graph::apply_animation()` samples before the topological
   walk, so **no node knows animation exists**; it reads the attribute it
   always read. Keep it that way.
7. **`for (x : json::parse(s)["nodes"])` iterates a subobject of a destroyed
   temporary.** Parse into a named value first. This silently produced empty
   MetaNode inner graphs.

## Tests

1. **`node_tests` is one data-driven battery over the whole registry**, so a
   new node is tested the moment it registers and coverage cannot lag behind
   the node count. If a node genuinely cannot be evaluated in a test, extend
   the exemption predicates (`needs_file`, `writes_file`, `is_config_node`,
   `is_sink`, `is_container`) rather than special-casing it by name.
2. **The regression lock is a ratchet.** A node may never be removed or change
   category; an attribute may never be removed or be retyped; the golden
   projects must keep evaluating to the same hash; every feature in
   `tests/manifest/features.json` must keep naming a test that exists.
3. **`regression_tests --update` is not a way to make a failure go away.**
   Re-record only when the change was intentional, and say in the commit
   message why the output moved. It caught two real bugs (a non-deterministic
   Snow and a Cracks node that did nothing) by refusing to be quiet.
4. **Add a feature manifest entry whenever a phase ships something a user can
   see.** Never delete one without a written reason.
5. `.gitignore` excludes `*.gpxt`; the golden corpus is re-included by
   `!tests/projects/*.gpxt`. Do not lose that line.
6. Multi-line commit messages break PowerShell here-strings — use
   `git commit -F <file>`.

## AI, API and MCP

1. The UI assistant, the Python API and MCP tools all execute the **same**
   `ai_apply_actions` path. Add an operation once; all three get it.
2. Extend `ai_action_schema()` whenever you add an operation, and teach the
   model the constraints (for example the exposure triangle) rather than
   clamping away a physically correct result.

## Planets and infinite terrains

1. **Planets are parameters, never data.** A planet is a `PlanetData` block
   plus its `InfiniteSurface` layers; the surface is generated on the GPU
   from those numbers each frame. Never cache a planet heightmap, texture or
   mesh — that is what makes an unlimited number of them possible. Three
   shared sphere LODs and one shared surround grid are the whole footprint.
2. **The relief function is mirrored, not duplicated in spirit.**
   `engine/gpx/planet_math.hpp` (CPU) and `PL_FN` in
   `studio/planet_renderer.cpp` (GLSL) must stay in agreement — the CPU copy
   is what tests and picking use. It is evaluated in **3D** on the sphere
   direction; a 2D parameterization pinches at the poles.
3. **LOD must be continuous.** Detail is driven by `octf`, a *float* octave
   count from projected pixel size, and the top octave fades in with a
   `clamp(octf - i, 0, 1)` weight. Never step the octave count with an int —
   that pops. Mesh LOD swaps use overlapping thresholds (hysteresis) for the
   same reason.
4. **Progressive quality is by projected size, not distance alone.** Skip
   sub-pixel objects outright; shed shadow maps, volumetric clouds and heavy
   material maps once the camera leaves the ground (`near_ground`, also
   hysteretic).
5. Planets draw between the sky and the terrain with depth writes **off**
   and a far-plane depth clamp in the vertex shader, so they are never
   clipped however far the camera zooms out.

## Undo

1. **Every mutation is preceded by `undo_push(a, "what changed")`.** A change a
   user can see and cannot take back is a bug. That includes changes made by
   the assistant, the API and MCP — `ai_apply_actions` pushes one step for the
   whole action document.
2. `undo_push` is called **before** the mutation and names it. The resulting
   state is recorded lazily, so a slider drag that fires every frame collapses
   into one step ending at the value the user released on. Push on the first
   frame of an interaction only, never per frame.
3. Snapshots hold the graph as JSON, plus the scene and world settings. **Node
   ids are reassigned when that JSON is loaded**, so anything referring to a
   node across a restore travels as an index, not an id.
4. Imported mesh vertices are shared between snapshots rather than copied.
   Keep any new bulk data out of the per-step copy the same way.
5. `undo_tests` covers restore correctness, redo branching, history jumps and
   that a restored graph recomputes bit-identically. Extend it when you add
   state that undo must cover.

## Before you commit

- `.\build.ps1` must succeed (it fails loudly; do not trust a stale binary).
  Kill a running `geekatplay_studio` first — a locked exe fails the link with
  `Permission denied`.
- `.\test.ps1` must pass — all six suites.
- Never commit `docs_private/`, build output, or `external/`. Roadmaps,
  development notes and progress documents stay out of the repository.
