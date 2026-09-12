# Known defects and gaps

Things found to be wrong, missing, or not built the way they should be —
written down when they are found, whether or not they were what anyone was
working on at the time. A defect noticed in passing and not recorded is a
defect found again six months later at full price.

**How to use this.** Add an entry the moment you find something, with the
evidence you already have: what you saw, what you measured, what you ruled
out. Do not investigate further just to make the entry tidy — an honest
"suspect X, ruled out Y" is worth more than a polished guess. Delete an entry
when it is fixed and verified, and say in the commit which one it was.

This is not the roadmap. [`roadmaps/`](roadmaps/) is what we mean to build;
this is what is broken or missing in what already exists.

---

## Open

### 1. A scattered wood draws no copies at all
**Severity: high — `plant_forest` produces nothing visible.**

Planted 900 Scots pine over 1400 m (`plant_forest`), stood the camera among
them, and nothing is drawn. The live viewport reports `instances_drawn: 0` of
`instances_total: 19845` — so the objects are visited and their instances
counted, and every cell is then rejected.

Ruled out: the LOD distances. With `scatter_lod_cull_m` 500 km,
`scatter_lod_full_m` 300 km, `scatter_lod_min_keep` 1.0 the count is still 0,
so `scatter_keep` is not the filter.

Remaining suspect: the frustum test in `instance_runs`
([`studio/renderer_instances.cpp:162`](../studio/renderer_instances.cpp)) —
`aabb_visible(*fr, lo, hi)` against cell bounds `c.lo`/`c.hi`. If the cells
are built in the object's local frame while the frustum and `eye` are world,
every cell misses. Check what `InstanceCell::lo/hi` are filled with at scatter
time.

Not caused by the wind-field change: the same count is reported by a build
without it, and the change only alters arithmetic inside `if (u_inst_sway >
0.0)`, which runs after the run list is built.

### 2. The ground still carries a repeating block pattern
**Severity: medium — visible at landscape scale, the user's original report.**

After the micro-relief was fixed (see Closed #1), a soft rectangular pattern
remains on the land: measured as a strongest horizontal period of 200 px in a
1100 px frame covering ~0.55 of the tile, about 500 m on the ground, with a
peak-to-mean ratio of 8.2 in the row spectrum.

It is **not** the micro-relief: the spectrum is identical with
`fractal_detail` at 0 and at 0.0025 (period 200.0 px, peak/mean 8.17 vs 8.12).
So it comes from the baked heightmap or the terrain material underneath.
Next step: check the default scene's terrain graph and the procedural albedo
in [`studio/shaders_terrain_frag.cpp`](../studio/shaders_terrain_frag.cpp) for
a single-octave or axis-aligned band.

### 3. The sea has a diagonal stripe pattern
**Severity: medium.**

Plainly visible in a wide shot of open water: regular diagonal banding across
the whole surface, at one scale. The Gerstner sum in
[`gpx/water_waves.hpp`](../engine/gpx/water_waves.hpp) uses a small number of
wave directions; a spectrum with too few components reads as a corduroy rather
than a sea. Wants the same treatment the relief got: several scales, no two
sharing a heading.

### 4. A camera looking exactly straight down renders a blank frame
**Severity: low — easy to avoid, easy to hit by accident.**

`set_camera` with `position` directly above `look_at` (same x and z) produces a
white frame. The look-at basis is degenerate against a fixed (0,1,0) up.
A plan view is a thing people ask for; the up vector should fall back to
world -Z when the view direction is within a degree or so of vertical.

### 5. A plant does not feel the world's wind
**Severity: medium.**

`SceneObject::plant_wind` is built from the species node's attributes alone
([`studio/scene_plants_species.cpp:245`](../studio/scene_plants_species.cpp)),
so a plant's `strength` and direction are what the species says and never what
the weather is doing. The species should say how a plant *responds*; the scene
wind should say how hard it blows. Scattered copies now read the gust field
([`studio/wind_field.hpp`](../studio/wind_field.hpp)); a single plant object
still does not.

### 6. The cloud volumes have not been checked for the lattice the relief had
**Severity: unknown — worth half an hour.**

`cloud_noise.cpp` builds its 3D Perlin-Worley volumes on an integer lattice
with a float hash, the same construction that turned out to be laying a square
maze over the terrain (Closed #1). The clouds hide it better — the volume is
warped by the weather lookup and marched — but nobody has looked. Render a
slice of `tex_cloud_shape` and see.

### 7. The API inbox drops rapid sends
**Severity: low for people, high for scripts and tests.**

`actions_inbox.json` is one file. Two `Studio.send()` calls in quick succession
and the first is overwritten unread, silently — the second simply wins. Every
script that drives the app has to sleep between actions, and a test that
forgets appears to pass while doing half the work. Wants a queue, or at least
a sequence number the app acknowledges.

---

## Closed

### 1. The terrain micro-relief was a square lattice
*Fixed 12 September 2026.* Nine octaves of ridged value noise, every one
axis-aligned and offset along the diagonal, on a hash ending in
`fract(p.x * p.y)` — symmetric about the diagonal. They reinforced into a
rounded-square maze, which was the relief everywhere its amplitude showed and
glaring under water, where depth maps a few metres of height onto the whole
colour range. Fixed by turning every band off the grid (each octave at the
golden angle), a domain warp, a roughness band, and an integer hash:
`FRACTAL_FN` in [`studio/shaders_terrain.cpp`](../studio/shaders_terrain.cpp)
and its twin [`studio/terrain_relief.hpp`](../studio/terrain_relief.hpp).

### 2. `capture` ignored the view's camera
*Fixed 12 September 2026.* `renderer_render_to_file` called
`camera_matrices` without setting `renderer_camera_override`, so it drew
through whichever camera happened to be active — a view locked to one camera
photographed something else, and a script could not photograph a named point
of view at all. The op now takes `"camera"`.

### 3. `camera_to_view` "was not implemented"
*Was never true.* It is implemented, in
[`studio/layout_store.cpp:245`](../studio/layout_store.cpp), along with
`view_to_camera` and the layout ops. An earlier note here was written after
grepping only `ai_ops_*.cpp` and `ai_actions_*.cpp`. The reason it appeared to
do nothing was Closed #2 above.
