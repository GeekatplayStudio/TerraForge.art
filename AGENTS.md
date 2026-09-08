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
   a viewport or the Objects tree, or a node in the graph, must not change
   `App::prop_tab`. It changes only when the user clicks a tab, presses an
   explicit button, or the tab cannot apply to the selection.
4. **Every user-visible string goes through `tr("tag")`** (see `studio/i18n.*`).
   Add the tag to the English dictionary in the same commit.
5. **Flat visual language.** Zero rounding, dark grey with a dim orange
   accent, `studio::Checkbox` (no check marks) instead of `ImGui::Checkbox`.
6. **Numeric rows** are `[-] [slider-look drag] [+]`: drag to scrub, click to
   type, mouse wheel to step while hovering.
7. **Never rebuild the dock layout to change one window.**
   `build_default_layout` starts with `DockBuilderRemoveNode`, which throws
   away every window the user placed by hand. Adding, closing, splitting or
   rearranging a viewport edits one dock node and calls `DockBuilderFinish`
   on that node's root (`studio/layout.cpp`); only "Reset layout" rebuilds.
   Watch the one trap: when no viewport is docked, the reference node *is*
   the root dockspace, and clearing its children flattens the whole
   application into a single tab bar - `views_arrange` checks for that.
8. **Viewports are a set, not a count.** `Prefs::view_mask` holds one bit per
   slot (up to `RenderSettings::MAX_VIEWS`), so closing View 2 of four leaves
   1, 3 and 4 where they are instead of renumbering them under the user's
   hands. The mask is never 0: an application with no viewport is a bug, so
   the last one has no close box.
9. **A layout is arrangement, never content.** `LayoutRecord` carries ImGui's
   ini text, the open viewports and what each shows, the open panels and the
   extra node editors - and nothing about the scene or the graph, so a saved
   layout loads over any project. ImGui can only take new window state
   between frames, so a load parks the ini in `App::pending_layout_ini` and
   `app.cpp` applies it at the top of the next frame, before the first
   `Begin`.
10. **Offscreen render targets are named, never numbered by hand**
    (`SLOT_PREVIEW`, `SLOT_AOV`, `SLOT_CAMERA` in `render_settings.hpp`).
    The camera thumbnail used to borrow slot 4, which was View 5's target as
    soon as a fifth viewport existed - two features resizing one FBO every
    frame.

## Adding a component

1. **Adding anything goes through `component_new.cpp`, never
   `scene_add_primitive` on its own.** A component is the object *plus* the
   node that drives it *plus* a material assigned to it. Adding a cube used to
   produce a bare object: no node in the editor, no material, nothing to open
   in the Material Studio - none of the architecture the application is built
   around existed for that cube until somebody made it by hand.
2. **A new component wears `App::last_material`, or a plain grey one.** Set
   `last_material` wherever a material is assigned. Grey rather than coloured
   because it is a starting point and not a decision, and because every value
   in it is easy to judge against.
3. **The object is made before the node, and bound with `driver_node`.** The
   object then exists with geometry and a name immediately rather than
   appearing a frame later, and `scene_nodes_objects.cpp` adopts it by that id
   instead of by name.

## ImGui identity

1. **A widget's identity is its label, so two things with the same label in
   one window are one thing.** ImGui answers that by drawing its conflict
   highlight over both, which reads as a rendering fault rather than as a
   programming one - "the Shape node has some overlay error" was the Shape
   node and the Shape *category header* colliding in the library list.
2. **Give list rows an identity that is not their text**: `label + "##" + type`
   for a node, `name + "##cat"` for a category. Display names are chosen to
   read well and are allowed to collide; types are not.
3. **`GPX_ID_AUDIT` finds these on purpose.** Configure with
   `-DCMAKE_CXX_FLAGS=-DGPX_ID_AUDIT` and every id submitted twice in one
   frame in one window is logged with its window name (studio/id_audit.cpp).
   ImGui's own detector only fires for the item under the pointer, which on a
   screen with a few thousand widgets means finding the conflict by hovering
   the right pixel; this one names it.

   It needs a two-part hook in `external/imgui/imgui.cpp`, which `external/`
   being gitignored means is **not** in the repository — re-apply it by hand,
   and again after any ImGui update. Immediately above `bool ImGui::ItemAdd`:

   ```cpp
   #ifdef GPX_ID_AUDIT
   void gpx_id_audit(unsigned id, const char *window);
   #endif
   ```

   and as the first statement inside it, after `ImGuiWindow* window = ...`:

   ```cpp
   #ifdef GPX_ID_AUDIT
       if (id != 0) gpx_id_audit((unsigned)id, window ? window->Name : "?");
   #endif
   ```

   Then run the application, open every panel in every workspace, and read
   the log for `[idaudit]`. A clean sweep is the evidence; hovering is not.

## Panels and the graph lock

1. **A panel that skips its body takes the user's menus down with it.** ImGui
   keeps a dropdown open only while the window that opened it submits the combo
   again next frame. `if (!lk.owns_lock()) { ...; return; }` submits nothing,
   so every open popup in that panel dies. One missed frame in six hundred is
   enough — that was "the menus close as soon as I open them, in every tab of
   the Material Studio", and it was measured: the popup closed on the first
   frame the panel failed to take the lock, and stayed open indefinitely once
   the panel stopped skipping.
2. **Take the graph through `GraphLease` (studio/graph_lease.hpp), not
   `try_to_lock` directly.** It waits a bounded time when a menu is open and
   not at all when none is, so normal frames cost exactly what they did before
   and an open dropdown survives any interactive evaluation. `graph_mtx` is a
   `std::timed_mutex` (`App::GraphMutex`) for that reason.
3. **Never block on `graph_mtx` from the UI thread.** Evaluation holds it for
   its whole run; an erosion bake is seconds. The choice is the lease's bounded
   wait or a mirror (`panel_properties_node.cpp`), never an unbounded one.

## Modes you cannot see, and cannot leave

A mode the application is in, that the screen does not show and the user
cannot undo, turns every later action into an apparent no-op. It reads as
"the change had no effect", which sends the search to the feature that was
changed rather than to the mode that is hiding it.

1. **The 3D views can be pinned to one node.** Double-clicking a node in the
   graph sets `App::view_node`, and from then on the viewport draws that
   node's output instead of the Terrain Output. That was the whole of "the
   erosion effect is not reflected in the viewport": measured on a
   Noise→Hydraulic→Output chain, changing the erosion moved the viewport by a
   mean of 1.022 unpinned and 0.030 while pinned to the Noise upstream of it.
2. **So it says so, and there are three ways out**: the "Unpin" button in the
   view header, double-clicking the same node again, and "Follow the Terrain
   Output again" in the graph canvas's right-click menu. The viewport also
   draws a badge naming the pinned node.
