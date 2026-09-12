# Building complete scenes by language — a guide for AI agents

This page is for an agent (the in-app assistant, an MCP client, a script)
that has to turn a sentence like *"a glacial valley at dawn with a lake, a
pine forest on the north slopes and a ring world overhead"* into a finished
TerraForge scene. It states the scene model in plain words, the grammar of
the operations, the vocabulary per module, and eight complete recipes with
their verification. Every operation named here exists and is tested
(`tests/test_api_coverage.py`); every node named here is in
[`NODES.md`](NODES.md) with its ports and parameters.

## 1. The scene model in one paragraph

A scene is **a node graph** (the terrain: generators, filters, erosion, the
`TerrainOutput` at the end; materials, each a `MaterialOutput`; cloud
layers, water, sun, atmosphere as nodes that drive settings) plus **a scene
tree** (objects: the home planet with its terrain tiles, water, atmosphere
and surface layers as children; meshes; lights; cameras; planets, moons and
nebulas in the sky) plus **the environment settings** (154 saved fields:
sun, sky, fog, clouds, water, world shape, placement, render defaults) plus
**a timeline** with tracks on any of them. The graph is re-evaluated when it
changes; the viewport draws the result on a curved world; a camera renders
it with an engine.

Units: the terrain tile is 1 world unit = `terrain_size_m` metres (5000 by
default). Ops that take metres say so (`_m`); positions in ops are tile
units unless they say metres. Heights in the graph are 0..1 of
`height_scale` × tile width. Angles are degrees.

## 2. The grammar

Every change is an **op**: a JSON object with `"op"` and fields. A document
is `{"actions":[...]}` (a bare object is accepted). Fields you omit are
left unchanged. Nodes are addressed by id, by `alias` you gave them, or by
type when unique. Objects and cameras are addressed by name.

Three ways to send ops:
- MCP: one tool per op, `studio_<op>` (aliases: `studio_set_world` folds the
  five environment ops; `studio_graph` builds a whole graph).
- Python: `Studio().send({...}, {...})` — batch several ops in one `send`;
  rapid separate sends overwrite each other (the inbox is one file).
- The console: `op key=value key=[1,2,3]`.

Read back: `Studio.state()` / `studio_get_state` (objects, cameras, planets,
nebulas, space, world, sun, sky, fog, clouds, water, terrain, nodes, links,
eval, perf, `status`, `reply`); `studio_get_graph`; `list_settings`;
`keys`; `probe_height`; `points_stats`; `capture` (a PNG of the viewport
through the active camera); `render_preset action:list`. The `reply` field
holds the last answering op's JSON.

Rules that save time:
1. Add cameras with `activate:true` or `render` has nothing to render.
2. `set_camera` needs `activate:true` to make the view follow.
3. Batch: one `send` per step, then `wait_for_eval` before reading a height.
4. Evaluation holds the graph lock; heavy erosion is seconds — set
   `set_resolution` 512 while iterating, 2048 for the final.
5. A node's parameters are in `NODES.md`; `find_nodes {"query":"…"}` finds a
   node by what it does when you do not know its name.
6. Undo is one step per document: `{"op":"undo"}` reverts your last send.

## 3. Vocabulary per module

**Terrain** — build the chain, end at `TerrainOutput`:
`Noise`/`Fractal`/`TerrainFractal2`/`RockyMountains`/`Landform`/`Crater`/
`Dunes`/`Stamp`/`HeightmapFile`(DEM) → filters (`Terrace`, `Plateau`,
`Smooth`, `Curve`) → erosion → `TerrainOutput`. Or the editor:
`terrain_style` (Mountain, Ridged peaks, Eroded mountain, Canyon, Mounds,
Dunes, Iceberg, Lunar, Realistic mountain range), `terrain_effect`,
`terrain_clip`, `terrain_global`, `terrain_import_picture`, `set_sculpt`.
Tile size is `set_setting terrain_size_m`, relief is `set_setting
height_scale`. Outline:
`terrain_shape` 0 square / 1 round / 2 rectangle; placement on the planet:
`place_mode` 0 features / 1 whole / 2 zero edge / 3 clip low / 4 clip high.

**Erosion** — nodes `Hydraulic` (droplet or pipes), `Thermal`, `StreamPower`,
`Wind`, `Glaciation`, `Dissolve`, `Coast`, `Rivers`, `FillBasins`, `Flood`,
`Lake`, `ErosionLayers` (masks for materials); or `terrain_effect` with
`hardness`.

