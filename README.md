# TerraForge

**A native node-based 3D terrain and environment studio.**
Geekatplay Studio.

TerraForge builds photoreal landscapes from a node graph: procedural and
simulated terrain, layered PBR materials, volumetric sky, water, and offline
path-traced rendering — all in a single native C++ application with a
real-time OpenGL viewport.

![Alpine granite and snow, built from one node chain](docs/images/hero.jpg)

*Five nodes made this. Ridged noise, pipe-model water erosion, then one
ErosionLayers node that hands back where the rock, scree, soil, grass and snow
belong — each one a photoscanned surface, placed by the erosion rather than by
hand. Rendered in the viewport, in real time.*

## Status

*9 September 2026.* Working and shipped on `main`: the node engine (252 node
types, 1,530 attributes, all under the regression lock), the studio with its
eight workspaces, materials as graphs with a base-material and gradient
library, volumetric fog and volumetric materials, cloud layers as nodes,
planets, ecosystems, animation, and offline rendering through Mitsuba,
Cycles and LuxCore. The world has a shape - a globe, a ring world, a Dyson
sphere or a flat disc or square - and a thickness, so a ring seen from its
rim or a flat world from below is a body; the atmosphere is a layer of a
set height on that surface whatever its shape, the clouds and the water lie
on it, and beyond the air is deep space: a star field, the Milky Way, and
nebulas, galaxies and moons as objects. Everything the interface can
change is reachable from the Python API, the MCP tools and the assistant,
and two audit tests keep that true. Twenty-seven test suites pass.

Audited 10 September 2026: [docs/AUDIT.md](docs/AUDIT.md) with a roadmap per
module in [docs/roadmaps/](docs/roadmaps/README.md) and a guide for building
scenes by language in [docs/AI_SCENE_GUIDE.md](docs/AI_SCENE_GUIDE.md).
Open at the moment: per-planet atmospheres (the home world has its air
layer; planets in the sky get a limb glow); 21 component nodes
are still `[Planned]` placeholders; and the performance watcher has found
frames in the Materials workspace that work while nothing changes, not yet
chased.

## Gallery

Every image on this page comes out of the application's own viewport, from a
node graph, with no compositing and no touch-ups.

| | |
|:--:|:--:|
| ![Alpine](docs/images/terrain_alpine.jpg) **Alpine** — granite, scree and snow above the treeline | ![Red mesa](docs/images/terrain_mesa.jpg) **Red mesa** — sandstone under a low afternoon sun |
| ![Island](docs/images/terrain_island.jpg) **Island** — shoreline foam, shallow water, grass on the lee slopes | ![Volcanic](docs/images/terrain_volcanic.jpg) **Volcanic** — dark basalt and a caldera at dusk |
| ![Limestone ridge](docs/images/terrain_ridge.jpg) **Limestone ridge** — pale rock cut by drainage | ![The studio](docs/images/ui_terrain.jpg) **The studio** — the graph that made the view above it |

The five landscapes differ only in their noise, their materials and their
light. The erosion chain underneath is the same one every time, and every
picture is reproducible: start the app and run

```
python scripts/make_gallery.py
```

which builds each scene through the same action inbox the assistant and the
MCP tools use, then photographs it.

---