3. **A state that changes what is drawn but is not part of the evaluation
   must force the upload itself** (`a.uploaded_serial = 0`). Releasing the pin
   through the API changed the answer and left the previous picture on screen,
   because nothing downstream marked the frame stale: 0.023 mean change before,
   1.166 after.
4. **An overlay drawn on top of the viewport image cannot take a click.** The
   image is submitted first and owns the hover for the frame; every
   `IsWindowHovered` variant reads false at that call site, and an
   `InvisibleButton` over it highlights on hover and goes cold on the press
   frame. Put the control in the view header, where ordinary widget behaviour
   applies, and let the overlay only say what is going on.

## Everything the UI can change, scripting can change - enforced

`tests/test_settings_coverage.py` reads the source and fails when it stops
being true: every plain field of `RenderSettings` must be in the saved-
settings table (`env_fields`, scene_io.cpp), which is what `set_setting` /
`list_settings` reach, so a new setting is scriptable by being saved; every
`App::show_*` flag must have a `show_panel` name; the count of node
attributes without a tooltip may only go down (the tooltip is what the
natural-language search indexes and what the assistant is told a property
means). `tests/test_api_coverage.py` holds the op ↔ MCP tool side. Add a
setting: put it in `env_fields`. Add a panel: put it in `show_panel`. Add
an attribute: give it a tooltip.

Cloud layers are nodes: every `CloudLayer` in the graph after the first is
its own layer (`rs.cloud_layers`, collected in scene_nodes.cpp, eight at
most in the sky pass); they chain through their `clouds` port into
`AtmosphereSettings` for order, but are drawn whether wired or not.

## The performance watcher

`studio/perf_watch.cpp` counts events the subsystems report (`perf_count`:
`eval`, `upload`, `view.draw.primary/secondary`, `lease.miss`,
`input.frames`) alongside the frame phases, and every ten seconds writes
`logs/perf_watch.json` and logs its findings - work done while nothing
changed, evaluations with no input, secondary views redrawn while idle,
panels losing the lock, uploads outrunning evaluations, the heaviest phase
when over budget, GPU-bound frames. `perf_report` (op, MCP, console) asks for
it now. **When you add a subsystem that can run for nothing, give it a
counter**, or the watcher cannot see it. Read `logs/perf_watch.json` before
optimising anything: it says where the time went.

## Participating media

1. **Fog, clouds and volumetric materials are one model**: Beer-Lambert
   extinction, single scattering through a Henyey-Greenstein phase, a short
   march toward the sun for self-shadowing, and (clouds) capped
   multiple-scattering octaves. `fog_terms` in `shaders_terrain.cpp` is the
   fog; the `u_v_*` block in `renderer_matparams.cpp` and the march at the
   top of `FS_MESH` are a material with `vol_density > 0`. Mitsuba gets the
   same terms as a homogeneous medium (`orchestrator/render_mitsuba.py`).
2. **Every march has a ceiling and an early exit.** Steps are a parameter,
   and the loop stops once transmittance falls below 1%: the cost is bounded
   by the setting, not by the scene. Never add a march without both.
3. **A volume is drawn after every surface, blended, without writing depth**
   (`renderer_meshes.cpp`, pass 1, back to front). Its alpha is what survives
   of what is behind it; drawn as a surface it would occlude the world with a
   box.
4. **A material with density 0 is a surface, exactly as before.** Old projects
   have no `vol_*` keys and must not change; the defaults make sure of it, and
   `test_material_editor.cpp` pins them.

## Clicks under a lease act after it

A panel that draws under `GraphLease` must not, from a click handler, call
anything that takes `graph_mtx` itself - `material_library_load`,
`material_preset_create`, `undo_push`, project loading. A `std::timed_mutex`
taken twice on one thread does not fail; it waits forever, and the window
stops pumping messages. "Click on a library material does nothing" was that
hang. The pattern (panel_material_browser.cpp `run_pending`): the click
records what it wants, the panel ends, `lk.unlock()`, then the action runs
with the lock-taking functions and nothing held. The paint canvas learned the
same lesson first (studio/paint_canvas.cpp).

Saving is the same trap: `material_library_save` takes the lock, and the
Material Studio's Save button, its unsaved-changes dialog, the browser's
context menu and the Materials panel all draw under a lease - "the app
locked when I saved a material". Where the action cannot be deferred, the
function has a `_locked` twin that assumes the lock is held
(`material_library_save_locked`, `material_preset_create_locked`); a panel
under a lease calls that one. Adding a function that takes `graph_mtx`
means adding its `_locked` twin and checking every panel that calls it.

## Hangs leave a report; a session starts by reading it

studio/hang_watch.cpp watches the frame loop from a second thread. Six
seconds without a finished frame (config `perf.hang_seconds`) and it
suspends the main thread, walks its stack, and writes
`logs/hang_<stamp>.txt` - the same module+RVA form as a crash report, so
scripts/resolve_crash.py resolves it. If the frame comes back the file says
"recovered after N s". Native file dialogs bracket themselves with
`hang_watch_pause`; the Windows move/size loop is detected and ignored.

studio/crash_ledger.cpp lists crash reports, hang reports and logs with no
"=== clean exit" line (a killed session, which is what a person does after
a hang). The console's first line at startup counts the open ones;
`crash_reports` lists them, `crash_mark_fixed` closes one with a note
(logs/crash_ledger.json). Do this at the start of every development
session, and kill the application only through the harness that then marks
the killed session as such - an unexplained unclean log is a finding.

Two things the log cannot tell you: consecutive identical lines are
collapsed into one entry with a repeat count (console.cpp `log_add`), so a
probe that waits for "actions applied (62 bytes)" must vary the document
size; and a status line proves an op ran, a log line does not.

## Workspaces and materials

1. **Every workspace owns its arrangement.** `workspace_layout_switch`
   (studio/layout_workspace.cpp) captures the arrangement being left into
   `layouts/workspace-<name>.json` and restores the one being entered; the
   Materials workspace has its own default (`build_materials_layout`) and the
   rest share the default. A default builder must dock **every** window the
   application can show - one it forgets floats at wherever it last was, over
   the new arrangement (that is how the Node List covered the studio the
   first time).
2. **A material's type is read from its graph, never stored.**
   `material_type_of` looks at what feeds the MaterialOutput; `material_set_type`
   scaffolds nodes and keeps what was already connected. Change the graph and
   the type follows; there is no second source of truth to drift.
3. **The Material Studio's "modified" is a fingerprint** of
   `material_to_json`, taken when opened or saved. Comparing text, not
   tracking edits, so an edit made from the node editor, a script or undo
   counts the same as one made in the studio.
4. **The node-contract battery compares point clouds.** `snapshot()` in
   tests/cpp/test_nodes.cpp appends every Points output. It did not until
   2026-09-04, which left every scatter node invisible to the determinism and
   seed checks; a DistributionLayer was the first node to fail for that
   reason. A node whose seed only acts under a non-default setting is listed
   in `seed_is_conditional` with the reason, not silently skipped.
