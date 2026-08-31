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

## AI, API and MCP

1. The UI assistant, the Python API and MCP tools all execute the **same**
   `ai_apply_actions` path. Add an operation once; all three get it.
2. Extend `ai_action_schema()` whenever you add an operation, and teach the
   model the constraints (for example the exposure triangle) rather than
   clamping away a physically correct result.

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
- `.\test.ps1` must pass.
- Never commit `docs_private/`, build output, or `external/`.