**[terraforge.art](https://geekatplaystudio.github.io/TerraForge.art/)** — the
gallery and the studio, in pictures.
**[Install guide](docs/INSTALL.md)** — Windows, macOS and Linux, from a
one-click installer to building it yourself.
**[Node reference](docs/NODES.md)** — every node, port and parameter,
generated from the registry itself.
**[Node previews](docs/NODE_PREVIEWS.md)** — what the thumbnail on each card
is drawn from, and why a scatter that clumps looks like one.
**[Finding a node](docs/NODE_SEARCH.md)** — search by what you want, not by
what it is called: *rocks* finds stones, boulders, scree and talus.
**[Terrain analysis](docs/TERRAIN_ANALYSIS.md)** — which way a slope faces,
how much sky it sees and how much sun it gets, and the horizon sweep behind
the last two.
**[What the terrain pass costs](docs/TERRAIN_PERFORMANCE.md)** — the measured
baseline at six altitudes, how to take it yourself, and what it says about
where the frame actually goes.
**[Where the GPU is used](docs/GPU_USE.md)** — an honest audit of what runs
on the GPU and what does not, the cost of the difference, and the first node
family moved across.
**[Developer guide](docs/DEVELOPER_GUIDE.md)** — goals, roadmap, architecture
and how we work, for anyone who wants to join.
**[Layered materials](docs/MATERIAL_LAYERS.md)** — how a material stacks,
what decides where each layer shows, and what was taken from Vue.
**[Animation](docs/ANIMATION.md)** — the specification (what Cinema 4D,
Blender, After Effects and Resolve have), the status against it, the manual.
**[The interface](docs/INTERFACE.md)** — the Object Manager, the tool
palettes and their icons, the viewport header, languages.
**[Community posts](docs/COMMUNITY_POSTS.md)** — the project in three lengths.

## Who is building it

TerraForge is written by **Vladimir Chopine**, co-founder of
[Geekatplay Studio](https://www.geekatplay.com) — co-author of the official
Vue guide, *Vue 7: From the Ground Up* (Focal Press), and *3D Art Essentials*
(Focal Press), and the author of more than 3,000 tutorial episodes and
workshops on landscape and environment work in Vue, Terragen, World Machine
and the pipelines around them, over 35 years in film, VFX and design. This is
the landscape workflow he has taught for twenty years, built as the tool he
wanted his students to have.

## Screenshots

| | |
| :--- | :--- |
| ![Terrain workspace](docs/images/ui_terrain.jpg) *Terrain: noise into water erosion into layered erosion, each node carrying its own preview and its own timing.* | ![Materials workspace](docs/images/ui_materials.jpg) *Materials: five photoscanned surfaces, each masked by what the erosion decided, blended height-aware.* |
| ![Objects workspace](docs/images/ui_objects.jpg) *Objects: the scene tree — terrain, water, sun, atmosphere, cameras and planet surfaces, each lockable and hideable.* | ![Atmosphere workspace](docs/images/ui_atmosphere.jpg) *Atmosphere: sun by clock and calendar or by hand, sky, haze, volumetric cloud and water in one place.* |
| ![Cameras workspace](docs/images/ui_cameras.jpg) *Cameras: real optics — sensor format, focal length, and an exposure triangle that tells you when the shot is blown.* | ![Render workspace](docs/images/ui_render.jpg) *Render: engines, and the twelve G-buffer passes each written as a linear EXR beside the beauty.* |
| ![Lighting workspace](docs/images/ui_lighting.jpg) *Lighting: a LightSource node places and keeps a real scene light, alongside the sun.* | ![Animation workspace](docs/images/ui_animation.jpg) *Animation: transport, keys on any attribute, and an AnimationSequence node that declares the shot.* |
| ![A 250 m planet](docs/images/planet_250m.png) *The terrain tile wrapped onto a 250 m planet — same nodes, same erosion.* | ![Per-planet surface graph](docs/images/planet_surface_graph.png) *A new planet shaped by its own surface graph, the home tile in front.* |

## Features

### The node graph
- **Two domains, one graph.** Alongside the raster graph — buffers, neighbour-
  aware, where erosion lives — is a **field domain**: nodes that evaluate a
  single point in 3D and are therefore resolution-independent. `Rasterize` and
  `Sample` bridge the two, so a procedural field can be baked for erosion and
  an eroded heightfield can be read back by a field graph.
- **Field graphs compile to GLSL.** A field network is transpiled to a shader
  and evaluated on the GPU, sharing its noise implementation with the planet
  renderer so the two agree by construction. CPU and GPU results are verified
  to agree to within 1.3e-5 over 20,480 samples.
- **Bypass any node** (`Ctrl+E`) — the graph resolves links straight through
  it, so it works down chains, in both domains, and for every future node.
- **MetaNodes** (`Ctrl+G`): group a subgraph into one node, publish the few
  parameters that matter, and save it to your library as a reusable node with
  its tuned values intact. Saved nodes appear under **My nodes**.
- **Universal blend.** Any node that turns terrain into terrain gets a mask
  input from the graph itself, so its effect can be limited anywhere without
  each node reimplementing masking.
- **Node List** — the same graph read as a tree from the result backwards,
  with per-row bypass, so a scene can be built and understood without
  untangling the network view.
- **Animation-ready:** every parameter carries a keyframe track (per-key
  linear / smooth / hold / bezier, tangents, extrapolation, modifiers,
  expressions), sampled before evaluation; see *Animation* below.

### Vue-class fractals
- **`NoiseFractal`** — the manual's Simple / Grainy / Variable-Roughness /
  Fast Perlin fractals in one node, grouped as Vue groups them: base noise
  (Perlin, value, cellular, cell edges, grainy; per-harmonic rotation; double
  noise; filter steepness), scale (wavelength, X/Y stretch, stretch damping),
  fractal (iterations, scale ratio, amplitude ratio, roughness, gain, nine
  combination modes), variation (smooth level, influence, local influence,
  grain variation), distortion (amount, scale, an optional distortion map),
  filter (profile, terrace steps, creep-in, range) and output. Every fractal
  has the second output Vue's have: **rough areas**, the local roughness,
  with a reference feature size — to drive material distribution.
- **`TerrainFractal`** adds the landscape type (plain, ridges, billows, ridge
  mix, billow-ridge mix), blend, ridge smoothness and bump surge.
- **`TerrainFractal2`** — rocks emerging from sedimentary soil: overall
  aspect (turbulence and its damping, large-scale smoothness and contrast,
  buoyancy), ground aspect (bump surge, rock abundance, soil thickness, rock
  dispersion), relief-following strata (strength, spacing, offset).
- **`RockyMountains`** — ridge networks added per iteration, as separate
  mountains or basins between ridges; scale factor, flat level, ground level,
  subdivision quality, per-iteration stretch, distortion, optional rocks
  (correlation iteration, roughness, height) and the eroded variant.
- **Images and fractals both drive the terrain:** a texture wired into any
  heightmap input reads as its luminance (an image file straight into
  `TerrainOutput`), and a heightmap into any texture input reads as a grey
  image. Field-domain nodes show a preview in the graph, and output
  connectors show what they carry (range, size, point count, a field's value).

### The studio
- **A menu for every workflow.** File and Edit, then Terrain, Objects,
  Materials, Atmosphere, Animation, Render, View, AI and Help, in the order
  the work runs. Every command the tool rows and panels offer is in a menu
  too, so nothing depends on finding the right button. **View > Console**
  is a terminal: type `add_node type=Noise octaves=9` or paste the JSON an
  action document uses, with completion on the op names and `help` for the
  schema.
- **Cinema 4D's arrangement.** A left tool column holds the *modes* of the
  chosen workflow — the three transform tools head every workspace, then
  sculpt brushes, deformers, Autokey — and the row above the viewports holds
  the *settings* (resolution, sun height, camera, frame). Every button is a
  vector icon from one set of 85, drawn in C4D's language on its 18/26/36 px
  ladder (Settings ▸ General ▸ Icon size), named in its tooltip.
- **The Objects panel is an Object Manager:** dotted hierarchy lines, the
  two three-state visibility dots (grey inherits from the parent, green and
  red override it — for the viewport and for the render separately;
  Ctrl-click applies to children, drag to paint a state down the list), an
  enable tick, layer colour swatches, tags for material / driving node /
  scatter, search and a type filter, flat or by-layer views, *Set as root*
  with a path bar, drag-and-drop reorder and reparent, and a context menu
  that acts on the whole selection.
- **Three languages.** Every visible string goes through the translation
  table; English is built in, German and French ship as
  `resources/lang/<code>.json`, and any file dropped beside them appears in
  Settings ▸ General ▸ Language. A missing tag falls back to English.
- **Node cards:** rounded nodes with a category-coloured title bar,
  connectors on the left and right edges coloured by data type (the wires
  carry the same colour), and three detail levels per node — expanded,
  compact, title bar only — from the header chevron or `H`. Text stays
  crisp at any zoom.
- **Several node editors:** `View > New node editor` opens another graph
  window pinned to a domain (terrain, materials, atmosphere, render, all),
  each with its own canvas and a side pane showing the selected node's
  parameters.
- **Windows that leave the window:** every panel has a corner button that
  floats it out of the main window — onto a second monitor when there is
  one — and docks it back.
- **As many viewports as you want, wherever you want them.** Up to eight 3D
  views, each a normal window: drag one anywhere, split it, tab it beside
  the node editor, float it onto another monitor, close it from its own tab.
  **Viewports ▸ Arrange** lays the viewport area out in 1–8 cells (2×2 is one
  click) and touches nothing else on screen; **Split right** and **Split
  down** divide the view you last worked in. Adding or closing a viewport
  never rebuilds the layout, so an arrangement you built by hand survives.
- **Layouts, saved by name.** The arrangement is remembered between sessions
  on its own, and **View ▸ Layouts ▸ Save current layout…** keeps as many
  named ones as you like — the dock arrangement, which viewports are open and
  what each shows, which panels are open and which node editors exist. A
  layout never contains the scene, so *Modelling*, *Texturing* and *Lighting*
  load over any project. Scripts and the assistant reach all of it:
  `save_layout`, `load_layout`, `arrange_views`, `add_view`, `close_view`.
- **Gizmos on everything, with a lock:** meshes, lights, cameras, planets,
  the water level and the sun all move (and turn, and scale where it makes
  sense) with the viewport gizmo. The padlock beside each object in the
  Objects tree locks it in place: no gizmo, no dragging, transform fields
  read-only. `set_locked` does the same from the API.
- **A viewport pinned to one node** shows that node's result instead of
  the terrain output — useful while tuning an erosion, and the reason
  "erosion is not reflected" reports happen. The header shows an unpin
  button and the overlay a badge while a pin is on. **New viewport through
  camera** (view options) opens another view looking through any scene
  camera; **Free orbit** lets it go.
- **ID colours** as a fourth shading mode: one flat bright colour per
  object, or per material, so masks and distributions read at a glance.
- **The wheel over a number changes the number**, in every panel: a notch
  is a hundredth of the range; the panel scrolls only over something that
  is not a value.
- **The performance watcher** (View > Performance watcher, on by default in
  developer builds) counts what each frame did — lease misses, evaluations,
  uploads, redraws — and every ten seconds writes `logs/perf_watch.json`
  with findings in words: frames that did work while nothing changed,
  GPU-bound frames, evaluations no one asked for. `perf_report` returns the
  same to a script, which is how the assistant reads it.
- **Frame pacing:** Edit ▸ Preferences sets the viewport rate, the idle rate
  the whole application drops to when nothing is happening (it wakes on the
  first input), and the Preview panel's own rate and render scale — so six
  views and a live preview never hold the GPU while you think.
- **Preview panel:** follows whichever camera is selected in the Objects
  tree (falling back to the active one when the selection is not a camera),
  at that camera's own aspect ratio, with its own sky/clouds/water/shadow
  switches and render scale. It watches the camera actually on screen — its
  position, lens and optics — so moving or animating that camera redraws at
  once rather than on the next timer tick. One button renders it with the
  camera's final engine and shows the passes as they arrive.

### Displacement
- **Redirect** moves where another field is evaluated. Warp, flow, swirl and
  domain distortion are all this one node, and it works on anything — noise,
  a sampled heightfield, another redirect.
- **Displace** turns a value into relief: along the surface normal, straight
  up, along a vector or a fixed direction; depth in real units or relative to
  a reference size; smoothing; a **quality boost** so relief can resolve finer
  than the geometry carrying it; and displace-outwards-only.
- **Compute Normal** recovers the surface direction *after* displacement, so
  "snow above this altitude on slopes below this angle" means the displaced
  terrain rather than the flat plane underneath it.
- **Zones** confine a field to a sphere or box with a smooth fade, and expose
  the region on its own as a mask.
- **Live on the GPU:** wire a field into a `TerrainDisplacement` node and the
  viewport compiles it to a shader and evaluates it per vertex, normals and
  shadows included. A field has no resolution, so the relief keeps resolving
  as the camera closes in. If a graph produces a shader that will not build,
  the viewport keeps the last good one instead of going black.
- **Adaptive subdivision.** The terrain is tessellated to whatever the camera
  needs rather than to a fixed grid — 64×64 patches subdivided per edge by
  that edge's length in pixels, from an effective 512 across (exactly the
  fixed grid it replaces, so it is never coarser) up to 2048 where the camera
  is close. Levels are chosen per edge from its two endpoints, so adjacent
  patches always agree and no crack can open; spacing is fractional, so a
  patch's level changes continuously instead of popping.
- **Both domains, both directions.** A graph that samples a buffer can drive
  the viewport too, so an *eroded* heightfield can shade or displace the
  surface — bake a field for erosion, then take the result back out.

### Environment-sensitive materials
- **Distribution** answers *where a material belongs* — by altitude, steepness
  and which way the ground faces, each with a soft band. The criteria multiply,
  so a material sits where every condition holds, the way you would say it out
  loud: "rock, on the steep bits, below the snow line."
- Because a distribution reads the surface *after* displacement, "snow above
  this altitude on slopes below this angle" means the displaced terrain rather
  than the flat plane underneath it.
- **Colour blending** in the field domain — mix, add, multiply, screen,
  overlay, darken, lighten — with the factor meaning the same thing in every
  mode.
- **Live on the GPU:** wire a colour field into a `TerrainSurface` node and
  the viewport shades the terrain with it per pixel. Slope and facing come
  from the shaded normal, so the distribution follows detail finer than the
  heightmap carrying it.
- **One graph, several channels.** The same node also takes **roughness** and
  **bump**, each its own field. The distribution that decides where the grass
  goes can say that the grass is rougher than the rock and has a finer grain.
  An unconnected channel leaves that part of the shading alone.

### Meshes: import, analyse, repair, reduce, export
- **Bring a model in:** OBJ, STL (binary and ascii), PLY, OFF, glTF/GLB and
  **binary FBX**, read and written by hand with no dependency. FBX and glTF
  bring their texture coordinates and their materials' pictures (a file
  beside the model, or embedded), so a textured model arrives textured. The
  file's own coordinates are kept and the object is placed and sized by its
  transform, so a millimetre in the file is still a millimetre in the
  report. The ImportObject node's card shows a thumbnail of what it loaded.
- **Know what is wrong with it.** Eleven checks, each with a count, a
  severity and *where it is on the model*: open holes and their boundary
  loops, non-manifold edges, inconsistent winding, inside-out normals,
  degenerate and duplicate triangles, duplicate and unused vertices, sliver
  triangles, separate shells, unusual scale - with volume, surface area,
  Euler number, genus and size, a **0-100 readiness score** and a verdict.
  Click an issue and the spots are marked in the viewport.
- **Repair, measured.** Junk geometry, welding, winding, the inside-out
  flip and hole filling, repeated until the mesh stops changing - then
  **re-analysed from scratch**, so the before/after table is measured on
  the repaired mesh rather than predicted from the operations that ran.
  Undoable like everything else.
- **Reduce** by quadric edge collapse to a triangle target (Vue's Decimate),
  reporting the largest surface deviation it *measured* against the
  original, in millimetres and as a fraction of the model.
- **Rebuild it as quads.** *Reduce* thins the triangles it was given;
  **Rebuild as quads** replaces the surface with evenly sized,
  curvature-aligned quads — the right tool for a sculpt or a scan, and the
  only one of the two that changes the topology. A 768-triangle sphere comes
  back as 164 quads, still watertight. Powered by
  [QuadriFlow](https://github.com/hjwdzh/QuadriFlow) (BSD-3), built without
  Boost by using its own dependency-free max-flow solver — the patch and what
  it costs are in [docs/LICENSING.md](docs/LICENSING.md).
- **Split** a multi-part model into one object per shell; **export** to STL,
  OBJ, PLY or OFF - and it says on the way out when the mesh is not a closed
  solid, because an open surface slices as a single-wall shell with no
  infill however the slicer is set.
- Objects ▸ *import mesh · analyse · repair · mesh tools*, or the whole set
  from a script: `import_mesh`, `mesh_analyse`, `mesh_repair`,
  `mesh_reduce`, `mesh_split`, `mesh_export`.
- **Rebuild it as a solid.** Overlapping shells are the defect nothing else
  here can even see: two closed pieces running through each other read as
  perfectly valid, and print as neither. **Solidify** rebuilds the surface as
  a solid that is manifold by construction and unions the pieces into one —
  two half-overlapping cubes go from 2 shells to 1, from 24 triangles to 36,
  enclosing the union rather than the sum. Repair runs it by itself when its
  own stages cannot close a mesh. Powered by
  [Manifold](https://github.com/elalish/manifold) v3.5.2 (Apache-2.0),
  which `scripts/get_deps` fetches; without it the stage is disabled and
  **says so** rather than quietly doing something weaker.
- This is [Meshwright](https://github.com/GeekatplayStudio/Meshwright)
  (Geekatplay Studio, MIT) ported from Python to C++ and rebuilt in the
  studio's own UI. Its GPL-licensed engines — MeshLab and MeshFix — are
  deliberately not part of it; see [docs/LICENSING.md](docs/LICENSING.md) for
  what was checked, when, and why.

### The Materials workspace
- **Arranged like a material editor, not a terrain screen.** Switch to
  Materials and the screen becomes: the **Material Studio** on top — the
  material being edited, its live preview and every one of its properties —
  a thin **Material Browser** beneath it, and under those the node graph
  that makes the material beside a scene view that shows it on the ground.
  Every workspace now keeps its own arrangement and remembers what you did
  to it.
- **Six kinds of material**, read from the graph rather than stored:
  *Simple* (channels fed directly), *PBR textures*, *Mixed* (two materials
  through a mask), *Layered* (a stack with presence rules per layer),
  **Distribution** — a layer whose presence also decides where objects
  stand, so the ground and the population share one rule — and
  **Effector**, a material that influences rather than colours: a typed
  field (pressure, wind, light, heat, moisture) that systems arriving later
  read by kind. Choose a type and the studio scaffolds the nodes it needs,
  keeping what the material already had.
- **A live preview per material** — sphere, cube or flat, three
  backgrounds, a turntable — from a texture cache that re-uploads only when
  the material actually changed, so a spinning preview costs nothing and a
  browser page of thumbnails uploads each once.
- **Laid out as Vue's Advanced Material Editor.** Preview with its options,
  Randomize, Zoom and Store (snapshots you can come back to); the material
  hierarchy — the material, its layers top first, the two materials a mix
  mixes — with the three-state visibility switch and Add / Remove / Up /
  Down; and the tabs of whichever line is selected. Every channel has a
  mode: **Constant**, **Mapped picture** (load, rotate, invert, mirror,
  gamma, scale, offset), **Procedural** (any function node) or **Natural
  grain** (two colours through a noise). A layer has its **Presence** tab —
  altitude by terrain, absolute or relative to sea, slope, orientation,
  alpha boost — and a mixed material its **Materials to mix**, **Alpha** and
  **Influence of environment** tabs.
- **Every property, in Vue's tabs.** *Color* (overall tint, brightness,
  saturation, map scale and origin), *Bump* (normal intensity, bump depth,
  slope dependence, displacement), *Highlights* (GGX or Phong, intensity,
  roughness, highlight colour, anisotropy), *Transparency* (amount,
  refraction index, turn reflective with angle, flare, thin surface,
  additive), *Reflection* (reflectivity, minimal reflectivity, angle
  sensitivity, blur, metalness), *Translucency* (subsurface depth, balance,
  scattering colour, backlight) and *Effects* (diffuse/ambient split,
  luminous, contrast, shadows, coloured reflected and transmitted light).
  Each channel has a mode — none, a picture from disk, or procedural — and
  the terrain and the preview sphere shade from the same numbers through the
  same GLSL, so what the sphere shows is what the ground gets.
- **New / Save / Load** as Vue lays them out; a material changed since it
  was opened is marked *modified* and the studio asks before switching away.
- **The browser**: *Project* materials and the *Library*, thumbnails with a
  hover card showing what each is made of; double-click opens, right-click
  assigns or saves. Removing from the library moves to a trash folder.
- **Base materials, made rather than loaded.** The Library opens with
  twenty-two starting points in six groups — *Basic* (colour, mirror, wax,
  slime, glow), *Metal* (metal, copper, gold, iron), *Glass & liquid*
  (glass, water, ice), *Volume* (smoke, cloud, dust), *Ground* (ground,
  sand, rock, snow, swamp) and *Relief* (displacement rock and ground) —
  each a rendered preview sphere, each a small graph dropped into the
  project ready to be changed. One search box narrows base and saved
  materials alike by name, group or description. `preset_material` does it
  from a script and can assign the result in the same call.
- **Natural gradients.** Fourteen colour ramps — alpine, temperate hills,
  desert, red rock, volcanic, tundra, wetland, ocean depth, autumn,
  snowfield, sandstone strata, granite, sunset sky, greyscale — one click
  in any gradient attribute.
- **Mapping modes** on every picture channel, Vue's three: *Parametric*
  (the object's own UVs, as authored), *Standard* (a solid texture in the
  object's space, so it moves with the object), *Fill* (a solid texture in
  world space, so the object moves through it) — beside the planar, box,
  cylindrical and spherical projections.
- **Materials that are volumes.** A material with a volume density is
  marched through the object it sits on: Beer–Lambert absorption, a
  Henyey–Greenstein phase, a short sun march per step, an early exit at 1%
  transmittance. Smoke you can see through, a cloud in a box, dust that
  glows on the sun side — in the viewport and, through Mitsuba's `volpath`
  medium, in the render. [docs/VOLUMETRICS.md](docs/VOLUMETRICS.md) has the
  measurements and the cost rules.
- **The asset manager** is the browser's third tab. Every watched folder —
  the material library and your layouts by default, any folder of meshes or
  textures you add — is indexed, and found by typing what a thing *is*: name,
  the folders it sits in, the tags and the note you gave it. "mossy rock"
  finds `Moss_Rock_02`; "mos" already does. It is a TF-IDF vector search
  with no model and no service, deterministic and microseconds a record;
  double-click opens (a material into the studio, a mesh as an object, a
  layout applied), right-click tags, notes, trashes — never deletes — and
  restores. The index is one JSON file you can read.
- **Everything the studio and the browser do is an op** — `open_material`,
  `set_material_type`, `save_material`, `asset_search`, `asset_tag`... — so
  the assistant, the Python API and MCP drive them the same way you do.

### Health at a glance, and a governor
- **A status bar** along the bottom: FPS shown and possible, where the frame
  goes (interface, views, GPU), the last evaluation, process and system
  memory, CPU, VRAM, the GPU, and what the governor is doing. Hover any
  figure for the story; a script reads the same figures over the API.
- **The performance governor** (Settings > General): set the frame rate the
  main view must keep; when the work would drop below it, secondary views,
  the preview panel, shadows, clouds and subdivision are lightened a step at
  a time and given back once the frame is comfortable. Nothing it changes is
  saved.

### Gizmos and deformers
- **Every object has a gizmo in every view** — Move, Rotate, Scale — and a
  mesh also has **Twist**, **Bend**, **Skew** and **Taper** gadgets (`W E R`
  and `T B K J`): dashed rings twist about an axis, half rings curl the object
  from its base, diamonds slide its top, the centre dial narrows it. Vue's
  *Show Gizmos* switch (`Ctrl+G`) hides them all; each object has its own
  switch too. Every number a gadget drags is in the object's Properties —
  Position, Rotation, Size and the **Deform** block — and in the scene file.
  The deformation is one function, in GLSL for the viewport and in C++ for
  the exports and the tests, so what you see is what you export.

### AI as a tool
- **Generate a tileable texture, a 360 skydome, an image or a 3D model** from
  a prompt — *AI* menu, or the Objects tool row. A local ComfyUI or Ollama,
  or OpenAI, Anthropic, Gemini, Stability, Meshy, Tripo, Hitem3D with your
  own keys. Results land in your library and the asset index; a texture can
  go straight onto the open material, a sky into the backdrop, a model into
  the scene. Jobs run in the background with progress and cancel.
- **Describe a scene, a terrain or an atmosphere in words** and the text
  model builds it through the same actions the assistant, the Python API and
  MCP use. See [docs/AI_SERVICES.md](docs/AI_SERVICES.md).
- **Settings** (`Ctrl+,`): keyboard shortcuts you can remap, AI services and
  keys (stored protected), the ComfyUI installation and workflow folders,
  external applications, the folders the asset index watches.

### The lens, simulated
- **One switch per camera, on by default.** A focal length is a physical
  thing and behaves like one the moment it is chosen — pick a 14 mm and the
  picture barrels at once, in the viewport, in the Preview panel and in
  every captured image. Off, the lens is perfect; the two settings that are
  taste rather than physics, chromatic aberration and flare, stay off until
  asked for.
- **Distortion follows the focal length.** A 14 mm barrels, a 35 mm is nearly
  rectilinear, a 200 mm pincushions — derived from the focal length and the
  sensor format, or dialled by hand. The frame is scaled to stay filled, as a
  sensor crops what the lens delivers, so a long lens never draws black
  corners.
- **Vignetting follows the aperture** — f/2 falls off far more than f/16 —
  scaled by an amount you set.
- **Chromatic aberration** as real lateral fringing: the channels focus at
  slightly different scales, so colour separates toward the corners and
  vanishes in the middle. Off until asked for.
- **Shutter blur**: how much of the camera's own movement the shutter smears
  into the frame, scaled by the shutter speed. (The viewport blurs what the
  camera did; the render engines get the shutter itself.)
- **Lens flare**, optional: ghosts along the sun–centre axis and a halo, and
  only while the sun is actually in frame.
- **Copy settings** puts one camera's lens, exposure and film on every other
  camera, leaving each shot where it stands.

### Points, paths and scattering
- **A third domain: point clouds.** ScatterPoints (random, jittered grid, or
  spaced), relaxation, mask filtering, merging, shuffling, values sampled
  from any heightmap; stamp clouds into rasters or exact distance fields.
- **Paths are ordered clouds.** Thread a scatter into a tour, resample it
  evenly, spline through it (Catmull-Rom), fractalize it into a coastline
  wander, or route it across the terrain with a slope-penalty least-cost
  search — then hand it to PathCarve for the riverbed.
- **EcoSystem-style scattering.** Bind any imported mesh to a Points node and
  instanced copies stand on the terrain — hashed yaw, size and brightness
  jitter per copy, live in the viewport and identical in every offline
  render engine. CSV/PLY export and CSV import round-trip the clouds.

### Terrain analysis
- **Flow accumulation** (D8): how much water passes through each point.
- **Wetness index** — `ln(a/tan b)`, the standard measure of where water
  collects. High in flat hollows fed from above, low on steep ground, so a
  vegetation or moss mask sits where you would expect it.
- **Resample** at half, quarter, double or a custom sampling — detail control
  without changing the buffer size.

### Terrain
- **The world is a planet, and everything on it is its child.** The scene
  opens with a **Home planet** at the root of the Objects tree: the terrain
  tiles, the water, the atmosphere and the surface layers are its children,
  and the + tile adds new ones there. Its radius is the world's curvature.
  The sun and the cameras stand outside it, in the global frame, and so
  can anything else; a second planet is a globe in the sky with its own
  surface. Any asset can be deleted, the home planet included, and put
  back with the + tile (`delete_object` from a script). A free perspective
  view draws the world flat - a modelling view wants no distortion - and
  a camera view curves it; *Planet curvature* in the view options turns it
  on for a free view. The Objects tree's layer swatch, clicked, paints
  every object in its layer's colour; right-click sets the layer's colour
  and the object's layer. The window comes back where it was and the
  panel arrangement comes back as it was left.
- **As many terrains as you like, and one Add tile for everything.** The
  **+** tile at the head of every tool row (and Objects ▸ Add component)
  lists every component the scene can take, grouped: heightfield terrain,
  infinite terrain, planet; atmosphere, cloud layer, sun, water; light,
  camera; cube, sphere, plane, cylinder, cone, a mesh from disk; scatter
  points, ecosystem layer; material. Each arrives whole - the object, the
  node that drives it, its material. A further **heightfield terrain** is a
  tile of its own: its own Noise ▸ Terrain Output chain, its own material
  and placement, standing beside the tiles already there, moved, turned and
  sized like any object, blending into the planet on every side. The
  scripting op is `add_component`.
- **Vue's Terrain Editor, on the Terrain object's tab.** The styles down
  its left edge (Mountain, Ridged peaks, Eroded mountain, Canyon, Mounds,
  Dunes, Iceberg, Lunar, and a realistic range), the Paint tab's brushes
  (Raise, Plateaus, Flatten, Altitude, Smooth, Terrace, Noise, Erase, Shade)
  with Radius, Flow, Falloff, Invert, a target altitude and *Constrain to
  clipping range*, the Effects tab's eight erosions (diffusive, thermal,
  glaciation, wind, dissolve, alluvium, fluvial, river valley) and twelve
  global effects (grit, gravel, pebbles, stones, peaks, fir trees, plateaus,
  terraces, stairs, craters, sharpen, cracks) under one **Rock hardness**
  slider, the **clipping** slider with an end at each altitude and a hole
  or a flat beyond each, the toolbar's Invert, Zero edges, Retopologize
  and resolution halve/double, and the **Picture** button that mixes an
  image into the terrain by blend, add, subtract, multiply, min or max.
  Every one is a node dropped in front of the Terrain Output, so it is
  undoable, retunable, and the same thing from a script: `terrain_clip`,
  `terrain_effect`, `terrain_style`, `terrain_global`,
  `terrain_import_picture`, `set_sculpt`.
- **The terrain is an object.** It has a position, heading, pitch and bank,
  a width, depth and height typed in metres (or as per-axis squeeze), and
  the four deformers - twist, bend, skew, taper - through the same fields,
  gizmo, undo, animation tracks and `place_object` op as any mesh. The
  transform runs in every terrain pass (colour, tessellated, shadow), the
  selection box follows it, grounded objects stand on the moved ground. The
  *world unit* (metres per tile) is a separate control, because changing it
  relabels every length rather than resizing the tile. **Shape** cuts the
  tile round or to a rectangle: on a planet the feather follows the outline
  and the planet shows through outside; off a planet the tile ends there.
  The **Surface** sliders edit the assigned material's roughness and
  reflection, and say so; with no material they are disabled and say why.
- **How the tile joins the planet is yours to set.** *Blend* chooses
  between the tile's features only (the planet shows through wherever the
  tile is flat), the whole tile, and **Zero edge**, which brings the whole
  tile's rim to the planet's own ground over the edge blend with a curve
  that is flat at both ends, so there is no crease on either side. The
  planet relief a tile blends to is sampled where the tile stands, so a
  moved or turned tile, or a second tile, meets the ground that is there; *Edge blend* is how far the join
  reaches, up to half the tile; *Edge gradient* is its curve, a plateau
  with a cliff below 1 and a beach above; and any heightmap wired into
  Terrain output's **blend mask** port decides where the join is, 1 the
  tile and 0 the planet - a Shape node's mask with a wandering rim makes an
  island, a slope mask keeps only the flats. A mask connected to a Shape
  node beside a named shape now carves that shape instead of being
  ignored. At the join the surround matches the tile's grain: its relief
  runs at the tile's heightmap resolution there and resolves further only
  away from it, its normal comes from the tile's heightmap at the border,
  the tile's fractal micro-relief runs straight across, the water is one
  shader on both sides, and across the placement's skirt the tile's own
  material gives way to the planet's palette by the same weight its relief
  does - so the join is the planet's colour on both sides of the border and
  nothing is dragged across it.
- **Node graph engine** with dirty-tracking evaluation, multithreaded solvers,
  per-node previews and timings, and any resolution from 64 to 8192.
- **Erosion that models the physics:** particle-droplet hydraulic erosion, a
  shallow-water pipe-model solver, explicit and implicit (Braun-Willett)
  stream-power fluvial incision with tectonic uplift and rock hardness,
  thermal talus weathering, aeolian dunes and sediment deposition.
- **Every erosion node reports what it did, as masks:** `Hydraulic` gives
  erosion, deposition, standing water, exposed bedrock and the signed change;
  `Thermal` gives exposed faces, talus and the change (and takes an angle of
  repose in degrees); `StreamPower` gives incision, deposit and the change;
  `Wind` gives abrasion, deposit and the change; `SedimentDeposit` gives the
  blanket and what it left exposed. Wire any of them into a material.
- **Terrain ▸ Realistic mountain range:** the preset that reads as a real
  range — eroded ridges, the shallow-water solver, stream power cutting the
  drainage network, thermal talus and gullies, material masks on the end —
  tuned headlessly with `tools/chain_preview` against a dozen alternatives.
- **Erosion that decides what grows where:** `ErosionLayers` runs thermal +
  hydraulic erosion and turns the simulation's side channels (scour, silt,
  talus, drainage, standing water) into a stack of material masks — bedrock,
  scree, soil, grass, sediment, riverbed, snow — plus wetness and flow. The
  layers are priority-ordered and always sum to one, and come packed as two
  splat textures too. One graph serves both editors: the erosion node shows
  in the Materials workspace as soon as its masks are wired into a material.
- **Generators:** multi-type coherent noise (Perlin/ridged/billow/Swiss/value/
  Worley), diamond-square and fault fractals, geometric shapes, geological
  strata, craters, dunes.
- **Surface realism:** multi-scale power-fractal displacement, procedural
  boulders, tilted stratification, shear folding, slope-targeted craggy
  detail, snow with settle-thaw, rivers and coastlines.
- **Modeling:** import heightfield images and stamp them onto the terrain
  with blending and falloff.
- **Sculpting:** brush directly on the terrain in any viewport — raise,
  flatten, smooth, terrace, noise and erase, with radius/flow/falloff, an
  on-surface brush ring and mouse-wheel resizing. Strokes live in a
  `TerrainSculpt` node in the graph as their own layer, so retuning the
  procedural terrain underneath never destroys your hand edits, and each
  stroke is one undo step.
- **Effects:** one-click, deterministic finishing nodes — Grit, Gravel
  (slope-seeking debris), Peaks, Sharpen, Cracks, Glaciation (U-valleys
  below an ice line), Dissolve (stream-carving with a flow-map output), and
  altitude clipping with flat tops, holes and a clip mask.
- **Style presets:** the Terrain menu drops complete editable node chains —
  Mountain, Ridged peaks, Eroded mountain, Canyon, Dunes, Iceberg, Lunar —
  with a fresh seed every click.

### Planets and infinite terrains
- **A new scene is already a landscape.** The home planet's surface is the
  *Realistic terrain* layer: eroded ridged mountains in upland belts, rolling
  hills, terraced plateaus, a network of carved valleys and lowland lakes,
  with the sea at the water level - and the same altitude/slope/wetness
  palette shades the tile, the ground beyond it and every planet, so
  beaches, fields, forest belts, rock, scree and snow land where they belong.
  Three more layer styles (hills, ridged, dunes) stack with it, and a field
  graph can shape the surface further.
- **Terrain is placed onto the planet, not laid over it.** Whatever the
  graph builds - a stamped mountain, a normalised range, a hole - is settled
  to the planet's ground level; the planet's relief shows through wherever
  the tile is flat, is levelled underneath the tile's features (or kept
  beneath them: *Flatten beneath*), and every join is feathered. A hole dug
  below the water level fills with water. Terrain ▸ Placement on planet.