5. **The asset index never deletes.** `AssetIndex::trash` moves a file (and
   its thumbnail) to `<root>/trash` and marks the record; `restore` moves it
   back. A scan indexes the trash folder as trashed, never as live. Tags and
   notes live only in `assets.json` - a rescan keeps them by id
   (`kind/relative-path`), so renaming a root loses them; that is the
   documented price of ids being paths.
6. **Search is TF-IDF over the record's words, no model.** `tokens_of` is
   name + up to three parent folders + tags + note + kind; a query term the
   vocabulary has never seen still matches by prefix. Deterministic and a
   few microseconds a record; the `embedding` slot exists for an external
   model to fill later without any caller changing.
7. **A material property is declared once.** `material_params_declare`
   (engine/material_params.cpp) puts it on the MaterialOutput node with its
   label, range, group and tooltip; `material_params_from` reads it into
   `MaterialParams` with clamping and a default for files that predate it;
   `renderer_material_uniforms` uploads it; the studio's tabs draw whatever
   is in the group. Adding a property means those three functions and the
   shared GLSL in renderer_matparams.cpp - never a hard-coded slider list.
   Every lit shader takes the material through the MATERIAL_*_PLACEHOLDER
   snippets, so the preview sphere and the terrain cannot disagree.
8. **An op that reports keeps its status line.** The API inbox only writes
   the generic "actions applied" when the batch said nothing, and
   `scene_state.json` carries `status`, so a script can read what an op did.

## Performance

1. **Never hash a struct's bytes for a change key.** `placement_key` did,
   PlaceSettings has padding after a bool, and the key changed every frame:
   the tile was re-placed and re-uploaded 9 ms a frame with nothing on
   screen moving. Hash fields. The same rule for any "did it change" key.
2. **Measure before optimising.** `perf_mark("name")` around a phase puts it
   in the status bar's breakdown and in `scene_state.json` under
   `perf.phases`; a script can read the figures with the app running. The
   marks in app.cpp are the frame's map; keep them when moving code.
3. **The governor judges work, not frame rate.** It reads
   `potential_fps` (1000 / max(CPU work, GPU time)), so a frame sleeping to
   the viewport rate is never "slow". It changes render scale, the preview
   panel, shadows, cloud quality and subdivision through `perf_quality()`
   at the read sites, never by writing RenderSettings, so nothing it does is
   ever saved into a project.
4. **The UI snapshot is rebuilt on change, not per frame** (eval serial,
   layout serial, node count, a pointer button, or a quarter second).

## The tile's join with the planet

studio/planet_place.cpp composes weight = feature halo (or 1 in whole-tile
mode) x border feather x mask. `PlaceSettings` carries edge (distance),
gradient (the feather's curve, pow before the smoothstep), mode and an
optional mask copied from Terrain output's "blend mask" input at request
time - copied, because the placement runs on a worker after the graph's
buffers move on. `place_gradient`/`place_mode` are render settings (saved,
`set_setting`, `set_viewport`, in the placement key). A control that changes
the join must be mixed into `placement_key()` or the tile is not re-placed
until the next evaluation. tests/cpp/test_planet_place.cpp pins the three.

## The terrain is an object with a transform

studio/terrain_xform.cpp: the Terrain SceneObject's pos (an offset; pos[1]
in height units like every object), yaw/pitch/roll, scl and deform are the
tile's transform, applied in the tile's own frame before the planet
placement - `tile_xform()` in every terrain vertex stage (TERRAIN_VERT_COMMON,
the TCS's culling corners, VS_DEPTH_SRC), `tile_xform_normal()` in the
fragment stage, `terrain_xform_apply()` on the CPU for the selection box and
`terrain_xform_ground()` for grounding. Rules:

1. A new terrain pass, or a new reader of the tile's geometry, goes through
   the transform or it draws the old tile. `upload_terrain_xform` in
   renderer_passes.cpp is the one upload; the placeholders are
   TILE_XFORM_PLACEHOLDER (vertex/control stages, after DEFORM_FN's
   uniforms) and TILE_XFORM_FS_PLACEHOLDER (fragment), spliced by inject_sky -
   which the TCS did not go through until this landed.
2. The world unit (`terrain_size_m`) is not the tile's size. It says how many
   metres one tile stands for; Width/Depth/Height in the panel are the
   object's scl and the height scale. Do not make either stand in for the
   other again.
3. A project without `"terrain_transform": true` on its Terrain object is
   from before this: the loader resets its transform to identity, because
   the old default pos was (0.5, 0.05, 0.5) and was never a transform.
4. The outline (`terrain_shape`) is a hard fragment cut (`tile_cut`) only
   when the tile is not placed on a planet; placed, planet_place's feather
   does it and the planet shows through - the same picture by two means.
5. The Surface sliders write the material's attributes; `rs.mat_roughness`
   and friends are overwritten from the material every upload and are not a
   control (they still drive the water and the export defaults).

## Objects, gizmos and deformers

1. **One deformation function, two twins.** `gpx::deform_point` (engine/gpx/
   deform.hpp) is the CPU truth: the exports, the tests and any picking use
   it; `deform()` in the mesh vertex shader (studio/shaders_scene.cpp) is
   its line-for-line GLSL twin. Change one, change the other, and the test
   in test_engine.cpp (`test_deformers`) pins the CPU side; the twist obeys
   the right-hand rule (+X at the top goes to -Z about +Y).
2. **Deform keys are written only when set.** scene_io_object.cpp writes
   twist/bend/shear/taper only for a deformed object, so a file from an
   older build and an undeformed object serialise unchanged.
3. **A gizmo is gated three ways**: the global `gizmo_visible()` switch, the
   object's `show_gizmo`, and the object's `locked`. Deform tools apply to
   meshes only; `axis_mask` says what each object type offers.

## Configuration and AI services

1. **Configuration is not preferences.** `config.json` (studio/config.cpp)
   holds what the application connects to - keys, endpoints, ComfyUI, apps,
   shortcuts; `terraforge_prefs.json` holds how it looks and performs. A
   preferences reset never loses a key. Secrets go through `secret_protect`
   (DPAPI on Windows; `plain:` elsewhere, stated in the file) - never write a
   key in clear.
2. **Every provider is its documented wire format, as pure functions.** A
   request builder and a response parser per provider, tested on canned JSON
   in tests/cpp/test_ai_services.cpp with error mutations. Adding a provider
   means those two functions, a `ProviderInfo` row, and an adapter branch;
   the source of the formats is docs_private/IMAGEEXPRESS_PORT.md.
3. **Results are written to disk before anyone is told.** Provider URLs
   expire; a job reports DONE only with a file path, and the UI thread
   (`ai_jobs_service`) is the only place a result touches the graph or the
   scene.
4. **Nothing blocks the UI thread.** `ai_text`, `ai_generate_image`,
   `ai_generate_model` and `comfy_run` are blocking by design and run on the
   job thread (ai_jobs.cpp) or in tests; the `ai_ask` op is the one
   deliberate exception and says so.
5. **ComfyUI injection is by (node id, input name), empty values skipped.**
   `comfy_inject` errors on a missing node and keeps workflow defaults for
   anything not given, so an imported workflow behaves as it does in ComfyUI.
6. **Shortcuts come from the table.** `shortcut_pressed("file.save")`, never
   `IsKeyPressed(ImGuiKey_S)`; the Settings window rebinds the table and the
   menus print `shortcut_chord(id)`.

## Mesh module

1. **An imported file's coordinates are never rewritten.** `scene_import_mesh`
   places and sizes an object with its transform, never by moving vertices:
   the whole value of a diagnosis is that a millimetre in the file is still a
   millimetre in the report. What one file unit *means* is a user statement
   (the panel's file-units selector), not something we infer and bake in.
2. **A repair stage counts as a fix only if the diagnosis changed.** Every
   operation in `engine/mesh_cleanup.cpp` returns how much it changed, and
   `mesh_repair` re-analyses the result from scratch rather than predicting
   it from the operations that ran. "Repaired ✓" with nothing behind it is
   the thing this module exists to replace.
3. **Never delete the user's whole model.** `mesh_drop_small_shells` refuses
   when every shell is small, and hole filling skips loops longer than
   `max_hole_edges` - a huge ring is a missing part, not a hole, and fanning
   it makes a lid.
4. **No GPL geometry libraries, ever** (the licence forbids it: see `LICENSE`).
   Meshwright's optional MeshLab and MeshFix stages cannot ship here (both
   GPL-3). QuadriFlow is MIT and Manifold3D is Apache-2.0 - those two are
   permitted, and are the two worth adding when the time comes.
   Where a stage is missing, the report says what is left over rather than
   pretending.
5. **A mesh object's material is scalar-only until the mesh has UVs.**
   `SceneObject::material_node` on a `Mesh` object is read in
   `renderer_scene.cpp`'s mesh loop (`gpx::material_params_from` on the
   assigned `MaterialOutput`'s attrs, uploaded with
   `renderer_material_uniforms`) and the mesh fragment shader (`FS_MESH` in
   `shaders_scene.cpp`) runs the same GGX pipeline as the terrain, through
   the same `MATERIAL_UNIFORMS_PLACEHOLDER`/`MATERIAL_FN_PLACEHOLDER`
   snippets - tint, roughness, metallic, specular, reflection, clearcoat,
   translucency, emissive, ambient from the sky colours. What it cannot do
   yet is sample a picture map: `gpx::TriMesh` and every loader (OBJ, STL,
   PLY, OFF, GLTF) carry position and a per-face flat normal only, no UV
   channel, and `SceneObject::verts` is interleaved pos(3)+normal(3). Adding
   real texturing means extending that layout and every loader/exporter/
   deformer that touches it together - do not sample a channel texture in
   `FS_MESH` until that lands.

