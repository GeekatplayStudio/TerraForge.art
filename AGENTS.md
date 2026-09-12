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
2. **So it says so, and there are three ways out**: the lock button in the
   view header, double-clicking the same node again, and "Follow the Terrain
   Output again" in the graph canvas's right-click menu. The viewport also
   draws a badge over the picture. A badge that only named the node was not
   enough - the same report came back on a Crater -> Terrain Shape -> Thermal
   chain pinned to the Crater - so it says what is missing: the nodes after
   it, the erosion, the output.
   The pin is not saved with the project. A reopened scene follows the Terrain
   Output, so a camera placed while pinned can stand inside ground the pinned
   node never had (that crater's camera faced the plateau's cliff).
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

## The scripting surface is audited, and the assistant is shown every op

`tests/test_api_coverage.py` reads every file that compares `op` -
`ai_*.cpp`, `*_ops.cpp`, `layout_store.cpp` - and asserts three things: an
op has an MCP tool (`studio_<op>`, or an alias in the test), the tool
dispatches, and `ai_schema.cpp` shows the op as a `{"op":"..."}` syntax
line. It used to glob `ai_*.cpp` only, which hid sixty ops and five with
no tool; and nothing checked the schema, so the assistant was never told
about `add_node`, `connect`, `set_attr` and 24 more. Add an op: add its
tool and its schema line in the same commit, or the audit fails and names
it. Views in ops are 1-based everywhere (`view:1` is View 1) - `shading`
used to be 0-based on its own.

## The mouse belongs to the window it is in

Viewport input is routed by **which view the pointer is over**, and the
camera that moves is the one **that view is showing** - `view_camera_index`
in the panel, passed down to `camera_fly` (renderer_camera.cpp). Never look
the camera up inside the input code: it used to drive
`scene_active_camera()`, so a drag in a free view swung the active camera
instead of the view, and a drag in a view locked to another camera swung the
active one anyway. Whatever the pointer is over is what has to move.

`renderer_camera_input` and `renderer_pan_screen` both return true when they
flew a scene camera, which is the caller's cue to call `camera_flown` - the
one place that writes the auto-key and invalidates the views. Every window
that flies a camera (the viewports, the Preview panel) goes through these
three, so the bindings, the distances and the keys are the same everywhere
rather than a second copy that drifts.

## Viewports come back where they were left

layout_workspace.cpp `workspace_layout_restore` runs on the first frame
(app.cpp): the record captured at exit is read back. It was written and
never read - the only restore path ran on a workspace *switch*, and the
first frame has no previous workspace - so every launch reset the views.
The record now carries the free orbit camera (`LayoutRecord::orbit`,
through `renderer_orbit_get/set`) and the ortho views' zoom and centre.
The orbit is one global for every perspective view (`Camera CAM`); a
per-view orbit is the viewports roadmap's first item.

## Render presets and the queue

render_presets.cpp: a `RenderPreset` is a named `RenderAssign` saved with
the scene (`SceneState::render_presets`, scene_io.cpp); applying one
copies everything but keeps the camera's own output file when the preset
has none, and records the name in `RenderAssign::preset`. A batch is
`App::render_queue`; `render_service_requests` (panel_render.cpp) starts
the next camera when no render is running, and `render_batch_queue` gives
a camera still on the default file one named after it, or every camera
would overwrite `render.png`. The `render` op errors without a camera -
it used to report success and do nothing.

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

## Anything found wrong goes on the list, the moment it is found

`docs/TODO.md` is the standing list of known defects and gaps. Anything found
to be broken, missing, or not built the way it should be goes on it **when it
is found**, whether or not it is what you were working on - and say so in the
reply as well, so the person knows. A defect noticed in passing and not
written down is a defect found again six months later at full price.

Record the evidence you already have: what you saw, what you measured, what
you ruled out. Do not go investigating to make the entry tidy; "suspect X,
ruled out Y" is worth more than a polished guess. Move an entry to Closed when
it is fixed and verified, and name it in the commit.

This includes things you find out about your own earlier claims. Closed #3 on
that list is a note recording that a previous session reported an op as
missing when it was implemented all along - the correction belongs there for
the same reason the defects do.

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

## Vue's Terrain Editor is a set of graph operations

studio/terrain_editor.cpp holds the table: every effect name Vue's Effects
tab offers, the node it is, and how Rock hardness reaches its parameter;
the styles (terrain_styles.cpp `styles()`), the clipping node, the toolbar
commands and the Picture import. The panel (panel_terrain_editor.cpp) and
the ops (ai_ops_terrain.cpp) both call these and nothing else, so adding an
effect means one table row: the panel button, the op name, the MCP tool's
list and the assistant's schema line follow (the schema and MCP text are
written by hand - update both). Every function takes the graph lock; call
from outside a lease. A node the editor added is recognised by type
(`is_editor_node`), which is what `remove_effects` walks back through -
so a new effect node type goes in the table or it is not removable.

## Numbers take the wheel

studio/wheel_widgets.hpp wraps ImGui's drag and slider widgets
(DragFloatW, SliderFloatW, DragIntW...) so the wheel over a value nudges
it and the panel does not scroll; scalar_float does the same. Use the
wrappers, not ImGui's, in any panel that shows a number - a raw
ImGui::DragFloat is a value the wheel scrolls past, which is the report
"the wheel does not work on the planet surface options" verbatim. The
mechanism is ImGui's key ownership, claimed while hovered, so the first
frame the pointer arrives with a wheel notch already in it can still
scroll; that is ImGui's, not ours.

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

## The arrangement scales with the window

ImGui's docking gives every pixel of a host resize to the central node:
shrink the window by 800 px and the viewport loses 800 px while a 640 px
graph editor keeps its 640 - "resizing from the corner scrambles the
panels". app.cpp scales every dock node's SizeRef by the resize ratio each
frame the dockspace changes size, and on the first frame against the size
the arrangement was saved in (prefs dock_w/dock_h), so the panels keep
their proportions. Do not set a docked window's size with ImGuiCond_Always
per frame; it fights this.

## The home planet is the parent

scene.cpp: one Planet object carries `planet.home`; scene_init_builtins
makes it first (index 0) and parents the terrain, the water, the atmosphere
and the surface layers under it; scene_ensure_home_planet does the same for
a project saved before it existed. `scene_planet_indices()` lists the
planets in the sky *without* the home one (the planet renderer and picking
must never draw the world as a globe); `scene_home_planet()` finds it;
`scene_planet_of(object)` walks up to the nearest planet. The world's ground
is `scene_surface_layers(-1)`, which includes the home planet's children as
well as root-level surfaces. A tile under a planet in the sky is not placed
(nothing draws it there yet) - app_place_settings switches placement off.
The home planet's radius is render_settings().planet_radius, mirrored into
its PlanetData.

Curvature is per view: `view_planet_radius(rs, vc)` is the radius every
pass draws with - the setting for a camera view or a view with `curved` on,
0 for a free view. A new pass that reads rs.planet_radius for geometry
reads that instead; the sky and the fog keep the setting (they are not
distortion).

## The world's shape

studio/world_shape.hpp: the home world is a globe, a ring (a cylinder, flat
along the tile's z) or either with the ground on the inside (RenderSettings
world_shape / world_inside / world_width / world_sun_inside), and every tile
and surface layer has a `side` - the world's own face or the other one.
gpx::planet::Shape {flat_x, flat_z, inside, flip} is the maths, in
`sphere_place` / `sphere_frame` (engine/gpx/planet_math.hpp) and their GLSL
twins in planet_shaders_common.cpp: PL_SPHERE_FN reads `u_world_shape`, a
vec4 whose all-zero - the value a uniform never set reads as - is the globe
every scene had, and the globe placement is bit-identical to the old one
(tested). Every program that places geometry on the world uploads it next
to u_planet_radius: `upload_world_shape(prog, tile_shape(RS))` in the
terrain and water passes, per face in infinite_draw. `flip` is the other
face of the *same* shell (h -> -h, up -> -up), never a second sphere; the
face that faces the centre is `shape_faces_centre` (inside xor flip).
Culling (terrain_cull.cpp and the TCS) swaps the box for a flip and adds
the curvature term with the face's sign.