- **A render is the picture the camera was showing.** It was not. The
  offline renderers were handed the graph's raw heightmap over one flat
  square: not placed on the planet, nothing around it, no curvature and no
  colour at all - so a path-traced frame was a grey slab in a void while the
  viewport through that same camera showed a landscape running to the
  horizon. They now get what the viewport draws: the placed tile, the ground
  beyond it out to thirty tiles, the sea, all on the world's curve, each
  painted by the same palette the viewport paints with. The palette has a
  second implementation for this (the engines have no shaders of ours to
  run), and the two are compared sample for sample against the real shader
  by the GPU agreement check, the same way the terrain maths already was.
- **The mouse moves whatever the window under it is showing.** Every
  viewport read the mouse, but the camera it moved was whichever one the
  scene had made active - so a drag in a free view swung the active camera
  instead of the view, a drag in a view locked to some other camera swung
  the active one anyway, and the picture that changed was rarely the one
  under the pointer. Each window now flies the camera it is actually
  showing, and a free view flies the free orbit and leaves every camera
  alone. The Preview panel is the same code rather than a second copy of it,
  so a drag of the same distance moves a camera by the same amount wherever
  it happens, and with Auto-key on it writes the same keys.
- **The tile and the ground beyond it are painted by one palette, from the
  same numbers.** Rounding the tile's border took the shape of the square
  away, but two of the palette's own inputs still changed at it. The
  variation grain - which picks between grass and meadow outright, so it *is*
  the ground's colour - was broad smooth blotches inside the tile and fine
  mottle outside, because the tile's shader had no access to the planet's
  noise and made do with a different fractal. And the palette darkens toward
  wet soil along valley floors and lake beds: the surround has always had
  that number, the tile passed zero for it, so every drainage line on the
  planet stopped dead at the tile's edge. Both come from one place now. The
  colour step across the border falls by a third. And the shading is handed
  over the same way: the tile is drawn with shadows, ambient occlusion, a
  specular lobe and a sky reflection, the ground beyond it with none of
  those, and no two such models can be made to agree by choosing constants -
  so the tile keeps its quality where it is the tile and gives way to the
  ground's own light exactly where its colour does. Straight down on a tile
  at noon, the border went from twice as sharp as ordinary ground to a
  quarter sharper.