## Animation

1. **Nothing moves unless it has a key.** A property is animated when its
   track has keys or an expression; `anim_unkey` drops a track that became
   empty so the property is static again. Never add an "enable animation"
   switch beside a track - the track's existence is the switch.
2. **One property table.** Every animatable scene/world property is declared
   once in `studio/anim_targets.cpp` (`OBJ_PROPS` / `WORLD_PROPS`) with a
   path, label, group and component count; `anim_ptr`/`anim_world_ptr`
   resolve the path to the live field. The Properties circle, the Timeline,
   the expression lookup, the API and `anim_apply` all read that table. To
   make a new field animatable, add a row and a `obj_ptr` case - nothing
   else. `test_anim_scene.cpp` checks every row resolves.
3. **Tracks live with what they animate.** `SceneObject::anim` (map by
   path), `SceneState::world_anim`, `Attribute::anim` / `anim_v`. So undo
   snapshots, duplicate, delete and the scene file carry them without any
   side table. Track ids for the UI/API are strings
   (`o:<index>:<key>`, `w:<key>`, `n:<node>:<attr>[:c]`) resolved fresh
   each frame by `anim_resolve` - never cache a `gpx::Track *` across frames.
4. **Time is seconds inside; frames are a display.** `gpx::Timeline` owns
   fps, range and formatting. Keys snap to whole frames when
   `Timeline::snap` is on; the ruler, the fields and the ops speak frames.
5. **The engine's curve is deterministic and testable without a window**:
   `engine/animation.cpp` (evaluation, tangents, extrapolation, modifiers,
   text form), `anim_curve.cpp` (every editing operation the panels use),
   `anim_expr.cpp` (the expression language). Panels must call `gpx::anim::*`
   rather than editing `keys` by hand, so a gesture and an API op do the same
   thing and `update_tangents()` is never forgotten. The old
   `"interp;t,v;..."` text form must keep loading; the new form starts `K2;`.
6. **Per-frame apply is the only writer.** `anim_service` (app.cpp, before
   any drawing) advances the clock and calls `anim_apply`; a keyed property's
   value is whatever the curve says at the current time, so with Autokey off
   an edit snaps back - that is correct and matches every DCC. With Autokey
   on, `anim_autokey` records the edit first (call it right after the widget,
   guarded by `IsItemDeactivatedAfterEdit`).
7. **Every key edit is one undo step per gesture**: push once on drag start
   (`drag_started`), not per mouse move.

## Objects on the terrain, and displacement layers

1. **A footprint is the convex hull of the base, never the bounding box.**
   `imprint_footprint` (studio/imprint_footprint.cpp, pure, tested in
   undo_tests) takes the vertices in the lowest band of the object's height,
   deforms and places them as the viewport does, and hulls their XZ. The
   TerrainImprint node measures a signed distance to that polygon, so a
   square base is flat at all four corners; the ellipse it replaced left the
   corners in the blend and a house floated at each one. Margin and blend are
   distances (tile fractions in the file, metres in the UI), sink is a
   height; blend <= 0 means the node's multiple of the footprint's radius.
2. **Sink is a clamp, not a mode.** The node's target height is
   `clamp(ground, base, base + sink)`: below the base the ground rises to the
   base, within the allowance it is left alone, deeper it is dug to
   `base + sink`. `sink = 0` is the exact flat base; there is no separate
   "buried" switch to keep in step.
3. **The grounded lock reads the highest ground under the hull**, so no
   corner is ever below the natural surface unless the user sinks it.
4. **A layer's relief is a delta in heightmap units, composited by
   presence** - the same `p` the colour uses - on top of or instead of the
   layers below (`disp_add`). A node that hands a layer relief exposes the
   delta (FakeStones' and GrassDisplacement's `displacement` ports), never
   the displaced terrain; a layer never subtracts its own input.