**Planets and the world** — `set_viewport world:"globe"|"ring"|"dyson"|"flat"`,
`planet_radius` (tile units; 1275 ≈ Earth at 5 km), `world_width`,
`world_thickness`, `world_outline`, `world_inside`, `world_sun_inside`;
`add_planet`, `set_planet`, `add_moon`, `add_infinite_terrain
style:terrain|mountains|hills|dunes|craters`; `place_object side:inside`.

**Space** — `set_space` (stars, galaxy band), `add_nebula type:nebula|dark|
galaxy|elliptical|planetary` with `azimuth`, `elevation`, `size_deg`,
colours; `add_moon`. Space shows where the air is thin: at night, above
`atmosphere_height`, beyond a ring's rim.

**Atmosphere** — `set_sky density height_m ambient zenith horizon`,
`set_fog type:haze|fog|pollution density level falloff color …`,
`set_clouds` and `add_node CloudLayer` per extra layer, `set_setting` for
the rest (`cloud_detail`, `cloud_wind_dir`, …).

**Water** — `set_water enabled level deep shallow foam`; `set_setting
water_clarity water_wave_amp foam_amount …`; bodies from the terrain:
`Lake`, `Flood`, `Rivers` nodes; a placed feature below ground becomes a
basin the water fills.

**Materials** — `preset_material name:"Rock"` (22 bases), `set_material
key value` on the `MaterialOutput`, layers: `add_node MaterialLayer` with
altitude/slope/orientation presence, `MaterialStack` fed by
`ErosionLayers` masks; `assign_material node object` (omit object = the
terrain); `load_material`/`save_material` for the library;
`ai_generate_texture` for a painted channel.

**Distribution** — `add_node EcosystemLayer` (density per hectare,
presence by mask/altitude/slope/orientation, species, affinity/repulsion,
decay near objects) or `ScatterPoints`/`ScatterArea`; `set_scatter object
node species size jitter`; `points_stats` to count; `place_on_terrain`,
`set_ground` for a placed object.

**Objects** — `add_primitive kind`, `import_object path`, `place_object`
(position, heading/pitch/bank, squeeze, twist, bend, skew, taper),
`combine_objects`, `metaball`, `mesh_repair`/`mesh_reduce`/`mesh_export`.

**Lighting** — `set_sun azimuth_deg altitude_deg intensity color`;
`set_setting sun_mode:1 latitude longitude month day hour` for a place and
time; `add_light`/`set_light` for point and spot lights; `set_viewport
shadows shadow_softness exposure`.

**Cameras** — `add_camera name focal_mm format aperture shutter iso film
look_at distance height azimuth_deg activate` or `position`/`look_at`
vectors; `view_to_camera`, `camera_to_view`; `set_camera_key`.

**Render** — `render_preset action:save name camera engine width height
samples output passes panorama`; `render camera preset`; `render_batch
cameras preset`; `render_passes`; `capture` for a quick look.

**Viewports** — `set_viewport view:1 shading projection scene_camera curved
atmosphere water grid outlines`; `arrange_views`, `add_view`, `save_layout`.

**Animation** — `set_range fps start end`, `set_key {object,prop|world|
node,attr} frame value interp`, `set_frame`, `play`, `add_modifier`,
`set_expression`, `render_sequence dir fps`.

## 4. Recipes

Each recipe is a sequence of documents. Names in quotes are real op fields;
numbers are sensible starting values. After each, **Verify** says what to
read back.