- **The ground runs out over a horizon, not at a line.** A free
  perspective view used to draw the world flat, which is a world with an
  edge: the ground reached the end of the surround, about thirty tiles out,
  and stopped dead against the sky, with none of the far shell overhead
  drawn at all. Free perspective views curve now - the sag over a tile is
  four ten-thousandths of it, so nothing about modelling on one changes -
  and the plan views stay flat, because a plan is a plan. The far shell
  takes over as soon as the eye is high enough for the horizon to reach
  past the surround, which is a height that depends on the world's size and
  used to be a fixed tile up; on a large world that left a band of heights
  with the ground stopping short of a horizon that was still further out.
  And where nothing stands behind the surround at all, its last few tiles
  now fade into the very sky that would be seen through them.
- **A clip rounds into its flat instead of creasing.** Nothing in a
  landscape meets a flat surface at a crease: a shore, a salt pan, a mesa
  top all round into theirs. TerrainClip's *Edge softness* was a ramp under
  the mark, which cannot be monotone - it arrived at the floor with the
  ground still climbing, so the surface folded back and left a low ridge
  ringing every flat, while the mark itself kept its crease. It is a smooth
  maximum now, rounded from both sides, and it defaults to a light rounding
  rather than to none. It cannot round deeper than it actually cuts, so a
  clip with its range left wide open is still an identity, to the bit. The
  same join is rounded where a placed tile is clipped against the planet's
  own ground.