An inside face (a ring, a Dyson sphere, a hollow globe's crust) needs the
far side: infinite_draw draws the surround once per face that has layers
(upload_layers / upload_terrain_xform_inverse take a side) and, for an
inside face, a second grid - the shell (`u_shell`) - which VS_INF spreads
round the whole shape and FS_INF shades per pixel. Its relief is
PL_SHELL_FN's pl_relief_w: the layers sampled by direction from the centre
or the axis, band-limited by distance, blending into the tile-scale
surround over 29..120 tiles; its palette variation is by direction too (a
grain scaled by distance smeared into stripes along the view); its fog is
two slant columns, the camera's layer and the far side's, because
fog_terms's world-y profile reads R up there as no air. The shell discards
within 29 tiles of the tile (the surround has the vertices there) and
beyond a ring's world_width. With world_sun_inside the sun direction is
(0,1,0) at the tile (compute_sun_dir), per pixel toward the centre or the
axis on the shell (`u_sun_mode`, pl_world_up_at), the day factor is 1 there,
and a sun body is drawn at the centre (renderer_scene.cpp); the sky's
`u_space` is at least 0.75. view_planet_radius curves every view of an
inside world, and the far plane reaches 2.4 R (renderer_camera.cpp).
Placement reads the layers of the tile's own face (planet_home_layers(side))
and its key mixes the face. Not done: shadow squares (a ring's night), a
Dyson sphere's outside from space, planets in the sky with these shapes.

**The world is a body, and the air lies on it.** `Shape::thick`
(RenderSettings world_thickness, `u_world_thick` uploaded by
upload_world_shape) puts the other face at its own radius - R - thick for
a globe's crust, R + thick for a ring's outside, -thick for a flat world's
underside - and `thick` below the ground; 0 leaves every number bit for
bit. The third shape is WORLD_FLAT (Shape flat_x && flat_z): a plane
`world_width` across cut to `world_outline` (disc or square) in FS_INF,
with the far grid spread flat when it is wider than the surround. A thick
ring or flat world (`world_has_body`) draws its other face even with no
layers on it - a bare crust: u_base 0, no water, no fractal grit, which on
an unlit flat face was the only thing there and read as a pattern - and
the rim wall between the faces (studio/planet_rim.cpp: the far grid
mapped along the edge, its top the ground's own relief there, its bottom
the other face). Culling shifts a flipped face's box by the thickness,
flat views included.

The atmosphere is not measured from world y any more:
`gpx::planet::world_alt / world_up` and their GLSL twins `pl_world_alt /
pl_world_up` (PL_SPHERE_FN) give a point's height over the world's
surface and the surface's up there. The sky pass uploads the world's shape,
the view's curvature (`u_world_r`) and a ring's width; a cloud layer is the
band of altitude [alt, alt + thick] - `layer_span` intersects the ray with
the two shells about the centre or the axis, cut to a ring's width, and
falls back to the two planes on a flat world, exactly as before - the
density's height fraction, the far-to-near ordering, the light march (a sun
inside shines from the axis) and the cloud shadows on the terrain all read
`pl_world_alt`. `g_sky_up` in SKY_FN is the up `sky_color` grades against:
(0,1,0) in every program (dot with it is dir.y to the bit) and the
surface's up under the eye in the sky pass, so a ring's sky stands over the
ring. Water needed nothing: it was already placed with the tile's face.
Placement gained two join modes (place_mode 3 clip low, 4 clip high): a
hard max/min against the planet's ground, the material weight fading over
`presence` either side of the crossing (tests/cpp/test_planet_place.cpp).

## The air is a layer, and beyond it is space

`RenderSettings::atmosphere_height` (tile units; the AtmosphereSettings
node's `height_km`, set_sky `height_m`) is how high the air reaches over
the world's surface. The sky pass (`atm_path`, shaders_sky.cpp) measures
how much of each ray lies in that band - two shells about the centre or
the axis, cut to a ring's width and a flat world's outline, a slab on a
flat view - and composes `space * vis + sun * T + sky * scat`: the air's
in-scatter grows with the path, what it lets through is deep space, and
stars fall away with the sky's own brightness (outshone, not hidden).
The sky's horizon haze is scaled by the same path, so nothing hazes a
ray that meets no air. Height 0 is the old rule (`u_space` by the
camera's distance from the tile). A view with scene_camera -2 looking
through the active camera is a camera view (`view_active_camera`), so
captures and the default viewport curve - they used to draw R = 0, which
is why every capture through a camera was flat.

Deep space is studio/shaders_space.cpp (`space_color(dir)`), spliced
through SPACE_FN_PLACEHOLDER and fed by renderer_space.cpp: the star
field (two cube-mapped grids, two slots a cell so no lattice shows, a
steep magnitude law, gaussian spots the size of a pixel), the galaxy
band (a great circle from a pole heading and elevation, its core turned
along it, fractal structure, dust lanes) and the nebulas - Nebula scene
objects (scene.hpp NebulaData; a Nebula node drives one the way a Planet
node drives a planet): emission cloud, dark cloud, spiral galaxy,
elliptical galaxy, planetary nebula, eight at most in the sky pass. A moon
is a planet with crater layer type 4 (`pl_craters`, CPU and GLSL twins),
no sea and no air; an airless planet is shaded as bare rock (FS_PLANET
`u_atmo <= 0`). The far shell now draws the outside face too once the
eye is a tile up (planet_renderer.cpp `aloft`): the world from above and
from space, and the surround no longer ends in a band at the horizon;
its cloud band (FS_INF `u_fc_*`) samples the sky's own shape texture at
the cloud altitude over the far surface, so a ring's far side carries
clouds. Ops: add_nebula/set_nebula/add_moon/set_space, each an MCP tool.

Any object can be deleted (`scene_delete_subtree`, the tree's Delete, the
delete_object op): the passes skip tile 0 when no Terrain object exists,
and nothing else may assume a builtin is there.

## More than one terrain tile

studio/terrain_tiles.cpp. The renderer's terrain state (tex_height,
tex_albedo, tex_place_w, tex_patch_bounds, cpu_height, cpu_patch_bounds,
hm_w, has_albedo, has_place_w, g_terrain_mean, RS.matp and the eight
material scalars) is tile 0's - the first Terrain object in the scene. Every
further Terrain object keeps the same set in a TerrainTileGpu, and a pass
draws it by `TileSwap swap(k)` around the draw it always made (pass_terrain,
pass_shadow, pass_water, renderer_pick all loop this way). Rules:

1. A new pass that draws "the terrain" loops `terrain_tile_count()` with a
   TileSwap, or it draws tile 0 only. A new global that the terrain draw
   reads goes into TerrainTileGpu and both halves of TileSwap, or every
   tile draws with tile 0's value.
2. A tile is a Terrain object whose driver_node is its own TerrainOutput
   (component_add.cpp `scene_add_terrain_tile`). Tile 0 takes its driver or
   else the first TerrainOutput; `terrain_tiles_bind` runs on every upload.
3. app_upload_tiles.cpp places the extra tiles: same PlaceSettings
   (`app_place_settings`), their own material, no object imprint. Grounding,
   imprint and probe_height read tile 0 only - by design for now.
4. The surround cuts a hole under every visible tile (tiles_dout /
   tiles_inside, arrays of up to 8 inverse transforms) but blends its height
   against tile 0's heightmap only; the others meet it through their own
   placement feather.
5. `terrain_xform_current()` is the tile being drawn (TileSwap sets it) and
   tile 0 between draws; anything about "the terrain's transform" outside a
   draw means tile 0, and a selected tile's own transform is
   `terrain_xform_of(sc.objects[sel], hs)`.

## The join between the tile and the surround

The planet surround (studio/planet_shaders.cpp VS_INF/FS_INF) is the tile's
continuation, and everything that would read as a seam is matched there
rather than hidden: its octave count is capped at the tile's baked
resolution (`u_tile_octf` = log2 of the heightmap size) within the 0.35-tile
blend ring and rises beyond it; its normal is mixed from the tile's own
heightmap over the same ring; the tile's fractal micro-relief (FRACTAL_FN,
`u_frac_amount/u_frac_scale`) is added in both stages with the same
function in the same tile units; the albedo is borrowed through the tile's
inverse transform; and the water belongs to neither - it is one surface
drawn over both (see "The sea is one surface" below). Change the tile's look
and the surround must change with it, or the border comes back.

The colour join is the tile's, not the surround's: planet_place.cpp
publishes its blend weight (`PlaceResult::weight`, uploaded as
`tex_place_w`, unit 12) and the terrain fragment shader mixes the material
toward the planet palette by it across the skirt. The surround borrows
nothing from the tile - its ring is 0.06 tile widths, for the last float of
mismatch only. A wide ring that extruded the tile's edge row (height or
colour, even from a coarse mip) painted stripes; do not bring it back.

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
   corner is ever below the surface unless the user sinks it - the surface
   the viewport draws, micro-relief included, at every octave so a seat
   never follows the camera. The imprint moulds the heightmap and the relief
   rides over the mould wherever it is, so the relief's lift of the seat
   (`ground_relief`, runtime only) is kept from the node the way
   `ground_sunk` is, taken off the base in `footprints_text`. Handed the
   lifted base, the mould rises to it and lifts the relief with it: a mound
   under every object, and the object no better seated.
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

