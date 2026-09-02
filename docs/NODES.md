# Node reference

Every node in Geekatplay TerraForge — 137 across 25 categories. Generated from the registry itself by `tools/gen_node_docs.cpp`, so what is written here is what is constructed; regenerate with the `node_docs_gen` target after adding a node.

| Category | Nodes |
| :--- | :--- |
| [Analysis](#analysis) | 3 |
| [Atmosphere](#atmosphere) | 4 |
| [Effect](#effect) | 7 |
| [Erosion](#erosion) | 9 |
| [Export](#export) | 7 |
| [Field Bridge](#field-bridge) | 2 |
| [Field Color](#field-color) | 2 |
| [Field Displace](#field-displace) | 4 |
| [Field Input](#field-input) | 9 |
| [Field Material](#field-material) | 1 |
| [Field Math](#field-math) | 6 |
| [Field Noise](#field-noise) | 3 |
| [Filter](#filter) | 16 |
| [Group](#group) | 1 |
| [Hydrology](#hydrology) | 2 |
| [Logic](#logic) | 6 |
| [Mask](#mask) | 7 |
| [Material](#material) | 18 |
| [Operator](#operator) | 4 |
| [Path](#path) | 1 |
| [Points](#points) | 5 |
| [Primitive](#primitive) | 11 |
| [Render](#render) | 2 |
| [Texture](#texture) | 3 |
| [Transform](#transform) | 4 |

## Analysis

### FlowAccumulation

How much water passes through each point — the basis of streams and erosion patterns

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Logarithmic | toggle, default on | Accumulation spans several orders of magnitude — a few channels carry almost everything. Without this the map is black with a handful of bright lines. |
| Channel threshold | float, 0 to 1, default 0 | Discards everything below this fraction, leaving only the established channels. |
| Route through basins | toggle, default on | Water that reaches a hollow fills it and flows on. Off follows the raw surface, where every stream stops at the first pit it meets. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Resample

Rebuilds the terrain at a coarser or finer sampling — detail control, not size

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Sampling | choice: Half / Quarter / Double / Custom | Coarser sampling discards fine detail, which is how you get a smooth base to build on. Finer sampling cannot invent detail — it interpolates. |
| Custom size | int, 8 to 8192, default 256 |  |
| Smooth interpolation | toggle, default on | Off: nearest neighbour, which keeps hard edges and gives a deliberately blocky, terraced look. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### WetnessIndex

Where water collects — high in flat hollows fed from above, low on steep ground

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Minimum slope | float, 0.0001 to 0.5, default 0.01 | Perfectly flat ground would divide by zero and give an infinitely wet pixel. This is the flattest slope the index will consider. |
| Contrast | float, 0.1 to 4, default 1 |  |
| Route through basins | toggle, default on | Water that reaches a hollow fills it and flows on. Off follows the raw surface, where every stream stops at the first pit it meets. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

## Atmosphere

### AtmosphereSettings

Sky colors, density, haze/fog and light absorption

| Port | Direction | Type |
| :--- | :--- | :--- |
| atmosphere | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Atmosphere density | float, 0.05 to 3, default 1 |  |
| Ambient light | float, 0 to 2, default 0.7 |  |
| Zenith R | float, 0 to 1, default 0.18 |  |
| Zenith G | float, 0 to 1, default 0.32 |  |
| Zenith B | float, 0 to 1, default 0.58 |  |
| Horizon R | float, 0 to 1, default 0.62 |  |
| Horizon G | float, 0 to 1, default 0.65 |  |
| Horizon B | float, 0 to 1, default 0.7 |  |
| Fog type | choice: Off / Haze / Fog / Pollution |  |
| Fog density | float, 0 to 6, default 0.9 |  |
| Fog level | float, 0 to 1, default 0.25 |  |
| Vertical falloff | float, 0.5 to 24, default 6 |  |
| Fog R | float, 0 to 1, default 0.55 |  |
| Fog G | float, 0 to 1, default 0.63 |  |
| Fog B | float, 0 to 1, default 0.75 |  |
| Sun scattering | float, 0 to 1, default 0.5 |  |

### CloudLayer

Volumetric cloud layer: type, coverage, altitude, wind

| Port | Direction | Type |
| :--- | :--- | :--- |
| clouds | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Enabled | toggle, default on |  |
| Cloud type | choice: Stratus / Cumulus / Cumulonimbus |  |
| Coverage | float, 0 to 1, default 0.55 | 0 = clear sky, 1 = solid overcast. |
| Density | float, 0.1 to 3, default 1 |  |
| Base altitude | float, 0.2 to 4, default 1.4 |  |
| Thickness | float, 0.05 to 2, default 0.8 |  |
| Detail erosion | float, 0 to 1, default 0.6 |  |
| Anvil spread | float, 0 to 1, default 0.3 |  |
| Wind speed | float, 0 to 0.3, default 0.02 |  |
| Wind direction | float, 0 to 360, default 45 |  |
| Sky light | float, 0 to 2, default 0.55 |  |
| Color R | float, 0 to 1, default 1 |  |
| Color G | float, 0 to 1, default 1 |  |
| Color B | float, 0 to 1, default 1 |  |
| Quality | choice: Draft / Normal / High |  |

### SunLight

Sun: direction (manual or geographic), color, intensity

| Port | Direction | Type |
| :--- | :--- | :--- |
| sun | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Direction mode | choice: Manual / Location & time |  |
| Azimuth | float, 0 to 360, default 135 |  |
| Altitude | float, 1 to 89, default 35 |  |
| Latitude | float, -89 to 89, default 40.7 |  |
| Longitude | float, -180 to 180, default -111.9 |  |
| UTC offset | float, -12 to 14, default -7 |  |
| Month | int, 1 to 12, default 6 |  |
| Day | int, 1 to 31, default 21 |  |
| Local time | float, 0 to 24, default 14 |  |
| Intensity | float, 0.2 to 8, default 2.6 |  |
| Color R | float, 0 to 1, default 1 |  |
| Color G | float, 0 to 1, default 0.93 |  |
| Color B | float, 0 to 1, default 0.82 |  |
| Cast shadows | toggle, default on |  |

### WaterLayer

Water body: level, colors, waves and foam

| Port | Direction | Type |
| :--- | :--- | :--- |
| water | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Enabled | toggle, default on |  |
| Level | float, 0 to 1, default 0.08 |  |
| Clarity | float, 1 to 60, default 18 |  |
| Opacity | float, 0.3 to 1, default 0.92 |  |
| Deep R | float, 0 to 1, default 0.02 |  |
| Deep G | float, 0 to 1, default 0.08 |  |
| Deep B | float, 0 to 1, default 0.12 |  |
| Shallow R | float, 0 to 1, default 0.1 |  |
| Shallow G | float, 0 to 1, default 0.26 |  |
| Shallow B | float, 0 to 1, default 0.36 |  |
| Wave amplitude | float, 0 to 4, default 1 |  |
| Wave scale | float, 0.2 to 6, default 1 |  |
| Wave speed | float, 0 to 5, default 1 |  |
| Foam | toggle, default on |  |
| Shoreline foam | float, 0 to 2, default 0.6 |  |
| Crest foam | float, 0 to 1, default 0.35 |  |
| Foam scale | float, 0.5 to 10, default 3 |  |

## Effect

### Cracks

Narrow fissures cut into the surface, as after a quake

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Depth | float, 0 to 0.4, default 0.06 |  |
| Width | float, 0.05 to 1, default 0.35 | Thickness of the fissures. Low values give hairlines. |
| Scale | float, 0.5 to 40, default 6 | How many fissures cross the terrain. |
| Wander | float, 0 to 2, default 0.35 | Makes the fissures meander instead of running straight. |
| Seed | seed |  |

### Gravel

Loose debris that gathers on slopes and leaves flats clean

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Amount | float, 0 to 0.25, default 0.03 |  |
| Grain size | float, 8 to 400, default 120 |  |
| Slope bias | float, 0 to 4, default 1.5 | How strongly the debris prefers steep ground. 0 spreads it evenly, high values keep it on slopes. |
| Seed | seed |  |

### Grit

Fine random bumps and holes over the whole surface

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Amount | float, 0 to 0.25, default 0.02 | Bump height as a fraction of the terrain's own range. |
| Grain size | float, 8 to 400, default 90 | Higher values give finer, denser grain. |
| Seed | seed |  |

### Peaks

Lifts high ground and digs the valleys deeper

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, 0 to 1, default 0.5 |  |
| Pivot altitude | float, 0 to 1, default 0.45 | Ground above this rises, ground below sinks. Lower it to keep more of the terrain high. |

### Sharpen

Makes steep ground steeper — crisp ridges and crests

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Amount | float, 0 to 3, default 0.6 |  |
| Radius | float, 0.002 to 0.1, default 0.01 | Size of the detail that gets emphasized. |
| Steep areas only | float, 0 to 3, default 1 | 0 sharpens everything evenly; higher values leave flat ground untouched. |

### TerrainClip

Clip altitudes — flat tops above, holes below

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| clip_mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Clip range | range | Ground below the low mark is cut away, ground above the high mark is flattened. Normalized altitudes. |
| Below low mark | choice: Leave alone / Flatten / Cut away (hole) |  |
| Above high mark | choice: Leave alone / Flatten |  |
| Edge softness | float, 0 to 0.2, default 0 | Blends the cut instead of leaving a hard step. |

### TerrainSculpt

Hand-sculpted layer — brush strokes kept separate from the procedural terrain

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| stroke_mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Sculpted relief | painted buffer | Painted in the viewport with the Terrain Editor brushes. Stored with the project; erasing it resets the sculpt. |
| Strength | float, 0 to 2, default 1 | Scales the whole sculpted layer — dial your edits back without losing them. |
| Soften | float, 0 to 0.05, default 0 | Blurs the sculpted layer only, leaving the terrain underneath crisp. |

## Erosion

### Coast

Coastal shaping: flat beach band, wave planation, bluff

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| beach_mask | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Water level | float, 0 to 0.8, default 0.12 |  |
| Beach height band | float, 0.005 to 0.2, default 0.04 | Heights within this band above water are planed into a gently sloping beach. |
| Beach slope | float, 0.02 to 1, default 0.25 |  |
| Bluff sharpness | float, 0 to 1, default 0.5 | Steepens the cut where the terrain rises out of the beach band — wave-cut bluffs. |
| Underwater smoothing | float, 0 to 1, default 0.4 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Dissolve

Rainwater dissolves the surface into streams, strongest low down

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| flow_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Amount | float, 0 to 1, default 0.25 |  |
| Rock hardness | float, 0 to 1, default 0.5 | Hard rock keeps the streams narrow and incised; soft rock lets them spread and flatten the surface. |
| Low ground bias | float, 0 to 3, default 1 | How much the effect concentrates at low altitude. |
| Smoothing | float, 0 to 1, default 0.15 |  |

### Glaciation

Glacial carving — broad U-shaped valleys, ridges left intact

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, 0 to 1, default 0.6 |  |
| Ice line | float, 0 to 1, default 0.55 | Ground below this altitude is carved by ice; peaks above it keep their sharp profile. |
| Valley width | float, 0.005 to 0.15, default 0.03 |  |
| Rock hardness | float, 0 to 1, default 0.4 | Hard rock resists the ice and keeps more relief. |

### Hydraulic

Hydraulic erosion: particle droplets or shallow-water pipe model

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| erosion_map | out | heightmap |
| deposition_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Method | choice: Particle droplets / Shallow water (pipe model) |  |
| Seed | seed |  |
| Particles (x1000) | int, 1 to 2000, default 120 |  |
| Particle lifetime | int, 8 to 256, default 48 |  |
| Inertia | float, 0 to 0.6, default 0.06 |  |
| Carry capacity | float, 0.5 to 20, default 5.5 |  |
| Erosion rate | float, 0.01 to 1, default 0.4 |  |
| Deposition rate | float, 0.01 to 1, default 0.25 |  |
| Evaporation | float, 0 to 0.1, default 0.015 |  |
| Gravity | float, 0.5 to 12, default 4 |  |
| Brush radius | int, 1 to 8, default 3 |  |
| Iterations | int, 10 to 600, default 120 |  |
| Rainfall | float, 0.001 to 0.1, default 0.012 |  |
| Capacity Kc | float, 0.1 to 4, default 1 |  |
| Erosion Ks | float, 0.05 to 2, default 0.5 |  |
| Deposition Kd | float, 0.05 to 2, default 0.5 |  |
| Evaporation | float, 0 to 0.2, default 0.015 |  |

### Rivers

Trace rivers from headwaters and carve channels; outputs river + depth masks

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| river_mask | out | heightmap |
| water_depth | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Headwaters | int, 2 to 200, default 24 | Number of river source points seeded on high ground with strong drainage; streams merge downstream. |
| River width | float, 0.001 to 0.05, default 0.006 |  |
| Carve depth | float, 0.005 to 0.3, default 0.05 |  |
| Valley width | float, 0 to 0.15, default 0.02 | Soft V-shaped valley carved around the channel. |
| Widen downstream | float, 0 to 1, default 0.6 |  |
| Seed | seed |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### SedimentDeposit

Fill valleys with smooth sediment

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| sediment_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 300, default 40 |  |
| Fill amount | float, 0 to 1, default 0.3 |  |

### StreamPower

Fluvial erosion E=K·A^m·S^n — explicit incision or implicit solver with tectonic uplift

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| uplift | in (optional) | heightmap |
| hardness | in (optional) | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| flow_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Method | choice: Explicit incision / Implicit + uplift (Braun-Willett) |  |
| Iterations | int, 1 to 400, default 40 |  |
| Erodibility K | float, 0.001 to 0.3, default 0.03 |  |
| Area exponent m | float, 0.2 to 1, default 0.5 |  |
| Slope exponent n (explicit) | float, 0.5 to 2, default 1 |  |
| Timestep (implicit) | float, 0.05 to 10, default 1 |  |
| Uplift rate | float, 0 to 0.05, default 0.004 |  |
| Diffusion | float, 0 to 0.5, default 0.08 |  |

### Thermal

Thermal weathering — talus slopes to angle of repose

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Talus angle | float, 0.05 to 4, default 1.2 |  |
| Iterations | int, 1 to 500, default 60 |  |
| Transport rate | float, 0.05 to 1, default 0.5 |  |
| Run to convergence | toggle, default off |  |

### Wind

Aeolian erosion — windward abrasion, leeward deposition (dunes)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Wind direction ° | float, -180 to 180, default 30 |  |
| Iterations | int, 1 to 300, default 40 |  |
| Strength | float, 0.05 to 1, default 0.4 |  |
| Carry distance | float, 0.005 to 0.15, default 0.03 |  |
| Shadow angle | float, 0.2 to 4, default 1 |  |

## Export

### ExportHeightmap

Write 16-bit PNG / RAW heightmap

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| File | file path |  |
| Format | choice: PNG 16-bit / RAW float32 |  |
| Export on every compute | toggle, default off |  |

### ExportMesh

Write OBJ mesh

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| File | file path |  |
| Mesh resolution | int, 32 to 1024, default 256 |  |
| Height scale | float, 0.01 to 2, default 0.25 |  |
| Export on every compute | toggle, default off |  |

### ExportTexture

Write albedo/texture PNG

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| File | file path |  |
| Export on every compute | toggle, default off |  |

### SurfaceDisplacement

Shapes planets and the infinite ground plane from a field graph

| Port | Direction | Type |
| :--- | :--- | :--- |
| field | in | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, -4 to 4, default 1 | Weight of this field against the built-in layers. They span roughly -0.5..0.5 of the relief budget, so 1.0 makes the field as strong as a full layer. |
| Update the viewport | toggle, default on | Off: keep the graph but stop shaping the surfaces, without having to disconnect it. |

### TerrainDisplacement

Displaces the viewport terrain on the GPU from a field graph

| Port | Direction | Type |
| :--- | :--- | :--- |
| field | in | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, -2 to 2, default 0.05 | How far the field moves the surface, in world units (the terrain tile is 1 unit across). |
| Update the viewport | toggle, default on | Off: keep the graph but stop displacing, without having to disconnect it. |

### TerrainOutput

Final terrain: combines height layers + material, zero edges

| Port | Direction | Type |
| :--- | :--- | :--- |
| heightmap | in | heightmap |
| extra layer 1 | in (optional) | heightmap |
| extra layer 2 | in (optional) | heightmap |
| albedo | in (optional) | texture |
| heightmap | out | heightmap |
| albedo | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Combine layers | choice: Add / Max (merge) / Min |  |
| Layer strength | float, 0 to 2, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0.12 | Fades terrain to zero at the borders — the final island/tile edge treatment. |
| Edge curve | choice: Smooth / Linear / Steep (cliff) |  |
| Final height range | range |  |
| Remap to range | toggle, default on |  |

### TerrainSurface

Shades the viewport terrain from a field graph, per pixel on the GPU

| Port | Direction | Type |
| :--- | :--- | :--- |
| color | in | field (color) |
| roughness | in (optional) | field (number) |
| bump | in (optional) | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Bump strength | float, 0 to 16, default 1 | How strongly the bump field tilts the surface normal. This is shading only — it does not move the geometry, which is what TerrainDisplacement is for. |
| Bump sample distance | float, 0.0001 to 0.1, default 0.004 | How far apart the bump is sampled. Too small and it is noise; too large and it flattens. |
| Update the viewport | toggle, default on | Off: keep the graph but go back to the usual shading, without having to disconnect it. |

## Field Bridge

### Rasterize

Bakes a field into a heightmap so raster nodes (erosion, blur) can work on it

| Port | Direction | Type |
| :--- | :--- | :--- |
| field | in | field (number) |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Region centre | x/y pair |  |
| Region size | float, 0.001 to 100, default 1 | How much of the field's space this buffer covers. Smaller values zoom in — the field has no resolution of its own, so this is what decides the detail you capture. |
| Sample height | float, -10 to 10, default 0 | The Y plane the field is sampled on, for 3D fields. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Sample

Reads a heightmap as a field, so sculpted or eroded terrain can drive a shader

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Region centre | x/y pair |  |
| Region size | float, 0.001 to 100, default 1 |  |
| Value scale | float, -8 to 8, default 1 |  |
| Repeat outside the region | toggle, default off | Off: points outside the buffer clamp to its edge. On: the buffer tiles infinitely. |

## Field Color

### FieldColorMix

Blends two colours — mix, add, multiply, screen, overlay, darken, lighten

| Port | Direction | Type |
| :--- | :--- | :--- |
| a | in (optional) | field (color) |
| b | in (optional) | field (color) |
| factor | in (optional) | field (number) |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Blend mode | choice: Mix / Add / Multiply / Screen / Overlay / Darken / Lighten |  |
| Amount (when unconnected) | float, 0 to 1, default 0.5 |  |

### FieldGradient

Turns a number into a colour through a gradient

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (number) |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Gradient | gradient |  |
| Input range | range |  |

## Field Displace

### FieldComputeNormal

Recovers the surface normal after displacement, so later nodes see the real shape

| Port | Direction | Type |
| :--- | :--- | :--- |
| height | in | field (number) |
| normal | out | field (vector) |
| slope | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Sample distance | float, 1e-05 to 1, default 0.01 | How far apart the samples are taken. Too small and the normal is noise; too large and it smooths real detail away. Roughly one pixel of the scale you care about. |
| Strength | float, 0 to 64, default 1 |  |
| Flip | toggle, default off |  |

### FieldDisplace

Turns a value into relief: displaces along the normal, up, or any direction

| Port | Direction | Type |
| :--- | :--- | :--- |
| amount | in | field (number) |
| direction | in (optional) | field (vector) |
| out | out | field (number) |
| offset | out | field (vector) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Direction | choice: Along the surface normal / Straight up / Along the vector input / Along a fixed direction | Along the normal gives relief that follows the surface, which is what you want on a curved world. Straight up is predictable and stacks cleanly. |
| Depth is in | choice: Real units / Relative to a size | Real units keep the displacement fixed when the scene is rescaled; relative keeps its proportion. |
| Depth | float, -1000 to 1000, default 1 |  |
| Reference size | float, 0.001 to 1000, default 1 | The size 'relative' depth is a fraction of. |
| Smoothing | float, 0 to 1, default 0 | Softens the displacement by sampling around each point. Costs four extra evaluations when above zero. |
| Smoothing radius | float, 0.0001 to 1, default 0.01 |  |
| Quality boost | int, 0 to 6, default 0 | Extra octaves of detail for this displacement only, beyond the caller's budget. Use when relief needs to be finer than the geometry carrying it. |
| Displace outwards only | toggle, default off | Discards negative displacement, so the surface can only be pushed out and never dented inward. |
| Direction X | float, -1 to 1, default 0 |  |
| Direction Y | float, -1 to 1, default 1 |  |
| Direction Z | float, -1 to 1, default 0 |  |

### FieldRedirect

Moves where another field is evaluated — warp, flow and distortion, on anything

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | field (number) |
| redirect | in (optional) | field (vector) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Mode | choice: Offset the position / Replace the position | Offset moves the evaluation point by the vector. Replace evaluates at the vector itself, which is how you project one space onto another. |
| Strength | float, -32 to 32, default 1 |  |
| Scale X | float, -8 to 8, default 1 |  |
| Scale Y | float, -8 to 8, default 1 |  |
| Scale Z | float, -8 to 8, default 1 |  |

### FieldZone

Confines one field to a region, fading into another outside it

| Port | Direction | Type |
| :--- | :--- | :--- |
| inside | in (optional) | field (number) |
| outside | in (optional) | field (number) |
| out | out | field (number) |
| mask | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape | choice: Sphere / Box |  |
| Centre (X,Z) | x/y pair |  |
| Centre Y | float, -1000 to 1000, default 0 |  |
| Size | float, 0.001 to 1000, default 1 |  |
| Fade | float, 0 to 1, default 0.25 | Width of the transition, as a fraction of the size. Zero gives a hard edge, which will show. |
| Ignore height | toggle, default on | On: the region is a column, so altitude does not matter. Off: a true sphere or box in 3D. |

## Field Input

### FieldAltitude

Height of this point above the reference plane

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (number) |

### FieldColorConstant

A fixed colour, to feed any colour input

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Colour | color |  |

### FieldConstant

A fixed number, to feed any field input

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Value | float, -1000 to 1000, default 0.5 |  |

### FieldNormal

Direction the surface faces at this point

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (vector) |

### FieldOrientation

Compass direction the surface faces, as -1 to 1

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (number) |

### FieldPosition

Position of the point being evaluated — the root of most graphs

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (vector) |

### FieldSlope

Steepness here: 1 flat, 0 vertical, -1 flat facing down

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (number) |

### FieldTexCoord

Texture coordinates for this point — the input to any mapped texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (uv) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Projection | choice: Top down (XZ) / Front (XY) / Side (ZY) |  |
| Scale | x/y pair |  |
| Offset | x/y pair |  |
| Rotation ° | float, -180 to 180, default 0 |  |

### FieldTime

Current time in seconds — the hook for animation

| Port | Direction | Type |
| :--- | :--- | :--- |
| out | out | field (number) |

## Field Material

### FieldDistribution

Where a material belongs: by altitude, steepness and which way the ground faces

| Port | Direction | Type |
| :--- | :--- | :--- |
| altitude | in (optional) | field (number) |
| slope | in (optional) | field (number) |
| orientation | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| By altitude | toggle, default on |  |
| Altitude band | range |  |
| Altitude fade | float, 0 to 10, default 0.1 | How gradually the material gives out at the edges of the band. Zero gives a hard line, which will look drawn on. |
| By steepness | toggle, default off |  |
| Slope band | range | 1 is flat ground, 0 is a vertical face. So rock wants a low band and grass a high one. |
| Steepness fade | float, 0 to 1, default 0.1 |  |
| By facing | toggle, default off |  |
| Facing band | range | Which compass direction the ground faces, as -1 to 1. Snow lingers on one side of a ridge and not the other. |
| Facing fade | float, 0 to 1, default 0.2 |  |
| Invert | toggle, default off |  |

## Field Math

### FieldCurve

Shapes a value with a curve: gain, bias, step or smoothstep

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape | choice: Gain (gamma) / Smoothstep / Step / Bias / Invert |  |
| Amount | float, 0.05 to 8, default 1 |  |
| Edges | range |  |

### FieldMath

Combines two values: add, subtract, multiply, and the rest

| Port | Direction | Type |
| :--- | :--- | :--- |
| a | in (optional) | field (number) |
| b | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Add / Subtract / Multiply / Divide / Minimum / Maximum / Power / Modulo / Absolute difference |  |
| A (when unconnected) | float, -100 to 100, default 0 |  |
| B (when unconnected) | float, -100 to 100, default 1 |  |

### FieldMix

Blends between two inputs by a factor

| Port | Direction | Type |
| :--- | :--- | :--- |
| a | in (optional) | field (number) |
| b | in (optional) | field (number) |
| factor | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Blend (when unconnected) | float, 0 to 1, default 0.5 |  |

### FieldRemap

Rescales a value from one range into another

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Input range | range |  |
| Output range | range |  |
| Clamp to the output range | toggle, default on |  |

### FieldTrig

Trigonometry: sine, cosine, tangent and their inverses

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Function | choice: Sine / Cosine / Tangent / Arc sine / Arc cosine / Arc tangent / Hyperbolic sine / Hyperbolic cosine / Hyperbolic tangent |  |
| Work in degrees | toggle, default off | Interpret the input (and produce the output of the inverse functions) in degrees rather than radians. |
| Input scale | float, -32 to 32, default 1 |  |

### FieldVectorOp

Vector maths: length, dot, cross, normalize, distance

| Port | Direction | Type |
| :--- | :--- | :--- |
| a | in (optional) | field (vector) |
| b | in (optional) | field (vector) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Length / Dot product / Distance / Normalize (X) / Cross product (X) |  |

## Field Noise

### FieldNoise

3D coherent noise — the basis of procedural terrain and texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| position | in (optional) | field (vector) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Rolling (fBm) / Ridged / Billow |  |
| Seed | seed |  |
| Feature scale | float, 0.01 to 200, default 3 | How many features fit across a unit of space. Low values give continents, high values give gravel. |
| Octaves | int, 1 to 12, default 6 | Levels of detail. Capped by the caller's level-of-detail budget, so distant points cost less automatically. |
| Amplitude | float, 0 to 8, default 1 |  |
| Offset | float, -4 to 4, default 0 |  |

### FieldShape

Analytic shapes - waves, bands, bumps, cones and steps, as a function

| Port | Direction | Type |
| :--- | :--- | :--- |
| position | in (optional) | field (vector) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape | choice: Sine wave / Square wave / Triangle wave / Sawtooth / Gaussian bump / Cone / Band / Step |  |
| Center | x/y pair |  |
| Direction | float, -180 to 180, default 0 | Which way the waves run, the band lies, or the step faces, in degrees on the ground plane. |
| Frequency | float, 0.01 to 200, default 4 | Wave repetitions per unit of ground. Waves only. |
| Width | float, 0.001 to 8, default 0.25 | Radius of the bump or cone; thickness of the band. |
| Phase | float, -2 to 2, default 0 |  |
| Amplitude | float, 0 to 8, default 1 |  |
| Offset | float, -4 to 4, default 0 |  |

### FieldVoronoi

Cellular (Worley) noise - cracks, plates, scree and crater fields

| Port | Direction | Type |
| :--- | :--- | :--- |
| position | in (optional) | field (vector) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Cell size | float, 0.01 to 200, default 6 | How many cells fit across a unit of space. Low values give continent-sized plates, high values give gravel. |
| Jitter | float, 0 to 1, default 1 | How far each cell's point may wander from its centre. 0 is a perfect grid; 1 is fully irregular. |
| Octaves | int, 1 to 6, default 1 | Stacks the cells at rising frequency and falling weight, the way fBm stacks noise: continents of plates with gravel in the cracks. 1 is the plain pattern. |
| Cell shape | choice: Round (Euclidean) / Diamond (Manhattan) / Square (Chebyshev) | The distance the cells are measured with, which is what decides their silhouette. |
| Pattern | choice: Distance to nearest (F1) / Distance to second (F2) / Distance to the seam (F2 - F1) / Flat cell value | F1 is zero at each cell's own point and rises outward: cell centres become pits and the seams between them become ridges. Crater fields, dimpled rock.  F2 is the same one cell further out - rounder, smoother swells.  F2 - F1 is zero exactly on the seam between two cells and highest at the centre: domes with sharp creases between them. Invert it and the seams become the cracks.  Flat cell value gives each cell one random height - plates, terraces, tectonic blocks. |
| Amplitude | float, 0 to 8, default 1 |  |
| Offset | float, -4 to 4, default 0 |  |
| Invert | toggle, default off | Turns pits into domes, and walls into channels. |

## Filter

### Clamp

Clamp with optional smooth shoulders

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Clamp range | range |  |
| Shoulder softness | float, 0 to 0.5, default 0 |  |

### Craggy

Slope-targeted rocky detail; flats stay clean

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Detail scale | float, 4 to 128, default 24 |  |
| Strength | float, 0 to 0.3, default 0.06 |  |
| Slope threshold | float, 0 to 1, default 0.3 |  |
| Threshold softness | float, 0.02 to 0.6, default 0.2 |  |
| Octaves | int, 2 to 9, default 5 |  |
| Seed | seed |  |

### Curve

Remaps elevations through a drawn curve - the gradient's brightness is the transfer function

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Curve | gradient |  |
| Strength | float, 0 to 1, default 1 |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Equalize

Spreads elevations across the full range - contrast back after a long chain

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, 0 to 1, default 1 | 1 is full equalisation; lower blends back toward the original distribution. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### ExpandShrink

Morphological dilate / erode

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius | float, 0.001 to 0.05, default 0.01 |  |
| Shrink (erode) | toggle, default off |  |

### Fold

Fold values around midline — creates ridged detail

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 6, default 1 |  |

### GammaCorrection

Power-curve contrast

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Gamma | float, 0.05 to 6, default 1 |  |

### Median

Removes single-cell spikes and pits without softening edges

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius | int, 1 to 3, default 1 | 1 looks at 3x3 cells, 2 at 5x5, 3 at 7x7. Larger wipes bigger artifacts and more real detail with them. |
| Passes | int, 1 to 4, default 1 | Applying it again flattens what one pass left; a few passes approach a stable, blocky simplification. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Morphology

Dilate, erode and their compositions

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Dilate / Erode / Open / Close / Gradient / Top hat / Black hat |  |
| Radius (px) | int, 1 to 64, default 3 |  |
| Element | choice: Square / Octagon |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Plateau

Flatten tops above a level

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Level | float, 0 to 1, default 0.7 |  |
| Softness | float, 0.01 to 1, default 0.1 |  |

### PowerFractal

Terragen-style multi-scale fractal displacement

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| displacement_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Noise flavour | choice: Perlin / Billows / Ridges / Voronoi billows / Voronoi ridges |  |
| Lead-in scale | float, 0.05 to 4, default 1 | Largest visible variation (fraction of terrain width). Octaves between lead-in and feature scale ramp in with reduced amplitude. |
| Feature scale | float, 0.01 to 2, default 0.25 | Scale of the dominant, full-amplitude features. |
| Smallest scale | float, 0.0005 to 0.1, default 0.004 | Detail cutoff — nothing finer than this is added. |
| Seed | seed |  |
| Displacement amplitude | float, 0 to 1, default 0.15 |  |
| Displacement offset | float, -0.5 to 0.5, default 0 | Shifts displacement: positive raises plinths, negative sinks features. |
| Roughness | float, 0.3 to 1.6, default 1 | Per-octave gain multiplier; below 1 smooths high frequencies, above 1 exaggerates them. |
| Spike limit | float, 0.05 to 1, default 0.7 | Damps octave contributions on already-steep ground to prevent needle spikes. |
| Displace along normal | toggle, default off | Scales displacement with slope so cliffs bulge outward like real overhung rock (approximated). |
| Apply on slopes | range | Restrict displacement to this normalized slope band (e.g. 0.4..1 = only on steep faces). |
| Slope softness | float, 0.01 to 0.5, default 0.15 |  |

### Remap

Remap value range

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Target range | range |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Smooth

Gaussian-like smoothing

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius | float, 0 to 0.2, default 0.01 |  |

### Snow

Snow cover: snowline, settle-thaw, slip-off; outputs depth mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| snow_mask | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Snow amount | float, 0 to 0.3, default 0.06 |  |
| Snowline | float, 0 to 1, default 0.55 |  |
| Snowline falloff | float, 0.02 to 0.6, default 0.15 |  |
| Slip-off slope | float, 0.1 to 1, default 0.55 | Snow cannot cling to slopes steeper than this. |
| Settle-thaw iterations | int, 0 to 60, default 12 | Lets snow slide into hollows and compact — smooth, wind-packed accumulation. |
| Melt (low areas) | float, 0 to 1, default 0.3 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Stratify

Tilted rock strata exposed on cliff faces

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, 0.05 to 1, default 0.6 |  |
| Layer count | int, 4 to 80, default 18 |  |
| Tilt | float, 0 to 0.8, default 0.15 | Strata are tilted planes, not horizontal bands — the single most important realism control. |
| Tilt direction ° | float, -180 to 180, default 30 |  |
| Warp | float, 0 to 1, default 0.2 |  |
| Substrata | float, 0 to 1, default 0.4 | Finer secondary layering nested inside each stratum. |
| Only on slopes above | float, 0 to 1, default 0.25 |  |
| Slope softness | float, 0.02 to 0.5, default 0.15 |  |
| Seed | seed |  |

### Terrace

Stratified terraces: uneven layers, warped edges, altitude band

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Levels | int, 2 to 64, default 8 |  |
| Edge sharpness | float, 0.5 to 12, default 3 |  |
| Cliff bias | float, -1 to 1, default 0 | Skews each step: negative = wide flats with sharp cliffs above; positive = sharp base, sloped tops. |
| Strength | float, 0 to 1, default 1 |  |
| Seed | seed |  |
| Level thickness jitter | float, 0 to 1, default 0.3 | Randomizes each layer's thickness — natural geological strata are never evenly spaced. |
| Edge warp | float, 0 to 1, default 0.15 | Warps terrace edges with noise so contour lines wander instead of following exact heights. |
| Edge warp scale | float, 2 to 64, default 12 |  |
| Altitude band | range | Only terrace heights inside this normalized band; terrain outside is left untouched. |
| Band softness | float, 0.01 to 0.5, default 0.1 |  |

## Group

### MetaNode

A sub-graph collapsed into one node — group, name and reuse

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Inner graph | text | The encapsulated graph, stored with the project. Edit it by opening the MetaNode, not by hand. |
| Published parameters | text | Which inner parameters are exposed on this node. |
| Note | text | What this MetaNode is for — it becomes the tooltip when the node is reused from the library. |

## Hydrology

### FillBasins

Floods closed basins to their outlet - filled terrain for flow routing, plus lake depth and mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| depth | out | heightmap |
| mask | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Drainage slope | float, 0 to 0.01, default 0 | A hair of tilt across each filled flat so water still crosses it toward the outlet. Leave at 0 for true level lakes; raise it slightly when the filled surface feeds flow accumulation or erosion. |
| Ignore puddles below | float, 0 to 0.2, default 0 | Depth and mask ignore anything shallower than this, so a thousand pinprick hollows do not read as lakes. The filled terrain is unaffected. |
| Normalise depth | toggle, default on | Scales depth to 0..1 so it can drive a mask or a blend directly. Off leaves it in terrain units. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Flood

Standing water at a set level

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| sources | in (optional) | ? |
| output | out | heightmap |
| depth | out | heightmap |
| water_mask | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Water level | float, 0 to 1, default 0.3 |  |
| Fill | choice: Everywhere below / Connected to the edge / From source points |  |
| Normalize depth | toggle, default on |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

## Logic

### Compare

Compare two inputs into a mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| input A | in | heightmap |
| input B | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: A > B / A < B / |A - B| < tol / |A - B| > tol |  |
| Tolerance | float, 0 to 1, default 0.05 |  |
| Softness | float, 0 to 0.3, default 0.02 |  |

### Repeat

Loop: apply an operation N times

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Smooth / Thermal step / Expand / Shrink / Fold ridges |  |
| Loop count | int, 1 to 64, default 4 |  |
| Strength per pass | float, 0.05 to 1, default 0.5 |  |

### Select

Select one of four inputs by index or selector map

| Port | Direction | Type |
| :--- | :--- | :--- |
| input 1 | in | heightmap |
| input 2 | in (optional) | heightmap |
| input 3 | in (optional) | heightmap |
| input 4 | in (optional) | heightmap |
| selector | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Index | int, 0 to 3, default 0 |  |
| Blend by selector map | toggle, default off | When on, the selector map (0..1) cross-fades between the connected inputs instead of the index. |

### Switch

Route input A or B to output

| Port | Direction | Type |
| :--- | :--- | :--- |
| input A | in | heightmap |
| input B | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Use input B | toggle, default off |  |

### Threshold

Binary/soft threshold to a mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Level | float, 0 to 1, default 0.5 |  |
| Softness | float, 0 to 0.5, default 0.05 |  |
| Invert | toggle, default off |  |

### Thru

Pass-through / organization pin

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |

## Mask

### AreaRemove

Drop small connected blobs

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Threshold | float, 0 to 1, default 0.5 |  |
| Min area (fraction) | float, 0 to 0.5, default 0.001 |  |
| Invert | toggle, default off |  |

### DistanceField

Distance to a shape - shoreline gradients, wetness falloffs, anything that happens near something

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape threshold | float, 0 to 1, default 0.5 | Where the input counts as being the shape. Feed a mask and leave it at 0.5; feed a heightmap and this becomes the altitude the distance is measured from. |
| Measure from the outside | toggle, default off | Swap what counts as the shape: distance from dry land instead of distance from the water. |
| Output | choice: Fade from the shape / Distance from the shape / Signed distance | Fade: 1 at the shape, falling to 0 at the reach - a ready-made falloff mask. Distance: 0 at the shape, 1 at the reach. Signed: 0.5 on the edge, below inside, above outside. |
| Reach | float, 0.005 to 1, default 0.15 | How far the field extends, as a fraction of the tile. Everything further than this saturates. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### SelectAltitude

Select by height band

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 |  |
| Invert | toggle, default off |  |
| Altitude band | range |  |

### SelectCavities

Ambient-occlusion-like cavity map

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 |  |
| Invert | toggle, default off |  |
| Radius | float, 0.005 to 0.1, default 0.02 |  |

### SelectCurvature

Select concave (valleys) or convex (ridges)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 |  |
| Invert | toggle, default off |  |
| Mode | choice: Convex (ridges) / Concave (valleys) |  |
| Feature scale | float, 0.002 to 0.1, default 0.01 |  |

### SelectSlope

Select by slope steepness

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 |  |
| Invert | toggle, default off |  |
| Slope band | range |  |

### Skeleton

Thin a mask to its centerlines

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Threshold | float, 0 to 1, default 0.5 |  |

## Material

### AOFromHeight

Ambient occlusion baked from a height input

| Port | Direction | Type |
| :--- | :--- | :--- |
| height | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius | float, 0.002 to 0.15, default 0.02 |  |
| Strength | float, 0 to 3, default 1 |  |

### AlbedoToPBR

Derive normal + roughness maps from an albedo texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| albedo | in | texture |
| normal | out | texture |
| roughness | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normal strength | float, 0.1 to 10, default 2 |  |
| Roughness base | float, 0 to 1, default 0.8 |  |
| Roughness variation | float, 0 to 1, default 0.3 |  |
| Bright = smooth | toggle, default on |  |

### ChannelMix

Pack three grayscale inputs into one RGB texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| red | in | heightmap |
| green | in (optional) | heightmap |
| blue | in (optional) | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normalize inputs | toggle, default on |  |

### ColorAdjust

Color correction: brightness, contrast, saturation, hue, tint

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Brightness | float, 0.2 to 3, default 1 |  |
| Contrast | float, 0.2 to 3, default 1 |  |
| Saturation | float, 0 to 3, default 1 |  |
| Hue shift ° | float, -180 to 180, default 0 |  |
| Tint R | float, 0 to 2, default 1 |  |
| Tint G | float, 0 to 2, default 1 |  |
| Tint B | float, 0 to 2, default 1 |  |

### CurvatureFromHeight

Convex/concave curvature map from height

| Port | Direction | Type |
| :--- | :--- | :--- |
| height | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Feature scale | float, 0.002 to 0.1, default 0.01 |  |
| Contrast | float, 0.1 to 6, default 1 |  |

### FlatColor

Solid color material (procedural function + color)

| Port | Direction | Type |
| :--- | :--- | :--- |
| mask | in (optional) | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Red | float, 0 to 1, default 0.5 |  |
| Green | float, 0 to 1, default 0.45 |  |
| Blue | float, 0 to 1, default 0.4 |  |

### GradientMap

Recolor a texture through a gradient by luminance

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Gradient | gradient |  |
| Amount | float, 0 to 1, default 1 |  |

### Levels

Levels: remap input black/white/gamma to output range

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Input black | float, 0 to 1, default 0 |  |
| Input white | float, 0 to 1, default 1 |  |
| Gamma | float, 0.1 to 4, default 1 |  |
| Output black | float, 0 to 1, default 0 |  |
| Output white | float, 0 to 1, default 1 |  |
| Per channel | toggle, default off | Off: operate on luminance and keep the hue. On: apply the curve to R, G and B separately. |

### MaskToTexture

Grayscale mask or heightmap as a texture channel

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normalize | toggle, default on |  |
| Scale | float, 0 to 2, default 1 |  |
| Offset | float, -1 to 1, default 0 |  |

### MaterialOutput

The material: base color, normal, roughness, metallic, height and AO channels

| Port | Direction | Type |
| :--- | :--- | :--- |
| base color | in (optional) | texture |
| normal | in (optional) | texture |
| roughness | in (optional) | texture |
| metallic | in (optional) | texture |
| height | in (optional) | texture |
| ambient occlusion | in (optional) | texture |
| preview | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Material name | text |  |
| Roughness | float, 0.02 to 1, default 0.85 | Used where no roughness map is connected, and as a multiplier for the map. |
| Metallic | float, 0 to 1, default 0 |  |
| Specular | float, 0 to 1, default 0.35 |  |
| Sky reflection | float, 0 to 1, default 0.25 |  |
| Translucency | float, 0 to 1, default 0 |  |
| Transparency | float, 0 to 1, default 0 |  |
| Normal strength | float, 0 to 4, default 1 |  |
| Displacement | float, 0 to 0.1, default 0 | Height map displacement applied to the surface, in world units. |

### NormalBlend

Combine two normal maps (whiteout blend)

| Port | Direction | Type |
| :--- | :--- | :--- |
| base | in | texture |
| detail | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Detail strength | float, 0 to 3, default 1 |  |

### PBRMaterial

Download a CC0 photoscanned PBR material set from ambientCG (albedo/normal/roughness/AO)

| Port | Direction | Type |
| :--- | :--- | :--- |
| albedo | out | texture |
| normal | out | texture |
| roughness | out | texture |
| ao | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| ambientCG asset ID | text |  |
| Resolution | choice: 1K / 2K / 4K / 8K |  |
| Mapping | choice: Stretch / Tile |  |
| Tiles across | float, 1 to 64, default 8 |  |

### SplatMaterial

Compose albedo from a splatmap and up to 4 layer textures

| Port | Direction | Type |
| :--- | :--- | :--- |
| splat | in | texture |
| layer R | in | texture |
| layer G | in (optional) | texture |
| layer B | in (optional) | texture |
| layer A | in (optional) | texture |
| texture | out | texture |

### Splatmap

Pack up to 4 masks into RGBA splat weights (normalized)

| Port | Direction | Type |
| :--- | :--- | :--- |
| mask R | in | heightmap |
| mask G | in (optional) | heightmap |
| mask B | in (optional) | heightmap |
| mask A | in (optional) | heightmap |
| splat | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normalize weights | toggle, default on |  |

### TextureBlend

Blend two textures by mask / mode / opacity

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture A | in | texture |
| texture B | in | texture |
| mask | in (optional) | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Mode | choice: Normal / Multiply / Add / Overlay / Screen / Height tint |  |
| Opacity | float, 0 to 1, default 1 |  |

### TextureFile

Load an image texture (PNG/JPG/TGA/BMP) with mapping modes

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Image file | file path |  |
| Mapping | choice: Stretch / Tile / Tile offset |  |
| Tiles across | float, 1 to 64, default 8 |  |
| Brightness | float, 0.2 to 3, default 1 |  |

### TextureToMask

Texture luminance back into a mask/heightmap

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Channel | choice: Luminance / Red / Green / Blue / Alpha |  |

### TextureTransform

Tile, scale, offset and rotate a texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Tiles | x/y pair |  |
| Offset | x/y pair |  |
| Rotation | float, -180 to 180, default 0 |  |
| Mirror repeat | toggle, default off | Flips alternate tiles so seams are less visible. |

## Operator

### Blend

Blend two heightmaps (many modes)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input A | in | heightmap |
| input B | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Mode | choice: Mix / Add / Subtract / Multiply / Min / Max / Smooth min / Smooth max / Overlay / Screen / Difference |  |
| Factor | float, 0 to 1, default 0.5 |  |
| Smooth k | float, 0.01 to 0.5, default 0.1 |  |

### Math

Per-pixel math on one input

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Multiply / Add / Power / Absolute / Negate / One minus / Square root / Log1p / Sine / Smoothstep |  |
| Value | float, -4 to 4, default 1 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### MathGradient

Derivatives: dx, dy, slope magnitude, laplacian

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| dx | out | heightmap |
| dy | out | heightmap |
| magnitude | out | heightmap |
| laplacian | out | heightmap |

### MixLayers

Height-stack: stack up to 4 layers by max

| Port | Direction | Type |
| :--- | :--- | :--- |
| layer 1 | in | heightmap |
| layer 2 | in (optional) | heightmap |
| layer 3 | in (optional) | heightmap |
| layer 4 | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Blend softness | float, 0 to 0.4, default 0.05 |  |

## Path

### PathCarve

Carves along a drawn path - riverbeds, road cuts, canyons; negative depth builds walls

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| path_mask | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Points | text | The path, as x,z pairs in tile coordinates (0..1), separated by spaces. Edit here, or ask the AI to "draw a river from the northwest to the sea". |
| Smoothing | int, 0 to 6, default 3 | Chaikin corner-cutting passes: 0 keeps the polyline's corners, a few make a flowing curve. |
| Closed loop | toggle, default off |  |
| Width | float, 0.001 to 0.3, default 0.02 | Half the carve reaches this far from the line, as a fraction of the tile. |
| Depth | float, -0.5 to 0.5, default 0.08 | How deep the centre cuts below the surface. Negative raises instead: walls, levees, causeways. |
| Profile | choice: Rounded (U) / Sharp (V) / Flat bed | The cross-section: U for rivers, V for gorges, a flat bed with shoulders for roads and canals. |
| Grade along the path | float, 0 to 1, default 0.5 | 0 follows the terrain exactly - the cut is everywhere the same depth. 1 grades the bed toward the path's smoothed height, the way water and roadbuilders do, cutting deeper through rises and shallower in dips. |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

## Points

### PointsFilter

Keep points by mask and chance

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| mask | in (optional) | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Mask band | range |  |
| Keep fraction | float, 0 to 1, default 1 |  |
| Seed | seed |  |

### PointsRelax

Even out point spacing

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 50, default 8 |  |
| Strength | float, 0.01 to 1, default 0.5 |  |

### PointsSDF

Distance to the nearest point

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| distance | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Reach | float, 0.005 to 1, default 0.2 |  |
| Invert | toggle, default off |  |

### PointsToMask

Stamp points into a raster

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Kernel | choice: Gaussian / Cone / Disc |  |
| Radius | float, 0.001 to 0.5, default 0.03 |  |
| Amplitude | float, 0 to 4, default 1 |  |
| Scale by point value | toggle, default off |  |
| Blend | choice: Max / Add |  |

### ScatterPoints

Scatter points over the tile

| Port | Direction | Type |
| :--- | :--- | :--- |
| density | in (optional) | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Point count | int, 1 to 50000, default 500 |  |
| Mode | choice: Random / Jittered grid / Spaced |  |
| Min spacing | float, 0.001 to 0.3, default 0.02 |  |
| Seed | seed |  |

## Primitive

### Constant

Constant level

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Value | float, -1 to 2, default 0.5 |  |

### Crater

Impact craters: bowl, rim lip, ejecta blanket (single or field)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in (optional) | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Profile | choice: Single crater / Crater field |  |
| Scale | float, 0.02 to 1, default 0.3 |  |
| Depth | float, 0.05 to 1, default 0.4 |  |
| Rim lip | float, 0 to 1, default 0.5 | Sharpness/height of the raised rim wall. |
| Ejecta extent | float, 0.1 to 2, default 0.6 |  |
| Floor level | float, 0 to 1, default 0.15 | Clamps the bowl bottom — flat crater floors. |
| Rim irregularity | float, 0 to 1, default 0.3 |  |
| Position | x/y pair |  |
| Field count | int, 2 to 64, default 12 |  |
| Seed | seed |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Dunes

Sand dunes: asymmetric slip faces, crest chaos, ripples

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Wind direction ° | float, -180 to 180, default 30 |  |
| Dune wavelength | float, 0.02 to 0.5, default 0.12 |  |
| Asymmetry | float, 0.5 to 0.95, default 0.75 | Windward slope is long and gentle; the slip face is short and steep (real dunes ~0.8). |
| Crest chaos | float, 0 to 1, default 0.5 |  |
| Ripples | float, 0 to 1, default 0.25 | Secondary small-scale ripple field on top. |
| Ripple scale | float, 2 to 20, default 6 |  |
| Seed | seed |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### FakeStones

Terragen-style fake stones: boulders/rocks as displacement

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| density_mask | in (optional) | heightmap |
| output | out | heightmap |
| stone_mask | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Stone scale | float, 0.004 to 0.25, default 0.03 | Stone size as a fraction of terrain width; smaller = more, denser stones. |
| Stone density | float, 0.02 to 1, default 0.5 |  |
| Stone tallness | float, 0.05 to 2, default 0.6 |  |
| Pancake effect | float, 0 to 1, default 0.3 | Squashes stones flat into slabs while keeping their footprint — 0 round boulders, 1 flat plates. |
| Seed | seed |  |
| Vary density | float, 0 to 1, default 0.6 | Large-scale patchiness: clusters of stones with clear ground between. |
| Density variation scale | float, 1 to 16, default 4 |  |
| Size variation | float, 0 to 1, default 0.5 |  |
| Grow on slopes | range | Stones appear only where terrain slope is inside this band (rockfall collects on gentler ground). |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Fractal

Non-noise fractals: diamond-square, fault lines

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Diamond-square / Fault formation |  |
| Seed | seed |  |
| Roughness | float, 0.3 to 1.6, default 0.9 |  |
| Fault count | int, 10 to 2000, default 200 |  |
| Fault softness | float, 0 to 0.2, default 0.02 |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### GeologicalStrata

Layered rock strata from an input heightmap

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Layers | int, 2 to 32, default 8 |  |
| Layer hardness | float, 1 to 8, default 2.5 |  |
| Seed | seed |  |
| Thickness variation | float, 0 to 1, default 0.5 |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### HeightmapFile

Import a heightfield image (8/16-bit PNG, JPG, TGA)

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Heightfield image | file path |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Noise

Coherent noise: fBm, ridged, billow, swiss, value, cellular

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Perlin fBm / Ridged / Billow / Swiss (eroded ridges) / Value fBm / Worley F1 / Worley F2 / Worley edges / Worley F1*F2 |  |
| Seed | seed |  |
| Octaves | int, 1 to 16, default 9 |  |
| Lacunarity | float, 1.2 to 4, default 2 |  |
| Gain | float, 0.05 to 0.95, default 0.5 |  |
| Ridge weight | float, 0 to 1, default 0.7 |  |
| Swiss warp | float, 0 to 0.6, default 0.15 |  |
| Cell jitter | float, 0 to 1, default 1 |  |
| Wavenumber | x/y pair |  |
| Offset | x/y pair |  |
| Rotation ° | float, -180 to 180, default 0 |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Shape

Geometric base shapes: slope, bump, crater, cone, ridge line

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Slope plane / Bump / Crater / Cone / Ridge line / Border falloff / Wave sine / Wave square / Wave triangle / Step / Band / Paraboloid |  |
| Center | x/y pair |  |
| Radius | float, 0.01 to 1.5, default 0.35 |  |
| Hardness | float, 0.2 to 8, default 1 |  |
| Direction ° | float, -180 to 180, default 0 |  |
| Frequency | float, 0.25 to 64, default 4 |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Stamp

Terrain modeling: stamp a heightfield shape onto the terrain

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| shape | in (optional) | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape image (if no input) | file path |  |
| Position | x/y pair |  |
| Size | float, 0.02 to 2, default 0.5 |  |
| Rotation ° | float, -180 to 180, default 0 |  |
| Height | float, -2 to 2, default 0.5 |  |
| Blend | choice: Add / Max (merge) / Min (carve) / Replace by mask |  |
| Edge falloff | float, 0 to 0.5, default 0.15 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### WhiteNoise

Raw per-cell white noise

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Remap to range | toggle, default on |  |
| Output range | range |  |
| Invert | toggle, default off |  |
| Gain (gamma) | float, 0.05 to 4, default 1 |  |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

## Render

### RenderCamera

Camera and tone mapping for the render

| Port | Direction | Type |
| :--- | :--- | :--- |
| camera | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Exposure | float, 0.3 to 3, default 1.1 |  |
| Terrain height scale | float, 0.02 to 0.8, default 0.22 |  |
| Terrain size (m) | float, 100 to 100000, default 5000 |  |

### RenderQuality

Offline render engine, resolution and sampling

| Port | Direction | Type |
| :--- | :--- | :--- |
| quality | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Engine | choice: Mitsuba 3 / Blender Cycles / LuxCoreRender / OpenGL viewport |  |
| Width | int, 64 to 8192, default 1920 |  |
| Height | int, 64 to 8192, default 1080 |  |
| Samples | int, 8 to 4096, default 128 |  |
| Output file | file path |  |

## Texture

### ColorizeGradient

Map height to a color gradient

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Gradient | gradient |  |
| Multiply hillshade | toggle, default on |  |

### NormalMap

Tangent-space normal map from height

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, 0.05 to 8, default 1 |  |

### TerrainTexture

Physically-inspired layered terrain albedo

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| flow | in (optional) | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Snow line | float, 0 to 1, default 0.75 |  |
| Vegetation ceiling | float, 0 to 1, default 0.55 |  |
| Rock slope threshold | float, 0.05 to 1, default 0.45 |  |
| Snow max slope | float, 0.05 to 1, default 0.55 |  |
| Sand level | float, 0 to 0.4, default 0.06 |  |
| Noise breakup | float, 0 to 1, default 0.5 |  |
| Detail scale | float, 2 to 96, default 24 |  |
| Flow darkening | float, 0 to 1, default 0.4 |  |
| Multiply hillshade | toggle, default off |  |

## Transform

### Shear

Directional rock shearing / folding (Gaea-style)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shear scale | float, 0.02 to 1, default 0.15 |  |
| Shear amount | float, 0 to 0.3, default 0.05 |  |
| Folding | float, 0 to 1, default 0.3 |  |
| Direction ° | float, -180 to 180, default 0 |  |
| Self modulated | toggle, default on | Height drives shear strength — bands show on slopes, flats stay intact. |
| Seed | seed |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Transform

Translate / scale / rotate

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Translate | x/y pair |  |
| Scale | x/y pair |  |
| Rotate ° | float, -180 to 180, default 0 |  |
| Outside area | choice: Clamp / Mirror / Tile |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### WarpDirectional

Warp along gradient — wind-swept shapes

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Direction ° | float, -180 to 180, default 30 |  |
| Amplitude | float, 0 to 0.2, default 0.02 |  |
| Scale by height | toggle, default on |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### WarpNoise

Domain warp by internal fBm noise

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Amplitude | float, 0 to 0.5, default 0.08 |  |
| Warp frequency | x/y pair |  |
| Octaves | int, 1 to 10, default 4 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

