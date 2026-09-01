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
   accumulator.
7. **Removing within-round coupling costs rounds.** With particles no longer
   seeing each other inside a round, `ROUNDS` had to rise 8 → 48 or erosion
   spikes: mean |Laplacian| 0.0227 → 0.0087, and at 8 the surface punched
   below the normalised floor (range started at −0.0058 instead of +0.0288).
   That is what engine rule 2 is about, and it is why Hydraulic is 1.7× slower
   than the racy version. Wind, Thermal and StreamPower are unchanged.