## Plants

The Plants workspace (studio/panel_plants.cpp) is the plant library today
and the plant editor's home later (docs/roadmaps/plants.md).

1. **Three shelves, one manifest** (studio/plant_library.hpp): the built-in
   kinds (scene_plants.cpp), the free CC0 plants `orchestrator/plant_fetch.py`
   downloads from Poly Haven on request, and a person's own models recorded
   where they lie. Each downloaded or recorded plant is a folder with a
   `plant.json` under `<data_dir>/library/plants/<source>/<id>/`.
2. **Bundle nothing from a proprietary plant tool.** The dedicated plant
   modellers' licences forbid redistributing content derived from their
   assets and building asset collections for scene-assembly tools, and their
   formats are undocumented and node-locked. No reader for them, no presets
   recreated from their catalogues, no product names in shipped UI; a user's
   own export comes in through the generic importers and `plant_record`.
3. **A file of plants is split, not placed whole.** Poly Haven ships sets -
   fern_02 is four ferns a metre apart, grass_bermuda_01 twenty-one tufts -
   so `split_variants` writes one glTF per plant sharing the set's buffer:
   roots whose footprints overlap are one plant's parts (a pachira's bark
   and leaves), `_LOD<n>` roots keep their finest. A cut-out's colour is a
   JPEG with its alpha in a separate map; `merge_alpha` makes the PNG the
   viewport and the engines cut on. The CDN answers some thumbnail requests
   with WebP, which stb cannot read: `normalise_thumb`.
4. **An object's height is heightmap units.** `scene_object_matrix`
   multiplies `pos[1]` by `height_scale`; `renderer_ground_under` answers in
   world units. Divide - taken as it came, every plant stood a hundred
   metres under a 130 m hill.