- **The air thins with height, as air does.** The atmosphere was a slab of
  one density between the ground and a ceiling, so it ended on a line -
  which is why a planet seen from space had a drawn edge instead of a band.
  It is an exponential profile now, integrated exactly along each segment of
  the ray, and *Thins by* in Environment ▸ Atmosphere is the scale height: a
  real atmosphere has no top, only a height at which too little is left to
  see. Earth's is about an eighth of its visible air.
- **Clouds have weather, and can be a sheet when a sheet will do.** The
  cloud volume tiles every five and a half tiles, and a sky is seen thirty
  of them deep, so the same few kilometres used to repeat across the lower
  sky in a plain grid. One coarse lookup now both opens and closes the cover
  the way a front does and pushes the shape lookup about, so the repeat has
  nowhere to show; *Weather*, *Weather scale* and *Cloud size* control it.
  *Volumetric* off draws the layer as one flat sheet sampled once instead of
  marching it - a third of the frame cost, and for a high overcast seen from
  below it is most of what the march arrives at anyway.
- **A terrain tile stops looking like a square.** Every blend that joined a
  tile to the planet was keyed on the distance to the tile's square, and the
  contours of that distance are squares - with a crease running out to each
  corner. So the ground carried a square frame wherever a tile stood.
  *Corner rounding* takes the corners off and *Edge wander* moves the
  outline in and out by a fraction of the feather's own width, inward only,
  because the tile has no data past its own edge. What meets the planet is a
  coastline rather than an outline.
- **The Preview panel flies.** It is a viewport that happens to show one
  camera, so it takes the mouse like the others: left drag orbits, Shift +
  left, middle or right slides, Ctrl + left or the wheel moves along the
  view axis. The camera itself is what moves, so with Auto-key on a drag
  writes its keys, exactly as dragging its numbers does.
- **A view becomes a shot in one press.** Flying around until something
  looks right and then rebuilding it by typing numbers into a camera is the
  long way round. Every viewport carries a *Copy this view to the camera*
  button beside its gear: where the view looks from and at, and the lens
  that frames what is on screen, written into the selected camera. A view
  already looking through another camera hands its whole lens across.