5. **Surface features are applied after placement, on the ground the
   viewport shows.** The tile is placed on its planet by amplitude: a
   texel within `place_presence` of the tile's ground is not a feature and
   the planet shows through - so a house's mould and a layer's stones (both
   smaller than that) were swallowed. `surface_features_apply`
   (studio/surface_features.cpp, pure) adds the material's displacement and
   then the objects' imprint to the *placed* ground, with the same
   `gpx::imprint_apply` the node uses; the tile handed to placement is the
   imprint node's input, so nothing is moulded twice. The ground between
   the two steps is `app_natural_ground()`, which the grounded lock reads,
   so an object rests on what the user sees. The graph's own TerrainImprint
   output still exists for graph consumers and exports.
6. **Patching by text: a node's `apply_post` is not a landmark.** The
   FakeStones displacement port was first inserted after "the next
   apply_post", which belonged to Crater - a node without that port - and
   crashed every battery. Locate by the node's own registration, never by a
   call several nodes share.

## Populations and level of detail

1. **Candidates first, masks second.** A population's lattice is fixed by
   its spacing, never by its density or its masks (`gpx::scatter`,
   docs/ECOSYSTEM.md). Every instance's identity is `hash(seed, cell,
   index)`; a decision is `u(id, channel) < probability`. Raising the
   density is then a superset and a mask edit moves nothing outside its
   area - tested. A sequential RNG, a count that changes the lattice, or a
   decision that reads the camera, the clock or the thread count breaks
   that and reshuffles the forest under the user.
2. **Interaction is measured to the footprint's edge, and the search
   window must reach past the widest one.** `nearest_distance` subtracts
   the neighbour's radius; its grid cell is `reach + max radius`, because
   a broad canopy just outside a `reach`-wide window read as farther than
   it was and the grass grew under the trees.
3. **Stack surgery: remove every link before adding any.** Trading two
   layers' places rewires three channels; while one still runs the old way
   the new way is a cycle and `add_link` refuses it. An ecosystem's `below`
   is re-derived from the stack after every add, delete or swap
   (`wire_populations_below`), never edited by hand.
4. **A population's dials are metres; the node learns the tile's width and
   the height scale.** `size_m` and `height_scale` on `EcosystemLayer` /
   `ScatterArea` / `PointsInteract` / `PointsTransform` are written by
   `apply_object_nodes`, like the scene nodes' transforms, so a slope band
   in degrees is the slope the viewport shows (MaterialLayer's slope still
   assumes a height scale of 1 - a 1500 m noise block reads as all cliff
   there). An engine test sets both explicitly.
5. **LOD thins, it never re-scatters.** Cells are sorted by a per-instance
   key at rebuild; a pass draws a prefix. Both passes take the decision
   from the *view* camera, so a copy's shadow is where the copy is. A mesh
   with material parts is never decimated (its pictures would tear), and
   every level falls back to what the mesh actually has.
6. **A camera-driven service must use the camera the viewport draws
   with.** `renderer_get_camera` returns the orbit camera; when a scene
   camera is activated the view comes from *that* one instead. The
   dynamic population followed the orbit camera and generated its whole
   crowd behind the viewer - drawn 0, total 200k - which reads exactly
   like a broken generator. Read `scene_active_camera()` first
   (studio/eco_dynamic.cpp).
7. **What a world cell holds may never depend on the camera.** Only
   *which* cells exist does. A cell is `hash(seed, cell index, candidate
   index)` and nothing else, so re-entering it from the other side gives
   back the same instances; the moment a cell's contents read the eye,
   distance or frame, a population re-grows as you walk and the whole
   on-demand model is worthless.

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
5. **Never measure a renderer change with wall-clock frame time.** With vsync
   on, the GPU finishes early and waits, so every frame is 16.7 ms whatever the
   draw costs. Per-patch culling measured as a 0.5 % *regression* that way and
   as 47 % faster when timed properly. Use `GpuTimer::Scope` around the pass
   (`studio/gpu_timer.hpp`); it reads results from a query issued several
   frames earlier so the instrument never stalls the pipeline.
6. **A performance number that no mechanism explains is a measurement error
   until proven otherwise.** A benchmark once reported culling 24 % faster
   while drawing every patch — impossible, and it was smoothing bleed between
   arms. This is why the patch count is published beside the timing: the two
   check each other.
7. **Pair and interleave benchmark arms; never sweep once and compare across
   time.** A cloud sweep reported three scattering bounces as six times faster
   than one, because wind moved coverage through the frame during the run.
   Ten short A/B pairs made the drift cancel: +2.9 % median, against unpaired
   medians 22 % apart.
8. **A conservative bound must be built from the real data.** Patch height
   bounds come from the full-resolution heightmap, not the 256² picking copy,
   and are widened by a texel because bilinear filtering reaches the
   neighbour. A bound that under-covers does not cost performance — it puts a
   hole in the terrain.

## Engine rules

1. **Every step must be deterministic.** Same graph and seeds must produce
   bit-identical output, every run, on every thread count. Parallel solvers
   use per-worker buffers reduced in a fixed order — never unsynchronised
   writes to shared data. `test_workflow_determinism`,
   `test_thread_count_determinism` and `test_repeat_determinism` enforce this;
   see "Deterministic parallelism" for why all three are needed.
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
8. **A GLSL emitter must resolve its inputs before it streams.** Resolving an
   input appends that subtree's declarations to the same body, so interleaving
   it with a `<<` chain splices them into the middle of the statement being
   written. Compute the strings first, then write the line. A structural test
   (`glsl_declared_before_use`) catches this without a GPU.
9. **The transpiler's type conversions must mirror `FieldValue` exactly** — a
   number broadcasts to a vector, a vector read as a number is its length, a
   colour is its luminance. They did not, and a scalar redirect offset became
   `(n,0,0)` on the GPU against `(n,n,n)` on the CPU: a graph that rendered
   differently for no visible reason.
10. **Nodes may have several field outputs, and they are different values.**
    Emission is keyed by node *and* port, and by the evaluation point, since a
    redirect asks for the same subtree somewhere else.
11. **`NodeDef` is built by aggregate initialisation in `REGISTER_NODE`.** A new
    field goes at the **end** or every registration in the project breaks at
    once. `depends` is currently last.
12. **A node that reads another without a link must declare it** through
    `NodeDef::depends`. `Graph::edges()` is the links *plus* those, and
    `topo_order` and `mark_dirty` both walk it — so the source is evaluated
    first and a change to it reaches the reader in the same pass. This is not a
    way around links: links stay the only way data flows, and named routing was
    refused precisely so a graph shows its own data flow. It is for a node that
    reaches outside the graph and comes back holding another node. An edge to a
    node not in the graph is dropped rather than counted, or a stale reference
    stalls the sort and the whole project stops evaluating.
13. **An attribute holding another node's id must set `Attribute::node_ref`.**
    Loading renumbers every node, so a bare id would land on whichever node
    inherited that number — a perfectly valid node, which is why nothing
    downstream could ever report it. `graph_from_json` remaps flagged
    attributes with the links; one pointing at a node no longer in the file
    goes empty. `node_tests` checks the reference still names the same node
    across a round trip, on a graph whose ids the load cannot reproduce.

## Field type conversions

1. **Four value types, one conversion table.** `FieldValue::number() /
   as_color() / as_vector() / as_texcoord()` are the whole rule set for what
   a number, colour, vector or texture coordinate means when it lands on a
   port of another type. The transpiler mirrors each one (`as_number`,
   `as_vec4`, `as_vec3`, `as_vec2` in `field_glsl.cpp`, chosen by the port
   prefix `''`/`@`/`#`/`%`; `!` is the raw vec4). Add a conversion in both
   places in the same commit or the GPU disagrees with the CPU.