### 4.1 An alpine valley at dawn (terrain, erosion, materials, water, light)
```
{"op":"set_setting","key":"terrain_size_m","value":8000}
{"op":"set_resolution","resolution":1024}
{"op":"terrain_style","name":"Realistic mountain range"}
{"op":"terrain_effect","effect":"fluvial","hardness":0.4}
{"op":"terrain_effect","effect":"glaciation","hardness":0.5}
{"op":"set_viewport","place_mode":3,"place_edge":0.12}
{"op":"set_water","enabled":true,"level":0.10,"deep":[0.02,0.08,0.12]}
{"op":"preset_material","name":"Rock","alias":"rock"}   then assign: {"op":"assign_material","node":"rock"}
{"op":"set_sun","azimuth_deg":95,"altitude_deg":8,"intensity":2.4,"color":[1,0.82,0.62]}
{"op":"set_fog","type":"haze","density":1.4,"level":0.18,"falloff":7}
{"op":"set_clouds","enabled":true,"type":"cumulus","coverage":0.35,"altitude":0.5}
{"op":"add_camera","name":"Dawn","focal_mm":35,"look_at":"terrain","distance":1.6,"height":0.35,"azimuth_deg":210,"activate":true}
{"op":"capture","path":"D:/out/dawn.png","width":1920,"height":1080}
```
Better materials: build `ErosionLayers → MaterialStack → MaterialOutput`
(the schema's default block shows the wiring) so snow, scree and grass
follow the erosion. **Verify**: `probe_height x:0.5 z:0.5` reports a
peak in metres; `state()["water"]["level"]` is 0.10; the capture shows the
lake in the valley floor.

### 4.2 A ring world (world shape, atmosphere on the shape, clouds, space)
```
{"op":"set_viewport","world":"ring","planet_radius":300,"world_width":80,"world_thickness":3}
{"op":"set_sky","height_m":60000,"density":1.0}
{"op":"set_clouds","enabled":true,"coverage":0.5,"altitude":0.4}
{"op":"set_space","galaxy_intensity":0.8,"star_density":0.6}
{"op":"add_camera","name":"Along","position":[0.5,0.2,1.5],"look_at":[0.5,20,-40],"focal_mm":20,"activate":true}
{"op":"capture","path":"D:/out/ring_inside.png"}
{"op":"add_camera","name":"Outside","position":[200,300,700],"look_at":[0.5,300,0.5],"focal_mm":35,"activate":true}
{"op":"capture","path":"D:/out/ring_outside.png"}
```
The clouds form a band on the ring's inside and stop at the rim; the air
is a band seen through the ring's opening from outside; the far side
carries clouds. A tile on the ring's outer face: `add_component
kind:terrain` then `place_object name:"Terrain 2" side:"outside"`.
**Verify**: `state()["world"]["shape"] == "ring"`; the outside capture
shows a body (rim wall), not a sheet.

### 4.3 A night sky with a moon and nebulas
```
{"op":"set_sun","altitude_deg":-25,"azimuth_deg":135}
{"op":"set_clouds","enabled":false}
{"op":"set_space","stars":true,"star_density":0.6,"star_brightness":1.2,"galaxy":true,"galaxy_intensity":0.9,"galaxy_yaw":60,"galaxy_pitch":40,"galaxy_core":20}
{"op":"add_nebula","name":"Orion","type":"nebula","azimuth":20,"elevation":40,"size_deg":30,"brightness":1.2,"color1":[0.95,0.4,0.5],"color2":[0.3,0.55,0.95]}
{"op":"add_nebula","name":"Andromeda","type":"galaxy","azimuth":300,"elevation":30,"size_deg":16,"tilt_deg":60,"rotation_deg":30,"arms":2}
{"op":"add_nebula","name":"Helix","type":"planetary","azimuth":340,"elevation":55,"size_deg":7}
{"op":"add_moon","name":"Luna","radius":4,"position":[0.5,40,-60]}
{"op":"add_camera","name":"Night","position":[0.5,0.12,0.5],"look_at":[0.2,0.8,1.4],"focal_mm":18,"activate":true}
{"op":"capture","path":"D:/out/night.png"}
```
The sun lights the moon's day side: put it behind the camera
(`set_sun azimuth_deg:0 altitude_deg:30`) to see a crescent. **Verify**:
`state()["nebulas"]` lists three; the capture's upper half is dark with
stars (mean luminance under 60/255).

### 4.4 A desert with dunes and a scattered forest at the oasis
```
{"op":"terrain_style","name":"Dunes"}
{"op":"terrain_effect","effect":"wind","hardness":0.3}
{"op":"set_water","enabled":true,"level":0.04}
{"op":"preset_material","name":"Sand","alias":"sand"}  {"op":"assign_material","node":"sand"}
{"op":"import_object","path":"D:/assets/palm.obj","name":"Palm","scale":0.02}
{"op":"add_node","type":"EcosystemLayer","alias":"palms","attrs":{"density":40,"use_altitude":true,"altitude":[0.02,0.12],"use_slope":true,"slope":[0,18],"avoid_overlap":true,"repulsion":0.5,"repulsion_radius_m":6}}
{"op":"set_scatter","object":"Palm","node":"palms","size":0.02,"jitter":0.4,"seed":7}
{"op":"points_stats","node":"palms"}
```
Or `run_macro path:"examples/macros/scatter_forest.json"`. **Verify**:
`reply.count` > 0 and `mean_scale` near 0.02; `save_node_preview
node:"palms"` shows the clumps; nothing stands in the water (the altitude
band starts at the water level).

### 4.5 A real coast from a DEM
```
{"op":"set_setting","key":"terrain_size_m","value":30000}
{"op":"terrain_import_picture","path":"D:/dem/N44E006.hgt","mode":"blend","proportion":1.0}
{"op":"set_setting","key":"height_scale","value":0.12}
{"op":"terrain_effect","effect":"fluvial","hardness":0.7,"iterations":1}
{"op":"set_water","enabled":true,"level":0.02}
then materials by erosion masks as in 4.1 (keep the DEM as the base; no style on top)
```
Today the DEM is normalised to 0..1, so set `height_scale` from the known
relief (summit ÷ tile width); the terrain roadmap's Phase 1 keeps metres.
**Verify**: `probe_height` at a known summit against the map (`placed_m`;
`drawn_m` adds the viewport's micro-relief, metres either way).

### 4.6 A flat disc world
```
{"op":"set_viewport","world":"flat","world_outline":"disc","world_width":40,"world_thickness":2}
{"op":"set_sky","height":3}
{"op":"add_camera","name":"Below","position":[0.5,-8,30],"look_at":[0.5,-1,0.5],"focal_mm":28,"activate":true}
{"op":"capture","path":"D:/out/disc.png"}
```
**Verify**: the capture shows a slab with a rim and an underside; the sky
beyond the edge is space.

### 4.7 Render presets and a batch
```
{"op":"add_camera","name":"Hero","look_at":"terrain","distance":1.4,"height":0.3,"azimuth_deg":200,"activate":true}
{"op":"add_camera","name":"Wide","look_at":"terrain","distance":3.0,"height":1.2,"azimuth_deg":120}
{"op":"render_preset","action":"save","name":"Draft","engine":"mitsuba","width":960,"height":540,"samples":32}
{"op":"render_preset","action":"save","name":"Final 4K","engine":"cycles","width":3840,"height":2160,"samples":512,"passes":true}
{"op":"render_preset","action":"apply","name":"Final 4K","camera":"all"}
{"op":"render","camera":"Hero","preset":"Draft"}
{"op":"render_batch","cameras":["Hero","Wide"],"preset":"Final 4K"}
```
Each camera renders to its own file (`render_<camera>.png` when it had the
default). **Verify**: `render_preset action:list` in `reply`;
`state()["render_queue"]` counts down; the Render output window shows the
progressive image.

### 4.8 A day-to-night animation
```
{"op":"set_range","fps":30,"start":0,"end":10}
{"op":"set_key","world":"sun_altitude","frame":0,"value":60}
{"op":"set_key","world":"sun_altitude","frame":300,"value":-20}
{"op":"set_key","world":"sun_azimuth","frame":0,"value":90}   {"op":"set_key","world":"sun_azimuth","frame":300,"value":270}
{"op":"set_key","world":"star_brightness","frame":200,"value":0}  {"op":"set_key","world":"star_brightness","frame":300,"value":1.5}
{"op":"set_key","object":"Hero","prop":"cam.focal_mm","frame":0,"value":24}  {"op":"set_key","object":"Hero","prop":"cam.focal_mm","frame":300,"value":50}
{"op":"add_modifier","world":"cloud_coverage","type":"oscillator","amplitude":0.1,"frequency":0.2}
{"op":"render_sequence","dir":"D:/out/day","fps":30,"width":1920,"height":1080}
```
Or `run_macro path:"examples/macros/day_cycle_sequence.json"`.
**Verify**: `keys` lists the tracks; `set_frame frame:150` then `capture`
shows dusk.

## 5. Checking your work

- `capture` after every visible change and look at it; compare captures
  by mean difference when a change should be invisible.
- `probe_height` for heights, `points_stats` for populations,
  `list_settings` for the environment, `keys` for animation, `state()` for
  everything else; `perf_report` when the frame is slow; `crash_reports`
  at the start of a session.
- If a picture does not follow an edit, check for a pinned view
  (`view_node` with no node releases it) and for an inactive camera.

## 6. Where the details are

[`NODES.md`](NODES.md) every node · [`INTERFACE.md`](INTERFACE.md) the
panels · [`ANIMATION.md`](ANIMATION.md) · [`ECOSYSTEM.md`](ECOSYSTEM.md) ·
[`MATERIAL_LAYERS.md`](MATERIAL_LAYERS.md) · [`VOLUMETRICS.md`](VOLUMETRICS.md)
· the op syntax the assistant is given: `studio/ai_schema.cpp` · the
worked macros: `examples/macros/*.json` · the audit and roadmaps:
[`AUDIT.md`](AUDIT.md), [`roadmaps/`](roadmaps/README.md).