5. **Stand on the ground the viewport draws**: the heightmap *and* the
   fractal micro-relief the terrain shaders lay over it, metres either way
   on the default tile (`fractal_detail`). `terrain_relief.hpp` is the CPU
   twin of `gp_detail` in the same float arithmetic; placement, scattered
   copies (`scene_rebuild_scatter_instances`) and camera-driven populations
   add `relief_at(..., RELIEF_NEAR_OCTAVES)`. The render bake carries the
   octaves its grid resolves (`relief_octaves_for_grid`), and the export
   moves anything standing on the viewport's ground onto the baked
   triangles (`seat_offset`, `baked_tile_height`) - a ten-metre grid is not
   the viewport's surface, and a fern on the one was buried in the other.
   The CPU's other ground queries read the same surface:
   `renderer_ground_under` takes an octave count, the orbit pivot settles
   at the count its view draws there (`relief_view_octaves` with that
   view's `g_view_tri_k`), clicks and the sculpt brush walk onto it
   (`ground_march.hpp`), the grounded lock seats on it, and `probe_height`
   reports `drawn_m` beside `placed_m`. Change `gp_detail`, or
   terrain_place's octave choice, and change the twin.
6. **A plant is a component**: a Primitive node for a built-in, an
   ImportObject node for a model, driving the object, at its real size
   (`unit_m` metres per file unit over `terrain_size_m`), its own colours
   and pictures and no grey material over them.
7. **Heavy models load off the main thread from the panel**
   (`plant_add_async`, finished by `plant_place_service`); a script's
   `plant_add` waits for its object, with the hang watchdog paused around
   the read. The jacaranda is two hundred megabytes of scan.

8. **A species is a graph with a sink.** Plant links (`DataType::Plant`) run
   from a part to the part it grows on, so `PlantSpecies` evaluates last and
   an edit anywhere in the species reaches it (`plant_root_of`,
   `plant_subtree`). Only the root computes; every other plant node is data
   it reads, the way a MaterialLayer is data under a MaterialOutput. The mesh
   goes into the mailbox `plant_mesh_store` under the root's id, the studio
   picks it up after the evaluation (`scene_plants_species.cpp`), and the
   object is created, adopted by name and never deleted like every other
   node-driven object.
9. **One seed is one plant, everywhere.** Every draw hashes (seed, instance
   id, parameter key, draw index) - never a clock, a counter, a global or a
   thread id - and an instance's id hashes (parent id, node id, slot, index).
   That is what lets a second leaf node be added without reshuffling the
   first one's leaves, and what lets `plant_fingerprint` say that a rebuild
   would change nothing.
10. **The walker decides whether and where a part grows; its builder decides
    only what it looks like** (`plant_eval_walk.cpp`). Levels of detail,
    presence over the seasons, the selectors, the loops, the caps and which
    child fills a socket live in one place. New behaviour of that sort
    belongs on the walker's side of the line, or every part has to learn it
    again.
11. **Every number on a plant node is a Random attribute and every curve a
    Curve attribute** (`AttrType::Random`, `AttrType::Curve`), read through
    `ParamReader`, so spread modes, scopes, the two shaping curves and a
    field-driven override behave the same on every part. Parameters are
    declared once with their tooltips in `engine/plant/plant_schema*.cpp`;
    the nodes, the Properties rows, serialization, `set_attr` and the
    assistant all read that one table, and a key is permanent once shipped.
12. **Material slots are fixed**: 0..3 are the `material` .. `material 4`
    ports, 4 the cap, 5 and 6 the blades. `material_for` returns a default by
    the part's kind when nothing is connected; a part never makes a material
    of its own.
13. **The wind is baked, not simulated**: four numbers per vertex (phase,
    bend, flutter, height) and one function that moves them, whose GLSL twin
    (`plant_wind_glsl()`) is spliced into the mesh and depth shaders. Change
    one, change the other - a plant that sways differently in the render than
    in the viewport is the bug the pairing exists to prevent.

14. **A plant's mesh is per primitive; a scene part is per material.** A
    mature tree grows hundreds of thousands of `PlantPart` runs, one for each
    thing it grew, and the exporters need every one of them by name. The
    scene object does not: `mesh_to_object` gathers the triangles by the
    material they wear, so one picture is held and one texture uploaded per
    material. Copying a leaf picture per part ran the app out of memory on a
    24 m oak.
15. **A picture made from rules carries its own colour.** The bark is drawn
    with the material's colour between its cracks and the leaf with its own
    green, so the material's colour must be left at white when a picture is
    attached. Multiplying it in again squared a mid-brown bark into black.
16. **A plant is full-grown long before it dies.** Growth is measured against
    a little under half the lifespan (`plant_maturity`), not the whole of it -
    a 140-year oak of a 600-year species is a whole tree, not a quarter of
    one - and the size that maturity implies is `plant_size_at`. The engine
    and the archetype builders read the same two functions, because the
    builders cancel exactly what the engine applies.
17. **The height in a description is the height that stands up.** What an
    archetype's botany adds up to is only roughly the height asked for, and a
    gnarled tree wanders as it climbs. `fit_to_height` grows the species at
    the coarsest meshing and corrects the root's scale by what it made. The
    placement's size belongs to the object in the scene and must never be
    written onto the species' own scale.

18. **A part must not come away from the part it grows on.** The wind moves
    a vertex from four baked numbers alone (phase, bend, flutter, height), so
    two vertices in the same place that disagree about any of them are pulled
    apart. The phase is a function of distance out along the wood
    (`wind_phase_at`) and nothing else; `bend` and the phase are carried on
    the parent's axis samples and copied by the child from the socket it
    lands on, interpolated between samples exactly as its position is - never
    worked out a second time; and `flutter` is zero where a part meets its
    parent. A leaf that drew its own phase, with a flutter floor under its
    hook, shook 92,000 leaves off their twigs.
19. **Flutter is baked in metres.** A builder writes the amplitude it wants -
    a third of a leaf's own length - and `plant_build` divides it into the
    wind function's units once the plant's height is known, the same way it
    finishes the height weight. Baked as a bare 0..1 weight the wind scaled
    it by the whole plant's height, smearing every leaf on a 22 m oak over
    22 cm: twice its own length, and the crown drew as green streaks.
20. **A leaf is the size the species says, wherever it grows.** Wood inherits
    its parent's scale, because a twig is smaller than its branch; a leaf, a
    flower and a fruit do not, because an oak leaf is eleven centimetres on a
    low bough and on the highest twig alike. Inheriting it put a leaf three
    levels down at 0.62^3 of its size.

21. **A wood is a few individuals and many copies.** One species grown a
    handful of times, each with its own seed, bound to one population layer
    that splits its points among them by the species index every point
    already carries (`SceneObject::scatter_species`). Scattering a single
    plant gives clones the eye picks out at once; a mesh per tree is
    unaffordable. `plant_forest` is the only thing that should make one.
22. **A branch is as thick as what it feeds** (`pipe_ratio`): n branches of
    ratio r satisfy n*r^2 = 1. A fixed share per level gave a 28 m pine
    sixteen-centimetre twigs, as thick as its needle sprays were wide.
23. **A cut-out picture must cover enough of its card to read as what it
    is.** A single needle drawn across a whole card is 6% opaque and 94%
    hole, and a pine built from those is bare wood at every distance. Fifteen
    per cent is the floor the tests hold; a needle spray gives 37%.
24. **A part with no picture must not outweigh the tree.** It is the one
    thing that paints a flat colour, so it decides what the plant looks like
    from any distance: a pine's cones reached 47% of its triangles and turned
    the whole crown brown.
25. **A rasteriser claims the depth buffer only for what is actually there.**
    Writing the depth before testing a cut-out punches the picture's every
    transparent gap through everything behind it - and foliage, being thin,
    is mostly gap. The same rule makes a leaf lit from behind bright, not
    black: `double_sided` parts are lit by |N.L|.

26. **One wind blows over the whole scene** (`RenderSettings::wind`). The
    clouds drift with it, the sea is raised by it, the fog moves with it, the
    plants lean and gust with it. Each keeps only what is properly its own -
    how long the sea has been under it, how flexible a plant is, how much
    faster the air moves aloft - and each can be cut loose. A landscape where
    the cloud shadows cross one way, the trees lean another and the waves run
    a third reads as assembled however good each part is.
27. **Anything that drifts is given a distance, never a speed.** A gusting
    speed multiplied by the clock teleports the whole field the moment it
    changes. `wind_advance` integrates the drift a frame at a time and the
    clouds and fog are given that, so a gust surges and never jumps. The sea
    is the exception in the other direction: it takes the mean and not the
    gust, because a sea state is built over hours and rebuilding the wave
    spectrum every frame would cost more than the water pass.
28. **A leaf is a sheet, not a surface.** Which way its normal faces is an
    accident of how the card was built, so foliage is lit from both sides
    (`u_leaf`: N turned toward the viewer) and lit through from behind. Lit as
    a solid surface, every leaf that turns past the sun snaps to black and
    back - half of what reads as flicker in a moving crown.
29. **A cut-out edge is resolved to a pixel, not tested at a half.** A leaf
    that moves drags its picture across the pixel grid and each pixel flips as
    the texel it lands on crosses the threshold. Dividing by `fwidth(a)` makes
    the test a sub-pixel coverage, and the edge lands in the same place
    however the leaf is sampled - the other half of the flicker.
30. **A branch is as flexible as its thickness makes it.** A cantilever's
    stiffness is EI with I ~ r^4 and the wind's push goes with its diameter,
    so how far it swings relative to its own length goes as the slenderness
    (L/r) cubed, and along it the shape is the cantilever's own - which leaves
    the clamped end with no slope at all. `p^1.5` rose vertically out of the
    joint, so a branch appeared to shear away at the point it is fixed to.
31. **Plants do not move in a window unless that window says so**
    (`ViewConfig::animate_plants`, off by default). A swaying crown is the one
    thing on screen that never settles, and placing a tree or framing a shot
    is easier against a still picture.
32. **A population that is painted must be bounded by its paint.** An
    unbounded layer is realised cell by cell in world space around the camera
    and cannot see a mask painted on the tile, so an unpainted brush places
    thousands of instances anyway. `unbounded` is for a population meant to
    cover everything; a brush says where by being painted.

33. **Where a thing goes is a rule about its GROUP, never about the thing**
    (engine/gpx/ecology.hpp). "Moss gathers on stone and nowhere else" is a
    fact about moss; a particular moss model is a member of that group and
    inherits it. Written per model, every rule would have to be restated each
    time the library grew - written per group, a model added next year is
    placed correctly in every biome, including ones written before it existed.
34. **Tier is the order the ground assembles, and it cannot be fudged.** What
    lies on bare earth is placed first, then what stands up out of it, then
    what lives among that, then the cover, then the litter over everything. A
    layer can only be placed in relation to what is already there, so moss
    laid before the boulders has nothing to gather on - and that is exactly
    what makes assembled scenery look assembled. A rule about a group of a
    higher tier than its own is a modelling error; the ecology tests refuse
    it.
35. **What a layer is placed against is not what precedes it.** A population
    interacts with the one population wired into its "below", so that choice
    decides which rule can be expressed at all. It is whichever already-placed
    group the strongest rule names - moss against boulders, litter against the
    tree that dropped it, fungi against the dead wood - not whatever happened
    to be built last. Conflating order-of-creation with choice-of-host put the
    moss on the ferns.
36. **Affinity and repulsion say different things and the interesting cases
    need both.** Affinity is a gradient (more of them the closer you get);
    repulsion is a hole (none at all inside the radius); NEGATIVE repulsion is
    "only inside it", which is how a group sits ON another rather than merely
    near it. Bracken near the trees but not in their shade is both at once.

37. **A leaf is a surface, not a picture.** What makes foliage read as a leaf
    rather than as printed paper is not the colour - it is the midrib standing
    proud with the blade dished either side, the veins ridged across it, and a
    waxy cuticle that is glossy on the blade and dull along the veins and at a
    dry rim. None of that is in a colour map, so a leaf picture comes with a
    normal map and a roughness map (`plant_texture_leaf`), the part carries
    them, and the shader lights by them. The bark had a normal map from the
    start; the leaves went without one for far too long.
38. **A part's material terms belong to the part.** Roughness and translucency
    come from the plant material the part wears, not from a constant in the
    renderer or from whatever the material system last happened to set: a
    laurel is glossy and nearly opaque, a beech in spring is matte and glows
    through, and one number cannot be both.

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
8. **Plants are primitives, not files** (studio/scene_plants.cpp): `pine`,
   `juniper`, `palm`, `fern`, `grass`, `bush`, `boulder` are built from the
   kind like the cube, deterministically, with a bark part and a foliage part
   carrying their own colours - so a plant gets **no** grey component
   material, its object colour and node colour are white, and it comes at
   its real size (`scene_plant_size_m`). Everything that regenerates a
   primitive calls `scene_primitive_build`, which fills verts, uvs and parts;
   `scene_primitive_verts` alone loses a plant's colours. Sheets of leaf are
   one-sided: FS_MESH mirrors a normal that faces away from the eye across
   the view plane (continuous at a closed mesh's silhouette), so a leaf seen
   from below is lit, not black. A plant's **seed rides in its pseudo-path**
   (`primitive:pine#12`, the Primitive node's Plant seed): the object rebuilds
   when it changes and a saved scene grows the same tree; 0 is the tree it
   always was.
9. **A cut-out picture's mipmaps keep its coverage** (alpha_mips.hpp, part
   textures and baked cards): each level's alpha is scaled until the share of
   texels past the cut matches the base level. Plain mipmaps averaged the leaf
   into the clear texels round it and a far tree thinned to twigs.
10. **An imported mesh shades smooth where it is smooth** (`mesh_to_object`):
    a corner takes the area-weighted normal of every face meeting at its
    **position** (a file splits vertices at texture seams), unless that leans
    more than 50 degrees from its own face - a box keeps its edges.

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
   and are widened by two texels because the B-spline relief's bilinear taps
   reach that far. A bound that under-covers does not cost performance — it puts a
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
   Animation, Render, Plants (Plant - no node has it yet; the plant editor's
   will). Workspace numbers are historical - 4 is "all domains" and saved
   editor layouts carry them - so new workspaces append; never renumber.
   `WORKSPACE_ORDER` is the bar's order and `WORKSPACE_ORDER_COUNT` its
   length: loops over the bar use the count, never a literal.
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
   The other way round, **deleting an object deletes the node that would
   create it again** (Primitive, ImportObject, LightSource, SceneCamera,
   Planet, Nebula, InfiniteTerrain), in the same undo step: every delete -
   the Objects tree, a viewport's Delete key, `delete_object` - goes through
   `scene_delete_objects` (panel_scene_dnd.cpp) over
   `scene_delete_with_drivers` (scene.cpp, tested in undo_tests). Deleting
   the object alone brought it back on the next evaluation. A viewport's
   Delete spares `builtin` objects (the world, its terrain, sea, air, sun);
   the tree does not. Nothing is selected after a delete, so a second press
   cannot take whatever slid into the deleted index.
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

## The terrain's detail in the viewport

What the node graph shows and what the viewport draws have to be the same
ground. Four rules came out of "the node looks eroded, the viewport does not,
and the rims are jagged". (The scene that reported it had no imprint at all:
its viewport was pinned to the node before the erosion - see "Modes you
cannot see". Check `view_node` before anything below.)

1. **Only the imprint that feeds the output directly is lifted off the
   tile** (`final_imprint`, app_upload.cpp). Taking any TerrainImprint's
   input dropped every node after it - the erosion and Terrain Editor effects
   inserted before the output - from the viewport, while the output's own
   thumbnail still showed them.
2. **The relief is a cubic B-spline, in every pass that places the terrain**
   (HEIGHT_SMOOTH_FN, shaders_relief.cpp: the view's vertices and the shadow
   map's). Read bilinearly it is facets meeting at creases, and a rim drawn
   that way is a staircase however finely it is tessellated. `u_height_top`
   must be the tile's own last mip level, uploaded after its swap - a level
   past the texture reads size 0 and the surface goes NaN.
3. **The relief's level of detail follows the lens and the screen**
   (`relief_lod_k`): the mip whose texel is as long as a tessellation edge,
   times the dial - never a fixed multiple of distance, which put the whole
   tile at level 4 from the default camera. Vertex micro-relief stops at
   octaves longer than two triangle edges (`u_tri_k`); finer ones are the
   normal's. The fragment normal is differenced over a texel **of the level
   it reads** (`textureQueryLod`), or it shades as creased facets.
4. **The picture is anti-aliased** (renderer_fxaa.cpp) after `draw_scene`
   and before the lens pass, on 8-bit targets only - a float target is a
   render pass whose numbers must not be blended.
5. **Patch bounds reach two texels past a patch** (terrain_cull.cpp): the
   B-spline's bilinear taps read that far. One texel was right for plain
   bilinear and would cull ground the spline still draws.
6. **A released slider's edit is flushed once a frame, whatever panel is up**
   (`node_properties_flush`). The panel used to write its own node only while
   drawn: release a slider while the 256 pass held the graph, pick another
   node, and the full-resolution pass never came - the viewport stayed on the
   preview. While the heightmap on screen is smaller than the graph's
   resolution the view says so ("Preview at N px").
7. **The sun's shadow bias comes from the map's texel**, slope-scaled, plus
   the relief only the view draws (micro-relief, a material's displacement) -
   `u_shadow_geom`. A fixed 0.004 of the depth range was ~88 m on a 5 km tile
   and no gully or crater wall could shadow itself.
8. **`u_frac_gain` in FRACTAL_FN reads 0 when a program never uploads it**, and
   `gp_gain()` turns that into the 0.5 it always was: the tile and the surround
   draw the same micro-relief whether or not a program knows the setting.

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

## The sea is one surface

The water used to be two things: a plane under the tile's footprint,
blended over the tile, and the surround flattening its own ground to the
water level and tinting it. Two programs, two looks, and the tile's square
stood in the middle of every lake. Now (studio/renderer_water.cpp,
shaders_water.cpp, water_surface.*, engine/gpx/water_waves.hpp):

1. **One clipmap over the world.** Rings about the eye's point over the
   flat world (`water_eye_param`, the inverse of `sphere_place`), each
   level centred on a point snapped to twice its own cell, placed with
   `pl_sphere_place` so the sea follows the curve. A level owns what lies
   within `WATER_OWN` cells of its centre and no finer level owns; the
   fragment stage discards the rest by the point's *resting* position, and
   a ring leaves `WATER_HOLE` cells open. `test_water.cpp` `test_clipmap`
   proves the partition and that at every seam the fine side is fully
   morphed and the coarse side not at all - change a constant and it says
   so (224 hole and 2860 seam failures when they were loosened).
2. **Never take screen derivatives on a clipmap.** Where a level morphs its
   triangles thin to slivers, and `fwidth` there is nonsense: every sliver
   drew a dot along the level's rim. The pixel's footprint on the water is
   `dist * u_pixel_k / |V.up|` - measured at the far plane on the CPU,
   because at the near plane the two points are too close for a float.
3. **The surround no longer flattens to the water** (u_shell 0): the sea
   needs the bed there to see through. The far shell still flattens and
   shades its sea with `water_far_color`, the limit the full shading
   reaches when every wave is under a pixel, so the two meet at the 29-tile
   square - the water stops on that square (`u_reach_square`) exactly where
   the shell starts, or it fades with the surround's horizon when there is
   no shell (`surround_far_shell`, world_shape.cpp, is the one decision).
4. **The waves are a CPU truth with a GLSL twin.** `gpx::water::build`
   (Gerstner waves from the Pierson-Moskowitz spectrum of a wind) and
   `evaluate`; WATER_WAVES_GLSL is line for line. `verify_field_gpu`
   measures them (displacement 4e-5 m, slope 8e-5 at 600 m). Phases are
   rebased in double to a quarter-tile origin (`rebase`), so a 4 cm wave
   keeps its phase kilometres out. The offline render bakes its sea from
   the same truth (render_water_bake.cpp), about the camera, with the
   roughness of the waves its mesh cannot carry.
5. **The water composites what is behind it, in light.** The scene's depth
   and colour are copied before the pass; the colour is un-developed
   (gamma, ACES, saturation, grade, exposure undone) and the transmitted
   light added to what the surface sends, then developed once. Blending in
   the display's encoding made every shallow glow cyan.
6. **Depth-buffer measurements need a trust.** A lake a few metres deep at
   thirty kilometres sits inside one 24-bit depth step: measured, it reads
   as no water and turns see-through and white with foam. `grain_m` is the
   step in metres; past it the thickness counts as deep, and coast foam's
   probe straight down (`ground_below_m` - vertical, because a view-ray
   depth widens foam into a band at a grazing view) gives way.

## What you see is what you get

The viewport and the offline render have to be one picture. Four things had
to agree, and none of them did:

1. **The sky's light.** A diffuse surface takes the sky's radiance
   integrated against the cosine from its normal; the viewport used the
   average of the two sky *colours*, which is between 0 and 1 where the real
   number is several times larger. `sky_light.cpp` measures it - 512 rays
   through the same sky and cloud march the viewport draws, in a mapping
   whose plain average *is* the integral (`u_panorama == 2`: sin^2 of the
   elevation spread evenly, so there are no weights to get wrong in the
   reduction). Re-measured only when the sky changes, so a still frame pays
   nothing. **Every shader that lights a surface with skylight reads
   `u_sky_light`** - there were eight copies of the old expression, and
   eight places to forget.
2. **The exposure.** A camera view is developed by its own aperture, shutter
   and ISO and by its film stock; the export sent the bare scene exposure,
   so a render was developed differently from the picture it came from.
3. **The panorama's convention.** The sky HDR is read back as an environment
   map, and ours was written with the azimuth the other way round, so the
   render's sky was the viewport's sky turned a quarter turn - the camera
   looked at cloud and the render put clear blue there. It is
   `atan(d.x, -d.z)` now, which is both the engines' convention and
   `bd_uv`'s own for a lat-long backdrop.
4. **Where the panorama is shot from.** A cloud layer is at a finite
   altitude, so an environment map is only right for the point it was taken
   at. A camera render shoots it from that camera's eye.

What is left is honest: the path tracer has interreflection and the viewport
has none, so a bright landscape bounces light onto itself in the render and
not in the preview. Measured on the default scene, the two frames now sit
about 7% apart where they were a different picture entirely.

## A render is the viewport's world, not the graph's heightmap

The offline engines get meshes and textures, never our shaders, so anything
the viewport draws in a shader has to be baked for them
(studio/render_terrain_bake.cpp). What used to go out was the graph's raw
heightmap over one flat unit square - no placement, no surround, no
curvature, no albedo - which is why a render through a camera and the
viewport through that same camera were two different pictures.

What goes out now, all through the CPU twins the placement already uses:

- **terrain.obj** - the tile, placed on the planet (`planet_place_tile`),
  bent by the world's shape (`sphere_place`).
- **surround.obj** - the ground beyond it, out to 30.5 tiles, on the same
  cubic vertex concentration the surround shader uses, so both sample the
  relief in the same places.
- **water.obj** - the sea on the world's curve. A flat rectangle is right on
  a flat world and wrong on a round one: the land falls away with the curve
  and the plane does not, so it rises through the ground and draws a bright
  band along the horizon.
- **albedo.png / surround_albedo.png** - painted by `gpx::planet::palette`,
  the CPU twin of PL_PALETTE. Baked linear and written gamma-encoded,
  because the engines sRGB-decode an 8-bit texture on the way in.

Two rules for this path. **The palette's twin is checked, not trusted**:
planet_gpu_check.cpp runs the real shader over a grid and compares, exactly
as it does for `heightf`; if you change PL_PALETTE, change
gpx/planet_palette.hpp and watch that line say AGREE. And **watch the size**:
six fixed decimals over half a million vertices is a hundred and twenty
megabytes of OBJ per frame - five significant digits is a quarter of a metre
on the default world and a quarter of the cost.

What still differs, and is not a bug: the path tracer lights the ground from
the whole sky dome, and the viewport multiplies one averaged sky colour by
`ambient_intensity`. The physical answer is several times brighter. The two
will not agree until one of them changes, and changing either changes how
every existing scene looks.

Five faults made the rendered textures "all mixed up" (grey and green bands
over the mountain, black edges at the water), and each has a rule now:

1. **The surround leaves the tile's square out** (`keep` mask in
   `render_bake_terrain`), as FS_INF discards it. Kept, it lay a hair from
   the tile's surface and the path tracer picked one or the other per pixel.
2. **Palette rows are written in grid order.** The mesh's v is `1 - b` and
   the engines read v up from the image's bottom; writing rows upside down as
   well mirrored the palette north to south.
3. **The tile's colour is the viewport's**: `app_terrain_albedo()` (what
   app_upload.cpp picked, the assigned material first) through the
   material's tint/gain/saturation/blend, mixed toward the palette by the
   placement weight - never "the last node in the graph with a texture".
4. **`Ground::at` applies the tile's `scl.y` and `pos.y`**, as every viewport
   pass does.
5. **Engines clip at 1e-5 units** (Mitsuba `near_clip`, Blender
   `clip_start`): a unit is a tile, and the defaults cut away the first 50 m
   and 500 m. Terrain and surround BSDFs are `twosided`.

And the surround grid gathers about the **camera's** ground point (as the
sea's rings do), not the tile's centre, or a tile's width away its cells are
hundreds of metres across. A scattered copy's row in scene.json has twelve
numbers; read them through `orchestrator/render_instances.py`, never by
unpacking five.

Every engine builds the same scene (render_engines.py Mitsuba,
render_cycles.py, render_luxcore.py): terrain and surround with their
pictures, the exported sea mesh, scene meshes **by their parts**.

- **A mesh goes out one OBJ per part** (`mesh_N_part_K.obj`, `parts` in its
  scene.json entry) with the part's colour, picture and a grey alpha mask:
  every engine takes one material per shape, and a plant's colours are its
  parts'. Faces are written **wound to their own normals** - Cycles shades
  from the winding and drew mis-wound lumps and trunks black.
- **Cycles works in one frame**: Blender's OBJ importer turns y-up into z-up,
  (x, y, z) -> (x, -z, y), so everything the script places goes through the
  same turn (`CONV`, `to_blender`), and a placed mesh's `matrix_world` is
  `CONV @ M`. It used to swap y and z - a mirror - and overwrite the imported
  turn, so the camera looked at a reflection and meshes lay on their sides.
- **The sky panorama, per engine** (measured with a marked panorama - red at
  our +x, green at +z, blue at -z - and a camera looking along each). Our
  longitude is `u = atan2(x, -z) / 2pi + 0.5`, row 0 up.
  - Mitsuba's `envmap` reads it without the half turn: `to_world` is a
    180-degree turn about y.
  - Cycles: the world mapping turns it **270** degrees about z. It was 90,
    which matched a Mitsuba that was itself half a turn out - both engines
    drew the sky behind the camera, and nothing compared either with the
    viewport.
  - LuxCore: `transformation = mirror_y * RotZ(90)`; its infinite light runs
    longitude the other way round, which no turn can undo.
- **LuxCore** (render_luxcore.py, pinned by tests/test_render_luxcore.py) is
  z-up like Cycles (`CONV`, `to_lux`). Its field of view spans the picture's
  **longer** side. A `distant` light's `direction` is the way the light
  travels (minus our sun direction) and its `color` is a radiance over a
  cone of half-angle `theta`: irradiance E needs `E / (pi sin^2 theta)`.
  `DefineMesh(name, points, triangles, normals, uvs, None, None)`.
  `pyluxcore.Init()` takes no Python log callback: with one,
  `WaitForDone` deadlocked - poll `HasDone()` instead.
- **Numbers into OBJ text go through `ObjText`** (studio/obj_text.hpp,
  `std::to_chars`), never the stream: under MinGW the stream formats each
  float through its printf, and one small scene of plants froze the window
  for seven seconds. A part's picture goes out once per content
  (`png_once`, named by a hash, fast compression) - it used to be compressed
  again at the slowest setting on every render.

## One palette, from the same numbers, on both sides of a tile's border

The tile (shaders_terrain_frag.cpp) and the surround (planet_shaders.cpp
FS_INF) are different programs painting the same ground, and every input
`pl_palette` takes has to be the same at the border or it draws the square -
a blend that is smooth in shape but discontinuous in colour is still an
edge. The inputs, and where each side gets it:

- **var**, the variation grain. It picks between grass and meadow outright
  and weights the forest, so it is the ground's colour, not a garnish. Both
  sides call `pl_palette_var(flat world xz)` - it lives in PL_PALETTE, with
  the palette, because the tile's program has no PL_FN to take `pl_vnoise`
  from. It carries its own hash under its own name for that reason: the
  programs that do have PL_FN would otherwise define it twice. The tile
  passes `v_wxz`, set in `terrain_place` after `tile_xform` and before the
  curvature - the same number the surround indexes by, transform and all.
- **wet**, the planet's valley floors and lake beds. The surround reads it
  from `pl_relief_w`; the tile cannot (no layer stack in that program), so
  the placement computes it on the CPU and it rides in the green channel of
  the placement texture beside the blend weight - `planet_place_rg` builds
  the pair, and both upload sites go through it so they cannot drift.
- **t**, the altitude. Still not exactly shared: the tile ignores `u_txi_y`,
  so a tile with a vertical scale or offset paints at the wrong altitude.
- **The lighting is not shared, so it is handed over instead.** The tile is
  Cook-Torrance with shadows, ambient occlusion, a specular lobe and a sky
  reflection; the surround is Lambert with none of them, and no choice of
  constants reconciles two different models. So the tile's `direct` and
  `ambient` are each mixed toward the surround's own expression by the same
  placement weight its albedo uses, and its reflection and translucency are
  scaled by it - the tile keeps its quality where it is the tile and lets go
  exactly where its colour lets go. The two terms stay separate through the
  mix, because the render passes want the direct and ambient shares apart
  (`aov_out`), and the fade goes **before** the point-light loop: a lantern
  by the tile's edge lights the ground beyond it and must not fade with the
  skirt.

Measured on the default scene, straight down on the tile, as how much
sharper the border column is than ordinary ground: 1.99x with none of this,
1.48x once the palette agreed, 1.28x once the lighting was handed over. What
is left is the tile's own normal against the planet's - the tile carries
fractal micro-relief the analytic relief does not - which shows most at a
grazing sun, where a degree of normal is worth several per cent of light.

A warning about measuring this. Two strips of ground either side of the
border are not comparable at a low sun: they hold different slopes, and at
8 degrees of elevation the slope decides the brightness, so the measurement
reports the terrain rather than the seam. It said the seam had got three
times worse when it had not moved. Measure the seam as a peak in |dI/dx|
against the same quantity on ordinary ground nearby.

## The ground has to end somewhere, and it must not be a line

Three things have to agree, or the surround ends on a hard edge against the
sky - the thing people report as "clipping":

1. **The view has to curve.** `view_planet_radius` (render_settings.hpp)
   returns 0 for a flat view, and a flat world has no horizon at all: the
   surround simply stops at its outer ring. Free *perspective* views curve
   by default now; the orthographic ones do not, and a camera view always
   does. Saved layouts wrote `"curved": false` into every view, so
   layout_record.cpp migrates version 1 files - a default that changed is
   not a preference anyone expressed.
2. **The far shell has to take over in time.** planet_renderer.cpp draws it
   once the eye is high enough that the horizon reaches past the surround's
   ~30 tiles. That height is `d*d/(2R)`, not a fixed number of tiles: a
   fixed tile up left a band of heights on every world larger than the
   default where the ground stopped short of a horizon still further out.
3. **What is left has to fade.** Where no shell stands behind it
   (`u_horizon`), FS_INF blends its last few tiles into `sky_color()` in the
   fragment's own direction. Fog used to be the only thing hiding that edge,
   so turning fog off exposed it. The fade is **radial**, not on the grid's
   own square - keyed on the square it draws one, which is exactly the trap
   the tile's border fell into.

## Smooth maximum, not a ramp

Wherever two surfaces meet along "whichever is higher" - TerrainClip's
flatten (nodes_sculpt_layer.cpp), a placed tile clipped against the planet
(planet_place.cpp) - use the polynomial smooth maximum, never a ramp under
the mark. A ramp from "untouched at the mark" to "flat a softness below it"
cannot be monotone: it starts and ends at the floor with the ground still
falling in between, so it turns round somewhere and leaves a ridge ringing
the flat, and the mark keeps its crease because nothing above it moves.
`smax(a,b,k)` is exactly `max` outside a band k wide, a parabola across it,
and `k = 0` is `std::max` to the bit - which is what lets the softness be
switched off and reproduce every older project.

Two things it needs watching for. It lifts the join by `k/4` even where the
surfaces merely touch, so **k must never exceed the depth of the cut**:
TerrainClip clamps k to `mark - min` (and `max - mark`), which is what keeps
a clip with its range wide open an identity and stops a default softness
quietly changing every project. And a tile that is simply higher in the
middle crosses the planet out at its own rim, where the border feather has
already taken the weight to nothing - so a test of the rounding needs ground
that rolls above and below the planet *inside* the tile, or it measures
nothing at all.

## The air, and the clouds on it

The atmosphere is an exponential density profile, not a slab
(shaders_sky.cpp `atm_path`): the ray's interval over the world is found
first, then integrated in eight segments by `seg_air`, which is the exact
airmass of a straight segment through exp(-h/Hs). `Hs` is
`atmosphere_height * atmosphere_falloff`, and the result is divided by `Hs`
so straight up from the ground is 1 whatever the falloff - existing scenes
keep their sky. A slab ends on a line, which is why the limb used to be
drawn rather than faded.

`world_slab` clips a ray interval to the world's own extent - a ring's
width along its axis, a flat world's outline - and the air, every cloud
layer and anything else that is a layer on the surface must go through it,
or it fills a sky that has no ground under it. It must be defined **before**
`layer_span` and `atm_path`: GLSL has no forward declarations, and a sky
shader that fails to compile comes out an even grey with no stars, which
looks like a lighting bug rather than a build one.

Clouds: `cloud_density` takes one coarse lookup that does two jobs - its red
channel opens and closes the cover the way a weather front does, and its
other three warp the shape lookup. The warp is the half that matters: the
shape volume tiles every 5.5 tiles and a sky is seen thirty deep, so
modulating the cover alone only gives a modulated grid. One fetch for both,
because the density is sampled six times a step (once forward, five toward
the sun). `cloud_volumetric` off replaces the march with a single sample on
the middle of the layer - a third of the frame cost.

**The clouds are one layer seen from anywhere** (shaders_clouds.cpp,
renderer_clouds.cpp). The march lives once, in CLOUD_FN_GLSL, and runs in
the sky pass for rays that reach the sky and in `pass_clouds` (after the sea)
for rays that end on geometry, cut at the scene's depth - so a cloud stands
in front of a mountain, under a camera above it, and over the world seen
from orbit. Rules that keep it one layer:

1. **No altitude gate.** `clouds_ok` is `clouds_on`; the old `eye.y < 3`
   switched the weather off as the camera rose and the far shell drew a flat
   stand-in (removed). Cost is bounded by the march itself: steps follow how
   much of the band a pixel resolves (`u_cl_pixel_k`), and empty-space
   skipping is off on a short march, or thin cloud turns to salt and pepper.
2. **Far lookups go to their mean, not to noise.** `cl_lod` picks the
   volumes' mip by footprint (the 3D textures are mipmapped); past a few
   texels a pixel the weather's warp fades and the shape reads its mean, or
   the volume's tile prints over the planet as a regular pattern.
3. **One shape function for clouds and their shadows.** The terrain's
   `cloud_shadow` calls `cloud_shape_at` with the layer's scale, weather and
   `cloud_systems` - a lookup of its own at a fixed scale was a different
   pattern from the clouds casting it.
4. **Two pieces per layer on a curved world.** `layer_spans` returns the
   near and the far crossing; on an inside world the far one is the cloud
   over the far side, and on a globe it is dropped only where the ground is
   in front of it (and only when the eye is above that ground).
5. **The sea reflects the sky pass, clouds and all.** `sky_env_update` shoots
   a 512x256 lat-long panorama from the water under the eye (mipmapped;
   `wat_sky` samples it with an explicit level from the waves' slope variance
   - never with the sun's width, which only spreads the glint). One per view
   slot, kept: re-shot at most 15 times a second, or when the eye moves more
   than a quarter texel of parallax (0.003 x the cloud altitude) - every view
   every frame was a cloud march on pictures that had not moved. It uses the
   view's own pixel for the march's level of detail, because the far-field
   fades change the clouds' shapes, and the water's clouds must be the sky's.
6. **A view ray is `far point - eye`**, never the far point alone - that sat
   the whole sky up to a couple of degrees off the ground in front of it.
7. **The animation clock moves by real time once a frame** (renderer.cpp),
   not by the `dt` of whichever view drew first - the camera thumbnail passes
   0 and stopped the clouds and the waves. Captures and render passes draw at
   that same clock (`renderer_anim_time`), not at 0 - waves frozen at their
   first instant under clouds at the live time. And drifting clouds or a
   running sea keep the frame pacing at `ambient_fps` (30) when idle: at the
   15 fps idle rate the weather stutters (`renderer_ambient_motion`, which the
   performance watcher also counts as a change).
8. **The clouds' depth cut runs on by one depth step** (FS_CLOUD_OVER): from
   orbit a 24-bit step is longer than the cloud base is high, and a cut that
   falls short takes the bottom off the layer.
9. **Texture units above 7**: 8-11 the graph's sampled buffers
   (`bind_field_textures`), 12 the placement weight in the terrain program
   (and a half-size target's second picture in the cloud and sky passes), 13
   the backdrop dome, 14 the sky panorama everything reflects (`bind_sky_env`),
   15 a half-size target's first picture. The dome used to share 11 with the
   fourth graph buffer. **Unbind a target from its units before drawing into
   it**: a texture on a sampler the program declares while it is also the draw
   target is a feedback loop whether or not the branch reads it.
10. **A capture has its own slot** (`SLOT_CAPTURE`) and never takes the
    governor's lighter secondary settings; it used to borrow View 6's target.
11. **Everything glossy reflects the same sky** (SKY_ENV_GLSL,
    renderer_clouds.cpp): the sea, the ground's reflection term and meshes read
    the panorama through `sky_env`, shot when the view has a sea *or* clouds -
    under an overcast a glossy rock used to reflect a clear blue gradient.
    Below the horizon the panorama holds no ground (the sky pass draws only
    sky, which came out as a starry night under every glossy object): `sky_env`
    reads the horizon's light there, at a blurred level, dimmed.
12. **The sky is drawn after everything solid** (renderer_scene.cpp): VS_SKY and
    the planets sit on the far plane, drawn with GL_LEQUAL onto the pixels no
    solid wrote. Drawn first it was worked out for every pixel and painted
    over - 19.6 ms of sky for no visible pixel looking down, at 1718x798 - and
    the clouds over the ground then marched those pixels again. A see-through
    terrain (`mat_transparency`) and a flat background keep the old order. The
    media meshes blend after the sky (`draw_scene_meshes(..., media)`).
13. **A working view marches half size** (renderer_clouds_over.cpp,
    renderer_space_half.cpp): the clouds over the ground on a checkerboard of
    nearest/farthest depth per 2x2 block, brought up by bilinear weights times
    depth similarity (a pixel no texel was cut near is marched on its own);
    the nebulas into an add/transmittance pair read bilinearly (they are at
    infinity: no depth), their hot stars drawn at full size in the sky pass.
    A capture and a render pass march every pixel and take the nebulas a
    quality step finer. Measured: clouds 8.6 -> 3.5 ms looking down, nebula
    poster 40 -> 25 ms; raw captures differ by at most 13/255 (clouds).
14. **Measuring without a viewport**: GPX_TIME_CAPTURES=1 moves the pass
    timers from View 1 to the captures (`pass_timed`), GPX_CAPTURE_RAW=1 keeps
    a capture at its drawn 2x size (the 2x2 average hides half-size errors),
    GPX_CLOUDS_HALF / GPX_NEBULAS_HALF = 0 never, 1 views (default), 2 captures
    too. A script cannot see the viewport; it can see captures.

## A blend keyed on a square draws a square

planet_place.cpp: the tile's border used to be
`min(min(u,1-u), min(v,1-v))`, which is the Chebyshev distance to the unit
square. Its contours are squares, and its gradient jumps across the
diagonals, so the feather, the material weight and everything downstream of
them drew a square frame with a crease out to each corner - the square
people report seeing. It is a p-norm now (`place_round`: p = 2 is the
inscribed circle, p = 8 already reads square again, so the whole useful
range is between - a larger exponent leaves the default indistinguishable
from what it replaced), plus `place_wander`, a noise on the border.

The wander eats **inward only**. A border pushed outward asks for tile data
past the tile's own square, where there is none, and puts back the hard edge
it exists to remove; tests/cpp/test_planet_place.cpp holds that invariant.
Any test that pins a probe to a particular border distance must set
`round = 0, wander = 0` - it is testing the feather's curve, not the
border's shape.

## Deep space

studio/shaders_space*.cpp is four GLSL chunks and an entry, spliced through
SPACE_FN_PLACEHOLDER in that order because GLSL has no forward
declarations: common (noise, the Planckian colour, the cube grid,
`sp_falloff`), stars, the band and the discs, the nebulas, then
`space_color`. renderer_space.cpp uploads the uniforms and binds the noise
volume on unit 10; space_settings.hpp holds the settings, space_presets.cpp
the named skies and Fill the sky, space_noise.cpp the 96^3 RGBA16 volume
(billow, ridged, cellular, fine) both the nebulas and the band read.

**The rule that keeps the sky clean:** every falloff drawn from a cell grid
must reach zero inside the cells the grid searches. `sp_star_grid` looks at
the 3x3 cells around a pixel, so a glow wider than one cell is cut off at
that boundary - and a cut-off glow is a square. The sky used to have a soft
square around every star for exactly that reason. `sp_falloff` has compact
support and every reach is bounded by `cellang = 2/(3n)`, the smallest a
cell of that grid ever gets (a cube's faces are gnomonic, so a cell at a
corner subtends two thirds of one at the middle). The same trap took out
the band's grain and the young stars in a nebula, which used to fill a
whole `floor()` cell.

Two more that cost a day between them. A cell past the edge of a face
belongs to the *next* face: hashing it as though it were still on this one
gives the two sides of a cube edge different stars, and a cube's edge is a
great circle, which a perspective view draws as a dead straight line across
the sky. `sp_star_grid` resolves out-of-range cells to the face they truly
lie on. And clumping multiplies the odds a slot holds a star, so a
multiplier over one fills every slot in the crowded parts and stacks a
dozen halos into a white ball; the product is capped.

A nebula is marched, not painted: `sp_neb_cloud` intersects a sphere of
radius `sin(r)` at unit distance (a sphere at unit distance subtends
`asin(rho)`, so `tan` made every one too big), then integrates emission
(gas x how hard the hot stars inside have ionised it, which is what moves
the colour from hydrogen's red to oxygen's teal), reflection off lit dust,
and per-channel extinction (blue absorbed hardest, so dust reddens as it
dims). Its halo is worked out *before* the sphere is tested, or the glow
ends dead on the silhouette and draws an arc. Galaxies and planetary
nebulas stay flat, because a disc is flat.

The band and the nebulas read the filtered volume rather than hashing a
fractal per pixel: built by hand out of value noise the band showed the
lattice it stood on as boxes of haze with straight edges, and cost enough
to halve the frame rate. `sp_fbm` and `sp_ridge` survive for the star
clumping and the discs, with a quintic interpolant and a turn between
octaves so the grid does not come through.

The sky pass works out how much of space reaches the eye *before* calling
`space_color`, because the backdrop is the most expensive thing in that
shader and in daylight none of it shows: a terrain scene at noon must not
pay for a sky it cannot see.

Nebula detail is spent by footprint: `sp_neb_field` adds two finer reads of
the volume (billows) and a rim read toward the nearest ionising star only
where `foot` - a pixel in units of the nebula's radius - can resolve them.
Emission goes as gas squared (knots and voids, not an even haze) and the
gas band is narrow, so the cloud has edges. The bright few stars carry long
spikes no thinner than 0.55 px (`pix_k`), or they break into dashes.

More that hold now:

- **The pixel's size is taken outside every branch** (`sp_pix` in FS_SKY):
  `fwidth` inside `sp_w > 0.002 ? ... : ...` is undefined right where the
  branch starts and stops, which is dusk.
- **The irradiance probe passes a negative pixel size**, and space leaves
  every point source out (the stars, a nebula's hot stars): its pixel is a
  fifth of a radian, and stars drawn a pixel wide lit the night ground like
  dusk. The probe's key hashes the space settings and the nebulas **field by
  field**, never a struct's bytes, whose padding is not its own.
- **The nebula list is depth**: each cloud dims and reddens everything before
  it in the list, so a dark cloud laid over a bright one hides it
  (`add = add * T + S`).
- **Diffraction spikes belong to the camera**: one shared angle for every
  star, in 4, 6 or 8 points, each colour reaching a little further
  (`star_spike_points`, `_angle`, `_chroma`).
- **A nebula's hot stars are drawn before its sphere test**, like the halo, or
  their spikes end on the silhouette. New cloud dials (turbulence, lanes,
  core glow, outskirts colour) default to the old look; an outskirts colour
  never set is negative and reads as the cool gas's.
- The nebula march has a governor cap (`space_quality_cap`), as the clouds do.
- **One step spacing for every ray** through a nebula (the diameter over the
  step budget), and a one-read envelope bound before the field's other reads;
  the dither is the clouds' blue noise (`u_sp_blue`, unit 9), not a sin hash.
  None of that bought much (30 -> 29.5 ms): the half-size march did (rule 13 of
  the clouds).
- **Star clusters** (`sp_clusters`, `star_clusters`, `star_cluster_size`) are a
  6x6-per-face grid of King profiles obeying the same 3x3-cells rule at both
  scales: a haze plus members on a tangent grid of their own while a pixel
  can separate them (globulars 30 cells across, open clusters 8), all haze
  when it cannot. A bright haze over the members read as an out-of-focus disc.
  `star_bright_share` sets the magnitude law's exponent, 14 - 10 x share (0.5
  is the old 9); an unset uniform reads 0 and falls back to 9.
- **Other planets have weather** (planet_clouds.cpp, `PlanetData::clouds`,
  default 0.4, none without air): a warped fbm deck by direction, drifting on
  the cloud clock and sheared by latitude, its shadow sampled a little
  toward the sun. A disc a few hundred pixels across is not worth a volume
  march; its pattern, shadow and light are what read.

## The lens flare

renderer_post.cpp + renderer_post_flare.cpp, through a scene camera with
optics on. The sun's screen position comes from `compute_sun_dir` projected
**as a direction** (w = 0), in the pass's uv with y **up** - it used to be
rebuilt from azimuth/altitude with the axes swapped (half the sky off) and
flipped. How much of the sun shows is read from the picture in the vertex
stage (25 samples over the disc, once a frame), so terrain, planets, clouds
and night dim the flare without the pass knowing they exist. Styles: 0 the
old ghosts, 1 cinematic (core, starburst, ring, hexagonal ghosts), 2
anamorphic (plus the streak); the light is compressed and screened over the
picture, never added and clipped. Each part's shape is a camera setting
(core, ray count and length, streak length and tint, halo radius, ghost count,
aperture blades, colour parting, seed) whose defaults draw exactly the flare
from before they existed - a saved camera must not change. The ghosts'
polygon is the distance along the nearest edge normal,
`length(q) * cos(mod(angle, seg) - seg/2)`, which works for odd blade counts
where the old `max |dot|` over three directions only made hexagons. The flare
parts' strengths and shapes are keyable (anim_targets.cpp `cam.flare_*`).

**Bloom** (renderer_post_bloom.cpp, `CameraData::bloom`, off by default) is a
13-tap downsample chain from a soft-knee bright pass on the developed
picture (threshold in the picture's own terms: the sun and glints are what
clip) and a tent upsample back, summed and divided by the level count so the
amount means the same at any size, screened over the picture in light after
the flare and before the vignette.

**Round primitives are smooth** (scene_primitives.cpp `tri_n`): a sphere,
cylinder or cone carries its surface's normal at each corner. Face normals
made every one faceted at any detail, and a glossy sphere a disco ball.

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
   node across a restore travels as an index, or is translated through the
   loader's id map. `restore` sends every binding outside the graph through
   `scene_remap_node_ids` (objects' driver, material, population, planet and
   surface nodes; the settings' `'u'` fields) plus `last_material` and
   `seq_cam_path`. It used not to: after any node deletion the numbering had
   a hole, a restored object's driver named another node or none, and its
   node built the object a second time - undoing a delete doubled every
   plant. A new node-id field anywhere outside the graph goes into that
   function the day it is added.
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