2. **A converter branches on the upstream type at compile time.** The CPU
   node reads `FieldValue::type`; the emitter reads `InputFn::type(port)`
   (the upstream port's declared type). Both must take the same branch for
   the same graph. `field_gpu_verify_all` carries a case per converter and
   the worst measured disagreement is 2e-6.
3. **Every field output port needs its own emitter.** `emit_node` falls back
   to the node's primary emitter for a port it does not know, so a forgotten
   `reg_out` returns the *wrong quantity* rather than failing.
   `test_every_field_output_transpiles` transpiles every field output of
   every node and checks that sibling outputs emit different code.
4. **Colour maths is shared, not duplicated.** `gpx/color_math.hpp` (CPU) and
   `gpxf_rgb2hsv` / `gpxf_hsv2rgb` in the prelude are the same algorithm,
   branch for branch.

## Component nodes and workspaces

1. **A node belongs to one workspace through its category**
   (`domain_of_category`, `WS_*` in `app.hpp`). Terrain, Materials, Objects
   (Scene), Atmosphere (+Cloud), Lighting (Light), Cameras (Camera),
   Animation, Render. Workspace numbers are historical - 4 is "all domains"
   and saved editor layouts carry them - so new workspaces append; never
   renumber.
2. **Configuration nodes drive the scene, they do not compute buffers.**
   Light / Camera / Scene / Cloud / Render categories and every node whose
   description starts with `[Planned]` are configuration nodes in the
   contract battery: no output port required, no buffer expected. A planned
   node is a placeholder with its roadmap phase in its attributes, so the
   module cannot be forgotten; it does nothing else.
3. **Node-driven scene objects are found by `driver_node`, then adopted by
   name, then created - and never deleted** (`scene_nodes_objects.cpp`).
   Removing the node leaves an ordinary object behind. Lengths on nodes are
   metres; the conversion to tile units happens there and nowhere else.
4. **Render nodes are the source of truth when present.** `apply_scene_nodes`
   copies RenderOutput / RenderPasses / RenderBackdrop / PostProcess into
   `RenderSettings` after every evaluation; the Render panel edits the same
   fields and says when a node is overriding them.

## Render passes and the backdrop dome

1. **A pass is the same frame drawn again with `g_aov` set**, never derived
   from the beauty afterwards. Every shader that draws a surface reads
   `u_aov` and returns `aov_out(...)` before tone mapping; the sky writes its
   own cases. A surface shader that ignores `u_aov` writes its finished
   colour into every pass - the infinite surround did exactly that, and the
   object-id pass came back full of 0.8.
2. **Pass numbering is `RenderPass` bit + 1**, `AOV_BEAUTY_LINEAR` = 13.
   Depth and position leave the GPU in tile units and are scaled to metres
   when written. Passes go to a float framebuffer (`ensure_fbo(..., hdr)`)
   and to EXR; decoration (gizmos, outlines, overlays) is skipped while a
   pass draws.
3. **Fog is one function** (`FOG_FN`, spliced by `inject_sky`) shared by
   terrain, water and meshes, so everything at a distance disappears into
   the same air. Water and meshes had no fog at all before.
4. **The backdrop lives inside `sky_color()`** (`SKY_FN`), so the sky pass,
   water reflections and terrain reflections see one dome; bind it
   (`backdrop_bind`) in every program that carries SKY_FN. The dome is an
   absolute HDR picture: it is blended in after the procedural sky's
   nightfall factor and is not dimmed by it. Horizon haze and clouds apply
   on top; `u_bd_haze` is how much.
5. **Load images once, versioned by path and mtime** (performance rule 1).
   Our EXR reader covers scanline NONE/RLE/ZIPS/ZIP; PIZ and the rest are
   refused with a message rather than misread. Radiance .hdr goes through
   stb, which handles the run-length files every HDRI site ships; a
   hand-made flat-scanline .hdr showed banding, an EXR of the same pixels
   did not.

## Verifying a renderer change

1. **Look at the picture.** `capture` renders the active camera to a PNG from
   the API, MCP and `Studio.capture()`. Multi-octave cloud scattering compiled,
   bound its uniform and timed flat while turning a shaped storm into a flat
   pale sheet — every non-visual check was consistent with it working.
2. **Compare against a number, not an impression.** Mean brightness and
   contrast (std) over the captured image caught the same defect as
   128 → 198 and 31.6 → 17.3, and later chose the default by measurement
   rather than by taste.
3. **Choose a scene the effect can show in.** The first cloud A/B was
   indistinguishable because the clouds were clipped at white; added light had
   nowhere to go. A dense cloud, lower exposure and the sun behind made the
   same change obvious.
4. **A technique from a paper carries the constants of the engine it was
   written for.** The scattering octave attenuation of 0.5 assumes an
   optical-depth scale that is not ours. Expose the constant and pick the
   value by measuring, rather than copying it and trusting the result.

## Generated shaders

1. **Always substitute the placeholder**, with the generated function or with
   a stub. A shader that is well-formed either way has no second code path to
   get wrong.
2. **Check `GL_LINK_STATUS` on anything generated** (`link_checked`). The
   built-in shaders are known good, which is why nothing checked before;
   generated code comes from the user's graph and a silently unlinked program
   renders nothing at all.
3. **Keep the old program until the new one links**, so a bad graph leaves the
   viewport as it was rather than turning it black.
4. **Remember the request, not just the live source.** Clearing the source on
   failure makes the next frame see a difference and relink again — that
   pinned a core until the app stopped responding.
5. **Displace the normal wherever you displace the geometry.** The fragment
   stage takes central differences of the same function; moving vertices alone
   lights a surface that is not there.
6. The vertex and fragment stages are separate translation units, so each may
   carry its own copy of a generated prelude. Duplicate definitions only
   collide *within* one stage — so when a stage holds **two** generated
   functions, emit the prelude once and `field_glsl_strip_prelude()` the rest.
7. **Every pass that draws the terrain must see the same surface.** The vertex
   and tessellation-evaluation shaders share one body (`TERRAIN_VERT_COMMON`)
   so they cannot drift, and the shadow pass carries the same displacement —
   terrain casting a shadow from where it used to be reads as broken, not as
   an approximation.
8. **Tessellation needs a floor as well as a ceiling.** Displacement and
   fractal relief are evaluated per vertex, so a purely screen-space metric
   throws them away whenever the whole tile is small on screen. The minimum
   subdivision is set so the adaptive path is never coarser than the fixed
   grid it replaced.
9. **Bind every sampler the generated code declares.** An unbound sampler
   reads black — not a crash, a silently wrong picture. If there are not
   enough texture units, refuse the graph and say so on the node.
10. **Build a declaration with `EmitCtx::declare()`, never by streaming into
   `body`.** Resolving an input appends that subtree's declarations to the same
   buffer; a `<<` chain therefore splices them into the middle of the line
   being written. A function call evaluates its arguments first, so `declare()`
   cannot get this wrong. This bug was written twice before the API was fixed.

## Persisted UI state

1. **Never hand a saved layout or view file to a widget without validating
   it.** A node-editor view file with a collapsed zoom and `INT_MIN` positions
   made the editor lay out a canvas billions of units across and never finish
   a frame — so the app spun behind a black window on *every* launch, for ever,
   with nothing to say why. See `graph_view_is_sane`.
2. Prefer discarding unusable UI state to trying to repair it. Losing pan and
   zoom is trivial; an application that will not start is not.

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
7. **A golden must produce varying output and connect every link it declares.**
   `graph_from_json` drops a link naming a port that does not exist, silently,
   so a golden can look like it exercises a whole chain while half of it is
   disconnected. Four committed goldens were doing exactly that.
8. **A structural check only bites on a graph that exercises it.** The
   declared-before-use test passed against a bare node because an unconnected
   input resolves to a literal and emits nothing. Connect the inputs, and
   prove the check can fail before trusting that it passed.

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
6. **The terrain tile is placed onto the planet, not laid over it**
   (`studio/planet_place.cpp`, run at upload time, before
   `renderer_set_terrain`). The planet's relief shows through wherever the
   tile is flat at its own ground level, is levelled under anything the tile
   builds (`place_flatten`), and every join - a feature's footprint, the
   tile's border - is feathered. Picking, shadows, culling bounds and the
   overlays all see the placed map: what you click is what you see.
7. **The planet has its own ground level** (`place_ground`, heightmap
   units); the tile's ground (the median of its border) is *settled* to it.
   Never derive the surround's base from the tile's own level again: a
   normalised mountain whose rim sits at 0.6 lifted the whole world 600 m
   and put snow on every plain.
8. **The surround's relief and the placed tile are the same function.** The
   CPU composites with `gpx::planet::heightf` at `1.2 x relief + ground`
   and the surround shader draws `pl_height * 1.2 * hscale + ground * hscale`;
   `planet_gpu_verify` (run by `verify_field_gpu`) measures the two against
   each other, currently 2e-6. Change one side, run it.
9. **Sphere placement never subtracts R from something of size R.**
   `gpx::planet::sphere_place` / `pl_sphere_place` build the drop from
   `2R sin^2(a/2)` terms and the reach from `s * sinc(a)`; `c + d*(R+h)`
   lost the terrain height entirely past a few thousand tile radii. A tile
   wrapped onto a sub-tile globe shrinks its heights by the square of the
   wrap ratio. The floor is float precision at the tile's position: a globe
   below ~1e-6 of the tile width (1 cm at 5 km) vanishes into the 6e-8
   rounding of a vertex at x = 0.5, camera positions included. Going lower
   means tile-centred vertex maths plus a double-precision camera, not a
   smaller near plane.
10. **The surround fogs with `FOG_FN`, like everything else.** A distance
    fog of its own painted it pale right up to the tile's border, which read
    as a cliff around the tile.

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

## Portability

TerraForge builds on Windows, macOS and Linux from one CMake project. Windows
is the reference platform: the golden hashes were recorded there, and it is
what most contributors run.

1. **macOS caps OpenGL at 4.1**, and every shader in the repository declares
   `#version 430 core`. `studio/glsl_version.hpp` rewrites that first line to
   410 on Apple, and **every** `glShaderSource` call site goes through it -
   `compile()`, `pl_compile()` and the CPU/GPU checker's own compiler. A new
   compile path that skips it works on Windows and fails on a Mac with a
   version error nobody will connect to the new code.
2. **Nothing above 4.1 may be used**: no compute shaders, no shader storage
   buffers, no `layout(binding=)`, no explicit uniform locations, no immutable
   texture storage, no debug callback. Tessellation is fine - it is core in
   4.0. If a feature genuinely needs 4.3, it needs a fallback, not a broken
   Mac build.
3. **Platform code is written for all three, or it is not written.** Every
   `#ifdef _WIN32` gets a `#elif defined(__APPLE__)` and an `#else`. A branch
   that silently returns nothing is how the file dialogs "worked" everywhere
   and opened on Windows only.
4. **A POSIX signal handler must be async-signal-safe.** The crash path writes
   with `write()` to a descriptor opened at init and leaves with `_exit()`. No
   `printf`, no allocation, no `std::string`. A crash handler that crashes
   tells you nothing at all.
5. **Windows behaviour is the thing being preserved.** After any portability
   change, the Windows link line and the test hashes must be unchanged. Both
   platform ports in this repository were verified that way before anything
   else was believed.

## Where files go at run time

`studio/paths.cpp` answers this once, and everything asks rather than guessing.

1. **`install_dir()` is read-only, `data_dir()` is writable.** Running from
   `build/` these are the same place, which is why the distinction went
   unnoticed for so long. An installed copy breaks it: a macOS bundle is
   launched with the working directory set to `/`, and a Windows install may
   sit somewhere the user cannot write.
2. **Anything the application writes for itself goes through
   `settings_path()`** - preferences, the ImGui layout, the node-editor view
   files. It prefers a file of that name in the current directory, so a
   developer's checkout keeps its own settings and nothing about that workflow
   changes.
3. **A new relative path in an `fstream` or an ImGui `SettingsFile` is a bug on
   macOS.** It will write to `/` and fail without saying so.
4. `install_dir()` finds the shipped tree by looking for `orchestrator`. The
   macOS packaging test asserts the bundle still carries it, because if it
   stops, the offline renderers stop with nothing in the log.

## Packaging

1. **One definition of what ships.** `packaging/windows/stage.ps1` is the only
   list of what an installed TerraForge consists of, used by both the one-click
   installer and the setup builder. Two lists is how a package loses the Python
   layer on a Friday.
2. **Ship stripped, keep the symbols.** The build carries `-g` on purpose (a
   crash report's `module+RVA` resolves to `file:line`), and that is 148 of the
   executable's 159 MB. The addresses in a report are module offsets, so they
   resolve against the unstripped build the developer still has - which is why
   the symbols are copied to `dist/symbols` rather than discarded.
3. **Link the MinGW runtime statically** (`-static-libgcc -static-libstdc++
   -static`). Otherwise the package needs three DLLs that exist only on a
   machine with the toolchain. `stage.ps1` reads the real dependency list out
   of the binary with `objdump` rather than assuming either way.
4. **Shell scripts must be checked out with LF.** `.gitattributes` pins
   `*.sh` and `*.command`, and `tests/test_packaging.py` enforces it. A CRLF
   `.command` fails on macOS with `bad interpreter: /usr/bin/env bash^M`,
   which reads as a broken script rather than a line-ending problem - and the
   file looks perfectly fine in the repository that produced it.
5. **The executable bit lives in the git index**, not the filesystem: Windows
   checkouts have none. `git update-index --chmod=+x` on every new script, and
   the packaging test checks the mode is `100755`.
6. **Both `get_deps` scripts fetch the same versions.** They are the only place
   the third-party versions are written down; a drift means Windows and macOS
   build against different sources. The packaging test compares them.
7. **Install per user, never machine-wide.** No elevation prompt, and the
   install folder stays writable - which is where the logs go the first time
   something breaks.
8. **The uninstaller removes what the application wrote**, and leaves
   `%LOCALAPPDATA%\GeekatplayTerraForge` alone. Removing the program must not
   remove someone's work.

## Before you commit

- `.\build.ps1` (or `./build.sh`) must succeed. It fails loudly; do not trust a
  stale binary.
  Kill a running `geekatplay_studio` first — a locked exe fails the link with
  `Permission denied`.
- `.\test.ps1` must pass — all six suites.
- Never commit `docs_private/`, build output, or `external/`. Roadmaps,
  development notes and progress documents stay out of the repository.

## Floating point

1. **Never build with `-ffast-math` / `/fp:fast`.** It implies
   `-ffinite-math-only`, so the compiler folds every `std::isfinite()` to true.
   Measured with our exact flags: `isfinite(NaN)` returned 1. That silently
   deleted the guard in `gpx::FieldValue::finite()`, both "output has NaN/Inf"
   checks in the node contract battery — which cover every port of every node —
   and nine more assertions in the engine tests. The whole Tier-1 finiteness
   requirement passed for months without testing anything.
2. **What it bought was ~1%**, inside run-to-run noise, measured on the solvers
   that dominate a real graph: noise 1024 50.1 → 49.9 ms, hydraulic 512
   128.7 → 125.2, thermal 512 253.8 → 251.7, stream power 512 796.1 → 784.8,
   5-node chain 512 404.3 → 403.5. A safety net is worth more than 1%.
3. **A checker must be proved able to fail before it is trusted.**
   `test_finiteness_checker_binds` runs first in the node suite, feeds a real
   runtime NaN and Inf through the same helpers the battery uses, and fails
   loudly if the build ever picks up finite-math-only again.
4. **The toolchain is part of a golden.** All fourteen golden hashes moved when
   the flag came out. Thirteen projects moved by under 2e-5 on 0..1 values —
   rounding. `erosion_all` moved by 0.84. `goldens.txt` now carries a
   `# built by:` line naming the compiler and float flags, and a mismatch
   prints which of the two changed.
5. **Derived difference maps are ill-conditioned; the height field is not.**
   Same comparison, per node: `Noise` output agreed to nine decimal places and
   `Hydraulic` output to 0.012 %, while `Hydraulic`'s `erosion_map` mean moved
   35 % and `deposition_map` 30 %. They are differences of similar numbers fed
   through an iterative solver. Validate them by conservation residual and
   sign, never by value equality against another build.

## Deterministic parallelism

Engine rule 1 promises bit-identical output "on every thread count". That half
was untested and false. `test_thread_count_determinism` now runs the parallel
solvers at 1, 2, 3, 5 and 8 workers and demands one answer; `gpx::worker_count()`
is overridable (`set_worker_count()`, `GPX_WORKERS`) so it can.

1. **A worker id must never reach the physics.** The droplet solver dealt
   particles out as `per_round / T`, seeded its RNG from `tid`, and let each
   worker sample its own accumulator. The partition therefore decided which
   particles existed, where they started and what they saw: workers 1/2/3/4/8
   gave five different terrains from one seed.
2. **Seed from the item, not the worker.** A particle's start comes from a
   counter hash of `(seed, round, global index)`, so it lands in the same place
   under any partition. This also removes `std::uniform_real_distribution`,
   which the standard does not specify — libstdc++ and MSVC produce different
   streams from the same engine, so it can never appear in a canonical path.
3. **Read only shared state inside a parallel section.** Reading a per-worker
   accumulator is what coupled the physics to the partition.
4. **Reduce in integers, never in floats.** Float addition is not associative,
   so the same deltas split eight ways total differently from two ways. Every
   other cause was fixed and the hashes still differed until the accumulators
   became fixed point at 2^40 — 9.1e-13 resolution, six orders below float's
   1.2e-7 at 1.0, with ±8.4e6 of int64 headroom.
5. **Prefer one shared atomic accumulator to T private ones.** Integer add is
   associative *and* commutative, so a relaxed `fetch_add` needs no reduction
   pass and no per-worker buffers. Per-worker fixed-point buffers were equally
   correct and 5.5× slower (Hydraulic 512²: 95 → 575 ms); the shared atomic
   brings it to 164 ms. Apply and clear in one `exchange` pass — a separate
   clearing sweep doubles the memory traffic.
6. **A scatter write inside `parallel_rows` is a race.** `Wind` pushed material
   downwind with `delta.at(dxp, dyp) += lift`, and `dyp` routinely lands in
   another worker's band: an unsynchronised read-modify-write, so updates could
   be lost outright. The determinism test found it at 5 workers. Any solver
   that writes somewhere other than the cell it is visiting needs the atomic
   accumulator — or must stop letting bands own their sources.

   `Rivers` had the same bug and the same comment excusing it ("concurrent min
   writes race benignly across bands"). A min is not benign: both threads read
   the old value and one update is lost. **Two tests are needed, because they
   see different things.** `test_thread_count_determinism` catches a solver
   that *divides* work differently and so answers differently — that
   reproduces every run, and it never fired on Rivers because the partition
   was not the variable. A timing race gives the same answer most runs and a
   different one occasionally: `test_repeat_determinism` evaluates one graph
   two dozen times over with every splat radius opened to its maximum, so
   nearly every write crosses a band. At the default radii the Rivers race
   showed up in one suite run in three; widened, in every one.

   For a **sparse** splat, having the bands own the *output* beats an atomic:
   collect the cells that actually write, then have each thread walk that list
   and touch only rows it owns. No texel gets two writers, the total write
   work is unchanged, and there is no atomic traffic at all.
7. **Removing within-round coupling costs rounds.** With particles no longer
   seeing each other inside a round, `ROUNDS` had to rise 8 → 48 or erosion
   spikes: mean |Laplacian| 0.0227 → 0.0087, and at 8 the surface punched
   below the normalised floor (range started at −0.0058 instead of +0.0288).
   That is what engine rule 2 is about, and it is why Hydraulic is 1.7× slower
   than the racy version. Wind, Thermal and StreamPower are unchanged.
