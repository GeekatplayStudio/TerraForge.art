# Node reference

Every node in Geekatplay TerraForge — 245 across 32 categories. Generated from the registry itself by `tools/gen_node_docs.cpp`, so what is written here is what is constructed; regenerate with the `node_docs_gen` target after adding a node.

| Category | Nodes |
| :--- | :--- |
| [Analysis](#analysis) | 5 |
| [Animation](#animation) | 6 |
| [Atmosphere](#atmosphere) | 4 |
| [Camera](#camera) | 6 |
| [Cloud](#cloud) | 4 |
| [Effect](#effect) | 8 |
| [Erosion](#erosion) | 11 |
| [Export](#export) | 8 |
| [Field Bridge](#field-bridge) | 2 |
| [Field Color](#field-color) | 5 |
| [Field Convert](#field-convert) | 10 |
| [Field Displace](#field-displace) | 4 |
| [Field Input](#field-input) | 9 |
| [Field Material](#field-material) | 1 |
| [Field Math](#field-math) | 6 |
| [Field Noise](#field-noise) | 5 |
| [Filter](#filter) | 23 |
| [Group](#group) | 1 |
| [Hydrology](#hydrology) | 3 |
| [Light](#light) | 6 |
| [Logic](#logic) | 6 |
| [Mask](#mask) | 14 |
| [Material](#material) | 26 |
| [Operator](#operator) | 4 |
| [Path](#path) | 7 |
| [Points](#points) | 12 |
| [Primitive](#primitive) | 22 |
| [Render](#render) | 8 |
| [Scene](#scene) | 7 |
| [Shape](#shape) | 1 |
| [Texture](#texture) | 3 |
| [Transform](#transform) | 8 |

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
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### RelativeElevation

Height relative to the neighborhood

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius (px) | int, 2 to 128, default 24 | How far out the surrounding ground is sampled before asking how high this point stands above it. Small radii find local bumps; large ones find whether you are on a ridge or in a valley at all. |

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
| Custom size | int, 8 to 8192, default 256 | The size to resample to, when Custom is chosen above. Resampling changes how much detail a buffer can hold without changing the graph's own resolution. |
| Smooth interpolation | toggle, default on | Off: nearest neighbour, which keeps hard edges and gives a deliberately blocky, terraced look. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### TerrainMetrics

Surface statistics: rugosity, TRI, shape index

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Metric | choice: Rugosity (local std dev) / Ruggedness (TRI) / Shape index / Unsphericity / Valley depth | Which statistic is measured. Rugosity is surface area against footprint - how crumpled the ground is. TRI is the average height difference to the neighbours, which finds broken ground. Shape index separates domes from hollows. |
| Radius (px) | int, 1 to 64, default 4 | How far the measurement reaches. Small radii describe the surface texture; large ones describe the landform. |

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
| Contrast | float, 0.1 to 4, default 1 | Stretches the wetness values apart. The raw index is bunched into a narrow band, so without this most of the map reads as the same dampness. |
| Route through basins | toggle, default on | Water that reaches a hollow fills it and flows on. Off follows the raw surface, where every stream stops at the first pit it meets. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

## Animation

### AnimationClip

[Planned] Reusable clip of keyed attributes: paste, shift, stretch, reverse

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### AnimationSequence

The shot: frame range, frame rate, output size and folder for the sequence renderer

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Start (s) | float, 0 to 100000, default 0 |  |
| End (s) | float, 0 to 100000, default 10 |  |
| Frames per second | float, 1 to 240, default 30 |  |
| Width | int, 64 to 8192, default 1280 |  |
| Height | int, 64 to 8192, default 720 |  |
| Output folder | text |  |
| Sweep the sun | toggle, default off |  |
| Sun from: azimuth ° | float, 0 to 360, default 90 |  |
| Sun from: altitude ° | float, -10 to 90, default 10 |  |
| Sun to: azimuth ° | float, 0 to 360, default 270 |  |
| Sun to: altitude ° | float, -10 to 90, default 10 |  |

### Dynamics

[Planned] Forward dynamics, linking and tracking between objects

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### KeyframeCurve

[Planned] Editable time spline: keys, tangents, interpolation per segment

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### Oscillator

A wave of time: sine, triangle, square or sawtooth, for anything that should pulse

| Port | Direction | Type |
| :--- | :--- | :--- |
| time | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Waveform | choice: Sine / Triangle / Square / Sawtooth |  |
| Frequency (Hz) | float, 0.001 to 100, default 0.5 |  |
| Phase | float, 0 to 1, default 0 |  |
| Amplitude | float, 0 to 1000, default 1 |  |
| Offset | float, -1000 to 1000, default 0 |  |

### TimeRemap

Speeds, offsets, loops or ping-pongs time before it reaches a graph

| Port | Direction | Type |
| :--- | :--- | :--- |
| time | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Speed | float, -100 to 100, default 1 |  |
| Offset (s) | float, -10000 to 10000, default 0 |  |
| Loop length (s, 0 = none) | float, 0 to 10000, default 0 |  |
| Ping-pong | toggle, default off | With a loop length: run forward then backward instead of jumping. |

## Atmosphere

### AtmosphereSettings

Sky colors, density, haze/fog and light absorption

| Port | Direction | Type |
| :--- | :--- | :--- |
| atmosphere | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Atmosphere density | float, 0.05 to 3, default 1 | How thick the air is. It reddens the sun near the horizon and washes distance out to blue - the single strongest cue of scale in a landscape, because it tells the eye how far away a ridge is. |
| Ambient light | float, 0 to 2, default 0.7 | How much light the sky itself throws down. This is what fills the shadows; too little and they read as black holes rather than shade. |
| Zenith R | float, 0 to 1, default 0.18 | The red component of the sky straight overhead, linear rather than sRGB. |
| Zenith G | float, 0 to 1, default 0.32 | The green component of the sky straight overhead, linear rather than sRGB. |
| Zenith B | float, 0 to 1, default 0.58 | The blue component of the sky straight overhead, linear rather than sRGB. |
| Horizon R | float, 0 to 1, default 0.62 | The red component of the sky at the horizon, linear rather than sRGB. |
| Horizon G | float, 0 to 1, default 0.65 | The green component of the sky at the horizon, linear rather than sRGB. |
| Horizon B | float, 0 to 1, default 0.7 | The blue component of the sky at the horizon, linear rather than sRGB. |
| Fog type | choice: Off / Haze / Fog / Pollution | How the fog is distributed. Uniform fills the air evenly; the height-based kinds pool it in the valleys and leave the peaks clear, which is what morning mist actually does. |
| Fog density | float, 0 to 6, default 0.9 | How thick the fog is. |
| Fog level | float, 0 to 1, default 0.25 | The height the fog sits at, for the height-based kinds. Below it the air is thick, above it clear. |
| Vertical falloff | float, 0.5 to 24, default 6 | How quickly the fog thins above its level. Sharp gives a defined top surface with peaks standing out of it; soft gives a haze that fades away. |
| Fog R | float, 0 to 1, default 0.55 | The red component of the fog, linear rather than sRGB. |
| Fog G | float, 0 to 1, default 0.63 | The green component of the fog, linear rather than sRGB. |
| Fog B | float, 0 to 1, default 0.75 | The blue component of the fog, linear rather than sRGB. |
| Sun scattering | float, 0 to 1, default 0.5 | How much the fog glows toward the sun. This is what makes looking into a misty sunrise bright and looking away from it flat, and without it fog reads as grey paint. |

### CloudLayer

Volumetric cloud layer: type, coverage, altitude, wind

| Port | Direction | Type |
| :--- | :--- | :--- |
| clouds | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Enabled | toggle, default on | Turns the cloud layer off without losing its settings. |
| Cloud type | choice: Stratus / Cumulus / Cumulonimbus | The kind of cloud. Each has its own shape and altitude behaviour - flat sheets, heaped cumulus, high wisps. |
| Coverage | float, 0 to 1, default 0.55 | 0 = clear sky, 1 = solid overcast. |
| Density | float, 0.1 to 3, default 1 | How opaque the cloud is. Thin lets the sun through and lights the cloud from within; thick blocks it and casts shadow on the ground. |
| Base altitude | float, 0.2 to 4, default 1.4 | How high the layer sits. |
| Thickness | float, 0.05 to 2, default 0.8 | How deep the layer is from base to top. Depth is what lets a cloud be lit brightly on top and dark underneath. |
| Detail erosion | float, 0 to 1, default 0.6 | How much fine structure the cloud has. Low gives soft blobs; high gives the wispy, torn edges of real cloud. |
| Anvil spread | float, 0 to 1, default 0.3 | How far the cloud spreads out at its top, the way a storm cell flattens against the top of the troposphere. |
| Wind speed | float, 0 to 0.3, default 0.02 | How fast the layer drifts. Cloud is the only thing in a still landscape that moves, so this is what makes a sequence read as time passing. |
| Wind direction | float, 0 to 360, default 45 | Which way the layer drifts. |
| Sky light | float, 0 to 2, default 0.55 | How much sky light the cloud picks up where the sun does not reach it directly, which sets how dark its underside goes. |
| Color R | float, 0 to 1, default 1 | The red component of the cloud, linear rather than sRGB. |
| Color G | float, 0 to 1, default 1 | The green component of the cloud, linear rather than sRGB. |
| Color B | float, 0 to 1, default 1 | The blue component of the cloud, linear rather than sRGB. |
| Quality | choice: Draft / Normal / High | How many samples the raymarcher takes through the cloud. This is the direct trade between how solid the cloud looks and how fast the frame draws. |

### SunLight

Sun: direction (manual or geographic), color, intensity

| Port | Direction | Type |
| :--- | :--- | :--- |
| sun | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Direction mode | choice: Manual / Location & time | Whether the sun is placed by hand, or worked out from a place and a time. By date and time is what you want when the shot has to match a real location and hour. |
| Azimuth | float, 0 to 360, default 135 | Which way the sun lies, in degrees around the compass. |
| Altitude | float, 1 to 89, default 35 | How high the sun stands above the horizon. Low light is long shadows and warm colour; overhead is short shadows and flat ground. |
| Latitude | float, -89 to 89, default 40.7 | Where on the earth the scene is, north or south. With the date and hour this decides where the sun actually sits. |
| Longitude | float, -180 to 180, default -111.9 | Where on the earth the scene is, east or west. |
| UTC offset | float, -12 to 14, default -7 | The time zone, so the hour below means local clock time. |
| Month | int, 1 to 12, default 6 | The month, which sets how high the sun can climb at this latitude. |
| Day | int, 1 to 31, default 21 | The day of the month. |
| Local time | float, 0 to 24, default 14 | The hour of the day, local time. |
| Intensity | float, 0.2 to 8, default 2.6 | How bright the sun is. |
| Color R | float, 0 to 1, default 1 | The red component of the sunlight, linear rather than sRGB. |
| Color G | float, 0 to 1, default 0.93 | The green component of the sunlight, linear rather than sRGB. |
| Color B | float, 0 to 1, default 0.82 | The blue component of the sunlight, linear rather than sRGB. |
| Cast shadows | toggle, default on | Whether the sun casts shadows. Turning them off is a great deal faster and makes the terrain's form much harder to read. |

### WaterLayer

Water body: level, colors, waves and foam

| Port | Direction | Type |
| :--- | :--- | :--- |
| water | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Enabled | toggle, default on | Turns the water off without losing its settings. |
| Level | float, 0 to 1, default 0.08 | The height the water surface sits at. |
| Clarity | float, 1 to 60, default 18 | How far light travels into the water before it is absorbed. Clear water shows the bed in the shallows and goes deep blue further out; murky water hides the bed at once. |
| Opacity | float, 0.3 to 1, default 0.92 | How much the surface hides what is beneath it. |
| Deep R | float, 0 to 1, default 0.02 | The red component of deep water, linear rather than sRGB. |
| Deep G | float, 0 to 1, default 0.08 | The green component of deep water, linear rather than sRGB. |
| Deep B | float, 0 to 1, default 0.12 | The blue component of deep water, linear rather than sRGB. |
| Shallow R | float, 0 to 1, default 0.1 | The red component of shallow water, linear rather than sRGB. |
| Shallow G | float, 0 to 1, default 0.26 | The green component of shallow water, linear rather than sRGB. |
| Shallow B | float, 0 to 1, default 0.36 | The blue component of shallow water, linear rather than sRGB. |
| Wave amplitude | float, 0 to 4, default 1 | How high the waves stand. |
| Wave scale | float, 0.2 to 6, default 1 | How far apart the waves are. Together with the height this is what sets the apparent size of the body of water: small close-set waves read as a pond however wide it is. |
| Wave speed | float, 0 to 5, default 1 | How fast the waves travel. |
| Foam | toggle, default on | Turns foam on at the shoreline and the wave crests. |
| Shoreline foam | float, 0 to 2, default 0.6 | How much foam there is. |
| Crest foam | float, 0 to 1, default 0.35 | How much foam appears on the wave tops as against at the shore. Open water foams on its crests; a beach foams where the water meets the land. |
| Foam scale | float, 0.5 to 10, default 3 | How fine the foam's own texture is. |

## Camera

### CameraPath

Fly a camera along a path for the rendered sequence

| Port | Direction | Type |
| :--- | :--- | :--- |
| path | in | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Height above ground (m) | float, 0 to 50000, default 400 |  |
| Ride the path | toggle, default on | When on, the sequence renderer moves the active camera along the connected path over the length of the animation. |

### CameraSwitch

[Planned] Cut between cameras over the timeline

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### CameraTarget

[Planned] Aim a camera at a scene object and keep tracking it

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### DepthOfField

[Planned] Focus distance and bokeh from the camera's aperture

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### MotionBlur

[Planned] Shutter-time motion blur for camera and object movement

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### SceneCamera

A camera in the scene: position, aim, lens, exposure triangle, film

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Scene object | text | Name of the scene camera this node drives. Created when missing; an existing camera of that name is adopted. |
| Look through it | toggle, default off | Makes this the active camera: the perspective views and the render use it. |
| Eye X (m) | float, -100000 to 100000, default 2500 |  |
| Eye height (m) | float, -10000 to 100000, default 2250 |  |
| Eye Z (m) | float, -100000 to 100000, default 8500 |  |
| Target X (m) | float, -100000 to 100000, default 2500 |  |
| Target height (m) | float, -10000 to 100000, default 500 |  |
| Target Z (m) | float, -100000 to 100000, default 2500 |  |
| Focal length (mm) | float, 8 to 800, default 35 |  |
| Aperture f/ | float, 1.2 to 22, default 8 |  |
| Shutter 1/x s | float, 0.5 to 8000, default 125 |  |
| ISO | float, 25 to 25600, default 100 |  |
| Film stock | int, 0 to 7, default 0 | Index into the film stock list (see the Camera properties). |

## Cloud

### CloudDensityField

[Planned] Drive a cloud layer's density from a field graph

| Port | Direction | Type |
| :--- | :--- | :--- |
| density | in (optional) | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### CloudMaterial

[Planned] Shade clouds through the material system

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### CloudZone

[Planned] Confine a cloud layer to a region of the sky

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### SpectralClouds

[Planned] Spectral / high-altitude cloud model and morphing clouds

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

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
| Depth | float, 0 to 0.4, default 0.06 | How deep the fissures cut. |
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
| Amount | float, 0 to 0.25, default 0.03 | How much debris is laid down. |
| Grain size | float, 8 to 400, default 120 | How coarse the debris is, in repeats across the tile. |
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
| Strength | float, 0 to 1, default 0.5 | How far the high ground is lifted and the low ground pushed down. This exaggerates the relief that is already there rather than adding new shapes. |
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
| Amount | float, 0 to 3, default 0.6 | How much local contrast is added. It steepens what is already steep, so ridges come to a crest instead of a rounded top. |
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
| Below low mark | choice: Leave alone / Flatten / Cut away (hole) | What happens below the low mark. Flatten gives a level floor - a salt pan or a lake bed. Cut away removes the ground entirely, which is how you punch a hole through the terrain. |
| Above high mark | choice: Leave alone / Flatten | What happens above the high mark. Flatten cuts the summits off level, which is what makes a mesa or a plateau out of a hill. |
| Edge softness | float, 0 to 0.2, default 0 | Blends the cut instead of leaving a hard step. |

### TerrainImprint

Mould the ground to the objects standing on it - flat under each, blended around it

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| imprint_mask | out | heightmap |
| objects | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Footprints | text | Written by the studio from the objects placed on the terrain (Properties > Ground). One per line: base sink margin blend, then the base's convex hull. |
| Blend width | float, 0 to 6, default 1.5 | How far around an object the ground responds, as a multiple of the footprint's radius - for objects that do not set their own blend distance. |
| Smoothness | float, 0 to 1, default 0.5 | The shape of the blend: 0 is a firm shoulder, 1 a long soft tail. |
| Keep relief | float, 0 to 1, default 0.35 | How much of the terrain's own small relief survives inside the blend, so the mould still looks like the same ground. |
| Flatten under | float, 0 to 1, default 1 | How flat the ground is made inside the footprint itself. |
| Strength | float, 0 to 1, default 1 | Dial the whole effect back without losing it. |

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
| Water level | float, 0 to 0.8, default 0.12 | The sea level, as a fraction of the terrain's range. Everything this node does is measured from it. |
| Beach height band | float, 0.005 to 0.2, default 0.04 | Heights within this band above water are planed into a gently sloping beach. |
| Beach slope | float, 0.02 to 1, default 0.25 | How steeply the beach shelves into the water. Gentle gives a wide tidal flat; steep gives a narrow strand under a bluff. |
| Bluff sharpness | float, 0 to 1, default 0.5 | Steepens the cut where the terrain rises out of the beach band — wave-cut bluffs. |
| Underwater smoothing | float, 0 to 1, default 0.4 | How much the ground below the waterline is smoothed. Wave action planes off the shallows, so a seabed with the same roughness as the hills above it reads as wrong. |
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
| Amount | float, 0 to 1, default 0.25 | How much rock is taken into solution. This eats out hollows and sinkholes from within rather than cutting from the surface, which is how limestone country gets its pitted, karst character. |
| Rock hardness | float, 0 to 1, default 0.5 | Hard rock keeps the streams narrow and incised; soft rock lets them spread and flatten the surface. |
| Low ground bias | float, 0 to 3, default 1 | How much the effect concentrates at low altitude. |
| Smoothing | float, 0 to 1, default 0.15 | Rounds the dissolved forms. Solution features are smooth- walled, unlike the sharp edges of mechanical erosion. |

### ErosionLayers

Erode the terrain and derive material layer masks from what the water and rock did

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| bedrock | out | heightmap |
| scree | out | heightmap |
| soil | out | heightmap |
| grass | out | heightmap |
| sediment | out | heightmap |
| riverbed | out | heightmap |
| snow | out | heightmap |
| wetness | out | heightmap |
| flow | out | heightmap |
| splat A | out | texture |
| splat B | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Erosion | choice: Droplets / Shallow water / Thermal only / Thermal + droplets / Thermal + shallow water | Thermal weathering first drops scree below the cliffs; the hydraulic pass then carves channels and settles silt. |
| Seed | seed |  |
| Strength | float, 0.1 to 3, default 1 | Scales droplet count / solver iterations. |
| Talus angle | float, 0.05 to 4, default 1.2 | The steepest slope loose material will hold before it slides - the angle of repose. Low makes everything slump into gentle scree cones; high lets steep faces stand. |
| Thermal iterations | int, 1 to 300, default 40 | How many passes of material shedding are run before the layers are read off. More approaches the angle of repose everywhere. |
| Relief (height / width) | float, 0.02 to 1, default 0.2 | How tall the terrain is compared with the tile width. A heightmap is 0..1 over a 0..1 tile; real ground rises a fifth of its width or less. Slopes are measured against this, so 0.5 means 45° on the real terrain. |
| Bedrock slope | float, 0.05 to 0.95, default 0.45 | Slope (0 flat .. 1 vertical, 0.5 = 45°) above which soil cannot hold and rock is exposed. |
| Grass slope limit | float, 0.02 to 0.9, default 0.25 | The steepest ground grass will hold on. Above this the layer stops, which is what keeps vegetation off the cliffs and in the hollows. |
| Sediment threshold | float, 0.02 to 0.95, default 0.25 | How much deposited material makes a cell sand/silt. |
| Stream threshold | float, 0.1 to 0.98, default 0.55 | Drainage (log scale, 0..1) above which the cell is a riverbed. |
| Snowline | float, 0 to 1, default 1 | Height above which snow lies on gentle ground. 1 = no snow. |
| Edge softness | float, 0.005 to 0.4, default 0.08 | How gradually one layer's mask gives way to the next. Hard edges read as drawn on; this is what lets rock, scree, soil and grass blend into one another. |
| Wetness spread | float, 0 to 0.05, default 0.01 | Blur radius (fraction of the map) that lets moisture reach past the channel itself. |

### Glaciation

Glacial carving — broad U-shaped valleys, ridges left intact

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Strength | float, 0 to 1, default 0.6 | How deeply the glacier cuts. It carves a U-shaped trough along the drainage, which is what tells a glaciated valley from a river one. |
| Ice line | float, 0 to 1, default 0.55 | Ground below this altitude is carved by ice; peaks above it keep their sharp profile. |
| Valley width | float, 0.005 to 0.15, default 0.03 | How wide the trough is, as a fraction of the tile. Glaciers cut far broader valleys than rivers of the same catchment. |
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
| water_map | out | heightmap |
| delta_map | out | heightmap |
| exposed_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Method | choice: Particle droplets / Shallow water (pipe model) | Two ways of simulating running water. Particle droplets follow thousands of individual raindrops downhill, each picking up and dropping sediment - fast, and good at carving channels. Shallow water solves the whole sheet of water at once through the pipe model, which is slower but handles standing water and broad flow properly. |
| Seed | seed |  |
| Particles (x1000) | int, 1 to 2000, default 120 | How many droplets are released, in thousands. More is smoother and more thoroughly carved, and costs proportionally more. |
| Particle lifetime | int, 8 to 256, default 48 | How many steps a droplet takes before it gives up. Short lives erode near the ridges only; long ones carry sediment all the way to the basins. |
| Inertia | float, 0 to 0.6, default 0.06 | How much a droplet keeps its heading rather than turning straight downhill. Low follows the terrain exactly and gives tight, branching channels; high sweeps across contours and gives straighter, broader valleys. |
| Carry capacity | float, 0.5 to 20, default 5.5 | How much sediment a droplet can hold, per unit of speed and slope. This is the main dial for how deeply the terrain is cut: a droplet erodes while it is under capacity and deposits once it is over. |
| Erosion rate | float, 0.01 to 1, default 0.4 | How fast a droplet takes material when it has room to carry more. High values cut sharp gullies quickly and can punch through thin ridges. |
| Deposition rate | float, 0.01 to 1, default 0.25 | How fast a droplet drops material once it is carrying more than it can hold. This is what builds the fans and flats at the bottom of the slope. |
| Evaporation | float, 0 to 0.1, default 0.015 | How fast a droplet shrinks as it travels. Faster evaporation shortens its reach and makes it drop its load sooner, which piles sediment higher up the slope. |
| Gravity | float, 0.5 to 12, default 4 | How strongly slope accelerates a droplet. Higher makes fast water on steep ground far more erosive than slow water on flat. |
| Brush radius | int, 1 to 8, default 3 | How wide an area each droplet takes material from. 1 gives thin, noisy scratches; wider spreads the cut and gives smoother, more believable channels. |
| Iterations | int, 10 to 600, default 120 | How many time steps the water sheet is advanced. This is the main cost and the main dial for how far the erosion has progressed. |
| Rainfall | float, 0.001 to 0.1, default 0.012 | How much water falls per step, everywhere. More water means more flow, deeper channels and more standing water in the hollows. |
| Capacity Kc | float, 0.1 to 4, default 1 | How much sediment the flow can carry for a given speed and slope. The single strongest control on how deeply the terrain is cut. |
| Erosion Ks | float, 0.05 to 2, default 0.5 | How fast the bed gives up material where the flow is under capacity. |
| Deposition Kd | float, 0.05 to 2, default 0.5 | How fast sediment settles where the flow is over capacity. High values fill the basins quickly and flatten them. |
| Evaporation | float, 0 to 0.2, default 0.015 | How fast standing water disappears each step. Low leaves lakes in the hollows; high dries the map between rainfalls and concentrates the cutting in the channels. |

### HydraulicBlur

The erosion look at one percent of the cost

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius (px) | int, 1 to 64, default 8 | How far the smoothing reaches. This is a blur that follows the drainage, so it softens the slopes water would have run down while leaving the ridge lines alone. |
| Amount | float, 0 to 1, default 0.7 | How much of the blurred result is mixed in. Full strength reads as a landscape long weathered; a little takes the hard edges off fresh erosion. |
| Keep ridges | float, 0 to 1, default 0.7 | Convex ground (ridges, crests) resists the smoothing; concave ground (gullies, hollows) takes it fully - which is the shape hydraulic erosion carves. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

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
| River width | float, 0.001 to 0.05, default 0.006 | How wide the channel is cut, as a fraction of the tile. |
| Carve depth | float, 0.005 to 0.3, default 0.05 | How deeply the channel is cut below the surrounding ground. |
| Valley width | float, 0 to 0.15, default 0.02 | Soft V-shaped valley carved around the channel. |
| Widen downstream | float, 0 to 1, default 0.6 | How much the channel broadens as more water joins it. 0 gives a ditch of constant width the whole way; higher makes the headwaters narrow and the lower reaches broad, which is what a real drainage network looks like. |
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
| exposed_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 300, default 40 | How many settling passes are run. More lets sediment travel further downhill before it comes to rest. |
| Fill amount | float, 0 to 1, default 0.3 | How much material is deposited into the hollows. This is the counterpart to erosion: it fills the low ground and flattens the basins rather than cutting the high ground. |

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
| incision_map | out | heightmap |
| deposit_map | out | heightmap |
| delta_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Method | choice: Explicit incision / Implicit + uplift (Braun-Willett) | How the incision law is stepped. The explicit scheme is simple and fast but needs a small time step to stay stable; the implicit one (Braun and Willett) is unconditionally stable and lets you take large steps, which is how you get a mature drainage network without waiting. |
| Iterations | int, 1 to 400, default 40 | How many time steps the incision law is advanced. This is the main cost and the main dial for how mature the drainage network becomes. |
| Erodibility K | float, 0.001 to 0.3, default 0.03 | How erodible the rock is. This is the overall rate of the whole process - harder rock, slower incision, and a landscape that keeps its steep ground for longer. |
| Area exponent m | float, 0.2 to 1, default 0.5 | How strongly drainage area drives incision. Higher makes the big rivers cut far faster than the small ones, which deepens the main valleys and leaves the tributaries hanging. |
| Slope exponent n (explicit) | float, 0.5 to 2, default 1 | How strongly slope drives incision. Above 1 the steep reaches cut away fastest and the profile straightens out; below 1 they persist. |
| Timestep (implicit) | float, 0.05 to 10, default 1 | The time step. Larger advances the landscape faster per iteration; with the explicit method too large a step goes unstable and spikes. |
| Uplift rate | float, 0 to 0.05, default 0.004 | How fast the land is pushed up while the rivers cut down. A landscape only reaches a steady shape when the two are in balance, and this is what stops the terrain simply wearing flat. |
| Diffusion | float, 0 to 0.5, default 0.08 | Smooths the result at the end, which takes off the numerical roughness the solver leaves without undoing the drainage pattern it found. |

### Thermal

Thermal weathering — talus slopes to angle of repose

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| exposed_map | out | heightmap |
| talus_map | out | heightmap |
| delta_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Talus angle (legacy) | float, 0.05 to 4, default 1.2 | Relief units per texel. Used only while the angle of repose below is 0. |
| Angle of repose | float, 0 to 80, default 0 | In degrees on the real terrain: scree settles at about 35°, dry sand at 30-34°, wet soil steeper. 0 keeps the legacy talus value. |
| Relief (height / width) | float, 0.02 to 1, default 0.2 | How tall the terrain is against the tile width; the angle above is measured against this. |
| Iterations | int, 1 to 500, default 60 | How many passes of material are shed. More approaches the angle of repose everywhere and costs proportionally. |
| Transport rate | float, 0.05 to 1, default 0.5 | How much material moves per pass. Low is a slow, even creep; high collapses the slopes quickly and can overshoot into terracing. |
| Run to convergence | toggle, default off | Stops early once a pass moves less than this, so a terrain that has already reached its angle of repose does not keep paying for iterations that do nothing. |

### Wind

Aeolian erosion — windward abrasion, leeward deposition (dunes)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| abrasion_map | out | heightmap |
| deposit_map | out | heightmap |
| delta_map | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Wind direction ° | float, -180 to 180, default 30 | Which way the wind blows. Everything this node does is oriented by it: material is lifted from the windward faces and dropped in the lee. |
| Iterations | int, 1 to 300, default 40 | How many passes of transport are run. More moves material further and settles the dunes into longer, more continuous forms. |
| Strength | float, 0.05 to 1, default 0.4 | How much material the wind lifts per pass. High scours the exposed ground hard and piles it deep behind obstacles. |
| Carry distance | float, 0.005 to 0.15, default 0.03 | How far the wind carries a grain before dropping it, as a fraction of the tile. Short gives sharp drifts against every obstruction; long spreads material across the whole map. |
| Shadow angle | float, 0.2 to 4, default 1 | How far into the lee of a rise the wind stays too weak to lift anything. This is what creates the sheltered pocket where sand accumulates, and the reason dunes form downwind of an obstacle rather than on it. |

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

### ExportPoints

Write a point cloud or path to CSV / PLY

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| terrain | in (optional) | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| File | file path |  |
| Format | choice: CSV (x,y,z,value) / PLY |  |
| Height scale | float, 0.01 to 100, default 1 |  |
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
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
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

### FieldColorAdjust

Colour correction: hue shift, saturation, contrast, brightness, gamma, invert

| Port | Direction | Type |
| :--- | :--- | :--- |
| color | in (optional) | field (color) |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Hue shift ° | float, -180 to 180, default 0 |  |
| Saturation | float, 0 to 3, default 1 |  |
| Contrast | float, 0 to 3, default 1 |  |
| Brightness | float, -1 to 1, default 0 |  |
| Gamma | float, 0.1 to 5, default 1 |  |
| Invert | toggle, default off |  |

### FieldColorFromHSV

Builds a colour from hue, saturation, value and alpha numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| h | in (optional) | field (number) |
| s | in (optional) | field (number) |
| v | in (optional) | field (number) |
| a | in (optional) | field (number) |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Hue (when unconnected) | float, 0 to 1, default 0.1 |  |
| Saturation (when unconnected) | float, 0 to 1, default 0.4 |  |
| Value (when unconnected) | float, 0 to 1, default 0.6 |  |
| Alpha (when unconnected) | float, 0 to 1, default 1 |  |

### FieldColorHSV

Takes a colour apart as hue, saturation and value (each 0..1)

| Port | Direction | Type |
| :--- | :--- | :--- |
| color | in (optional) | field (color) |
| h | out | field (number) |
| s | out | field (number) |
| v | out | field (number) |

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

## Field Convert

### FieldColorCombine

Builds a colour from red, green, blue and alpha numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| r | in (optional) | field (number) |
| g | in (optional) | field (number) |
| b | in (optional) | field (number) |
| a | in (optional) | field (number) |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Red (when unconnected) | float, 0 to 1, default 0.5 |  |
| Green (when unconnected) | float, 0 to 1, default 0.5 |  |
| Blue (when unconnected) | float, 0 to 1, default 0.5 |  |
| Alpha (when unconnected) | float, 0 to 1, default 1 |  |

### FieldColorSplit

Takes a colour apart: luminance, red, green, blue and alpha as numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| color | in (optional) | field (color) |
| luminance | out | field (number) |
| r | out | field (number) |
| g | out | field (number) |
| b | out | field (number) |
| a | out | field (number) |

### FieldTexCoordCombine

Builds texture coordinates from u and v numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| u | in (optional) | field (number) |
| v | in (optional) | field (number) |
| out | out | field (uv) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| U (when unconnected) | float, -64 to 64, default 0 |  |
| V (when unconnected) | float, -64 to 64, default 0 |  |

### FieldTexCoordSplit

Takes texture coordinates apart into u and v numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| uv | in (optional) | field (uv) |
| u | out | field (number) |
| v | out | field (number) |

### FieldToColor

Any value as a colour: grey from a number, RGB from a vector, with an alpha

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (color) |
| out | out | field (color) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Alpha (non-colour inputs) | float, 0 to 1, default 1 |  |
| Vector is -1..1 (remap to 0..1) | toggle, default on | A direction or normal spans -1..1; on it maps that range onto 0..1 the way a normal map does. |

### FieldToNumber

Any value as a number: luminance / length, one lane, alpha, max or average

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (number) |
| out | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Read as | choice: Auto (luminance, length, u) / First lane (R / X / U) / Second lane (G / Y / V) / Third lane (B / Z) / Alpha / Largest lane / Average of lanes | Auto is what an unconverted link does: a colour is its luminance, a vector its length. The lane modes pick one component; a type with fewer lanes gives its last one. |

### FieldToTexCoord

Any value as texture coordinates: a vector projected on a plane, RG, or n,n

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (uv) |
| out | out | field (uv) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Project a vector on | choice: XZ (ground) / XY (front) / ZY (side) |  |

### FieldToVector

Any value as a vector: a number broadcast, RGB of a colour, UV on a plane

| Port | Direction | Type |
| :--- | :--- | :--- |
| in | in (optional) | field (vector) |
| out | out | field (vector) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Texture coordinates lie on | choice: XZ (ground) / XY (front) / ZY (side) |  |

### FieldVectorCombine

Builds a vector from x, y and z numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| x | in (optional) | field (number) |
| y | in (optional) | field (number) |
| z | in (optional) | field (number) |
| out | out | field (vector) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| X (when unconnected) | float, -1000 to 1000, default 0 |  |
| Y (when unconnected) | float, -1000 to 1000, default 0 |  |
| Z (when unconnected) | float, -1000 to 1000, default 0 |  |

### FieldVectorSplit

Takes a vector apart: x, y, z and its length as numbers

| Port | Direction | Type |
| :--- | :--- | :--- |
| vector | in (optional) | field (vector) |
| x | out | field (number) |
| y | out | field (number) |
| z | out | field (number) |
| length | out | field (number) |

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

Vector maths: length, dot, distance, normalize, cross, add, subtract, multiply, reflect

| Port | Direction | Type |
| :--- | :--- | :--- |
| a | in (optional) | field (vector) |
| b | in (optional) | field (vector) |
| out | out | field (number) |
| vec | out | field (vector) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Length / Dot product / Distance / Normalize (X) / Cross product (X) / Add (X) / Subtract (X) / Multiply (X) / Reflect A off B (X) | 'out' is a number: the scalar result, or the X lane of a vector result. 'vec' is the whole vector result; for the scalar operations it passes A through (A - B for Distance). |

## Field Noise

### FieldGrass

A sward of grass - tufts, blades and bare ground - as a function, at any scale

| Port | Direction | Type |
| :--- | :--- | :--- |
| position | in (optional) | field (vector) |
| out | out | field (number) |
| mask | out | field (number) |
| shade | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Tuft size (m) | float, 0.004 to 20, default 0.12 | How far across one tuft of grass is, in metres. 0.12 is a clump of meadow grass, 0.03 is a lawn, 1 is tussock.  Below about a centimetre on a 5 km tile the tile's own coordinates run out of precision and the tufts go blocky. Shrink the terrain, not the grass. |
| Blade height (m) | float, 0.002 to 20, default 0.16 | How tall the longest blades stand, in metres. Grass is taller than it is wide, which is most of what tells it apart from a field of small stones. |
| Sizes | int, 1 to 5, default 3 | How many halvings of the tuft size to add, so how wide a range of sizes one sward holds - big clumps with finer grass filling between them. |
| Amount | float, 0 to 1, default 0.85 | How much grass there is. Up to about three quarters it thins the sward; past that every cell holds a tuft and they grow into one another, so 1 closes it completely with no ground showing through. |
| Seed | seed |  |
| Pointedness | float, 0 to 1, default 0.6 | How sharply a tuft comes to a point. This is the one control that most decides whether the field reads as grass or as gravel: 0 gives domes, which is what a stone is, and no amount of blade detail rescues that. |
| Blade relief | float, 0 to 1, default 0.7 | How strongly the individual blades show against the tuft they belong to. 0 is a smooth mound. |
| Blade count | float, 0 to 1, default 0.5 | 0 a few broad blades, 1 many fine ones. |
| Size variation | float, 0 to 1, default 0.55 | 0: every tuft the same size. 1: many small tufts and a few large, which is what a real sward has. |
| Shape variation | float, 0 to 1, default 0.6 | How much tufts differ from one another. 0 shapes every tuft in the field alike, which is the look of a texture; 1 puts fine soft grass and coarse spiky clumps side by side. |
| Height variation | float, 0 to 1, default 0.5 | How much tufts differ in height from one another, about the average. The average is unchanged whatever this is set to. |
| Size mix | float, -1 to 1, default 0 | Which sizes the field is actually made of, across the octaves it has. Below zero leans toward the large and the small become an accent; above zero the small take over and the large are the accent. 0 gives every size the same share, which is what it always did. |
| Wind | float, 0 to 1, default 0.35 | How far the blades lean. The whole field leans one way, which is the strongest single cue that what you are looking at is grass and not small stones - those each lean whichever way they fell. |
| Wind direction | float, -180 to 180, default 0 | Which way the wind is blowing across the ground. |
| Tall blades bend more | float, 0 to 1, default 0.5 | How much further the tall blades lean than the short ones. They do, so this is 0.5 rather than 0. |
| Cluster / repel | float, -1 to 1, default 0.3 | Above zero the tufts gather into patches and are pulled together inside one; below zero they stand off from one another. The count is unchanged either way. |
| Patch size (m) | float, 0.02 to 500, default 1.2 | How far across one patch of tufts is, in metres. |
| Bare ground | float, 0 to 1, default 0.2 | How much ground is bare of grass altogether. Grass is not a carpet - it gives out where it is trodden, dry or shaded - and a sward that only ever thins a little reads as one. |
| Bare patch size (m) | float, 0.05 to 2000, default 7 | How far across a bare patch is, in metres. |
| Terrain size (m) | float, 1 to 1e+06, default 5000 | The tile's width; the studio keeps this in step with the project so the sizes above mean metres. |

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

### FieldStones

A field of stones - boulders, cobbles and gravel - as a function, at any scale

| Port | Direction | Type |
| :--- | :--- | :--- |
| position | in (optional) | field (vector) |
| out | out | field (number) |
| mask | out | field (number) |
| shade | out | field (number) |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Stone size (m) | float, 0.005 to 200, default 0.12 | The biggest stone's width across, in metres, and it is really metres: 0.12 is a cobble, 0.02 is grit, 3 is a boulder. Every size below it comes from the octaves, each half as wide and four times as many, so one field holds boulders, cobbles and grit at once.  Below about a centimetre across on a 5 km tile the tile's own coordinates run out of precision and the outlines go blocky. Shrink the terrain, not the stone. |
| Sizes | int, 1 to 6, default 4 | How many halvings of the stone size to add, so how wide a range of sizes one field holds. 1 is a single size; 6 spans thirty-two to one, boulders down to grit. Distance takes octaves away again, so this is a ceiling, not a cost. |
| Amount | float, 0 to 1, default 0.55 | How much stone there is. Up to about three quarters it thins the field; past that every cell holds a stone and they grow into one another, so 1 paves the ground end to end with no bare earth left between. |
| Tallness | float, 0.05 to 2, default 0.6 | A stone's height as a fraction of its radius. |
| Height variation | float, 0 to 1, default 0.5 | How much stones differ in height from one another, about the average. The average is unchanged whatever this is, so widening the spread does not quietly raise or lower the whole field. |
| Size mix | float, -1 to 1, default 0 | Which sizes the field is actually made of, across the octaves it has. Below zero leans toward the large and the small become an accent; above zero the small take over and the large are the accent. 0 gives every size the same share, which is what it always did. |
| Size variation | float, 0 to 1, default 0.7 | 0: every stone the same size. 1: the power-law spectrum a scree slope has - many small, a few large. |
| Shape variation | float, 0 to 1, default 0.6 | How much stones differ from one another. 0 breaks, flattens and pits every stone in the field to exactly the same degree, which is the look of a texture; 1 puts rounded cobbles and shattered blocks side by side, the way real scree does. |
| Flatten | float, 0 to 1, default 0.25 | Raises the top into a plateau while keeping the footprint: 0 boulders, 1 slabs. |
| Settled into the ground | float, 0 to 0.9, default 0.25 | How deep a stone sits. Buried stones show only their tops, and the ground cuts their outline instead of meeting them tangentially. |
| Elongation | float, 0 to 1, default 0.5 | How much longer a stone may be one way than the other, turned as it fell. Round in plan is the tell of a procedural field. |
| Outline roughness | float, 0 to 1, default 0.45 | How far the outline departs from an ellipse. |
| Broken faces | float, 0 to 1, default 0.55 | Cuts flat faces into each stone. 0 leaves rounded pebbles; high values give the angular, broken look of quarried or frost-shattered rock. |
| Surface relief | float, 0 to 1, default 0.4 | How far a stone's own surface departs from a smooth shell. 0 is polished. |
| Lean | float, 0 to 1, default 0.35 | Moves each stone's high point off centre, so it has a downhill side rather than being a dome. |
| Cluster / repel | float, -1 to 1, default 0.5 | Stones are not spread evenly. Above zero they collect in drifts with bare ground between, and are pulled together inside a drift until they lie shoulder to shoulder - 1 heaps them hard. Below zero they push apart instead and stand off from one another, the way frost heave sorts a boulder field. The count is unchanged either way: this rearranges a field, it does not thin it. |
| Drift size (m) | float, 0.05 to 2000, default 2 | How far across one clump of stones is, in metres. |
| Seed | seed |  |
| Terrain size (m) | float, 1 to 1e+06, default 5000 | The tile's width; the studio keeps this in step with the project so the size above means metres. |

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
| Clamp range | range | Everything below the low value is lifted to it and everything above the high value pushed down to it - flat floors and flat tops, with the middle untouched. |
| Shoulder softness | float, 0 to 0.5, default 0 | Rounds the corner where the terrain meets the clamp instead of cutting it flat. 0 leaves a hard crease that catches the light as a line. |

### Convolve

Convolution by a preset or typed kernel

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Kernel | choice: Sharpen / Edge (laplacian) / Emboss NW / Sobel X / Sobel Y / Custom | A small matrix swept over the map. Sharpen boosts local contrast; the edge and Sobel kernels find boundaries and make good masks rather than terrain; Emboss lights it from one side. Custom takes your own numbers below. |
| Custom (row-major) | text | 9 or 25 numbers, row-major 3x3 or 5x5, any whitespace. |
| Strength | float, 0 to 4, default 1 | Scales the kernel's result before it is used. |
| Add to input | toggle, default off | On, the result is added on top of the original terrain instead of replacing it - which is how an edge kernel becomes extra relief along the breaks rather than a picture of them. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Craggy

Slope-targeted rocky detail; flats stay clean

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Detail scale | float, 4 to 128, default 24 | How fine the added crag detail is, in repeats across the tile. High values give the broken texture of shattered rock; low ones give lumps. |
| Strength | float, 0 to 0.3, default 0.06 | How far the crags stand off the surface. This is surface roughness, not landform - a little goes a long way. |
| Slope threshold | float, 0 to 1, default 0.3 | How steep ground must be before crags appear on it. Bare broken rock belongs on the steeps; flat ground collects soil and stays smooth. |
| Threshold softness | float, 0.02 to 0.6, default 0.2 | How gradually the crags fade in as the slope steepens, so the treated ground does not end on a line. |
| Octaves | int, 2 to 9, default 5 | How many sizes of crag are layered together. |
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
| Curve | gradient | The transfer curve, read as brightness: the horizontal axis is the height coming in, the gradient's brightness at that point is the height going out. A straight ramp changes nothing; bending it up raises the midlands, an S makes the flats flatter and the steeps steeper. |
| Strength | float, 0 to 1, default 1 | How much of the curved result replaces the original. Part- way is a gentler version of the same shaping. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### DetailEqualizer

Per-band detail gains, like an audio EQ

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Fine (1-4 px) | float, 0 to 3, default 1 | The grain of the surface. Turning it down calms a noisy terrain without softening its shape; turning it up sharpens the texture without adding relief. |
| Medium (4-16 px) | float, 0 to 3, default 1 | Gullies and small outcrops - the scale that carries most of a landscape's character. |
| Coarse (16-64 px) | float, 0 to 3, default 1 | Ridges and valleys: the landforms themselves. |
| Base (blur 64 px+) | float, 0 to 3, default 1 | The overall lie of the land under everything else. Turning this down flattens the map without losing any of its detail - all four bands together are the original, so 1 everywhere changes nothing. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Detrend

Subtract the best-fit plane

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Amount | float, 0 to 1, default 1 | How much of the overall tilt is removed. An imported heightfield often leans as a whole; taking the plane out levels it without touching the relief on top. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### DirectionalBlur

Streak the surface along a direction

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Direction ° | float, -180 to 180, default 0 | Which way the streaks run. Along the prevailing wind this reads as scouring; down the slope, as material having run. |
| Length | float, 0.002 to 0.5, default 0.05 | How far the smearing reaches, as a fraction of the tile. |
| Both directions | toggle, default on | On, it smears symmetrically and the surface stays put. Off, it drags one way only, so features shift downstream as well as blurring - which is what makes it look like flow rather than blur. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

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
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
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
| Radius | float, 0.001 to 0.05, default 0.01 | How far the high ground grows outward, as a fraction of the tile. On a mask this fattens or thins the selected region; on terrain it broadens ridges or widens valleys. |
| Shrink (erode) | toggle, default off | Runs it the other way: the low ground grows instead, eating into the high. Expand then shrink at the same radius closes small gaps and leaves the rest alone. |

### Fold

Fold values around midline — creates ridged detail

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 6, default 1 | How many times the heights are reflected about the middle. Each fold turns every valley into a ridge, so one pass makes smooth noise ridged and several make an intricate crumpled surface. This is where ridged noise comes from, applied after the fact. |

### GammaCorrection

Power-curve contrast

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Gamma | float, 0.05 to 6, default 1 | Bends the heights toward the low or the high end without moving either. Below 1 lifts the middle, so more of the map sits high and the lowland shrinks; above 1 pushes it down, so peaks become sparse and the valleys broad. |

### Kuwahara

Edge-preserving smoothing (painterly flats)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius (px) | int, 1 to 16, default 4 | How far it looks for a flat neighbourhood to average instead. Unlike a blur this keeps the edges: it flattens the ground between features while leaving the breaks between them sharp. |
| Mix | float, 0 to 1, default 1 | How much of the flattened result replaces the original. The full effect is strongly painterly; part of it just calms a noisy surface. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### MeanShift

Mode-seeking smoothing (flattens toward plateaus)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius (px) | int, 2 to 24, default 6 | How far across the map each point looks for company. Larger merges more ground into each plateau. |
| Value tolerance | float, 0.005 to 0.5, default 0.08 | How close in height two points must be to count as the same surface. This is what keeps a cliff a cliff while the ground either side of it flattens: only similar heights are averaged together. |
| Iterations | int, 1 to 8, default 3 | How many times the search is repeated. Each pass moves points further toward the nearest flat, so more of them converge into fewer, cleaner terraces. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

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
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
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
| Operation | choice: Dilate / Erode / Open / Close / Gradient / Top hat / Black hat | Dilate grows the bright regions, erode shrinks them. Open is erode then dilate - it removes specks and leaves everything else the same size. Close is the reverse and fills small holes. The two are how you clean up a mask without blurring it. |
| Radius (px) | int, 1 to 64, default 3 | How far the operation reaches. On a mask this is the size of the gap it can close or the speck it can remove. |
| Element | choice: Square / Octagon | The neighbourhood the operation uses. A square is fastest but leaves corners on round features; the octagon is closer to a circle and does not. |
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
| Level | float, 0 to 1, default 0.7 | The height everything above is flattened to. This is how a mesa or a tableland is made from a hill: the summit is cut off level and the flanks keep their shape. |
| Softness | float, 0.01 to 1, default 0.1 | How gradually the flank gives way to the flat top. Low gives the sharp shoulder of a lava-capped mesa; high gives a rounded summit. |

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
| Noise flavour | choice: Perlin / Billows / Ridges / Voronoi billows / Voronoi ridges | Which noise the displacement is built from. Each has a different character at the same settings - one gives rolling forms, another sharp ridges, another cellular blocks. |
| Lead-in scale | float, 0.05 to 4, default 1 | Largest visible variation (fraction of terrain width). Octaves between lead-in and feature scale ramp in with reduced amplitude. |
| Feature scale | float, 0.01 to 2, default 0.25 | Scale of the dominant, full-amplitude features. |
| Smallest scale | float, 0.0005 to 0.1, default 0.004 | Detail cutoff — nothing finer than this is added. |
| Seed | seed |  |
| Displacement amplitude | float, 0 to 1, default 0.15 | How far the pattern displaces the surface, in heightmap units. This is the strength of the whole effect. |
| Displacement offset | float, -0.5 to 0.5, default 0 | Shifts displacement: positive raises plinths, negative sinks features. |
| Roughness | float, 0.3 to 1.6, default 1 | Per-octave gain multiplier; below 1 smooths high frequencies, above 1 exaggerates them. |
| Spike limit | float, 0.05 to 1, default 0.7 | Damps octave contributions on already-steep ground to prevent needle spikes. |
| Displace along normal | toggle, default off | Scales displacement with slope so cliffs bulge outward like real overhung rock (approximated). |
| Apply on slopes | range | Restrict displacement to this normalized slope band (e.g. 0.4..1 = only on steep faces). |
| Slope softness | float, 0.01 to 0.5, default 0.15 | How gradually the effect fades in as the slope steepens, so treated ground does not end on a visible line. |

### Remap

Remap value range

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Target range | range | Rescales the whole map so its lowest point lands on the first value and its highest on the second. Nothing is clipped - the shape is unchanged, only its range. Reversing the two turns the terrain upside down. |
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
| Radius | float, 0 to 0.2, default 0.01 | How far the blur reaches, as a fraction of the tile. Small values take the noise off a surface without touching its shape; large ones dissolve the shape as well. |

### SmoothFill

Fill hollows up to the smoothed surface

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| fill_depth | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius (px) | int, 1 to 128, default 16 | How large a hollow counts as one worth filling. Anything narrower than this is levelled; anything broader is left as terrain. |
| Direction | choice: Fill up / Shave down | Fill up raises hollows to the smoothed surface - sediment settling into dips. Shave down cuts the bumps off instead, which is weathering rather than deposition. The second output reports how much was moved, which makes a good sediment mask. |
| Amount | float, 0 to 1, default 1 | How much of the way to the smoothed surface it goes. Part-way leaves the hollow visible but softened. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

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
| Snow amount | float, 0 to 0.3, default 0.06 | How thick the snow lies, in heightmap units. It is added on top of the terrain, so this also softens whatever it covers. |
| Snowline | float, 0 to 1, default 0.55 | The height above which snow settles, as a fraction of the terrain's range. Below it the ground stays bare. |
| Snowline falloff | float, 0.02 to 0.6, default 0.15 | How gradually the snowline is crossed. A hard line looks painted on; real snow thins out over a band, and thins faster on the sunnier side. |
| Slip-off slope | float, 0.1 to 1, default 0.55 | Snow cannot cling to slopes steeper than this. |
| Settle-thaw iterations | int, 0 to 60, default 12 | Lets snow slide into hollows and compact — smooth, wind-packed accumulation. |
| Melt (low areas) | float, 0 to 1, default 0.3 | How much snow disappears from the low, sheltered ground - the hollows where it goes first. 0 leaves an even blanket above the snowline. |
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
| Strength | float, 0.05 to 1, default 0.6 | How strongly the bedding shows. The beds are cut into the existing surface rather than laid over it, so this is how much of the original slope survives. |
| Layer count | int, 4 to 80, default 18 | How many beds are stacked through the height range. Few gives the broad benches of a canyon wall; many gives fine banding. |
| Tilt | float, 0 to 0.8, default 0.15 | Strata are tilted planes, not horizontal bands — the single most important realism control. |
| Tilt direction ° | float, -180 to 180, default 30 | Which way the beds dip. Sedimentary rock is rarely level, and the direction it leans is the single strongest clue to a landscape's geological history. |
| Warp | float, 0 to 1, default 0.2 | How much the beds are bent out of true. Real strata are folded by the same forces that raised them; perfectly flat bedding reads as printed on. |
| Substrata | float, 0 to 1, default 0.4 | Finer secondary layering nested inside each stratum. |
| Only on slopes above | float, 0 to 1, default 0.25 | How steep ground must be before the strata show. Bedding is exposed where rock is bare and cut into; gentle ground carries soil that hides it. |
| Slope softness | float, 0.02 to 0.5, default 0.15 | How gradually the strata fade in as the slope steepens, so the bedded ground does not end on a visible line. |
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
| Levels | int, 2 to 64, default 8 | How many steps the height range is cut into. Few gives the broad benches of a canyon wall; many gives fine bedding, and past a point they are finer than the terrain can show. |
| Edge sharpness | float, 0.5 to 12, default 3 | How abruptly one step gives way to the next. Low leaves rounded treads that still read as a slope; high gives a flat tread and a near-vertical riser. |
| Cliff bias | float, -1 to 1, default 0 | Skews each step: negative = wide flats with sharp cliffs above; positive = sharp base, sloped tops. |
| Strength | float, 0 to 1, default 1 | How much of the terraced result replaces the original. Below 1 leaves the underlying slope showing through, which is usually more convincing than a fully stepped hillside. |
| Seed | seed |  |
| Level thickness jitter | float, 0 to 1, default 0.3 | Randomizes each layer's thickness — natural geological strata are never evenly spaced. |
| Edge warp | float, 0 to 1, default 0.15 | Warps terrace edges with noise so contour lines wander instead of following exact heights. |
| Edge warp scale | float, 2 to 64, default 12 | How fine the wander in the terrace edges is. Low makes each contour meander in broad curves; high gives a ragged, crumbling edge. |
| Altitude band | range | Only terrace heights inside this normalized band; terrain outside is left untouched. |
| Band softness | float, 0.01 to 0.5, default 0.1 | How gradually the terracing fades in at the edges of the altitude band, so the treated ground does not end on a visible line. |

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
| Water level | float, 0 to 1, default 0.3 | The height the water stands at, as a fraction of the terrain's own range. |
| Fill | choice: Everywhere below / Connected to the edge / From source points | Everywhere below fills every hollow at once, whether or not water could reach it. Connected to the edge floods only what the sea can actually get into, so a walled basin stays dry. From source points floods outward from the points you supply. |
| Normalize depth | toggle, default on | Scales the depth output to 0..1 so it can drive a mask directly. Off leaves it in terrain units. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Lake

A body of water at a place: centre, max radius and water level, with a wandering shore, a carved bed and a beach mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| water | out | heightmap |
| depth | out | heightmap |
| mask | out | heightmap |
| shore | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Water level | float, 0 to 1, default 0.32 | The height of the water surface, as a fraction of the terrain's own range. Terragen states this as a height above the planet; here it follows the terrain so a lake stays put when the relief is re-scaled. |
| Centre | x/y pair | Where the lake sits on the tile. |
| Max radius (m) | float, 0.5 to 200000, default 400 | The furthest the water can reach from the centre, in metres. The wandering rim moves inside this, never past it - which is exactly what Terragen's Max radius means. |
| Stretch | float, 0.05 to 20, default 1 | 1 is round, which is the only shape Terragen's Lake can be. Higher stretches it one way, so a lake can lie along a valley. |
| Rotation | float, -180 to 180, default 0 | Turns the lake's outline, which matters once Stretch has pulled it away from round. |
| Outline | choice: Rectangle / Rounded rectangle / Round / Diamond / From mask | From mask takes the outline from the mask input, so a lake can be traced from a real one. |
| Shore wander | float, 0 to 1, default 0.3 | How far the waterline departs from the perfect curve, as a fraction of the radius. This is what makes bays and spits; 0 gives the drawing-board circle. |
| Shore detail | float, 0.2 to 64, default 4 | How many bays and headlands around the shore. |
| Shore roughness | int, 1 to 10, default 4 | How much finer detail rides on the large bays. |
| Shore width | float, 0.001 to 1, default 0.12 | How far in from the rim the water shallows, as a fraction of the lake's radius. This is the band the 'shore' output marks. |
| Shore gradient | float, 0.05 to 8, default 1 | The profile from the middle out to the shore. Below 1 the water stays deep and shallows abruptly - a tarn in a rock basin. Above 1 it shallows from far out - a wide beach. |
| Seed | seed |  |
| Follow the ground | toggle, default on | On, the lake only fills where the ground is already below the water level, so it settles into the valley it is in. Off, it is a flat disc that ignores the terrain, which is what Terragen's Lake object is. |
| Carve the bed | float, 0 to 1, default 0.35 | Pulls the ground under the lake down below the water, deepest in the middle. 0 leaves the terrain alone and the water may be a film over it. |
| Bank the shore | float, 0 to 1, default 0.5 | Levels the ground just outside the waterline toward the water, so the lake meets a shore rather than a wall. This is the thing that reads as a lake. |
| Terrain size (m) | float, 1 to 1e+06, default 5000 | The tile's width; the studio keeps this in step with the project so the radius above means metres. |

## Light

### AreaLight

[Planned] Rectangular and disc area lights with soft shadows

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### LensFlare

[Planned] Lens flare and reflections editor per light

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### LightGel

[Planned] Projected texture (gel / gobo) on a spot light

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### LightSource

A point or spot light in the scene: position, colour, intensity, reach, cone

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Scene object | text | Name of the scene light this node drives. Created when missing; an existing light of that name is adopted. |
| Enabled | toggle, default on |  |
| Type | choice: Point / Spot |  |
| Colour | color |  |
| Intensity | float, 0 to 50, default 1 |  |
| Reach (m) | float, 1 to 100000, default 1750 | Distance at which the light has faded to nothing. |
| X (m) | float, -100000 to 100000, default 2500 |  |
| Height (m) | float, -10000 to 100000, default 1500 |  |
| Z (m) | float, -100000 to 100000, default 2500 |  |
| Heading ° | float, -180 to 180, default 0 |  |
| Pitch ° | float, -90 to 90, default -60 |  |
| Cone angle ° | float, 1 to 179, default 40 |  |
| Cast shadows | toggle, default off | Recorded now, honoured by the offline engines; the viewport's point lights do not cast shadows yet (roadmap P3). |

### Skylight

[Planned] Global illumination model: ambient, hemispherical sky light, GI

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### VolumetricLight

[Planned] Visible light shafts through haze and cloud

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

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
| Operation | choice: A > B / A < B / |A - B| < tol / |A - B| > tol | The comparison made at every point. |
| Tolerance | float, 0 to 1, default 0.05 | How close two values must be to count as equal, since exact equality between floats almost never happens. |
| Softness | float, 0 to 0.3, default 0.02 | How gradually the result crosses from false to true, so the mask has an edge rather than a step. |

### Repeat

Loop: apply an operation N times

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Smooth / Thermal step / Expand / Shrink / Fold ridges | What is done on each repeat. |
| Loop count | int, 1 to 64, default 4 | How many times it is repeated. |
| Strength per pass | float, 0.05 to 1, default 0.5 | How much each repeat contributes. |

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
| Index | int, 0 to 3, default 0 | Which input is passed through. Everything else is ignored, which is how you switch between variants of a graph without rewiring it. |
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
| Use input B | toggle, default off | Passes the second input instead of the first. A plain either-or, for turning part of a graph on and off. |

### Threshold

Binary/soft threshold to a mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Level | float, 0 to 1, default 0.5 | The value the input is cut at. |
| Softness | float, 0 to 0.5, default 0.05 | How gradually it crosses from 0 to 1. Zero gives a hard edge, which on terrain reads as drawn on. |
| Invert | toggle, default off | Selects below the level instead of above it. |

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
| Threshold | float, 0 to 1, default 0.5 | The value above which a point counts as part of a patch. |
| Min area (fraction) | float, 0 to 0.5, default 0.001 | Patches smaller than this are removed. This is how you clear speckle out of a mask without blurring the edges of what is left. |
| Invert | toggle, default off | Removes the large patches and keeps the small ones instead. |

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
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### KMeans

Cluster the terrain into zones

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| feature B | in (optional) | heightmap |
| clusters | out | heightmap |
| mask A | out | heightmap |
| mask B | out | heightmap |
| mask C | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Clusters | int, 2 to 8, default 4 | How many regions the terrain is grouped into. Each point joins the group whose character it most resembles, so this is how many distinct kinds of ground you are claiming exist. |
| Slope weight | float, 0 to 4, default 1 | How much steepness counts against height when deciding which group a point belongs to. At 0 the grouping is purely by altitude. |
| Seed | seed |  |

### MaskPaint

Paint a mask in the viewport

| Port | Direction | Type |
| :--- | :--- | :--- |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Painted mask | painted buffer | Select this node and paint in the viewport with the Terrain Editor brushes. Raise paints in, invert (or the eraser) paints out. |
| Soften | float, 0 to 0.05, default 0 | Blurs the painted mask, so a stroke's edge is a gradient rather than the hard rim of the brush. |

### SelectAltitude

Select by height band

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 | How gradually the selection gives out at its edges. Near zero gives a hard cut, which reads as drawn on; a soft edge is what lets one material give way to another. |
| Invert | toggle, default off | Selects everything this node did not - the ground it rejected becomes the mask. |
| Altitude band | range | The band of heights that is selected. Everything inside is chosen, everything outside rejected, with the edge softness above deciding how abruptly. |

### SelectBlobs

Find blob-shaped features at a size

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Blob size | float, 0.005 to 0.3, default 0.03 | The feature size to look for. This is a band-pass: blobs much larger or smaller than this are ignored. |
| Strength | float, 0 to 1, default 0.15 | How strongly a blob must stand out from its surroundings to be selected at all. |
| Hollows instead of bumps | toggle, default off | Finds dips instead of bumps - the same detector run the other way up. |

### SelectBorder

A band along a mask's boundary

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Threshold | float, 0 to 1, default 0.5 | The value that counts as inside the region whose border is wanted. |
| Reach | float, 0.002 to 0.5, default 0.05 | How wide the band along the boundary is. |
| Side | choice: Both / Inward / Outward | Whether the band lies inside the region, outside it, or straddles the line. Inside is what you want to darken a shore; outside to spill something past an edge. |

### SelectCavities

Ambient-occlusion-like cavity map

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 | How gradually the selection gives out at its edges. Near zero gives a hard cut, which reads as drawn on; a soft edge is what lets one material give way to another. |
| Invert | toggle, default off | Selects everything this node did not - the ground it rejected becomes the mask. |
| Radius | float, 0.005 to 0.1, default 0.02 | How far the sampling reaches when deciding how enclosed a point is. Larger radii find broad basins; small ones find pits and crevices. |

### SelectCurvature

Select concave (valleys) or convex (ridges)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 | How gradually the selection gives out at its edges. Near zero gives a hard cut, which reads as drawn on; a soft edge is what lets one material give way to another. |
| Invert | toggle, default off | Selects everything this node did not - the ground it rejected becomes the mask. |
| Mode | choice: Convex (ridges) / Concave (valleys) | Concave picks the hollows - valley floors, gullies, the places water and soil collect. Convex picks the ridges and outcrops where they are stripped away. |
| Feature scale | float, 0.002 to 0.1, default 0.01 | How large a feature counts. Small scales find surface crinkles; large ones find whole landforms. |

### SelectMidrange

Select the middle elevations

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 | How gradually the selection gives out at its edges. Near zero gives a hard cut, which reads as drawn on; a soft edge is what lets one material give way to another. |
| Invert | toggle, default off | Selects everything this node did not - the ground it rejected becomes the mask. |
| Center | float, 0 to 1, default 0.5 | The height the selection is centred on, as a fraction of the terrain's range. |
| Width | float, 0.02 to 1, default 0.25 | How far either side of the centre is still selected. This is the band-pass of the three altitude selectors: it takes the middle ground and leaves the peaks and the floors. |

### SelectSlope

Select by slope steepness

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Edge softness | float, 0.001 to 1, default 0.1 | How gradually the selection gives out at its edges. Near zero gives a hard cut, which reads as drawn on; a soft edge is what lets one material give way to another. |
| Invert | toggle, default off | Selects everything this node did not - the ground it rejected becomes the mask. |
| Slope band | range | The band of steepness that is selected, 1 being flat ground and 0 a vertical face. Rock wants a low band, meadow a high one. |

### SelectTransitions

Select where two surfaces trade places

| Port | Direction | Type |
| :--- | :--- | :--- |
| input A | in | heightmap |
| input B | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Tolerance | float, 0.001 to 0.5, default 0.05 | How close two surfaces must come before the place they meet is called a transition. |
| Invert | toggle, default off | Selects everywhere the two surfaces do not trade places. |

### Skeleton

Thin a mask to its centerlines

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Threshold | float, 0 to 1, default 0.5 | The value above which a point counts as part of the region being reduced to a centre line. |

### SkeletonDistance

How deep into the shape each cell sits

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Threshold | float, 0 to 1, default 0.5 | The value above which a point counts as part of the region whose centre line the distance is measured from. |

## Material

### AOFromHeight

Ambient occlusion baked from a height input

| Port | Direction | Type |
| :--- | :--- | :--- |
| height | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Radius | float, 0.002 to 0.15, default 0.02 | How far around each point the surroundings are sampled to decide how enclosed it is. Small radii darken creases and pits; large ones darken whole valleys. |
| Strength | float, 0 to 3, default 1 | How dark the enclosed places get. Ambient occlusion is the soft shadow of a surface against itself, and a little of it does more for the sense of depth than any amount of bump. |

### AlbedoToPBR

Derive normal + roughness maps from an albedo texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| albedo | in | texture |
| normal | out | texture |
| roughness | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normal strength | float, 0.1 to 10, default 2 | How much relief is inferred from the photograph's brightness. A photograph has no depth in it, so this is a guess: too much and every dark patch becomes a dent. |
| Roughness base | float, 0 to 1, default 0.8 | The roughness the whole surface starts at, before the picture varies it. |
| Roughness variation | float, 0 to 1, default 0.3 | How much the picture's own detail varies the roughness, so darker, damper-looking areas come out glossier than pale dry ones. |
| Bright = smooth | toggle, default on | Swaps which end of the picture reads as glossy. If the highlights are landing on the wrong parts of the surface, this is the switch. |

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
| Normalize inputs | toggle, default on | Rescales the result to fill 0..1. Off keeps the raw values, which is what you want when the output feeds arithmetic rather than a blend. |

### ChannelSplit

Split a texture into red, green, blue, alpha and luminance masks

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| r | out | heightmap |
| g | out | heightmap |
| b | out | heightmap |
| a | out | heightmap |
| luminance | out | heightmap |

### ColorAdjust

Color correction: brightness, contrast, saturation, hue, tint

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Brightness | float, 0.2 to 3, default 1 | Scales every colour up or down. |
| Contrast | float, 0.2 to 3, default 1 | Pushes colours away from mid-grey, or toward it below 1. |
| Saturation | float, 0 to 3, default 1 | How strong the colour is. 0 leaves greyscale with all the detail intact. |
| Hue shift ° | float, -180 to 180, default 0 | Rotates every colour around the wheel, in degrees. A small shift is the cheapest way to make one photographed surface look like a different rock. |
| Tint R | float, 0 to 2, default 1 | Multiplies the red channel, for correcting a cast rather than recolouring. |
| Tint G | float, 0 to 2, default 1 | Multiplies the green channel. |
| Tint B | float, 0 to 2, default 1 | Multiplies the blue channel. |

### CurvatureFromHeight

Convex/concave curvature map from height

| Port | Direction | Type |
| :--- | :--- | :--- |
| height | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Feature scale | float, 0.002 to 0.1, default 0.01 | How large a feature the curvature is measured over. Small finds surface crinkle; large finds whether you are on a ridge or in a hollow. |
| Contrast | float, 0.1 to 6, default 1 | Stretches the result apart. Raw curvature bunches around zero, so without this most of the map reads as flat. |

### DistributionLayer

Distribution layer: the presence that shades also places objects

| Port | Direction | Type |
| :--- | :--- | :--- |
| albedo | in (optional) | texture |
| presence | in (optional) | heightmap |
| albedo | out | texture |
| presence | out | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Instances | int, 1 to 50000, default 800 | How many objects the presence places at full presence. |
| Min spacing | float, 0.001 to 0.3, default 0.015 | The closest two placed items may come to one another. This is what stops a distribution clumping into overlapping piles at high density. |
| Presence threshold | float, 0 to 1, default 0.15 | Presence below this places nothing at all. |
| Seed | seed |  |
| Instance size from | choice: Uniform / Presence / Power law | The point value: the same for all, following the presence (strong presence, big plant), or many small and a few large. |

### EcosystemLayer

Ecosystem layer: a population placed by the layer's presence, reacting to the layer below

| Port | Direction | Type |
| :--- | :--- | :--- |
| below albedo | in (optional) | texture |
| below normal | in (optional) | texture |
| below rough | in (optional) | texture |
| mask | in (optional) | heightmap |
| terrain | in (optional) | heightmap |
| below | in (optional) | ? |
| objects | in (optional) | heightmap |
| driver | in (optional) | heightmap |
| albedo | out | texture |
| normal | out | texture |
| roughness | out | texture |
| presence | out | heightmap |
| points | out | ? |
| density | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Name | text | What this population is called in the stack and the Objects tree. |
| Populate | toggle, default on | Turns the layer off without removing it or losing its settings. |
| Invert presence | toggle, default off | Uses the mask the other way round: the layer appears where the mask is dark. |
| Density (per hectare) | float, 0.01 to 100000, default 20 | Instances per hectare (100 m x 100 m) at full presence. A rate, not a count: the same setting fills a 1 km tile and a 20 km one to the same look. |
| Minimum spacing (m) | float, 0.05 to 1000, default 5 | The lattice the candidates stand on. Changing the density never moves an instance; changing this reseeds them all. |
| Placement | choice: Jittered / Random / Regular | How the candidate positions are laid out before anything is rejected. A jittered lattice covers ground evenly; purely random leaves clumps and bald patches, which is sometimes what you want. |
| Clumping | float, 0 to 1, default 0 | Groups instances together as species do in nature. |
| Clump size (m) | float, 0.5 to 5000, default 60 | How far across one clump of plants is, in metres. Vegetation gathers where the ground suits it, and a population spread perfectly evenly is the clearest sign of a generated one. |
| Seed | seed |  |
| Populate around the camera | toggle, default off | Off: the population covers the terrain tile, computed once. On: it covers the ground wherever the camera goes - a planet or an infinite terrain has no tile to cover - generated in cells on demand and thrown away behind you. What a cell holds never depends on where it was seen from. |
| Populate within (m) | float, 10 to 200000, default 2000 | How far from the camera the ground is populated when 'Populate around the camera' is on. |
| Terrain size (m) | float, 1 to 1e+06, default 5000 | The tile's width; the studio keeps this in step with the project so the rate above means what it says. |
| Presence threshold | float, 0 to 1, default 0.05 | Presence below this places nothing at all. |
| Slope influence | float, 0 to 1, default 0.5 | 1: instances thin out on steep ground. 0: the same density whatever the slope. |
| By altitude | toggle, default off | Limits the layer to a band of heights. |
| Range of altitudes | choice: By terrain / Absolute / Relative to sea | Whether the altitude band is read against the terrain's own range, in absolute height units, or from sea level. |
| Altitude band | range | As a fraction of the terrain's own height range. |
| Sea level | float, 0 to 1, default 0 | The height that counts as sea level, when the altitude band is measured from it rather than from the terrain's own range. |
| Fade | float, 0 to 0.5, default 0.08 | How gradually the layer gives out at the edges of its altitude band. Zero draws a contour line across the hillside. |
| By slope | toggle, default off | Limits the layer to a band of steepness. |
| Slope band | range | Degrees from horizontal. 0 is flat, 90 is a cliff. |
| Fade | float, 0 to 45, default 6 | How gradually the layer gives out at the edges of its slope band. |
| By orientation | toggle, default off | Limits the layer to slopes facing a particular way. |
| Faces | float, 0 to 360, default 0 | Compass direction the surface looks towards. 0 is north. |
| Arc | float, 5 to 180, default 60 | How wide an arc of facings counts as the favoured direction. Narrow puts moss on the north face alone; wide covers most of the hill. |
| Fade | float, 0 to 90, default 20 | How gradually the layer gives out as a slope turns away from the favoured direction. |
| Terrain height scale | float, 0.001 to 100, default 1 | World height of a heightmap unit as a fraction of the tile's width; the studio keeps this in step with the project so a slope in degrees is the slope the viewport shows. |
| Decay near objects | float, 0 to 1, default 0 | Thins the population around the objects standing on the terrain (the 'objects' input: 0 at an object, 1 far away). 1 leaves a void right at them. |
| Reach | float, 0.001 to 0.5, default 0.05 | How far from the objects the decay extends, as a fraction of the terrain. |
| Falloff | float, -1 to 1, default 0 | 0 linear. Positive: the void is larger and more sudden. Negative: gentler. |
| Affinity with layer below | float, -1 to 1, default 0 | Positive: instances gather around the instances of the layer below (primroses around the trees) and thin out elsewhere. Negative: everywhere except near them. |
| Affinity radius (m) | float, 0.1 to 2000, default 25 | How far this population reaches to gather around the one below it, in metres. |
| Repulsion from layer below | float, -1 to 1, default 0 | Sudden. Positive: a void around each instance below (no grass under the canopy). Negative: only inside that void (small stones at the foot of the boulder). Use both: near the trees but not under them. |
| Repulsion radius (m) | float, 0.1 to 2000, default 8 | How far this population is pushed back from the one below it, in metres - the bare ring around the base of a tree. |
| Avoid overlapping instances | toggle, default on | No two instances closer than their footprints allow. |
| Species | int, 1 to 8, default 1 | How many kinds of object this layer places. Each scene object bound to the layer picks the species it stands for. |
| Species 1 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 1 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 2 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 2 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 3 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 3 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 4 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 4 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 5 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 5 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 6 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 6 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 7 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 7 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 8 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 8 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Overall scaling | float, 0.05 to 10, default 1 | The size of one copy, as a multiple of the mesh's own size. |
| Size variation | float, 0 to 1, default 0.3 | 1: instances range from half to twice the size. |
| Keep proportions | float, 0 to 1, default 1 | 1: the three axes scale together. 0: each on its own. |
| Direction from surface | float, 0 to 1, default 0 | 0: instances grow vertically whatever the slope. 1: perpendicular to the ground (rocks); trees want 0. |
| Rotation | choice: Up axis / None / Driven | How far a copy may be turned about its up axis. Full rotation is right for anything without a front; less keeps a set of objects aligned. |
| Maximum angle | float, 0 to 1, default 1 | As a fraction of a half turn either way. |
| Offset from surface (m) | float, -50 to 50, default 0 | Negative buries the instance. |
| Footprint radius (m) | float, 0.01 to 500, default 2 | The ground one instance claims at scale 1; what overlap avoidance and the layer above measure against. |
| Shrink at low density | float, -1 to 1, default 0 | Lone instances are smaller (negative: larger), as at the edge of a wood. |
| Low-density radius (m) | float, 0.1 to 2000, default 30 | How close to the population below a copy must be before it is made smaller, in metres. This is what puts stunted growth under a canopy rather than an abrupt edge. |
| Lean out at low density | float, 0 to 1, default 0 | Lone instances lean into the slope, as plants reaching for light. |
| Color variation | float, 0 to 1, default 0.3 | How much copies differ in brightness from one another. Identical tint across a whole population is the second clearest sign of instancing, after identical size. |
| Time offset range (s) | float, 0 to 10, default 1 | Each instance's wind phase is shifted by up to this, so a field sways as a crowd, not a marching army. |

### EffectorLayer

Effector layer: a typed influence field for other systems to read

| Port | Direction | Type |
| :--- | :--- | :--- |
| albedo | in (optional) | texture |
| mask | in (optional) | heightmap |
| albedo | out | texture |
| effector | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Effector kind | choice: Pressure / Wind / Light / Heat / Moisture / Custom | What this field means to the systems that read it. Pressure: trodden or loaded ground. Wind: local air movement. Light and Heat: exposure. Moisture: wetness. Custom: whatever a script assigns it. |
| Strength | float, 0 to 4, default 1 | How strongly this layer pushes the layers below it around. |
| Falloff | float, 0.1 to 6, default 1 | A power on the mask: above 1 the field concentrates where the mask is strongest, below 1 it spreads. |
| Invert | toggle, default off | Pushes where it would have pulled, and the other way about. |
| Tint the material by the field | toggle, default off | Blends the field into the colour so it can be seen in the viewport while it is being painted. Off, the material's colour passes through untouched. |

### FlatColor

Solid color material (procedural function + color)

| Port | Direction | Type |
| :--- | :--- | :--- |
| mask | in (optional) | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Red | float, 0 to 1, default 0.5 | The red component, linear rather than sRGB: 0.5 here is not the mid-grey you would pick in a paint program. That matters when a value is matched against a photograph. |
| Green | float, 0 to 1, default 0.45 | The green component, linear rather than sRGB. |
| Blue | float, 0 to 1, default 0.4 | The blue component, linear rather than sRGB. |

### FractalColor

A fractal through a colour map: RGBA texture out, and the same pattern as a mask

| Port | Direction | Type |
| :--- | :--- | :--- |
| warp | in (optional) | heightmap |
| mask | in (optional) | heightmap |
| texture | out | texture |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Base noise | choice: Perlin / Value / Cellular / Cell edges / Grainy | The noise the fractal is built from. |
| Wavelength | float, 0.005 to 2, default 0.25 | Size of the largest feature, in tile widths. |
| Iterations | int, 1 to 16, default 8 | How many times the pattern is added at a smaller size. |
| Roughness | float, 0 to 2, default 1 | 1 keeps the same detail at every scale. Lower is smoother, higher is grittier. |
| Gain | float, 0.2 to 6, default 1 | Contrast of the result. |
| Distortion | float, 0 to 1, default 0 | Smears the sampling position with a low-frequency noise, which breaks up the lattice the noise sits on. |
| Shape | choice: Plain / Ridges / Billows / Ridge mix / Billow/ridge mix | The shape the harmonics take - rolling, ridged or billowed. As colour rather than terrain, ridges read as veining and billows as mottling. |
| Warp by input | float, 0 to 2, default 0.3 | How far the warp input displaces the sample position. |
| Bias | float, 0.01 to 0.99, default 0.5 | Moves the midpoint of the pattern: below 0.5 the colour map's left end takes more of the surface. |
| Gain | float, 0.01 to 0.99, default 0.5 | Pushes values away from the middle. High values give hard-edged patches rather than a smooth wash. |
| Colour map | gradient | Colour and alpha both come from here. A stop's alpha becomes the texture's alpha, so one fractal can decide a layer's look and its presence at the same time. |

### GradientMap

Recolor a texture through a gradient by luminance

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Gradient | gradient | The colour ramp the incoming value is looked up in: 0 takes the left end, 1 the right. This is how a height, a slope or a mask becomes colour. |
| Amount | float, 0 to 1, default 1 | How much of the mapped colour replaces what came in. |

### Levels

Levels: remap input black/white/gamma to output range

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Input black | float, 0 to 1, default 0 | Input values at or below this are pulled to black. Raising it deepens the shadows and throws away whatever detail was below it. |
| Input white | float, 0 to 1, default 1 | Input values at or above this are pushed to white. |
| Gamma | float, 0.1 to 4, default 1 | Bends the midtones without moving the two ends. Below 1 darkens them, above 1 lifts them. |
| Output black | float, 0 to 1, default 0 | The darkest the result is allowed to be. Raising it lifts the whole picture off black, which is what a hazy or dusty surface actually does. |
| Output white | float, 0 to 1, default 1 | The brightest the result is allowed to be. |
| Per channel | toggle, default off | Off: operate on luminance and keep the hue. On: apply the curve to R, G and B separately. |

### MaskToTexture

Grayscale mask or heightmap as a texture channel

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normalize | toggle, default on | Rescales the mask to fill 0..1 before it becomes a picture. |
| Scale | float, 0 to 2, default 1 | Multiplies the mask on its way into the texture. |
| Offset | float, -1 to 1, default 0 | Added to the mask on its way into the texture. |

### MaterialLayer

One layer of a material stack: its own maps, its own mask, and its own reaction to altitude, slope and orientation

| Port | Direction | Type |
| :--- | :--- | :--- |
| below albedo | in (optional) | texture |
| below normal | in (optional) | texture |
| below rough | in (optional) | texture |
| albedo | in (optional) | texture |
| normal | in (optional) | texture |
| roughness | in (optional) | texture |
| mask | in (optional) | heightmap |
| terrain | in (optional) | heightmap |
| below displacement | in (optional) | heightmap |
| displacement | in (optional) | heightmap |
| albedo | out | texture |
| normal | out | texture |
| roughness | out | texture |
| presence | out | heightmap |
| displacement | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Name | text | What this layer is called in the stack. Naming them is the difference between a readable material and six rows of 'Layer'. |
| Visible | toggle, default on | Turns the layer off without removing it or losing its settings. |
| Opacity | float, 0 to 1, default 1 | Overall presence of the layer, within whatever the environment constraints below already allow. It cannot put the layer anywhere they exclude. |
| Blend | choice: Normal / Cover / Colour only / Add / Multiply | Normal: ordinary alpha-over. Cover: colour switches without a ramp, only the normal transitions, so the layer reads as sitting on top. Colour only: takes colour from here, everything else from below. |
| Invert mask | toggle, default off | Uses the mask the other way round: the layer appears where the mask is dark. |
| Roughness | float, 0 to 1, default 0.8 | Used where this layer has no roughness map connected. |
| Add to normals below | float, 0 to 1, default 1 | 1: this layer's relief adds to the layer beneath, the way lichen sits on rock. 0: it replaces it, the way snow flattens what it covers. |
| Displacement | float, 0 to 4, default 1 | Multiplies the displacement input - a FakeStones or GrassDisplacement 'displacement' output, or any relief in heightmap units - where this layer is present. |
| Add to displacement below | float, 0 to 1, default 1 | 1 stacks this layer's relief on the layers below; 0 replaces theirs where this layer is present. |
| Alpha boost | float, -1 to 1, default 0 | The layer's overall presence, within what the constraints below allow. Positive: stronger. |
| Highlight (solid color) | toggle, default off | Shows the layer as a flat colour so you can see where it lands. Shading is off while highlighted. |
| Highlight color | color | The flat colour the layer is shown in while Highlight is on, so you can see exactly where it lands. |
| By altitude | toggle, default off | Limits the layer to a band of heights. |
| Range of altitudes | choice: By terrain / Absolute / Relative to sea | By terrain: the band is a fraction of this terrain's own range. Absolute: in the terrain's height units. Relative to sea: measured from the sea level below. |
| Altitude band | range | As a fraction of the terrain's own height range. |
| Sea level | float, 0 to 1, default 0 | The height that counts as sea level, when the altitude band is measured from it rather than from the terrain's own range. |
| Fade | float, 0 to 0.5, default 0.08 | How gradually the layer gives out at the edges of its altitude band. Zero draws a contour line across the hillside. |
| By slope | toggle, default off | Limits the layer to a band of steepness. |
| Slope band | range | Degrees from horizontal. 0 is flat, 90 is a cliff. |
| Fade | float, 0 to 45, default 6 | How gradually the layer gives out at the edges of its slope band. |
| Height scale | float, 0.0001 to 1000, default 1 | The terrain's vertical scale, so 'By slope' and 'By orientation' read real degrees. Kept in step with the project automatically. |
| By orientation | toggle, default off | Limits the layer to slopes facing a particular way. |
| Faces | float, 0 to 360, default 0 | Compass direction the surface looks towards, in degrees. 0 is north. North faces hold snow; south faces dry out. |
| Arc | float, 5 to 180, default 60 | How far either side of that direction still counts. |
| Fade | float, 0 to 90, default 20 | How gradually the layer gives out as a slope turns away from the favoured direction. |
| Tiling | float, 0.05 to 64, default 1 | How many times this layer's own maps repeat across the terrain. Does not affect the mask or the constraints. |
| Offset | x/y pair | Slides this layer's own maps across the surface, without moving the layers around it. |
| Rotation | float, -180 to 180, default 0 | Turns this layer's own maps. Rotating one layer of several breaks the alignment that makes a stack read as printed. |

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
| alpha | in (optional) | texture |
| displacement | in (optional) | heightmap |
| preview | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Material name | text | What this material is called in the Objects tree and the material browser. It is how you pick it when assigning it to something. |
| Overall color | color | Multiplies every colour in the material. White leaves it alone. |
| Brightness | float, 0 to 2, default 1 | Scales the whole colour up or down after everything upstream. Use it to sit a material into a scene without going back and editing the maps that made it. |
| Saturation | float, 0 to 2, default 1 | How strong the colour is. 0 leaves a greyscale surface with all of its detail intact, which is often closer to real weathered rock than the photograph it came from. |
| Color blend | toggle, default off | Blend the picture with a solid colour, in product mode. |
| Blend color | color | A colour mixed into the whole surface. This is the quickest way to tint a shared material differently per object - a wash of ochre over the same rock. |
| Blend amount | float, 0 to 1, default 0.5 | How much of the blend colour is mixed in. |
| Color mask | float, 0 to 1, default 0 | 0: the colour multiplies the picture. 1: it replaces it. |
| Global alpha | float, 0 to 1, default 1 | Where no alpha map is connected. Alpha does not bend light; transparency does. |
| Alpha boost | float, -1 to 1, default 0 | For a layer of a multi-layer material: its overall presence, within the limits the Presence tab sets. |
| Normal intensity | float, 0 to 4, default 1 | How much of the normal map's vector is applied. |
| Bump depth | float, -4 to 4, default 1 | The amount of bump. Negative turns bumps into holes. |
| Dependent on slope | float, 0 to 1, default 0 | Higher bumps on steep faces than on flat ground, as on eroded terrain. |
| Invert normal map | toggle, default off | Flips the green channel of the normal map. There are two conventions for which way is up and they are visually identical until the light moves - if the bumps read as dents, this is the switch. |
| Displacement depth | float, 0 to 0.1, default 0 | Height map displacement applied to the surface, in world units. Moves geometry, not only normals. |
| Displacement smoothing | float, 0 to 1, default 0 | Softens the displacement before it moves the surface, without touching the colour. Use it when a height map is noisier than the geometry can carry. |
| Lighting model | choice: GGX / Phong | GGX: the physically based microfacet model, size is roughness. Phong: the legacy model, size and intensity independent. |
| Highlight intensity | float, 0 to 1, default 0.35 | How bright the direct highlight is. This is the sheen a light leaves on the surface, as against Reflectivity below, which is how much of the surroundings it mirrors. |
| Roughness (highlight size) | float, 0.02 to 1, default 0.85 | Small: a polished surface with tight bright spots. Large: dull. Also multiplies a connected roughness map. |
| Highlight color | color | A uniform shade for the highlights - blue for pearl. |
| Anisotropy | float, 0 to 1, default 0 | Stretched highlights along a direction, for brushed metal or hair. |
| Global transparency | float, 0 to 1, default 0 | How much light passes straight through. 0 is opaque. |
| Refraction index | float, 1 to 2.5, default 1 | 1 air, 1.33 water, 1.52 glass. Bends light crossing the surface; also sets how reflective a transparent surface is. |
| Turn reflective with angle | float, 0 to 1, default 0 | Glass and water mirror at a low angle. About 0.4 looks right. |
| Fade out | float, 0 to 1, default 0 | How much the surface thins toward its silhouette, so an edge dissolves rather than ending on a hard line. |
| Thin surface (no refraction) | toggle, default off | Treats the surface as having no thickness, so light passes through without bending. Right for glass panes, leaves and water films; wrong for a solid body of water, which does refract. |
| Additive | toggle, default off | Adds the colour to the background: luminous, immaterial objects. |
| Flare intensity | float, 0 to 1, default 0 | Brightening when light is seen through a partly transparent surface. Strongest at 50% transparency. |
| Flare span | float, 0 to 1, default 0.2 | How far the highlight spreads. Narrow reads as polished, wide as satin. |
| Global reflectivity | float, 0 to 1, default 0.25 | How much of the surroundings the surface mirrors. Distinct from the highlight: this is the world reflected, that is the light source. |
| Minimal reflectivity | float, 0 to 1, default 0 | Reflectivity looking straight at the surface; the angle sensitivity raises it toward grazing. |
| Sensitivity to incidence angle | float, 0 to 1, default 0.5 | How much more reflective the surface becomes at a grazing angle. Nearly every real material does this - it is why a wet road mirrors the sky ahead but not underfoot - so 0 reads as wrong. |
| Blurred reflections | float, 0 to 1, default 0 | How blurred the reflection is. 0 is a mirror; higher is brushed metal or rippled water. |
| Metalness | float, 0 to 1, default 0 | Metal reflects its own colour and has no diffuse. Also multiplies a connected metallic map. |
| Specular level (PBR) | float, 0 to 1, default 0.5 | F0 of the non-metal parts: 0.5 is the common 4 %, 1 is 8 %. |
| Translucency | float, 0 to 1, default 0 | Light bleeding through thin material toward the viewer. |
| Subsurface scattering | toggle, default off | Lets light enter the surface, scatter inside and leave elsewhere. This is what makes skin, wax, marble and snow look lit from within rather than merely lit. |
| Average depth (m) | float, 0.0001 to 1, default 0.01 | How far light travels inside: a fraction of a millimetre for skin, centimetres for wax. |
| Absorption / scattering balance | float, 0 to 1, default 0.5 | Whether light inside the surface is mostly absorbed or mostly scattered onward. Toward absorption gives dense, waxy material; toward scattering gives translucent, glowing material. |
| Scattering color | color | The colour light picks up inside - the red of a finger over a torch. |
| Backlight | toggle, default off | Thin enough that light shows through from behind, like a leaf. |
| Coat intensity | float, 0 to 1, default 0 | A thin reflective layer on top: the lacquer over car paint. |
| Coat tint | color | The colour of the clear coat over the surface - the lacquer on car paint, the wet film on a stone. It reflects and tints without changing the material underneath. |
| Coat roughness | float, 0.02 to 1, default 0.1 | How polished the clear coat is, independently of the surface under it. A rough stone under a smooth wet coat is exactly what a rock in a stream is. |
| Coat refraction index | float, 1 to 2.5, default 1.5 | The coat's refractive index, which sets how strongly it reflects at a glancing angle. 1.5 is a lacquer or a varnish; 1.33 is water. |
| Flatten | float, 0 to 1, default 1 | 1: the coat has its own smooth normal. 0: it follows the bumps below. |
| Diffuse lighting | float, 0 to 1, default 0.6 | How the material reacts to light from light sources. Diffuse + ambient should stay at 100%. |
| Ambient lighting | float, 0 to 1, default 0.4 | How much of the surrounding sky light the surface picks up where nothing shines on it directly. Too low makes the shadows read as black holes. |
| Luminous | float, 0 to 2, default 0 | Seems to emit light. Does not cast real light. |
| Luminous color | color | Light the surface emits by itself. It lights nothing else in the viewport - this is the surface glowing, not a lamp. |
| Contrast | float, 0.2 to 4, default 1 | How fast the surface goes from light to shadow; low for fluffy things. |
| Color reflected light | toggle, default off | Highlights and reflections take the surface colour: metal. |
| Color transmitted light | toggle, default off | Light crossing a transparent surface takes its colour: stained glass. |
| Casts shadows | toggle, default on | Whether the surface blocks light. Turning it off is a lighting cheat, useful for glass and for foliage cards that would otherwise shadow themselves into mud. |
| Receives shadows | toggle, default on | Whether other things can cast shadows onto this surface. |
| One sided | toggle, default off | Traced for one intersection per ray; matters for transparent surfaces. |
| Hide from camera rays | toggle, default off | Seen only in reflections and refractions. |
| Hide from reflected / refracted rays | toggle, default off | Keeps the surface out of reflections and refractions while leaving it visible to the camera. A compositing convenience, not physics. |
| Ignore lighting | toggle, default off | No sun, no lights: the surface shows its own colour. |
| Ignore atmosphere | toggle, default off | No fog or haze between it and the camera. |
| Only shadows | toggle, default off | Invisible, but still casts a shadow. |
| Disable anti-aliasing | toggle, default off | Turns off edge smoothing for this material. Only wanted where a hard pixel boundary is the point, such as an index or ID pass. |
| Mapping | choice: Automatic / Flat / Faces / Cylindrical / Spherical | How the 2D maps wrap a 3D object. Terrain is always Flat (projected from above); the others are for objects. |
| Scale of the maps | float, 0.05 to 20, default 1 | Scales every texture map together. |
| Origin | x/y pair | Offsets the material in map space, for precise placement. |
| Rotation | float, -180 to 180, default 0 | Turns the maps about the surface normal, in degrees. |
| Turbulence | toggle, default off | A noise repeatedly displaces where the maps are read, so a tiled picture stops looking tiled. |
| Complexity | int, 1 to 8, default 3 | How many scales of small-scale disturbance ripple the surface normal. This is shading detail only - it does not move the geometry. |
| Amplitude | float, 0 to 0.5, default 0.05 | How strongly the disturbance tilts the surface normal. A little breaks up a surface that reads as too clean; a lot looks like hammered metal. |
| Scale | float, 0.25 to 64, default 4 | How fine the disturbance is, in repeats across the surface. |
| Harmonics | float, 0.1 to 0.9, default 0.5 | How scale and amplitude shrink with each repetition of the noise. |
| Cycling | float, 0 to 1, default 0 | A large, slow perturbation that keeps a material from repeating. |

### MaterialStack

Blend up to six material layers by mask, height-aware, into albedo + roughness

| Port | Direction | Type |
| :--- | :--- | :--- |
| mask 1 | in (optional) | heightmap |
| albedo 1 | in (optional) | texture |
| mask 2 | in (optional) | heightmap |
| albedo 2 | in (optional) | texture |
| mask 3 | in (optional) | heightmap |
| albedo 3 | in (optional) | texture |
| mask 4 | in (optional) | heightmap |
| albedo 4 | in (optional) | texture |
| mask 5 | in (optional) | heightmap |
| albedo 5 | in (optional) | texture |
| mask 6 | in (optional) | heightmap |
| albedo 6 | in (optional) | texture |
| terrain | in (optional) | heightmap |
| albedo | out | texture |
| roughness | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Mixing | choice: Weighted layers / Two materials (distribution) | Weighted: every layer by its own mask. Two materials: material 1 and 2 by one distribution (mask 1) against the proportions, as Vue mixes. |
| Mixing proportions | float, 0 to 1, default 0.5 | Left: more of material 1. Right: more of material 2. |
| Smooth blending strip | float, 0 to 1, default 0.2 | The width of the band where the two are blended. |
| Blending method | choice: Simple blend / Full blend (linear bumps) / Full blend (cubic bumps) / Cover / Color and lighting blend | How the layers of the stack are combined into one surface. |
| Distribution dependent on environment | toggle, default off | Whether the layers are allowed to react to altitude, slope and orientation at all. Off, they blend by their masks alone. |
| Influence of altitude | float, -1 to 1, default 0 | Positive: material 2 higher up. Negative: lower down. |
| Influence of slope | float, -1 to 1, default 0 | Positive: material 2 on steep faces. Negative: on flat. |
| Influence of orientation | float, 0 to 1, default 0 | How strongly the direction a slope faces counts toward which layer shows. This is what puts moss on the north face and dry grass on the south. |
| Azimuth | float, 0 to 360, default 0 | Material 2 gathers on faces looking this way. 0 is north. |
| Height blend | float, 0 to 1, default 0.5 | 0: plain weighted mix. 1: the layer whose texture is highest at this texel wins — silt fills the cracks of the rock before it covers the ridges. |
| Blend depth | float, 0.02 to 1, default 0.25 | How far below the winning layer others still show. |
| Roughness | float, 0 to 1, default 0.8 | How rough this layer's surface is: 0 is a mirror, 1 is matt. Wet rock and ice sit low, dry scree and grass high, and the difference between them is most of what tells one layer from another when they are the same colour. |
| Roughness | float, 0 to 1, default 0.8 | How rough this layer's surface is: 0 is a mirror, 1 is matt. Wet rock and ice sit low, dry scree and grass high, and the difference between them is most of what tells one layer from another when they are the same colour. |
| Roughness | float, 0 to 1, default 0.8 | How rough this layer's surface is: 0 is a mirror, 1 is matt. Wet rock and ice sit low, dry scree and grass high, and the difference between them is most of what tells one layer from another when they are the same colour. |
| Roughness | float, 0 to 1, default 0.8 | How rough this layer's surface is: 0 is a mirror, 1 is matt. Wet rock and ice sit low, dry scree and grass high, and the difference between them is most of what tells one layer from another when they are the same colour. |
| Roughness | float, 0 to 1, default 0.8 | How rough this layer's surface is: 0 is a mirror, 1 is matt. Wet rock and ice sit low, dry scree and grass high, and the difference between them is most of what tells one layer from another when they are the same colour. |
| Roughness | float, 0 to 1, default 0.8 | How rough this layer's surface is: 0 is a mirror, 1 is matt. Wet rock and ice sit low, dry scree and grass high, and the difference between them is most of what tells one layer from another when they are the same colour. |

### NaturalGrain

Natural grain: one or two colours varied by a noise, for ground and rock

| Port | Direction | Type |
| :--- | :--- | :--- |
| mask | in (optional) | heightmap |
| texture | out | texture |
| grain | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Base color | color | The first of the two colours the grain runs between. |
| Mix with a second color | toggle, default on | Off, the grain varies only in brightness. On, it runs between two colours, which is how granite gets its mixed mineral speckle. |
| Second color | color | The second colour, when two are used. |
| Scale | float, 0.005 to 4, default 0.2 | The overall size of the grain. Keep it large for a terrain, small for a pebble. |
| Roughness | float, 0 to 1, default 0.6 | How much fine detail rides on the large variation. |
| Contrast | float, 0 to 1, default 0.5 | How sharply the grain separates. Low is a soft mottle; high gives distinct grains against a background. |
| Balance | float, 0 to 1, default 0.5 | Which of the two colours dominates. |
| Distortion | float, 0 to 1, default 0 | Warps the grain so it stops looking like a noise. |
| Seed | seed |  |

### NormalBlend

Combine two normal maps (whiteout blend)

| Port | Direction | Type |
| :--- | :--- | :--- |
| base | in | texture |
| detail | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Detail strength | float, 0 to 3, default 1 | How much of the detail normal is laid over the base one. The two are combined so the fine map rides on the coarse one rather than replacing its direction. |

### PBRMaterial

A PBR material set (albedo/normal/roughness/AO): a folder of maps on disk, or a CC0 photoscan downloaded from ambientCG

| Port | Direction | Type |
| :--- | :--- | :--- |
| albedo | out | texture |
| normal | out | texture |
| roughness | out | texture |
| ao | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Source | choice: ambientCG download / Folder on disk | Where the maps come from. A folder holds the set's images; the maps are found by name (color/albedo/diffuse, normal, roughness, ao/ambientocclusion) - the layout every PBR library ships. |
| Material folder (or any map in it) | file path | Pick any image of the set; the folder it is in is scanned for the other maps. |
| ambientCG asset ID | text | Which surface from the library. Each brings its own colour, normal, roughness and height maps together, already matched. |
| Resolution | choice: 1K / 2K / 4K / 8K | Which size of the maps to load. Lower costs less memory and is invisible at a distance; the highest is for surfaces the camera comes close to. |
| Mapping | choice: Stretch / Tile | How the maps are laid onto the surface. Triplanar projects from three directions and blends, which is what stops the stretching on a cliff face. |
| Tiles across | float, 1 to 64, default 8 | How many times the surface repeats. Photographed materials show their repeat if this is pushed too high. |

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
| Normalize weights | toggle, default on | Rescales the four channels so they sum to one at every point. Splat weights that do not sum to one either darken the surface or blow it out. |

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
| Mode | choice: Normal / Multiply / Add / Overlay / Screen / Height tint | How the two pictures are combined. |
| Opacity | float, 0 to 1, default 1 | How much of the second picture shows over the first. |

### TextureFile

Load an image texture (PNG/JPG/TGA/BMP) with mapping modes

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Image file | file path | The image file. Colour maps are read as sRGB and everything else as linear, so a normal or roughness map loaded here is not silently gamma-corrected. |
| Mapping | choice: Stretch / Tile / Tile offset | How the picture is laid onto the surface. Flat projection is fine on ground seen from above; triplanar projects from three directions and blends, which is what stops the stretching on a cliff face. |
| Tiles across | float, 1 to 64, default 8 | How many times the picture repeats across the surface. High counts show the repeat unless the picture was made seamless. |
| Brightness | float, 0.2 to 3, default 1 | Scales the picture after loading. |
| Gamma | float, 0.2 to 3, default 1 | Gamma correction for this picture, overriding the global setting. |
| Rotate | choice: 0 / 90 / 180 / 270 | Turns the picture on the surface. |
| Invert colors | toggle, default off | Flips the picture's values. On a height or roughness map this turns bumps into dents and gloss into matt. |
| Mirror X | toggle, default off | Mirrors alternate repeats across, which hides the seam of a picture that does not tile. |
| Mirror Y | toggle, default off | Mirrors alternate repeats down. |
| Picture scale | x/y pair | Multiplies the picture's values after loading. |
| Image offset | x/y pair | Added to the picture's values after loading. |
| Interpolation | choice: Linear / Nearest | How the picture is sampled between its pixels. Smooth is right for nearly everything; nearest keeps hard pixel edges, which is what an index or ID map needs. |

### TextureToMask

Texture luminance back into a mask/heightmap

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Channel | choice: Luminance / Red / Green / Blue / Alpha | Which channel of the picture becomes the mask. Luminance is the usual choice; the single channels are for pictures that were packed with a different mask in each one. |

### TextureTransform

Tile, scale, offset and rotate a texture

| Port | Direction | Type |
| :--- | :--- | :--- |
| texture | in | texture |
| texture | out | texture |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Tiles | x/y pair | How many times the picture repeats across the surface. |
| Offset | x/y pair | Slides the picture across the surface. |
| Rotation | float, -180 to 180, default 0 | Turns the picture on the surface. |
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
| Mode | choice: Mix / Add / Subtract / Multiply / Min / Max / Smooth min / Smooth max / Overlay / Screen / Difference | How the two inputs are combined. Add and multiply are the workhorses; max and min take whichever input is higher or lower at each point, which keeps both shapes rather than averaging them into mush. |
| Factor | float, 0 to 1, default 0.5 | The balance between the two inputs, where the mode uses one. |
| Smooth k | float, 0.01 to 0.5, default 0.1 | Rounds the seam where max or min switches from one input to the other. A hard switch leaves a crease that catches the light as a line; this is the difference between two terrains meeting and two terrains merging. |

### Math

Per-pixel math on one input

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Operation | choice: Multiply / Add / Power / Absolute / Negate / One minus / Square root / Log1p / Sine / Smoothstep | The arithmetic applied to every point. |
| Value | float, -4 to 4, default 1 | The constant the operation uses when nothing is wired to the second input. |
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
| Blend softness | float, 0 to 0.4, default 0.05 | Rounds the seam where one layer gives way to the next, so stacked terrains merge rather than meeting at a crease. |

## Path

### PathCarve

Carves along a drawn path - riverbeds, road cuts, canyons; negative depth builds walls

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| path | in (optional) | ? |
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
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### PathFind

Route a path across the terrain

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| cost | in (optional) | heightmap |
| path | out | ? |
| path_mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Start | x/y pair |  |
| End | x/y pair |  |
| Slope penalty | float, 0 to 400, default 40 | How much climbing costs against walking flat. High values contour around hills the way real roads do. |
| Keep every Nth point | int, 1 to 32, default 4 |  |

### PathFractalize

Midpoint-displace a path into a wander

| Port | Direction | Type |
| :--- | :--- | :--- |
| path | in | ? |
| path | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 8, default 4 |  |
| Amplitude | float, 0 to 1, default 0.4 |  |
| Seed | seed |  |

### PathResample

Even spacing along a path

| Port | Direction | Type |
| :--- | :--- | :--- |
| path | in | ? |
| path | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Spacing | float, 0.002 to 0.5, default 0.02 |  |
| Smoothing | int, 0 to 6, default 0 |  |

### PathSDF

Distance to a path

| Port | Direction | Type |
| :--- | :--- | :--- |
| path | in | ? |
| distance | out | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Reach | float, 0.005 to 1, default 0.1 |  |
| Invert | toggle, default off |  |
| Closed loop | toggle, default off |  |
| Smoothing | int, 0 to 6, default 0 |  |

### PathSpline

A smooth curve through the points

| Port | Direction | Type |
| :--- | :--- | :--- |
| path | in | ? |
| path | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Samples per segment | int, 2 to 64, default 8 |  |
| Tension | float, 0 to 1, default 0.5 |  |

### PointsToPath

Order a point cloud into a path

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| path | out | ? |

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
| Mask band | range | The range of values a point must carry to be kept. |
| Keep fraction | float, 0 to 1, default 1 | Whether points inside the band are the ones kept or the ones dropped. |
| Seed | seed |  |

### PointsFromCsv

Read points from a CSV file

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| File | file path | The CSV file to read points from. One row per point. |

### PointsInteract

Attract to, repel from another cloud; keep instances from overlapping

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| below | in (optional) | ? |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Affinity with layer below | float, -1 to 1, default 0 | Positive: instances gather around the instances of the layer below (primroses around the trees) and thin out elsewhere. Negative: everywhere except near them. |
| Affinity radius (m) | float, 0.1 to 2000, default 25 | How far this population reaches to gather around the one below it, in metres. |
| Repulsion from layer below | float, -1 to 1, default 0 | Sudden. Positive: a void around each instance below (no grass under the canopy). Negative: only inside that void (small stones at the foot of the boulder). Use both: near the trees but not under them. |
| Repulsion radius (m) | float, 0.1 to 2000, default 8 | How far this population is pushed back from the one below it, in metres - the bare ring around the base of a tree. |
| Avoid overlapping instances | toggle, default on | No two instances closer than their footprints allow. |
| Terrain size (m) | float, 1 to 1e+06, default 5000 |  |

### PointsMerge

Combine two point clouds

| Port | Direction | Type |
| :--- | :--- | :--- |
| points A | in | ? |
| points B | in (optional) | ? |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Drop B closer than | float, 0 to 0.2, default 0 | 0 keeps everything. Above 0, a B point this close to any A point is dropped - A has right of way. |

### PointsRelax

Even out point spacing

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Iterations | int, 1 to 50, default 8 | How many times the points push each other apart. More approaches an even spacing; a few passes take the worst clumps out and leave the scatter looking natural. |
| Strength | float, 0.01 to 1, default 0.5 | How hard each pass pushes. |

### PointsSDF

Distance to the nearest point

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| distance | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Reach | float, 0.005 to 1, default 0.2 | How far from a point its influence extends. |
| Invert | toggle, default off | Measures distance the other way, so the field is high near the points instead of far from them. |

### PointsSetValues

Point values from the terrain

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| source | in | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Normalize 0..1 | toggle, default on | Rescales the values written onto the points to 0..1. |

### PointsShuffle

Reorder a cloud deterministically

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |

### PointsToMask

Stamp points into a raster

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Kernel | choice: Gaussian / Cone / Disc | The shape stamped at each point. A soft falloff blends into a smooth mask; a hard disc keeps every point countable. |
| Radius | float, 0.001 to 0.5, default 0.03 | How large each stamp is. |
| Amplitude | float, 0 to 4, default 1 | How strong each stamp is. |
| Scale by point value | toggle, default off | Sizes each stamp by the value the point carries, so a cloud that already knows how big each thing is can say so. |
| Blend | choice: Max / Add | How overlapping stamps combine. Add piles them up, which counts density; max keeps them at one, which draws coverage. |

### PointsTransform

Species, size, rotation, lean and tint per instance

| Port | Direction | Type |
| :--- | :--- | :--- |
| points | in | ? |
| driver | in (optional) | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Species | int, 1 to 8, default 1 | How many kinds of object this layer places. Each scene object bound to the layer picks the species it stands for. |
| Species 1 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 1 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 2 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 2 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 3 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 3 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 4 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 4 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 5 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 5 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 6 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 6 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 7 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 7 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Species 8 presence | float, 0 to 1, default 1 | Relative to the other species: raising every presence places no more instances. |
| Species 8 scale | float, 0.05 to 10, default 1 | This species' size, as a multiple of the layer's own overall scaling. A population of one mesh at several sizes reads as a stand of different ages; every copy identical reads as instancing. |
| Overall scaling | float, 0.05 to 10, default 1 | The size of one copy, as a multiple of the mesh's own size. |
| Size variation | float, 0 to 1, default 0.3 | 1: instances range from half to twice the size. |
| Keep proportions | float, 0 to 1, default 1 | 1: the three axes scale together. 0: each on its own. |
| Direction from surface | float, 0 to 1, default 0 | 0: instances grow vertically whatever the slope. 1: perpendicular to the ground (rocks); trees want 0. |
| Rotation | choice: Up axis / None / Driven | How far a copy may be turned about its up axis. Full rotation is right for anything without a front; less keeps a set of objects aligned. |
| Maximum angle | float, 0 to 1, default 1 | As a fraction of a half turn either way. |
| Offset from surface (m) | float, -50 to 50, default 0 | Negative buries the instance. |
| Footprint radius (m) | float, 0.01 to 500, default 2 | The ground one instance claims at scale 1; what overlap avoidance and the layer above measure against. |
| Shrink at low density | float, -1 to 1, default 0 | Lone instances are smaller (negative: larger), as at the edge of a wood. |
| Low-density radius (m) | float, 0.1 to 2000, default 30 | How close to the population below a copy must be before it is made smaller, in metres. This is what puts stunted growth under a canopy rather than an abrupt edge. |
| Lean out at low density | float, 0 to 1, default 0 | Lone instances lean into the slope, as plants reaching for light. |
| Color variation | float, 0 to 1, default 0.3 | How much copies differ in brightness from one another. Identical tint across a whole population is the second clearest sign of instancing, after identical size. |
| Time offset range (s) | float, 0 to 10, default 1 | Each instance's wind phase is shifted by up to this, so a field sways as a crowd, not a marching army. |
| Terrain size (m) | float, 1 to 1e+06, default 5000 |  |

### ScatterArea

Scatter by density per hectare and the presence of the ground

| Port | Direction | Type |
| :--- | :--- | :--- |
| presence | in (optional) | heightmap |
| terrain | in (optional) | heightmap |
| objects | in (optional) | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Invert presence | toggle, default off |  |
| Density (per hectare) | float, 0.01 to 100000, default 20 | Instances per hectare (100 m x 100 m) at full presence. A rate, not a count: the same setting fills a 1 km tile and a 20 km one to the same look. |
| Minimum spacing (m) | float, 0.05 to 1000, default 5 | The lattice the candidates stand on. Changing the density never moves an instance; changing this reseeds them all. |
| Placement | choice: Jittered / Random / Regular | How the candidate positions are laid out before anything is rejected. A jittered lattice covers ground evenly; purely random leaves clumps and bald patches, which is sometimes what you want. |
| Clumping | float, 0 to 1, default 0 | Groups instances together as species do in nature. |
| Clump size (m) | float, 0.5 to 5000, default 60 | How far across one clump of plants is, in metres. Vegetation gathers where the ground suits it, and a population spread perfectly evenly is the clearest sign of a generated one. |
| Seed | seed |  |
| Populate around the camera | toggle, default off | Off: the population covers the terrain tile, computed once. On: it covers the ground wherever the camera goes - a planet or an infinite terrain has no tile to cover - generated in cells on demand and thrown away behind you. What a cell holds never depends on where it was seen from. |
| Populate within (m) | float, 10 to 200000, default 2000 | How far from the camera the ground is populated when 'Populate around the camera' is on. |
| Terrain size (m) | float, 1 to 1e+06, default 5000 | The tile's width; the studio keeps this in step with the project so the rate above means what it says. |
| Presence threshold | float, 0 to 1, default 0.05 | Presence below this places nothing at all. |
| Slope influence | float, 0 to 1, default 0.5 | 1: instances thin out on steep ground. 0: the same density whatever the slope. |
| By altitude | toggle, default off | Limits the layer to a band of heights. |
| Range of altitudes | choice: By terrain / Absolute / Relative to sea | Whether the altitude band is read against the terrain's own range, in absolute height units, or from sea level. |
| Altitude band | range | As a fraction of the terrain's own height range. |
| Sea level | float, 0 to 1, default 0 | The height that counts as sea level, when the altitude band is measured from it rather than from the terrain's own range. |
| Fade | float, 0 to 0.5, default 0.08 | How gradually the layer gives out at the edges of its altitude band. Zero draws a contour line across the hillside. |
| By slope | toggle, default off | Limits the layer to a band of steepness. |
| Slope band | range | Degrees from horizontal. 0 is flat, 90 is a cliff. |
| Fade | float, 0 to 45, default 6 | How gradually the layer gives out at the edges of its slope band. |
| By orientation | toggle, default off | Limits the layer to slopes facing a particular way. |
| Faces | float, 0 to 360, default 0 | Compass direction the surface looks towards. 0 is north. |
| Arc | float, 5 to 180, default 60 | How wide an arc of facings counts as the favoured direction. Narrow puts moss on the north face alone; wide covers most of the hill. |
| Fade | float, 0 to 90, default 20 | How gradually the layer gives out as a slope turns away from the favoured direction. |
| Terrain height scale | float, 0.001 to 100, default 1 | World height of a heightmap unit as a fraction of the tile's width; the studio keeps this in step with the project so a slope in degrees is the slope the viewport shows. |
| Decay near objects | float, 0 to 1, default 0 | Thins the population around the objects standing on the terrain (the 'objects' input: 0 at an object, 1 far away). 1 leaves a void right at them. |
| Reach | float, 0.001 to 0.5, default 0.05 | How far from the objects the decay extends, as a fraction of the terrain. |
| Falloff | float, -1 to 1, default 0 | 0 linear. Positive: the void is larger and more sudden. Negative: gentler. |

### ScatterPoints

Scatter points over the tile

| Port | Direction | Type |
| :--- | :--- | :--- |
| density | in (optional) | heightmap |
| points | out | ? |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Point count | int, 1 to 50000, default 500 | How many points are scattered. |
| Mode | choice: Random / Jittered grid / Spaced | How they are laid out. Purely random leaves clumps and bald patches; a jittered grid covers the ground evenly while still looking unplanned; spaced refuses to place any two closer than the minimum below. |
| Min spacing | float, 0.001 to 0.3, default 0.02 | The closest two points may come, for the spaced mode. This is what turns scatter into a distribution. |
| Seed | seed |  |
| Value distribution | choice: Uniform / Power law / Weibull | The per-point value stream: uniform 0..1, a power law (many small, few large - natural size mixes), or Weibull (clustered around a typical size). |
| Distribution shape | float, 0.5 to 8, default 2 | How the spacing is enforced - a hard exclusion around each point, or a softer falling-off preference. |

## Primitive

### BasaltField

Columnar basalt: hexagonal steps and cracks

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |
| cracks | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Column scale | float, 2 to 64, default 12 | How many columns fit across the tile. Real columnar basalt is tens of centimetres across, so on a large terrain this wants to be high. |
| Height steps | int, 2 to 24, default 6 | Each column's flat top snaps to one of this many levels, the way cooling lava fractures in tiers. |
| Crack width | float, 0.01 to 0.4, default 0.06 | How wide the joint between neighbouring columns is, as a fraction of a column. Also drives the second output, which is the crack pattern alone. |
| Crack depth | float, 0 to 1, default 0.25 | How far the joints cut down between the columns. 0 leaves the tops without separating them. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Constant

Constant level

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Value | float, -1 to 2, default 0.5 | The height every point gets. A flat map is the starting point for building terrain out of displacement alone, and the quickest way to see what a material or a field node is doing with nothing underneath it. |

### Crater

Impact craters: bowl, rim lip, ejecta blanket (single or field)

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in (optional) | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Profile | choice: Single crater / Crater field | One crater placed where you say, or a scattered field of them at varying sizes - a cratered plain rather than an impact site. |
| Scale | float, 0.02 to 1, default 0.3 | How wide the crater is, as a fraction of the tile. In field mode this is the largest; the rest vary below it. |
| Depth | float, 0.05 to 1, default 0.4 | How far the floor sits below the surrounding ground. |
| Rim lip | float, 0 to 1, default 0.5 | Sharpness/height of the raised rim wall. |
| Ejecta extent | float, 0.1 to 2, default 0.6 | How far the thrown-out debris blanket reaches past the rim, as a multiple of the radius. It is the apron of raised ground that makes an impact read as an impact rather than a hole. |
| Floor level | float, 0 to 1, default 0.15 | Clamps the bowl bottom — flat crater floors. |
| Rim irregularity | float, 0 to 1, default 0.3 | How far the rim departs from a circle. 0 is a drawing-compass ring, which no impact leaves. |
| Position | x/y pair | Where the single crater sits, as a fraction of the tile. Ignored in field mode. |
| Field count | int, 2 to 64, default 12 | How many craters the field scatters. Field mode only. |
| Seed | seed |  |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### DiffusionLimited

Branching dendrites by particle aggregation

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Particles | int, 100 to 8000, default 1500 | How many particles are released to wander until they touch the growing cluster. More builds a larger, denser dendrite and costs proportionally more. |
| Stickiness | float, 0.1 to 1, default 1 | 1 sticks on first contact - wispy branches. Lower values let particles slide deeper before settling, thickening the arms. |
| Smoothing | float, 0 to 0.05, default 0.008 | Blurs the aggregate, turning a one-texel-wide skeleton into something with width that can be used as terrain. 0 leaves the bare structure. |

### Dunes

Sand dunes: asymmetric slip faces, crest chaos, ripples

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Wind direction ° | float, -180 to 180, default 30 | Which way the wind blows. Dunes run across it, and their steep slip face is on the downwind side - which is what tells a viewer which way the wind was going. |
| Dune wavelength | float, 0.02 to 0.5, default 0.12 | The distance from one crest to the next, as a fraction of the tile. |
| Asymmetry | float, 0.5 to 0.95, default 0.75 | Windward slope is long and gentle; the slip face is short and steep (real dunes ~0.8). |
| Crest chaos | float, 0 to 1, default 0.5 | How much the crests wander and break up along their length. 0 gives parallel corduroy; high gives the broken crescents of a real dune field. |
| Ripples | float, 0 to 1, default 0.25 | Secondary small-scale ripple field on top. |
| Ripple scale | float, 2 to 20, default 6 | How many small wind ripples ride across each dune. These are the centimetre-scale corrugations on the sand, not the dunes themselves. |
| Seed | seed |  |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### FakeStones

Terragen-style fake stones: boulders/rocks as displacement

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| density_mask | in (optional) | heightmap |
| output | out | heightmap |
| stone_mask | out | heightmap |
| displacement | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Stone scale | float, 0.004 to 0.25, default 0.03 | Stone size as a fraction of terrain width; smaller = more, denser stones. |
| Stone density | float, 0.02 to 1, default 0.5 | The share of lattice cells that hold a stone. This node writes into the heightmap, so its smallest possible stone is a couple of texels - about 14 m on a 5 km tile. For stones you can stand next to, use Stone field, which is a function and has no resolution. |
| Stone tallness | float, 0.05 to 2, default 0.6 | A stone's height as a fraction of its radius. Every stone here gets the same one, which is this node's most visible tell. |
| Pancake effect | float, 0 to 1, default 0.3 | Squashes stones flat into slabs while keeping their footprint — 0 round boulders, 1 flat plates. |
| Seed | seed |  |
| Vary density | float, 0 to 1, default 0.6 | Large-scale patchiness: clusters of stones with clear ground between. |
| Density variation scale | float, 1 to 16, default 4 | How large the patches of more and fewer stones are. Low gives a couple of broad drifts across the map; high breaks it into many small clusters. |
| Size variation | float, 0 to 1, default 0.5 | How much stones differ in size from one another. 0 makes every stone identical, which nothing in nature is. |
| Grow on slopes | range | Stones appear only where terrain slope is inside this band (rockfall collects on gentler ground). |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Fractal

Non-noise fractals: diamond-square, fault lines

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Diamond-square / Fault formation | Two classic terrain algorithms that are not noise. Diamond-square subdivides a grid, halving the randomness each time; Fault formation drops straight faults across the map and raises one side of each, which builds up into blocky, tectonic ground. |
| Seed | seed |  |
| Roughness | float, 0.3 to 1.6, default 0.9 | Diamond-square only: how much randomness survives each subdivision. Below 1 the detail dies away and the result is smooth hills; above 1 it grows and the surface turns jagged. |
| Fault count | int, 10 to 2000, default 200 | Fault formation only: how many faults are laid down. Few gives a handful of broad steps; many averages into smooth rolling ground. |
| Fault softness | float, 0 to 0.2, default 0.02 | How gradually each fault's step is spread across the map. 0 gives hard cliffs at every fault line. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### GaborNoise

Oriented sparse-kernel noise (streaked rock)

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Octaves | int, 1 to 6, default 3 | How many sizes of kernel are layered. Gabor noise is expensive per octave, so this stops at 6 where the other noises go to 16. |
| Kernel frequency | float, 0.5 to 16, default 3 | How many waves are packed inside each kernel. This is the pitch of the grain, as against Scale below, which is how large a patch of it is. |
| Orientation ° | float, -180 to 180, default 30 | Which way the grain runs, when Anisotropy is high enough for it to have a direction at all. |
| Anisotropy | float, 0 to 1, default 0.85 | 1 locks every kernel to the orientation - streaks. 0 draws orientations at random - isotropic grain. |
| Scale | float, 1 to 32, default 6 | How many kernel cells fit across the tile - the size of the pattern, as against Kernel frequency, which is the pitch of the grain inside it. |
| Flavor | choice: Gabor (amplitude) / Phasor sawtooth / Phasor sine / Phasor square | Phasor keeps only the phase of the kernel field, so the wave profile stays crisp everywhere - sawtooth reads as bedding planes, square as strata steps. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
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
| Layers | int, 2 to 32, default 8 | How many beds the height range is cut into. Few gives the broad benches of a canyon wall; many gives fine bedding. |
| Layer hardness | float, 1 to 8, default 2.5 | How sharply each bed stands out from the next. Low leaves the original slope showing through; high makes every bed a flat tread with a riser between. |
| Seed | seed |  |
| Thickness variation | float, 0 to 1, default 0.5 | How much the beds differ in thickness. 0 gives evenly spaced layers, which no real rock has. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### GrassDisplacement

Grass as displacement: a field of tufts in clumps, on ground flat enough to hold it

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| displacement | out | heightmap |
| grass_mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Tuft size | float, 0.0005 to 0.05, default 0.004 | The size of one tuft as a fraction of the terrain's width. Smaller is denser grass. |
| Height | float, 0 to 0.05, default 0.004 | How tall the tufts stand, as a fraction of the terrain's own height range. |
| Density | float, 0 to 1, default 0.75 | How much of the ground the clumps cover. |
| Clumping | float, 0 to 1, default 0.5 | 0 an even lawn, 1 tufts gathered into patches with bare ground between. |
| Clump size | float, 1 to 64, default 12 | Size of the patches, in tufts. |
| Raggedness | float, 0 to 1, default 0.5 | How uneven the tops of the tufts are. |
| Seed | seed |  |
| Grows on slopes | range | Grass takes only ground whose slope is inside this band (0 flat .. 1 vertical). |
| Slope fade | float, 0 to 0.5, default 0.1 | How gradually the grass gives out at the edge of the slope band. Zero gives a hard line across the hillside, which reads as drawn on. |

### HeightmapFile

Import a heightfield: 8/16-bit PNG, JPG, TGA, or SRTM .hgt real-world DEM

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Heightfield image | file path | An 8- or 16-bit PNG, a JPG or TGA, or an SRTM .hgt tile of real-world elevation. 16-bit carries far more height detail than 8-bit, which visibly terraces on a smooth slope. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Landform

Geological set pieces: island, mountain, caldera, rift, mesa

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Island / Mountain / Caldera / Rift valley / Mesa | A whole landform in one node, for when you want a particular thing in a particular place rather than whatever the noise happens to give. The rim is wobbled by noise, so none of them reads as a compass drawing. |
| Center | x/y pair | Where it sits, as a fraction of the tile. Outside 0..1 pushes it off the edge, so you get a coast or a flank rather than the whole thing. |
| Radius | float, 0.05 to 1, default 0.35 | How far it reaches, as a fraction of the tile. |
| Relief | float, 0 to 1, default 0.5 | How broken the form is. 0 is a clean geometric shape; higher adds ridged detail to the flanks and wobbles the outline further. |
| Direction ° | float, -180 to 180, default 0 | Which way the form points. The rift valley and the mesa use it; the round ones ignore it. |
| Seed | seed |  |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### LineNoise

Cellular noise seeded by line segments

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |
| cracks | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Line count | int, 4 to 400, default 40 | How many line segments seed the pattern. Cellular noise grown from lines rather than points gives elongated, fractured shapes - bedding planes and shattered rock rather than blobs. |
| Segment length | float, 0.02 to 0.6, default 0.18 | How long each seed segment is, as a fraction of the tile. Longer segments give longer, straighter features. |
| Reach | float, 0.01 to 0.5, default 0.08 | How far the influence of a segment extends from it, which sets how wide the resulting bands are. |
| Direction ° | float, -180 to 180, default 0 | The direction segments align to, before the jitter below scatters them. |
| Direction jitter | float, 0 to 1, default 1 | 0 aligns every segment to the direction - bedding planes. 1 scatters them freely - shattered rock. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Noise

Coherent noise: fBm, ridged, billow, swiss, value, cellular

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Perlin fBm / Ridged / Billow / Swiss (eroded ridges) / Value fBm / Worley F1 / Worley F2 / Worley edges / Worley F1*F2 / IQ (damped slopes) / Jordan (crumpled) / Pingpong (banded) / Voronoise (cell blend) | Which noise the octaves are built from. Perlin fBm is rolling ground; Ridged gives sharp crests and is the usual starting point for mountains; Billow the rounded lumps of weathered rock; Swiss adds an erosion-like warp to the ridges. The Worley family is cellular - F1 for blobs, edges for a crack network. |
| Seed | seed |  |
| Octaves | int, 1 to 16, default 9 | How many times the pattern is added at a smaller size. Each adds finer detail and costs about as much again; once an octave is finer than a texel it buys nothing. |
| Lacunarity | float, 1.2 to 4, default 2 | How much smaller each octave is than the last. 2 halves it, which is the usual choice; higher leaves a gap between the scales and reads as two separate patterns rather than one surface. |
| Gain | float, 0.05 to 0.95, default 0.5 | How much of its predecessor's strength each octave keeps. Low is smooth and dominated by the largest forms; high is uniformly rough at every scale. |
| Ridge weight | float, 0 to 1, default 0.7 | Ridged and Swiss only: how strongly a high octave is suppressed where the one below it was already high. This is what keeps detail in the valleys and off the crests, the way real erosion does. |
| Swiss warp | float, 0 to 0.6, default 0.15 | Swiss only: how far each octave is pushed sideways by the slope of the one below, which bends the ridges into something that looks worn rather than generated. |
| Cell jitter | float, 0 to 1, default 1 | Worley only: how far each cell's point strays from the middle of its square. 0 is a visible grid; 1 is scattered. |
| Wavenumber | x/y pair | How many times the pattern repeats across the tile, across and down. Higher is smaller features; the two differing stretches the pattern one way. |
| Offset | x/y pair | Slides the pattern under the terrain. Use it to move a feature off a spot rather than reaching for a new seed, which would change everything at once. |
| Rotation ° | float, -180 to 180, default 0 | Turns the pattern. Anything with a grain - strata, dunes, waves - reads very differently across the slope than along it. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### NoiseFractal

Vue-class fractal: base noise over harmonics with stretch, combination modes, variable roughness, distortion and a filter profile; second output is the local roughness

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| distortion map | in (optional) | heightmap |
| output | out | heightmap |
| rough_areas | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Base noise | choice: Perlin / Value / Cellular (F1) / Cell edges / Grainy | The pattern repeated at every harmonic. Grainy keeps detail at all frequencies (colour and bump work). |
| With rotation | toggle, default on | Rotate the noise between harmonics, so the lattice's directions never line up. |
| Double noise | toggle, default off | A second, offset noise multiplied in: richer variation, about twice the cost. |
| Filter steepness | float, 0.2 to 4, default 1 | Contrast of the base noise itself. |
| Seed | seed |  |
| Wavelength | float, 0.005 to 4, default 0.25 | Size of the largest feature, as a fraction of the tile. |
| Stretch X / Y | x/y pair | Pulls the pattern out along one axis. Unequal values give the grain that ridges running one way, or wind- blown ground, actually has. |
| Stretch damping | float, 0 to 1, default 0.5 | Less stretch on the finer harmonics, so the whole pattern does not read as smeared. |
| Iterations | int, 1 to 16, default 8 | How many times the pattern is added to itself at a smaller size. Each one adds finer detail and costs about as much again; past the point where an octave is smaller than a texel it adds cost and nothing else. |
| Scale ratio | float, 0.1 to 0.9, default 0.5 | Wavelength ratio between iterations. 0.5 is classic; above favours the large forms, below the fine detail. |
| Amplitude ratio | float, 0.05 to 0.95, default 0.5 | Amplitude ratio between iterations. |
| Roughness | float, 0 to 2, default 1 | Scales the amplitude ratio: more roughness, more detail. |
| Gain | float, 0.2 to 10, default 1 | Contrast of the result. |
| Combination mode | choice: Add / Blend / Variable roughness / Variable roughness (abs) / Max / Max (abs) / Min / Min (abs) / Multiply | How the iterations are put together (manual p866-870). |
| Smooth level | float, -1 to 1, default 0 | Altitude of least roughness; roughness grows with the distance from it (Variable Roughness Fractal). |
| Influence | float, 0 to 1, default 0 | 0 behaves exactly like a simple fractal. |
| Local influence | float, 0 to 1, default 0 | 0: keyed on the first iteration's altitude. 1: on the last iteration's, giving local patches of smoothness. |
| Variation strength | float, 0 to 1, default 0 | Grainy fractal: how much the grain varies over the map. |
| Variation roughness | float, 0.05 to 2, default 0.5 | How rough the rough areas get, once Influence has decided where they are. Does nothing while Influence is zero. |
| Smooth area altitude | float, -1 to 1, default 0 | The height the smooth areas settle to. Below the Smooth level this lifts them, above it lowers them - which is how a flat valley floor sits lower than the broken ground around it. |
| Distortion | float, 0 to 1, default 0 | Smears the pattern around, as if pushed by a random flow. |
| Distortion scale | float, 0.1 to 8, default 1 | How large the smearing flow is. Low values push the whole pattern about in broad sweeps; high values ripple its edges without moving the big forms. |
| Distortion map strength | float, 0 to 1, default 0 | The 'distortion map' input, when wired, warps the coordinates by this much. |
| Filter | choice: None / Terraces / Soft clip / S-curve / Plateau / Valleys | A profile applied to the altitudes (Vue's filter curve). |
| Terrace steps | float, 2 to 40, default 6 | How many levels the Terraces filter snaps altitudes to. Only the Terraces profile uses it. |
| Creep-in | float, 0 to 1, default 0 | How much of the unfiltered signal is mixed back. |
| Filter range | range | The part of the full range the filter acts on. |
| Amplitude | float, 0 to 4, default 1 | Scales the whole result. Use it to make one fractal a quieter contribution when several are added together. |
| Offset | float, -1 to 1, default 0 | Raises or lowers the whole result. Applied after the amplitude, before the output block below. |
| Rough areas: ref. feature size | float, 0 to 1, default 0 | Harmonics finer than this (fraction of the tile) count as roughness. 0 counts them all. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### RockyMountains

Vue's Rocky Mountains fractal: irregular ridge networks added per iteration, as separate mountains or basins between ridges, stretched, with optional rocks and an eroded variant

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| output | out | heightmap |
| rough_areas | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Wavelength | float, 0.01 to 4, default 0.35 | The size of the largest feature, as a fraction of the tile. 1 is one landform filling the map; 0.1 is ten across it. |
| Iterations | int, 1 to 16, default 8 | How many times the pattern is added at a smaller size. Each adds finer detail and costs about as much again; once an octave is finer than a texel it buys nothing. |
| Scale ratio | float, 0.1 to 0.9, default 0.5 | How much smaller each iteration is than the last. 0.5 halves it, which is the classic choice; higher leaves a gap between the scales and reads as two patterns rather than one surface. |
| Roughness | float, 0 to 2, default 1 | How much strength each iteration keeps. Low is smooth and dominated by the big forms; high is broken at every scale. |
| Gain | float, 0.2 to 10, default 1 | Contrast of the result: pushes the highs up and the lows down about the middle. |
| Distortion | float, 0 to 1, default 0 | Smears the pattern around as if pushed by a slow flow, which breaks up the lattice the noise sits on. |
| Separate mountains | toggle, default on | On: independent mountain blocks side by side. Off: basins separated by irregular ridges. |
| Scale factor | float, 0.3 to 0.9, default 0.55 | How much smaller each new iteration's features are. |
| Flat level (per iteration) | float, 0 to 1, default 0.3 | Balance of smooth areas against ridged ones per iteration. |
| Ground level | float, -1 to 1, default 0 | Sinks the fractal into the ground. |
| Subdivision quality | int, 0 to 2, default 1 | Higher hides the approximation's discontinuities, at a cost. |
| Stretch factor | float, 0 to 1, default 0.5 | Each iteration is stretched along its own direction, the way real ridge networks run. |
| Optional rocks | choice: None / Correlated / Everywhere | Adds broken rock on top of the range. Correlated puts it along the ridges, where erosion actually strips a mountain back to stone; Everywhere ignores the form and covers the lot. |
| Rock correlation | int, 0 to 8, default 2 | Rocks follow the ridges seen at this iteration. |
| Rock roughness | float, 0 to 2, default 1 | How broken the added rock is, independently of the range underneath it. |
| Rock height | float, 0 to 1, default 0.3 | How far the rock stands proud of the slope it sits on. |
| Eroded | toggle, default off | The Eroded Rocky Mountains variant: gullied flanks. |
| Rough areas: ref. feature size | float, 0 to 1, default 0 | Iterations finer than this, as a fraction of the tile, count as roughness in the second output. 0 counts them all. That output is what drives a material toward the broken ground. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### Shape

Geometric base shapes: slope, bump, crater, cone, ridge line

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Type | choice: Slope plane / Bump / Crater / Cone / Ridge line / Border falloff / Wave sine / Wave square / Wave triangle / Step / Band / Paraboloid | The base form. These are building blocks rather than terrain: a slope plane to tilt a map, a bump or cone to raise one hill, a border falloff to turn any terrain into an island, a wave to drive strata or dunes. |
| Center | x/y pair | Where the shape sits, as a fraction of the tile. Outside 0..1 pushes it off the edge, which is how you get a slope running out of frame rather than a hill in the middle. |
| Radius | float, 0.01 to 1.5, default 0.35 | How far the shape reaches from its centre, as a fraction of the tile. For Border falloff this is the width of the fade instead. |
| Hardness | float, 0.2 to 8, default 1 | The profile from the middle out. Below 1 gives a broad dome that falls away late; above 1 a narrow peak with skirts. |
| Direction ° | float, -180 to 180, default 0 | Which way the shape faces. Used by the slope plane, the ridge line and the waves; the round shapes ignore it. |
| Frequency | float, 0.25 to 64, default 4 | Waves only: how many repeats across the tile. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
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
| Shape image (if no input) | file path | The shape to stamp, when nothing is wired to the shape input. The input wins if both are given. |
| Position | x/y pair | Where the stamp lands, as a fraction of the tile. |
| Size | float, 0.02 to 2, default 0.5 | How much of the tile the stamp covers. |
| Rotation ° | float, -180 to 180, default 0 | Turns the stamp before it is applied. |
| Height | float, -2 to 2, default 0.5 | How far the stamp raises the ground. Negative presses it in instead, which is how the same shape carves a pit or raises a hill. |
| Blend | choice: Add / Max (merge) / Min (carve) / Replace by mask | How the stamp meets what is already there. Add sums them, so stamps pile up. Max merges - the stamp shows only where it is higher, which is what you want for laying a hill onto terrain. Min carves. Replace overwrites wherever the stamp is present. |
| Edge falloff | float, 0 to 0.5, default 0.15 | How far in from the stamp's border it fades out, so it blends into the terrain rather than ending at a visible square edge. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### TerrainFractal

Vue's Terrain Fractal: the fractal with a landscape type (plain, ridges, billows, mixes), ridge smoothness and bump surge; rough areas on the second output

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| distortion map | in (optional) | heightmap |
| output | out | heightmap |
| rough_areas | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Base noise | choice: Perlin / Value / Cellular (F1) / Cell edges / Grainy | The pattern repeated at every harmonic. Grainy keeps detail at all frequencies (colour and bump work). |
| With rotation | toggle, default on | Rotate the noise between harmonics, so the lattice's directions never line up. |
| Double noise | toggle, default off | A second, offset noise multiplied in: richer variation, about twice the cost. |
| Filter steepness | float, 0.2 to 4, default 1 | Contrast of the base noise itself. |
| Seed | seed |  |
| Wavelength | float, 0.005 to 4, default 0.25 | Size of the largest feature, as a fraction of the tile. |
| Stretch X / Y | x/y pair | Pulls the pattern out along one axis. Unequal values give the grain that ridges running one way, or wind- blown ground, actually has. |
| Stretch damping | float, 0 to 1, default 0.5 | Less stretch on the finer harmonics, so the whole pattern does not read as smeared. |
| Iterations | int, 1 to 16, default 8 | How many times the pattern is added to itself at a smaller size. Each one adds finer detail and costs about as much again; past the point where an octave is smaller than a texel it adds cost and nothing else. |
| Scale ratio | float, 0.1 to 0.9, default 0.5 | Wavelength ratio between iterations. 0.5 is classic; above favours the large forms, below the fine detail. |
| Amplitude ratio | float, 0.05 to 0.95, default 0.5 | Amplitude ratio between iterations. |
| Roughness | float, 0 to 2, default 1 | Scales the amplitude ratio: more roughness, more detail. |
| Gain | float, 0.2 to 10, default 1 | Contrast of the result. |
| Combination mode | choice: Add / Blend / Variable roughness / Variable roughness (abs) / Max / Max (abs) / Min / Min (abs) / Multiply | How the iterations are put together (manual p866-870). |
| Smooth level | float, -1 to 1, default 0 | Altitude of least roughness; roughness grows with the distance from it (Variable Roughness Fractal). |
| Influence | float, 0 to 1, default 0 | 0 behaves exactly like a simple fractal. |
| Local influence | float, 0 to 1, default 0 | 0: keyed on the first iteration's altitude. 1: on the last iteration's, giving local patches of smoothness. |
| Variation strength | float, 0 to 1, default 0 | Grainy fractal: how much the grain varies over the map. |
| Variation roughness | float, 0.05 to 2, default 0.5 | How rough the rough areas get, once Influence has decided where they are. Does nothing while Influence is zero. |
| Smooth area altitude | float, -1 to 1, default 0 | The height the smooth areas settle to. Below the Smooth level this lifts them, above it lowers them - which is how a flat valley floor sits lower than the broken ground around it. |
| Noise / landscape type | choice: Plain noise / Ridges / Billows / Ridge mix / Billow-ridge mix | The shape the harmonics take. Plain noise is rolling ground; Ridges gives the sharp crests of a young range; Billows the rounded lumps of a weathered one. The mixes blend two, weighted by Blend below. |
| Blend | float, 0 to 1, default 0.5 | Mixed types only: weight of the second shape. |
| Ridge smoothness | float, 0 to 1, default 0.2 | Rounding of the ridges / billows; not for plain noise. |
| Bump surge | float, -1 to 1, default 0 | Bumpy areas rise above (+) or sink below (-) the average. |
| Distortion | float, 0 to 1, default 0 | Smears the pattern around, as if pushed by a random flow. |
| Distortion scale | float, 0.1 to 8, default 1 | How large the smearing flow is. Low values push the whole pattern about in broad sweeps; high values ripple its edges without moving the big forms. |
| Distortion map strength | float, 0 to 1, default 0 | The 'distortion map' input, when wired, warps the coordinates by this much. |
| Filter | choice: None / Terraces / Soft clip / S-curve / Plateau / Valleys | A profile applied to the altitudes (Vue's filter curve). |
| Terrace steps | float, 2 to 40, default 6 | How many levels the Terraces filter snaps altitudes to. Only the Terraces profile uses it. |
| Creep-in | float, 0 to 1, default 0 | How much of the unfiltered signal is mixed back. |
| Filter range | range | The part of the full range the filter acts on. |
| Amplitude | float, 0 to 4, default 1 | Scales the whole result. Use it to make one fractal a quieter contribution when several are added together. |
| Offset | float, -1 to 1, default 0 | Raises or lowers the whole result. Applied after the amplitude, before the output block below. |
| Rough areas: ref. feature size | float, 0 to 1, default 0 | Harmonics finer than this (fraction of the tile) count as roughness. 0 counts them all. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### TerrainFractal2

Vue's Terrain Fractal 2: rocks emerging from sedimentary soil, with regions of rock density, soil thickness, buoyancy and relief-following strata

| Port | Direction | Type |
| :--- | :--- | :--- |
| envelope | in (optional) | heightmap |
| output | out | heightmap |
| rough_areas | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Wavelength | float, 0.01 to 4, default 0.35 | The size of the largest feature, as a fraction of the tile. 1 is one landform filling the map; 0.1 is ten across it. |
| Iterations | int, 1 to 16, default 8 | How many times the pattern is added at a smaller size. Each adds finer detail and costs about as much again; once an octave is finer than a texel it buys nothing. |
| Scale ratio | float, 0.1 to 0.9, default 0.5 | How much smaller each iteration is than the last. 0.5 halves it, which is the classic choice; higher leaves a gap between the scales and reads as two patterns rather than one surface. |
| Roughness | float, 0 to 2, default 1 | How much strength each iteration keeps. Low is smooth and dominated by the big forms; high is broken at every scale. |
| Gain | float, 0.2 to 10, default 1 | Contrast of the result: pushes the highs up and the lows down about the middle. |
| Distortion | float, 0 to 1, default 0 | Smears the pattern around as if pushed by a slow flow, which breaks up the lattice the noise sits on. |
| Turbulence | float, 0 to 1, default 0.3 | Overall distortion of the terrain by its first harmonics. |
| Turbulence damping | float, 0 to 1, default 0.5 | How much the first octaves' turbulence carries into the finer ones. |
| Large scale smoothness | float, 0 to 1, default 0.5 | Softness of the transition from low to high rock density regions. |
| Large scale contrast | float, 0 to 3, default 1 | Range over which the rock population can vary. |
| Buoyancy | float, -1 to 1, default 0.2 | +: low average altitude with rocks rising above it. -: features dig below a high average. 0: around zero. |
| Bump surge | float, 0 to 2, default 0.5 | How much the rocks spring out of the ground. |
| Rock abundance | float, 0 to 1, default 0.5 | How much bare rock emerges through the soil. This node models ground as rock under sediment, and this is the balance between them. |
| Soil thickness | float, 0 to 1, default 0.4 | Thin: more rocks show and smooth areas keep some roughness. Thick: rocks buried, smooth areas smooth. |
| Rock dispersion | float, 0 to 1, default 0.3 | Scattered over the landscape rather than gathered. |
| Processing strength | float, 0 to 1, default 0 | How strongly bedding shows in the surface. 0 turns the strata off entirely; the beds follow the relief rather than lying flat, so they bend over the landforms the way tilted sediment does. |
| Layer spacing | float, 0.01 to 0.5, default 0.08 | How far apart the beds are, as a fraction of the height range. Small gives fine banding; large gives the broad benches of a canyon wall. |
| Offset | float, -0.5 to 0.5, default 0 | Slides the whole stack of beds up or down, which moves where a bed boundary falls on a given slope. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### WaveletNoise

Band-limited noise that stays crisp

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Octaves | int, 1 to 12, default 6 | How many bands are summed. Wavelet noise is band-limited - each octave occupies a clean slice of frequency - so it stays crisp when magnified rather than turning to mush. |
| Scale | float, 1 to 64, default 8 | How many cells of the coarsest band fit across the tile. Higher is a finer pattern. |
| Gain | float, 0.1 to 0.95, default 0.55 | How much strength each band keeps from the one before. Low leaves the coarse bands dominant; high gives equal detail at every scale. |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

### WhiteNoise

Raw per-cell white noise

| Port | Direction | Type |
| :--- | :--- | :--- |
| output | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Remap to range | toggle, default on | Rescales the result so its lowest point sits at the bottom of the range below and its highest at the top. Off keeps the raw values, which is what you want when a node feeds arithmetic rather than a picture. |
| Output range | range | The low and high the result is rescaled into. 0..1 is the terrain's own range; a narrower band makes this node a gentler contribution when it is added to another. |
| Invert | toggle, default off | Turns the result upside down within its range - peaks become hollows. Applied after the remap. |
| Gain (gamma) | float, 0.05 to 4, default 1 | Bends the result toward its low or high end. Below 1 lifts the middle, so more of the map sits high; above 1 pushes it down, so peaks become sparser and sharper. |
| Zero edges width | float, 0 to 0.5, default 0 | Fades the terrain to zero at the borders over this fraction of the map — clean edges for islands/tiles. |

## Render

### PostProcess

Image finishing after tone mapping: exposure, saturation, colour tint

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Exposure multiplier | float, 0.1 to 10, default 1 | How much light the picture is given, in stops. |
| Saturation | float, 0 to 2, default 1 | How strong the colour is in the finished frame. |
| Tint | color | A colour cast over the whole frame - the grade, not the lighting. |
| Vignette | float, 0 to 1, default 0 | Recorded for the offline post pass; the viewport ignores it for now (roadmap P6 post-render options). |

### RenderBackdrop

An HDR image dome at infinity behind the scene, hazed and clouded by the atmosphere

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Enabled | toggle, default on | Turns the backdrop off without losing its settings. |
| HDR image (.hdr / .exr / .png / .jpg) | file path | The panoramic image behind the scene. It lights nothing - this is what the camera sees past the terrain, not an environment light. |
| Mapping | choice: Equirectangular (lat-long) / Angular map (light probe) / Mirror ball / Cube map cross / Cylindrical panorama / Sky dome (hemisphere) / Planar backdrop | How pixels map onto directions. Lat-long is what HDRI libraries ship; a cross is detected as horizontal or vertical by its aspect; cylindrical and planar use the vertical field of view below. |
| Vertical field of view ° | float, 5 to 179, default 90 | Cylindrical panorama and planar backdrop only. |
| Mirror horizontally | toggle, default off | Mirrors the panorama, for when it was shot or stored the other way round. |
| Rotate ° | float, -180 to 180, default 0 | Turns the backdrop about the vertical, to put its interesting part behind the shot. |
| Tilt ° | float, -90 to 90, default 0 | Tilts the backdrop, to raise or drop its horizon against the terrain's. |
| Exposure (EV) | float, -10 to 10, default 0 | How bright the backdrop is, in stops. Matching it to the scene's own exposure is what stops the join being visible. |
| Tint | color | A colour cast over the backdrop alone, for matching it to the scene's light. |
| Blend over the sky | float, 0 to 1, default 1 | 1 replaces the procedural sky with the image; lower values mix. Where a mapping has no pixel (below a sky dome, outside a planar backdrop) the procedural sky shows through. |
| Atmosphere on the dome | float, 0 to 1, default 1 | How much horizon haze and fog the dome receives, as if at infinite distance. Clouds always draw in front of it. |
| Hide the sun disc | toggle, default on | An HDRI usually contains its own sun. |

### RenderCamera

Camera and tone mapping for the render

| Port | Direction | Type |
| :--- | :--- | :--- |
| camera | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Exposure | float, 0.3 to 3, default 1.1 | How much light the picture is given, in stops. |
| Terrain height scale | float, 0.02 to 0.8, default 0.22 | The terrain's vertical scale, so the render matches what the viewport shows. |
| Terrain size (m) | float, 100 to 100000, default 5000 | How wide the tile is in metres, so anything sized in real units renders at the right size. |

### RenderLayers

[Planned] Objects and lights sorted into render layers for compositing

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### RenderOutput

Master output: file, format, size, engine and samples of the render

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Output file | file path | Where the finished image is written. |
| Beauty format | choice: PNG 8-bit (tone mapped) / EXR float (linear) / HDR Radiance (linear) | Passes are always written as linear float EXR beside the beauty, whatever this is. |
| Width | int, 64 to 8192, default 1920 | The width of the rendered image, in pixels. |
| Height | int, 64 to 8192, default 1080 | The height of the rendered image, in pixels. |
| Engine | choice: Mitsuba 3 / Blender Cycles / LuxCoreRender / appleseed / OpenGL viewport | Which renderer produces the frame. The viewport engine is immediate; the path tracer is slower and resolves real light transport. |
| Samples | int, 8 to 4096, default 128 | How many samples each pixel gets. This is the direct trade between noise and time - noise falls with the square root, so four times the samples is half the noise. |

### RenderPasses

Which channels the render writes beside the beauty: depth, normal, id, light, atmosphere...

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Depth (metres) | toggle, default on | Writes a linear EXR of distance from the camera to the surface beside the beauty pass, for compositing. |
| World normal | toggle, default on | Writes a linear EXR of the direction each surface faces beside the beauty pass, for compositing. |
| World position | toggle, default off | Writes a linear EXR of the world position of each surface point beside the beauty pass, for compositing. |
| Object id | toggle, default on | Writes a linear EXR of which object each pixel belongs to beside the beauty pass, for compositing. |
| Water mask | toggle, default off | Writes a linear EXR of where water covers the frame beside the beauty pass, for compositing. |
| Albedo | toggle, default on | Writes a linear EXR of the surface colour with no lighting on it beside the beauty pass, for compositing. |
| Direct sun light | toggle, default off | Writes a linear EXR of light arriving straight from a source beside the beauty pass, for compositing. |
| Shadow mask | toggle, default off | Writes a linear EXR of where light is blocked beside the beauty pass, for compositing. |
| Sky / ambient light | toggle, default off | Writes a linear EXR of light arriving from the sky as a whole beside the beauty pass, for compositing. |
| Specular / reflection | toggle, default off | Writes a linear EXR of the highlights alone beside the beauty pass, for compositing. |
| Fog & haze (rgb + transmittance) | toggle, default off | Writes a linear EXR of the haze and fog between camera and surface beside the beauty pass, for compositing. |
| Sky & backdrop only | toggle, default off | Writes a linear EXR of light arriving from the backdrop beside the beauty pass, for compositing. |

### RenderQuality

Offline render engine, resolution and sampling

| Port | Direction | Type |
| :--- | :--- | :--- |
| quality | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Engine | choice: Mitsuba 3 / Blender Cycles / LuxCoreRender / OpenGL viewport | Which renderer produces the frame. |
| Width | int, 64 to 8192, default 1920 | The width of the rendered image, in pixels. |
| Height | int, 64 to 8192, default 1080 | The height of the rendered image, in pixels. |
| Samples | int, 8 to 4096, default 128 | How many samples each pixel gets. Noise falls with the square root, so four times the samples is half the noise. |
| Output file | file path | Where the finished image is written. |

### RenderRegion

[Planned] Render only a rectangle of the frame

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

## Scene

### BooleanObject

[Planned] Union, intersection and difference of meshes

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### ImportObject

An imported 3D model (FBX, glTF/GLB, OBJ, STL, PLY, OFF) placed in the scene, with its textures, transform and colour

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Model file | file path | FBX, glTF and GLB with their textures; OBJ with its MTL pictures; STL, PLY and OFF. The Load button on the node card opens the dialog. |
| Scene object | text | Name in the Objects tree. Empty: the file name. |
| Colour | color | The object's colour, where no material is assigned to it. |
| X (m) | float, -100000 to 100000, default 2500 | Where the object stands, in metres from the middle of the tile along east. |
| Height (m) | float, -10000 to 100000, default 0 | How high the object stands, in metres. Objects placed on the terrain read the ground height for themselves; this offsets from it. |
| Z (m) | float, -100000 to 100000, default 2500 | Where the object stands, in metres from the middle of the tile along north. |
| Size (m) | float, 0.01 to 100000, default 400 | Uniform size of the object's unit box. |
| Heading ° | float, -180 to 180, default 0 | How far the object is turned about the vertical, in degrees. |
| Pitch ° | float, -180 to 180, default 0 | How far the object is tipped forward or back, in degrees. |
| Bank ° | float, -180 to 180, default 0 | How far the object is rolled about its own forward axis, in degrees. |
| Visible | toggle, default on | Whether the object is drawn. Hiding is not deleting - it keeps its place in the scene and all of its settings. |

### InfiniteTerrain

An endless procedural terrain layer: on the ground plane or shaping a planet

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Scene object | text |  |
| Parent planet | text | Name of the Planet object this layer shapes. Empty: extends the home ground plane to the horizon. |
| Seed | seed |  |
| Landscape | choice: Rolling hills / Ridged mountains / Billow dunes / Realistic terrain |  |
| Feature scale | float, 0.1 to 64, default 3 |  |
| Amplitude | float, 0 to 4, default 1 |  |
| Coverage | float, 0 to 1, default 1 | Fraction of the surface the layer occupies. |
| Region size | float, 0.1 to 10, default 1.5 |  |
| Height scale | float, 0 to 4, default 1 | Extra multiplier for ground-plane layers. |
| Visible | toggle, default on |  |

### ObjectGroup

[Planned] Group objects under one transform, with instancing

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

### Planet

A procedural planet: radius, relief, seas, snow, atmosphere and its surface layers

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Scene object | text |  |
| Radius (m) | float, 10 to 1e+08, default 15000 |  |
| Relief (fraction of radius) | float, 0 to 0.3, default 0.02 |  |
| Seed | seed |  |
| Sea level | float, 0 to 1, default 0.35 | Within the relief range; 0 = no ocean. |
| Snow line | float, 0 to 1, default 0.75 | Altitude where snow begins; 1 = none. |
| Lowland rock | color |  |
| Highland rock | color |  |
| Ocean | color |  |
| Atmosphere | color |  |
| Atmosphere density | float, 0 to 2, default 0.6 | 0 = airless rim. |
| Spin ° | float, -180 to 180, default 0 |  |
| X (m) | float, -1e+07 to 1e+07, default 70000 |  |
| Height (m) | float, -1e+07 to 1e+07, default 17500 |  |
| Z (m) | float, -1e+07 to 1e+07, default 2500 |  |
| Visible | toggle, default on |  |

### Primitive

A built-in primitive (cube, sphere, plane, cylinder, cone) placed in the scene

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape | choice: Cube / Sphere / Plane / Cylinder / Cone |  |
| Scene object | text | Name in the Objects tree. Empty: the shape's name. |
| Colour | color |  |
| X (m) | float, -100000 to 100000, default 2500 | Where the object stands, in metres from the middle of the tile along east. |
| Height (m) | float, -10000 to 100000, default 0 | How high the object stands, in metres. Objects placed on the terrain read the ground height for themselves; this offsets from it. |
| Z (m) | float, -100000 to 100000, default 2500 | Where the object stands, in metres from the middle of the tile along north. |
| Size (m) | float, 0.01 to 100000, default 400 | Uniform size of the object's unit box. |
| Heading ° | float, -180 to 180, default 0 | How far the object is turned about the vertical, in degrees. |
| Pitch ° | float, -180 to 180, default 0 | How far the object is tipped forward or back, in degrees. |
| Bank ° | float, -180 to 180, default 0 | How far the object is rolled about its own forward axis, in degrees. |
| Visible | toggle, default on | Whether the object is drawn. Hiding is not deleting - it keeps its place in the scene and all of its settings. |

### TerrainObject

[Planned] Several independent heightfield terrains in one scene

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Planned | text | This node is a placeholder: it documents a capability on the roadmap so the module is not forgotten. It has no effect on the scene yet. |
| Roadmap phase | text |  |

## Shape

### TerrainShape

The outline of the ground - rectangle, round, or your own mask - with a wandering rim and an edge that gives way over a distance you set

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| mask | in (optional) | heightmap |
| output | out | heightmap |
| mask | out | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Shape | choice: Rectangle / Rounded rectangle / Round / Diamond / From mask | Round is an ellipse when the width and height differ. From mask takes the outline from the mask input and only the edge treatment from here. |
| Size | x/y pair | Width and height across, as a fraction of the tile. 1 touches the borders; less leaves ground around it. |
| Centre | x/y pair | Where the shape sits on the tile. Outside 0..1 pushes it off the edge, which is how you get a coast rather than an island. |
| Rotation | float, -180 to 180, default 0 | Turns the shape. Meaningless for a circle, and the whole point for a stretched one. |
| Corner rounding | float, 0 to 1, default 0.3 | Rounded rectangle only: how much of the half-size the corners round off. 1 is a full stadium. |
| Edge wander | float, 0 to 1, default 0.25 | How far the rim departs from the perfect curve, as a fraction of the radius. 0 is a drawing-board outline, which is the one thing no real coast has. |
| Edge detail | float, 0.2 to 64, default 5 | How many bays and headlands there are around the rim. |
| Edge roughness | int, 1 to 10, default 4 | How much finer detail rides on the large bays. |
| Seed | seed |  |
| Blend extent | float, 0.001 to 1, default 0.3 | How far in from the rim the blend reaches, as a fraction of the shape's own radius - so it means the same on a big island and a small one. 1 blends from the very centre. |
| Blend gradient | float, 0.05 to 8, default 1 | The curve from the centre out to the sides. Below 1 the ground stays high and drops away near the rim - a plateau with a cliff. Above 1 it starts falling from well inside - a beach. 1 is the plain S-curve. |
| Blend intensity | float, 0 to 1, default 1 | How far down the edge actually goes. 1 takes it all the way to the base level; less leaves the rim standing proud of it. |
| Base level | choice: Lowest in the terrain / Zero / Set below | What the ground outside the shape falls to. |
| Level | float, -1 to 2, default 0 | Used when Base level is 'Set below'. |
| Keep relief outside | toggle, default off | On, the terrain outside keeps its shape and is only pulled down toward the base - an island on a seabed that still has hills. Off, outside is flat. |

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

### FlowWarp

Drag a mask downstream along the flow

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| terrain | in (optional) | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Steps | int, 1 to 200, default 24 | How many cells downstream each value is carried. |
| Fade per step | float, 0 to 0.2, default 0.02 |  |
| Route through pits | toggle, default on |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### MakeTileable

Blend the tile so it wraps seamlessly

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Feather | float, 0.1 to 1, default 1 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### Quilt

Resynthesize the surface from its own patches

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Seed | seed |  |
| Patch size | float, 0.03 to 0.4, default 0.12 |  |
| Overlap | float, 0.1 to 0.5, default 0.25 | As a fraction of the patch. Wider overlaps hide seams better and repeat more. |
| Candidates | int, 4 to 128, default 24 | Patches auditioned per cell; the best-matching overlap wins. More candidates, better joins, slower quilt. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

### SetBorders

Pin the tile's borders to a level

| Port | Direction | Type |
| :--- | :--- | :--- |
| input | in | heightmap |
| output | out | heightmap |
| blend | in (optional) | heightmap |

| Parameter | Kind | Notes |
| :--- | :--- | :--- |
| Border level | float, -1 to 2, default 0 |  |
| Feather | float, 0.005 to 0.5, default 0.1 |  |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

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
| Translate | x/y pair | Slides the terrain across the tile. |
| Scale | x/y pair | Zooms the terrain in or out, across and down separately. |
| Rotate ° | float, -180 to 180, default 0 | Rotates the terrain. |
| Outside area | choice: Clamp / Mirror / Tile | What fills the ground the transform has moved away from. Clamp smears the border outward, mirror reflects it, tile repeats the map - which only looks right if the terrain was seamless to begin with. |
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
| Direction ° | float, -180 to 180, default 30 | Which way the terrain is dragged. |
| Amplitude | float, 0 to 0.2, default 0.02 | How far it is dragged. |
| Scale by height | toggle, default on | On, the high ground is dragged further than the low, so peaks lean over and reads as wind-shaped or as material having slumped. Off, the whole map shifts together. |
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
| Amplitude | float, 0 to 0.5, default 0.08 | How far the terrain is pushed sideways. Warping the coordinates rather than the heights is what turns a regular pattern into something organic - the lattice the noise sits on stops being visible. |
| Warp frequency | x/y pair | How large the warping swirls are, across and down. Low values sweep the whole map about; high ones ripple its edges. |
| Octaves | int, 1 to 10, default 4 | How many scales of warping are layered. More gives a more intricately folded result. |
| Invert blend | toggle, default off | Applies this node where the blend input is dark instead of where it is bright. |