- **Deep space is generated, not a photograph.** Beyond the air there is a
  star field of a quarter of a million stars - laid out on a grid of the
  sky, coloured along the Planckian locus from a red dwarf's orange to a
  blue giant's, gathered into associations, the brightest carrying the four
  diffraction spikes a telescope cuts across them - the Milky Way as a band
  of unresolved stars with dust rifts that redden what they dim, and clouds
  of gas. A cloud is not a picture painted on the sky: a ray is marched
  through it as a real volume, and what comes back is emission where a hot
  star inside has ionised the gas, reflection off the dust that is lit, and
  extinction through the dust in front - which absorbs blue hardest, so
  what shows through is redder as well as darker. That one rule is most of
  why a photograph of a nebula looks the way it does, and why these come
  out teal in the heart and crimson at the edges. All of it is a function
  of a direction and a seed, so it holds its detail at any focal length and
  costs no memory: there is no backdrop image to run out of pixels.
  Environment ▸ Space carries six named skies - *A dark night sky*,
  *Hubble*, *Science fiction*, *Deep field*, *Star nursery*, *Empty* - and
  **Fill the sky**, which scatters as many as eight nebulas and galaxies
  over it, spread apart and varied. **Realism** is the dial the whole look
  turns on: 1 is what a camera records, 0 is what a film paints, cyan and
  magenta with a glow round everything. Script: `space_preset`,
  `space_populate`, `set_space`, `add_nebula` / `set_nebula`.
- **Ring worlds and Dyson spheres.** The world has a shape: a *Globe* (the
  planet), a *Ring world* - a cylinder of the planet's radius curving along
  the tile's east-west, *Ring width* across, with the ground on the inside
  and the sun on its axis - or a *Dyson sphere*, a globe with the ground on
  the inside and the sun at its centre. From an inside world the far side
  arches overhead: the surround is followed by a far shell that draws the
  whole shape, its relief the world's continents (the surface layers
  sampled by direction, the way a planet in the sky is shaped), lit toward
  the sun body at the centre and seen through two layers of air - the one
  over you and the one over the far side. Beyond a ring's rim there is
  space. One shell can carry ground on both faces: every terrain tile and
  surface layer has a *Side* - the world's own, or the other face, where
  the heights go the other way (a globe's other face is the inside of its
  crust, seen from within the hollow planet). Objects ▸ Home planet ▸
  World shape; from a script `set_viewport` with `world: ring|dyson|globe`
  (or `world_shape`, `world_inside`, `world_width`, `world_sun_inside`)
  and `place_object` with `side`.
- **The home planet is a sphere:** the terrain tile lies on a planet whose
  radius is a real length from a tenth of a millimetre to a billion
  kilometres (Objects ▸ Planet surface, or the Atmosphere tab). Large radii
  give the curved horizon; below the tile's own circumference the tile wraps
  the whole globe, equirectangular, its heights shrinking with it - a 1 m
  planet made from the heightmap, with the same nodes, materials and
  erosion as any terrain. Single-precision positions resolve a globe down
  to about a millionth of the tile width (1 cm at the default 5 km tile);
  for anything smaller, shrink the tile (Terrain ▸ Across) with it.
- **Every world its own graph:** each planet (and the home surround) names
  the `SurfaceDisplacement` node whose field graph displaces it, as a
  Terragen planet has its own terrain network. "New graph for this world" in
  the planet's properties, or `"surface_node":"new"` in `add_planet`.
- **Unlimited planets.** Each is a pure parameter block — radius, relief,
  seed, sea level, snow line, rock and water colors, atmosphere — generated
  on the GPU every frame. No textures, no meshes, no caches, so a hundred
  planets cost the same memory as one. Add them from the Objects panel or by
  asking the AI.
- **Infinite procedural terrains.** Stack any number of endless terrain
  layers (rolling hills / ridged mountains / billow dunes, each with its own
  scale, amplitude, coverage and seed). Parented to a planet they shape its
  surface; at the root they extend the home terrain tile past its edges to
  the horizon, blending seamlessly out of the tile's own heightmap.
- **Your work survives a crash.** The application autosaves every couple of
  minutes whenever the undo history has moved - three files in rotation, the
  user's own project path never touched - and a session that ends without
  closing properly is offered back on the next start, newest autosave first.
- **Lakes, and flow that reaches the sea.** `FillBasins` floods every closed
  basin to the height of its outlet (Priority-Flood). Read one way it is the
  water standing in the hollow - depth and mask outputs, ready to drive a
  material or a blend. Read the other way it is the surface flow routing needs:
  without it D8 routing dead-ends at every pit, and on plain fractal terrain
  81 such cells were swallowing the drainage of 41% of the map.
- **Two noise bases, not one.** fBm for ridges and rolling ground, and
  cellular (Worley) noise for everything fBm cannot make: cracked mud, basalt
  columns, scree, crater fields and tectonic plates. `FieldVoronoi` gives the
  distance to the nearest cell point, to the second, to the seam between them,
  or one flat height per cell - through round, diamond or square cells. Being
  a field node it compiles to GLSL, so it shapes planets and infinite terrain
  at any scale as well as the finite tile.
- **Shaped by a node graph.** A planet's surface and the endless ground plane
  have no heightmap - they are functions, evaluated on the GPU at whatever
  detail the camera has earned. So they are authored as functions: wire a
  field graph into a `SurfaceDisplacement` node and it is transpiled to GLSL
  and added to every procedural surface in the scene, at every scale, for no
  memory at all. The same field nodes run on the CPU for picking and baking,
  and a test holds the two definitions together.
- **Continuous zoom.** Pull back to see a whole planetary neighbourhood —
  the sky thins to a starfield as you leave the atmosphere — then fly to any
  world (double-click it in the Objects tree) and keep zooming until individual
  ridges resolve. Detail is a *continuous* function of on-screen size, so
  nothing pops as you travel.
- **Progressive quality.** Everything scales with how much a thing is
  actually worth: sub-pixel planets are skipped entirely, sphere meshes swap
  through three LODs with hysteresis so they never flicker at a threshold,
  and shadow maps, volumetric clouds and 4K material maps switch off once
  the camera leaves the ground. Surface shading always evaluates two octaves
  finer than the geometry, so detail below mesh resolution still shows.

### Materials
- **PBR terrain shading:** roughness, metallic, specular, sky reflections,
  translucency, transparency and true displacement mapping.
- **Photoscanned materials:** browse and download CC0 PBR sets (albedo,
  normal, roughness, AO) from ambientCG at up to 8K, cached locally.
- **Multilayer compositing:** splat maps from terrain masks, layer blending,
  color grading, and albedo-to-PBR derivation.
- **Material stack:** `MaterialStack` blends up to six mask + albedo layers
  with height-aware blending (silt fills the cracks of the rock before it
  covers the ridges) into albedo and per-layer roughness for `MaterialOutput`.
  `examples/macros/erosion_materials.json` builds the whole
  erosion → layers → material chain and assigns it to the terrain.
- **Rocks and grass as displacement layers**, Terragen's way: a
  `MaterialLayer` carries a displacement channel composited by its presence
  (slope, altitude, mask), `FakeStones` hands it the boulders alone and the
  new `GrassDisplacement` a field of clumped tufts, and the material's
  relief rides on the terrain it is assigned to — stones that cast shadows,
  meadows on the flats. `examples/macros/rocks_grass_layers.json` builds
  it; [docs/DISPLACEMENT_LAYERS.md](docs/DISPLACEMENT_LAYERS.md) says how it
  maps to Terragen and where a heightmap's resolution ends.
- **Stone fields that are functions, not texels:** `FieldStones` evaluates
  on the GPU per vertex and per pixel, so a 20 cm cobble and a 3 m boulder
  come out of one node and there is no ceiling on how many - where the
  raster `FakeStones` could express no stone smaller than 14 m and no more
  than 25 to the hectare. Power-law sizes, a height and a lean per stone,
  elongation, irregular outlines, broken faces, burial and slabs, and every
  stone shaped differently from its neighbour rather than only from the
  average. Amount is a coverage target, so turning it up paves the ground
  end to end with no bare earth left. Clustering both gathers stones into
  drifts *and* pulls them together inside one until they touch; below zero
  it pushes them apart. A `mask` and a per-stone `shade` output let a
  material colour each stone at its own position -
  `examples/macros/stone_field_material.json` wires it in one go. CPU and
  GPU verified identical over 33 cases.
  [docs/STONE_FIELDS.md](docs/STONE_FIELDS.md).
- **Grass that is a function, not a texture:** `FieldGrass` is the stones'
  argument one scale down - a sward evaluated per vertex and per pixel, with
  no resolution of its own. Tufts that come to a point rather than domes, a
  wind that leans the whole field one way with the tall blades bending
  further, blades as an angular ripple whose count blends across three
  harmonics rather than looping, and bare ground that actually reaches zero.
  Mask and per-tuft shade outputs so a material can colour each tuft.
  [docs/GRASS.md](docs/GRASS.md).
- **The shape of the ground, and the water on it:** `TerrainShape` gives
  the tile an outline - rectangle, rounded rectangle, round, diamond or
  your own mask - with a fractal rim that only ever eats inward, so the
  size stays a maximum you can reason about. Its edge blend is the three
  questions one slider used to run together: how far in it reaches, the
  curve it takes from the centre out (a plateau with a cliff, or a beach),
  and how far down the rim actually goes. `Lake` puts a body of water
  somewhere in particular, keeping Terragen's Water level, Centre and Max
  radius and going past its always-round flat disc - a wandering shore, a
  lake that settles into the valley it is in, a carved bed and a banked
  shore, with depth, mask and beach outputs.
  [docs/SHAPES_AND_WATER.md](docs/SHAPES_AND_WATER.md).
- **Ecosystems - intelligent distribution of objects**, Vue's EcoSystem as
  a layer in the material stack: `EcosystemLayer` places a population per
  hectare by the layer's presence (mask, altitude, slope, orientation,
  slope influence, decay near the objects standing on the terrain), in
  clumps, with up to eight species by relative presence or a driver map,
  each instance sized, turned, leaned into the slope and tinted from its
  own stable id - and a layer stacked above another has *affinity* with
  and *repulsion* from the population beneath it, so grass gathers near
  the trees but not under the canopy and pebbles collect at a boulder's
  feet. Deterministic and stable under editing: raising the density adds
  instances and moves none. `ScatterArea`, `PointsTransform` and
  `PointsInteract` are the same stages as Points nodes.
  `examples/macros/ecosystem_layers.json` builds a meadow;
  [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) maps it to Vue.
- **Level of detail** for scattered copies and for the terrain's baked
  relief: copies are bucketed by cell and thinned by distance (the
  survivors grow to keep the cover), drawn from reduced meshes far away
  and culled beyond a distance, in the shadow map too; far ground reads a
  calmer mip level of the relief so stone fields stop shimmering at the
  horizon. `instances_drawn` / `instances_total` in the API state are the
  proof; [docs/LOD.md](docs/LOD.md).
- **Materials on objects:** any mesh (a primitive or an import) can be
  assigned a `MaterialOutput` — from the Materials panel or
  `{"op":"assign_material","node":"...","object":"..."}` — and is lit by the
  same GGX PBR pipeline as the ground: tint, roughness, metallic, specular,
  reflection, clearcoat, translucency and emissive. A mesh has no UV channel
  yet, so this is scalar shading only — a picture-mapped texture on an
  object is the next step, not this one.

### Scene and lighting
- **Built-in primitives** — cube, sphere, plane, cylinder, cone — plus OBJ
  import; every mesh transforms, takes materials, and scatters.
- **Point lights** (up to eight) with color, intensity and reach, agreed on
  by the viewport and every offline engine.
- **Night**: let the sun set and stars come out over a sky, haze and
  surround that darken together — a campfire in the dark is one AI prompt.
- **Real-world terrain**: SRTM `.hgt` tiles and uncompressed GeoTIFF DEMs
  import directly, alongside 8/16-bit PNG heightmaps.
- **Spot lights** too: cone angle, heading and pitch, in the viewport and
  every engine.
- **Meshes cast shadows** — rocks, trees and every scattered copy are drawn
  into the sun's shadow map with their deformers, and receive the terrain's
  and each other's shadows.
- **Objects stand on the ground.** Make a mesh a child of the terrain and it
  is *grounded*: its base follows the surface as it is moved, and a
  `TerrainImprint` node in the graph moulds the ground to it — flat under
  the **whole base** (the convex hull of the object's lowest vertices, not a
  box or an ellipse, so every corner of a house sits on the ground), out to
  a *flat margin* you set in metres, blended back to the natural terrain
  over a *blend distance* you set in metres, a hollow when the object is
  pushed down and a mound when it is lifted. *May sink* lets a boulder sit
  that deep in the slope before the ground is dug out under it. Properties
  ▸ Ground, or `place_on_terrain` / `set_ground` from the API.

### Environment and rendering
- **Volumetric clouds** raymarched with Perlin-Worley noise, cloud types
  (stratus / cumulus / cumulonimbus), coverage, wind and self-shadowing.
- **As many cloud layers as you add nodes.** Every `CloudLayer` node in the
  graph is a layer of its own — low stratus, cumulus, a cirrus veil — chained
  through the `clouds` port into `AtmosphereSettings`; the sky pass marches
  up to eight, far to near, skipping clear air in longer strides.
- **Fog as a medium**, not a fade: the same radiative transfer as the volume
  materials, with albedo, anisotropy, heterogeneity and a step count you
  choose, so a valley fog catches the sun where it should.
- **Sky and light:** configurable atmosphere, height fog with absorption and
  sun scattering, and a sun that can be positioned manually or from a real
  latitude, longitude, date and time.
- **The terrain's outline and size:** square, round or a rectangle of any
  aspect, from the Environment panel or `set_viewport`, and the tile's
  size in metres (`terrain_size_m`, a setting like any other);
  `TileRotate` tiles a terrain with a random turn per segment so the
  repetition does not show.
- **Water:** depth-graded color, waves, and shoreline and crest foam.
- **Viewport:** up to 8 dockable view windows (perspective / top / front / right),
  shading modes, shadow mapping, scale bar, metric or imperial units.
- **Offline rendering:** path-traced output through Mitsuba 3, Blender Cycles
  or LuxCoreRender. The render reuses the viewport's own sky and clouds as an
  HDR environment, plus the same material, sun, water, fog and tone mapping,
  so the result matches the preview — scattered meshes and point lights
  included.
- **Render passes**: depth and world-normal EXRs beside the beauty image.
- **360° panoramas**: one equirectangular frame from any camera position,
  for skyboxes and VR stills.

### Animation
Everything is static until it has a key; the first key on a property is what
animates it. See [docs/ANIMATION.md](docs/ANIMATION.md) for the full
specification and manual.
- **The animation circle** beside every property in the Properties panel
  (Cinema 4D's): empty = static, ring = animated, filled = a key on this
  frame. Click to key, Ctrl/Shift to add/remove, right-click for ease and
  extrapolation. `K` keys the selected object's transform; **Autokey**
  records every edit.
- **Everything animates**: position, rotation, size, squeeze, colour,
  visibility, the deformers, scatter, lights, camera optics, planets and
  infinite-terrain layers, the sun, sky, fog, water and clouds, and every
  node attribute in the graph (vectors and colours per component).
- **Keys** have per-key interpolation (linear, smooth, hold, bezier),
  auto-clamped / user / broken tangents, ease presets (Easy Ease on F9),
  and five extrapolation modes — cycle, cycle with offset and ping-pong turn
  two keys into a windmill or a swing.
- **The Timeline**: transport with previous/next key, a frame field that
  reads and writes frames, timecode or seconds, range, fps, loop mode,
  preview range, markers; a track tree (object > group > axis) with key
  glyphs per interpolation, box select, drag, retime handles, copy/paste,
  mirror, snap; double-click a row to add a key.
- **The Curve editor**: F-curves in axis colours, draggable tangents (Alt
  breaks), speed and normalised views, Smooth / Flatten / Bake / Simplify,
  a ghost of the curve while you drag.
- **Modifiers and expressions**: noise, oscillator, offset, limit, smooth
  stacked on any curve; or a one-line expression over `t`, `frame`, `value`
  and other properties by name (`"Camera 1".cam.focal_mm`,
  `world.sun_azimuth`).
- **Playblast** captures the viewport per frame; **Render > Sequence**
  renders the range; fly-throughs ride any path node. Every gesture is one
  undo step; every operation is an API/MCP tool (`set_key`, `keys`,
  `set_range`, `play`...).

### AI assistance
Describe a landscape in plain language — or drop in a photograph — and a local
[Ollama](https://ollama.com) model builds the node graph, parameters,
materials and lighting for you. Everything runs on your machine.

**Everything the interface can change, a script can change** — and it is
checked, not promised. `list_settings` names every saved render and world
setting with its value; `set_setting` changes any of them. Two audit tests
fail the build when a setting, a panel or a node attribute drifts out of
reach: every field of the render settings must be in the saved-settings
table (which is what the ops read), every panel must have a `show_panel`
name, every op must have an MCP tool, and the count of node attributes
without a tooltip may only go down.

---

## Examples

- `examples/eroded_island.gpxt` — an island landform through hydraulic
  erosion, with a midslope mask feeding a point scatter.
- `examples/routed_river.gpxt` — PathFind routes a least-cost line across
  the noise and PathCarve cuts the riverbed along it.
- `examples/basalt_steps.gpxt` — columnar basalt softened by thermal
  weathering.
- `examples/showcase_valley.gpxt` — a forested island: eroded mountain,
  midrange scatter mask, 900 instanced pines with wind sway, hero camera.

Open them with File > Open (or the `open_project` scripting op) and press
around.

## Install

### Ready-made installers

| Platform | Download | Notes |
|---|---|---|
| **Windows 10/11** | `TerraForge-<version>-Setup.exe` | Installs for your account only, so there is no administrator prompt. A portable `.zip` is published beside it. |
| **macOS 11+** | `TerraForge-<version>.dmg` | Drag to Applications. First launch: right-click → Open (the builds are not notarised). |

### One click from source

```
git clone https://github.com/GeekatplayStudio/TerraForge.art.git
cd TerraForge.art
```

**Windows** — double-click **`install.bat`**
**macOS** — double-click **`install.command`**
**Linux** — run **`./scripts/install.sh`**

Each one checks for the tools it needs and offers to install the missing ones
(winget on Windows, Homebrew on macOS, apt/dnf/pacman on Linux), fetches the
third-party sources, builds, and puts TerraForge where your system expects to
find applications. Add `-Dev` / `--dev` to build without installing.

### Stay up to date

A build carries the commit it was made from. **Settings ▸ Updates** checks
GitHub for a newer head of the branch (optionally at every start) and, when
there is one, offers to update: the application closes, `scripts/update.ps1`
(or `update.sh`) pulls, rebuilds with the platform's build script, puts the
new executable where the old one ran from, and starts it again.

### Or build it by hand

```powershell
powershell -ExecutionPolicy Bypass -File scripts\get_deps.ps1   # Windows, once
.\build.ps1
.\start.ps1
```

```bash
./scripts/get_deps.sh                                           # macOS / Linux, once
./build.sh
./start.sh
```

`get_deps` fetches Dear ImGui, GLFW, imgui-node-editor, GLM, GLAD, miniz,
nlohmann/json and stb into `external/`. They are not committed to this
repository, and both scripts fetch the same versions.

**[The full install guide](docs/INSTALL.md)** covers prerequisites, flags,
uninstalling, where your files are kept, building the installers yourself, and
what to do when something does not work.

### Requirements

| | Minimum |
|---|---|
| Windows | 10 (1809) or 11, 64-bit |
| macOS | 11 Big Sur or newer, Intel or Apple silicon |
| Linux | any current distribution, X11 or Wayland |
| Graphics | OpenGL 4.3, or 4.1 on macOS — any GPU since roughly 2012 |
| Build tools | CMake 3.20+, Ninja, a C++20 compiler (MinGW-w64 GCC, MSVC, Clang or GCC) |
| Python 3.9+ | optional: the offline path tracers and the AI assistant |

macOS caps OpenGL at 4.1, so TerraForge asks for a 4.1 core profile there and
compiles its shaders at `#version 410 core`. It uses nothing above 4.1 — no
compute shaders, no shader storage buffers, no explicit binding layouts — so
adaptive tessellation, volumetric clouds, shadows and the render passes all
work on a Mac.

### Optional extras

```
pip install mitsuba                # path-traced offline rendering
pip install -r requirements.txt    # the whole Python layer
ollama pull llama3.1               # AI terrain from a description
ollama pull llava                  # AI terrain from a photograph
```

## Run the tests

```powershell
.\test.ps1      # Windows
./test.sh       # macOS / Linux
```

`test.ps1`/`test.sh` run the seven suites below, all of which must pass
before a commit:

| Suite | Covers |
|---|---|
| `nodeterrain_tests` | The original solver library and CLI |
| `engine_tests` | Registry, evaluation and caching, cycle rejection, determinism, erosion, materials, serialization, field domain, GLSL transpiler, bypass, MetaNodes, blend, animation |
| `undo_tests` | Restore correctness, redo branching, history jumps, node library round-trip |
| `node_tests` | **Universal node contract** — one data-driven battery over every node type: metadata, ports, determinism, bypass, serialization, extremes, and that every field node has a GLSL emitter. Adding a node automatically tests it. |
| `regression_tests` | **Regression lock** — a node may never be removed or change category, an attribute may never be removed or be retyped, every committed project must still evaluate to the same hash, and every entry in the feature manifest must still name a test that exists. |
| `render_tests` | Renderer maths that needs no GL context: patch culling, blue-noise/LOD scatter, planet placement |
| `pytest` | Render backends, AI helpers, and the two coverage audits: every setting scriptable (`test_settings_coverage.py`), every op an MCP tool (`test_api_coverage.py`) |

The contract and regression suites are the reason features do not quietly
disappear: 20,000+ contract assertions and 4,400+ regression checks over
245 node types, 1,500 attributes and 183 manifest features. If a change to
a golden is intentional, `regression_tests --update` re-records it —
review that diff rather than trusting it (the update tool can also just
rewrite line endings with no content change - check `git diff --stat`, not
only that a diff exists).

`ctest --test-dir build` runs the complete battery — the seven above
plus per-area suites (ecosystem, shapes/lake, scene tree/undo of the studio
UI, i18n, layout, mesh pipeline, material editor, config, icons, AI
services, assets, node names and search, points previews, terrain cracks,
horizon, animation, CSG, the crash ledger, the terrain transform) and a
performance guard — 27 suites in all, which is what CI runs.

---

## Logs and crash reports

Every console message is also appended, flushed line by line, to
`logs/terraforge_<stamp>.log` under the project root (or `$TERRAFORGE_LOG_DIR`),
so a crash cannot take the log with it. `std::terminate`, `abort()`, a UCRT
invalid-parameter and unhandled SEH exceptions each write
`logs/crash_<stamp>.txt` with the reason (an uncaught exception's `what()`
included) and a stack as `module+RVA`. The build carries `-g`, so
`python scripts/resolve_crash.py` turns those frames into `file:line`;
`python scripts/dump_stack.py <file.dmp>` reads a Windows minidump the same
way. `{"op":"debug_crash"}` on the actions API exercises the whole pipeline.
The `logs/` folder is local and never committed.

**Hangs leave a report too.** A watchdog thread watches the frame loop; when
no frame has finished for six seconds (Settings ▸ `perf.hang_seconds`, 0
turns it off) it suspends the main thread for a moment, walks its stack and
writes `logs/hang_<stamp>.txt` in the same module+RVA form. If the frame
later completes, the file gains a *recovered after N s* line, so a slow
evaluation reads differently from a deadlock. A window being moved or
resized, and a native file dialog, are not hangs.

**And every session starts by reading them.** The first line the console
logs is a count of the crash reports, hang reports and sessions that were
killed without a clean exit, each with a one-line summary.
`{"op":"crash_reports"}` lists them (MCP: `studio_crash_reports`);
`{"op":"crash_mark_fixed","file":"hang_20260908_120000.txt","note":"..."}`
closes one with a note saying what fixed it, and it leaves the count. The
ledger is `logs/crash_ledger.json`. Read the list at the start of a
development session, fix what it names, mark it.

## Project layout

| Path | Contents |
|---|---|
| `engine/` | Terrain engine: heightmaps, node graph, attributes, node packs |
| `studio/` | Desktop application: viewports, renderer, panels, AI, scene |
| `orchestrator/` | Python layer: render backends, AI helpers |
| `mcp_server/` | Tool server exposing the engine for automation |
| `src/`, `include/` | Original C++ solver library and CLI |
| `tests/` | C++ and Python test suites |
| `tests/manifest/` | The regression lock's records: node, attribute, golden and feature censuses |
| `tests/projects/` | Golden `.gpxt` projects, re-evaluated to a hash on every run |
| `scripts/` | Dependency fetchers and the one-click installers |
| `packaging/` | Windows setup program (Inno Setup) and macOS .app / .dmg builders |

## Usage notes

- **Workspaces** across the top switch the editor between Terrain, Materials,
  Objects, Atmosphere, Lighting, Cameras, Animation and Render.
- **Properties** is a tabbed editor (Render, Scene, World, Object, Material,
  Node) that follows what you select and has a search box.
- **Navigating a viewport:** left drag orbits; Shift+left, middle or right
  drag pans; Ctrl+left or Alt+right drag dollies, as does the wheel. Maya's
  Alt+left / Alt+middle / Alt+right do the same three. `[` and `]` resize a
  sculpt brush.
- **Right-click a viewport** for view options; the same menu sets how many
  view windows you want. Layouts persist between sessions.
- **In the node graph:** `Ctrl+E` bypasses the selected nodes, `Ctrl+G` groups
  them into a MetaNode and `Ctrl+Shift+G` expands one back out. Bypassed nodes
  are dimmed and tagged.
- **Node List** (tabbed with the Library) shows the same graph as a tree from
  the terrain result backwards, with each node's inputs indented beneath it.
  Nodes that do not reach the result are listed separately rather than being
  silently ignored.
- **Undo/redo** (`Ctrl+Z` / `Ctrl+Y`, also `Ctrl+Shift+Z`) covers the node
  graph, scene objects and world settings as one history. Every step is
  named, and **Edit > History** lists them so you can jump straight back to
  any earlier point. Changes made by the AI assistant, the Python API and
  MCP are ordinary history steps, so they can be taken back the same way.
- Projects are saved as `.gpxt` JSON files.

### Undo/redo from automation

```python
from mcp_server.studio_api import Studio

s = Studio()
s.set_sun(azimuth_deg=250, altitude_deg=12)  # one history step, "AI: set_sun"
s.undo()                              # take it back
s.redo()                              # and put it back
```

The same is available over MCP as `studio_undo` / `studio_redo`, and to the
in-app assistant as `{"op": "undo", "steps": 1}`. All four surfaces run the
same action schema, so a change is undoable no matter which one made it.

## License

Copyright © Geekatplay Studio.

TerraForge is **free for noncommercial use** under the
[PolyForm Noncommercial License 1.0.0](LICENSE) — use it, modify it, share
it, fork it. Students, hobbyists, teachers, schools, nonprofits, public
research and government bodies are free by name in the licence.

**Commercial use needs a paid licence.** Client work, production, selling
what you make, or use inside a for-profit company — see
[COMMERCIAL.md](COMMERCIAL.md) for where the line sits and how to get one.

**Whatever you make with it is yours.** The licence covers the software, not
your terrains, renders, exports or scenes.

The source is public, readable and forkable, so TerraForge is
*source-available* rather than open source in the formal sense — the one
restriction is commercial use.

Which licences we accept and what was verified when is recorded in
[docs/LICENSING.md](docs/LICENSING.md).

Third-party dependencies keep their own permissive licences (Dear ImGui,
GLFW, GLM, imgui-node-editor, glad, miniz, nlohmann/json, stb) — see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Downloaded material
libraries are CC0.
