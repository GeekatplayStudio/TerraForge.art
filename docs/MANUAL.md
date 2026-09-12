# TerraForge — the manual

This is the book you read once, from the top. Everything here is about
*using* the application; the reference documents linked at the end go deeper
on each part, and [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) is for people
working on the code rather than with it.

---

## 1. What it is

TerraForge builds landscapes and the worlds they sit on. You describe a
terrain as a graph of nodes — noise, erosion, rivers, stamps — and the
application turns that into ground you can walk a camera through, dress with
materials, wrap in air and cloud, and render.

Three things about it are worth knowing before you start, because they shape
everything else.

**A terrain is a recipe, not a picture.** The graph is the truth. You can
change the mountain generator at the top of it and everything downstream —
the erosion, the material masks, the scattered rocks — follows. Nothing is
baked until you ask for it.

**The ground does not end.** The terrain you author is one *tile*, and it
stands on a world. Beyond the tile's edge the same kind of ground continues
to the horizon, generated rather than authored, and the horizon curves
because the world is round. You are never editing a square of land floating
in a void.

**The world need not be a globe.** It can be a ring with the ground on the
inside, a Dyson sphere with a sun at its centre, or a flat disc with an edge
you can fall off. The terrain, the water and the air all follow whatever
shape you choose.

---

## 2. First run

Install as [INSTALL.md](INSTALL.md) describes, then start the application.

You get a scene with a world already in it: a home planet, one terrain tile
standing on it, water, air, and a camera. That is deliberate — an empty
document teaches you nothing, and this one is already a landscape you can
fly through and change.

**Spend the first minute moving.** Put the pointer over the big viewport and
drag: the left button swings the view around what it is looking at, the
middle button (or the right, or Shift and the left button) slides it, and
the wheel moves you in and out. Hold Ctrl with the left button to move along
the view axis instead of orbiting.

Those are the same bindings everywhere, and **the window under the pointer is
the one that moves**. A viewport showing a camera flies that camera. A free
viewport flies its own view and leaves every camera alone. The Preview panel
behaves exactly like a viewport, because it is one.

---

## 3. The window

**Workspaces** run along the top: Terrain, Materials, Objects, Plants,
Atmosphere, Lighting, Cameras, Animation, Render. Each is a saved arrangement
of panels for one job. Switching between them does not change your scene,
only what you are looking at.

**Panels** are ordinary windows. Drag a tab to move, split, float or re-dock
it; the arrangement is remembered per workspace, and View ▸ Layouts saves
named arrangements you can come back to.

The ones you will use most:

| Panel | What it is for |
|---|---|
| **Viewport** | The 3D view. Up to eight, each with its own camera, shading and overlays. |
| **Scene** | Every object in the world as a tree: the planet, its terrain, water, air, cameras, lights, meshes. |
| **Properties** | Everything about whatever is selected. Tabbed, and the tabs stay where you put them. |
| **Graph** | The terrain's node network. |
| **Library** | Every node, by family, with search. |
| **Preview** | One camera, rendered continuously at reduced size. |
| **Timeline** | Keys, tracks and sequences. |
| **Console** | What the application is doing, and a command line into it. |

**Deleting.** Click an object in a viewport (or in the Scene tree) and press
**Delete**: it goes, with everything under it and the node in the graph that
built it, and Ctrl+Z brings all of it back. In a viewport the key only acts
on the view you clicked last, and it leaves the world's own pieces - the
planet, its terrain, the sea, the air, the sun - alone; delete those from the
Scene tree. The key is Edit ▸ *Delete selection* in Settings ▸ Shortcuts.

Right-click almost anything for its menu. Hover almost anything for an
explanation of what it does and why you would want it.

---

## 4. The world

Select the home planet in the Scene panel and its Properties give you the
world itself.

**Radius** is how big the world is. The default is Earth's, 6375 km. A
smaller world curves harder — the horizon comes closer and mountains fall
away faster.

**Shape** is Globe, Ring or Flat. A ring has ground on the inside of a band
and the rest of the ring arching overhead; *Width* is how wide the band is,
and it matters — the default is two thousand kilometres across, which from
the ground reads as an ordinary sky, because it is. Narrow it and the ring
becomes a ring to look at.

**Thickness** turns a shell into a body: it gains an underside and a rim,
and you can stand on either face.

**Ground on the inside** puts your terrain on the face that looks toward the
centre. With a sun inside, that is the classic ring world.

Curvature in a viewport: a camera view always curves, because that is the
picture it is taking. A free perspective view curves too. The orthographic
plan views stay flat, because a plan is a plan. The switch is in each
viewport's gear menu.

---

## 5. Terrain

### The graph

Open the Terrain workspace. The Graph panel holds nodes wired left to right,
ending at **TerrainOutput** — whatever reaches that node is the terrain.

Add nodes from the Library, or right-click in the graph. The families you
will reach for first:

- **Generators** — Noise, PowerFractal, Voronoi, FakeStones: the raw shape.
- **Erosion** — hydraulic and thermal erosion, rivers, stratification. These
  are the difference between noise and landscape.
- **Filters** — Terrace, Clip, Curve, Blur, Warp: shaping what you have.
- **Selectors** — slope, altitude, curvature, cavities. These make *masks*,
  and masks are how you say "only here".
- **Combiners** — Blend, Math, MaskedBlend.

Every node's attributes are in the Properties panel when it is selected, and
every attribute has a tooltip saying what it does.

A few worth knowing early:

**TerrainClip** cuts altitudes: a flat floor below a mark, flat tops above
one. Its *Edge softness* rounds the cut into the flat from both sides, the
way a shore or a mesa top actually does. Set it to 0 for a hard step.

**TerrainSculpt** is the hand-edited layer. Brush strokes go into it rather
than into the heightmap, so you can change the generator upstream and your
carved riverbed survives.

**Gradient** is an image editor's gradient tool as a heightmap: *Linear*,
*Reflected*, *Circle*, *Ellipse*, *Square*, *Diamond*, *Angular* and *Spiral*.
The centred shapes are high in the middle and fall to their size - a
volcano's cone, an island's rise, a pyramid. *Start / end* moves where the
ramp begins and ends (a raised start is a mesa's flat top), *Profile* is the
curve between (Dome for a round hill, Bell for a wide soft one), *Repeats*
with *Past the end* turns one ramp into rings or ridges, *Terraces* cuts it
into steps, and *Distortion* pushes the geometric shape about with noise
until a circle is a coastline. Multiply one into a fractal to shape it.

The viewport draws the terrain's relief as a smooth surface, so ridges and
crater rims are curves rather than staircases, and anti-aliases the picture.
Close up, the finest detail is in the shading; the geometry carries what the
screen can resolve at that distance. Environment ▸ Planet & fractal detail ▸
*Detail roughness* decides how much of each finer size of that detail is kept:
low leaves smooth ground with its larger forms, high puts gravel over it.

While you drag a node's slider the terrain is evaluated at a low resolution
so the drag stays smooth; the view says **Preview at 256 px** until the full
terrain lands after you let go. Gullies and crater walls cast their own
shadows now - the shadow no longer needs the ground to drop a hundred metres
before it shows.

### The tile on its world

Terrain ▸ Placement decides how your tile meets the ground around it.

*Edge*, *Corner rounding* and *Edge wander* control the border. Left alone,
a square tile blends over a square border and the eye finds the square —
these round the corners off and make the outline wander, so what meets the
planet is a coastline rather than an outline.

*Flatten beneath* settles the planet's own relief under your features.
*Ground* is the altitude the tile sits at.

---

## 6. Materials

The Materials workspace assigns colour and surface to the ground.

Without a material the ground is painted by the world's own palette —
grass, meadow, forest, scrub, rock, snow — keyed on altitude, steepness,
latitude and wetness. That palette also paints everything beyond your tile,
which is why an untouched terrain still looks like a landscape and why the
tile and the ground around it match.

**MaterialLayer** stacks surfaces with a mask each: rock on the steep
ground, snow above a line, grass where neither. **SplatMaterial** mixes by
weights. **PBRMaterial** takes a full set of maps.

Materials are libraries too: save one, reuse it, or ask the AI panel for
one by description.

---

## 7. Water, air and cloud

The Atmosphere workspace.

**The sun** has a size as well as a direction. *Sun size* is its angular
width in degrees — our own Sun is 0.53, about a little finger held at arm's
length, and so is the Moon, which is why an eclipse fits so exactly. A larger
sun is a nearer or bigger star: it spreads the same light over more sky
rather than adding any, and it softens every shadow edge in the scene.

*Sun glow* is the aureole — the bright halo that is not the sun but the air
in front of it, throwing light forward past the dust and droplets it meets.
It is what makes a hazy sky glare and a clear one not, so it grows with the
atmosphere's density and disappears in vacuum. *Glow spread* is how far it
reaches: tight is clean mountain air, wide is haze or smoke.

The ball you can grab to move the sun is a handle, not the sun. It shows in
views that draw their furniture — the same switch as the selection outlines
— and never in a capture or a render.

**Water** is a level, not an object: everywhere the ground is below it fills,
and it is one sea over the whole world — across the terrain tile, the ground
beyond it and out to the horizon, like Vue's infinite water plane.

Its waves are raised by a wind, as a real sea's are. *Wind intensity* (metres
a second) decides how long the waves are and how much of each size there is:
4 is a breeze on a lake, 15 a gale, 0 a mirror. *Wind direction* is the way
they run. *Wave height* and *Wave scale* multiply what the wind makes,
*Agitation* is how fast they move, and *Choppiness* takes them from round
swells to sharp crests. With *Displaced water surface* on, the waves are real
geometry — a big swell hides the shore behind it; off, they are in the
shading only. The panel says how high the waves come out, in metres.

*Clarity* is how many metres of water the clearest colour carries through;
*Shallow color*'s proportions decide which colours the water swallows first,
so the shallows over sand show turquoise and deep water shows its *Deep
color*. Foam comes two ways, as in Vue: *Coast foam* wherever the water is
shallower than the *Typical depth* over the ground, a rock or anything else
standing in it, and *Crest foam* where a wave gathers in and breaks, as much
of each crest as *Crest coverage* says.

**Atmosphere ▸ Height** is how far up the air reaches over the world's
surface. **Thins by** is the scale height — how far up the air thins by a
factor of e. Air has no top, it fades, and this is what makes a planet seen
from space have a soft blue band rather than a drawn line. Earth's is about
an eighth of its visible air. Straight up from the ground looks the same
whatever you set here, so changing it does not change your existing sky.

**Clouds** are layers on the world. *Volumetric* marches them properly;
turned off they are drawn as one flat sheet, which costs about a third as
much and, for a high overcast seen from below, arrives at much the same
picture. *Weather* and *Weather scale* open and close the cover the way a
front does — leave these at zero and a deep sky shows the cloud volume
repeating in a grid.

A cloud layer is the same clouds from wherever you look: from under it, in
front of a mountain that rises into it, from above its tops, and from orbit,
where the weather gathers into systems hundreds of kilometres across. They
drift with the wind and slowly build and thin as they go. The water reflects
that same sky, clouds included, blurred by how rough the waves are — a calm
lake mirrors the clouds overhead, a breezy one smears them. Anything glossy
reflects the same sky: wet rock, a polished object, a material with a high
*Global reflectivity* shows the clouds overhead in it, blurred by its
roughness. While nothing else is happening the views keep redrawing the
drifting clouds and the sea at Settings ▸ Performance ▸ *Sky and sea motion
rate* (30 fps; 0 lets them fall to the idle rate).

The viewports work the clouds in front of the ground and the nebulas out at
half size and bring them back up to full size, which is most of what they
cost; a capture and a render work every pixel.

**Fog** is a participating medium, not a colour wash: extinction,
scattering, and a height profile. See [VOLUMETRICS.md](VOLUMETRICS.md).

**Deep space** beyond the air: stars, a Milky Way, and nebulas marched as
volumes. Environment ▸ Space has the count, brightness and how realistic
against how dramatic you want them. The stars' diffraction spikes belong to
the camera, so they share one *Spike angle* and come in 4, 6 or 8 *Spike
points*, with a *Spike colour fringe*; *Star saturation* and *Star glow* set
their colour and bloom. *Bright stars* is how many of them are bright ones,
and *Star clusters* scatters crowds of stars born together — loose blue open
clusters and tight yellow globulars, each resolving into its stars as you
zoom in — at *Cluster size* degrees across.

**Other planets** have weather of their own: a planet's *Cloud cover* (in its
Properties, 0 clear to 1 overcast) lays a deck of cloud over it that drifts
with the scene's clouds and shadows the ground under it. A world with no
atmosphere has none.

A nebula (select it in the Objects tree) can show the *Hot stars* that light
it, burn its heart toward white (*Core glow*), tear into filaments
(*Turbulence*), carry thin *Dust lanes* and take an *Outskirts* colour. The
order of nebulas in the tree is their depth: a dark nebula after a bright
one hides it. Space preset **Nebula poster** puts all of that in one sky.

---

## 8. Cameras and the Preview

Cameras are real optics. Sensor format, focal length, aperture, shutter and
ISO; the exposure follows from them, and depth of field and vignetting
follow from the lens.

Add one from the Cameras workspace or the Objects menu. A viewport can look
through any camera — its gear menu — and any viewport can hand its view to a
camera with the small camera button in its header, which copies the eye,
the target and the whole lens.

The **Preview** panel renders one camera continuously at reduced size. It
takes the mouse exactly as a viewport does, and with Auto-key on, flying a
camera writes its keys.

**Lens flare** (a camera's Optical simulation) sits on the sun and is dimmed
by whatever stands in front of it — a ridge, a planet, a cloud bank. *Flare
style* chooses *Classic ghosts*, *Cinematic* (a hot core, a starburst of
rays, a coloured ring and a chain of hexagonal reflections through the middle
of the frame) or *Anamorphic* (the same with a streak across the frame);
*Rays*, *Streak*, *Ghosts* and *Halo* dial each part, and *Flare shape* holds
the details: the core, how many rays and how long, the streak's length and
tint, the halo's radius, how many ghosts, the aperture's blades (the ghosts'
polygon), how far the colours part, and a seed. Every one of them can be
keyed on the timeline.

**Bloom** is the glow a lens spreads round everything bright — the sun, a
glint on water, snow in sunlight. It is off until you raise it; *Bloom from*
is how bright a part of the picture must be before it glows, and *Bloom size*
how far the glow spreads.

---

## 8a. Plants and rocks

**The Plants workspace** is the plant library. Its window shows a picture of
every plant you can place, on shelves - trees, shrubs, ground cover, flowers,
grass, deadwood, rocks - with a search box and a choice of where they come
from:

- **Built in**: the seven plants the studio builds itself (below).
- **Free (Poly Haven, CC0)**: photoscanned trees, saplings, shrubs, ferns,
  grass, flowers and dead wood, free for any use. They are not part of the
  download: **Get the free plants** (in the window, the tool row or the
  Plants menu) fetches them once, about 320 MB, into your library folder
  (`%LOCALAPPDATA%\GeekatplayTerraForge\library\plants`), each with a note
  of who made it. Running it again only fetches what is missing. Many of them
  come as a set - one fern file holds four ferns - and the details pick which
  one you place.
- **My models**: **Record a plant model...** adds a glTF, FBX or OBJ plant
  you own - say, one you exported from a plant tool you have a licence for -
  without copying it. Keep content whose licence forbids sharing on your own
  machine; TerraForge never bundles it.

Click a picture for its details: how tall, how many triangles, its licence
and author, its variants. **Add to the scene** (or double-click) stands it on
the ground under the point your view is turning around, at its real size,
with a node in the graph that drives it, turned at random if you like and
bigger or smaller with the size slider. **Scatter** covers the terrain with
copies of it through a Scatter points node, whose density, mask and spacing
you then set as for any population. A heavy tree loads in the background; the
window says what is loading. A plant stands on the ground as the viewport
draws it, grit and all, and renders standing on it too.

**The Plant Editor** grows plants from rules instead of loading them as
models. Open it from the Plants menu or the tool row; entering the Plants
workspace opens it beside the library.

Type a plant's name into *Grow a plant* - "old oak", "date palm", "lavender",
"dead pine in winter", "saguaro" - and press Grow. Over a hundred plants are
known by name, with their usual height, crown, bark, leaf shape and colour,
whether they keep their leaves through winter, and when they flower and
fruit; words like *old*, *young*, *dry*, *dead*, *thriving*, *tall*,
*weeping*, *in autumn* change what you get. The plant appears standing on the
ground under your view, and its species appears in the graph as parts you can
now change. Tick *Ask the AI model* for what the table does not know - a
cultivar, a local species, a plant in a photograph you point it at - and the
text model writes the botanical description first; everything after that is
the same. *New* builds a species of a kind (broadleaf tree, conifer, palm,
fern, cactus, bamboo, mushroom and a dozen more) without naming a plant.

A species is a small graph: a **species root** with the trunk growing into
it, branches growing into the trunk, twigs into the branches, and leaves,
flowers and fruit at the ends, each part wearing a material. The **Parts**
tree is that graph read as a tree: click a part to see all of its parameters
in the Properties editor, right-click it to add a part to it, to stop it
growing, or to delete it. The row of buttons above the tree adds a part from
a preset - trunk, branch, stem, palm frond, twig, leaf, billboard leaf,
growth, flower, cut-out leaf, fruit, bark material, leaf material - to
whichever part is selected.

**The individual.** A species grows a different plant from every seed. *New
variation* draws another one; *Flag this plant* keeps the seed of one worth
returning to, and the flagged seeds sit beside it as buttons. Age, health and
season are sliders on the root: age scales the whole plant and thins its
crown, health browns it, droops it and sheds its leaves, and the season
decides whether it is in leaf, in flower, in autumn colour or bare.

**Wind.** The root carries a wind - a strength, a direction, gusts, and how
much the leaves flutter - and the viewport sways the plant in it; *Preview in
the views* stills it while you work. The wind is baked into the plant, four
numbers per vertex, so a render and an export move it exactly as the viewport
does and the foot of the trunk never slides.

**Presets** keep an age, a health, a season, a seed and the parameters you
published (any number on any part, from its menu in Properties) under a name:
"sapling", "winter veteran", "in flower". *Save...* puts the species in the
plant library, where it joins the shelves and is placed and scattered like
any other plant. *Export...* writes the grown plant as a mesh: .glb with its
pictures inside it, or .obj with an .mtl beside it.

**Its parameters.** Every number on a part is more than a number. It carries
a spread - how far one individual strays from the next - a rule for when a
new draw is made (each time, per part, per plant, per ancestor), and two
curves that shape it: one along the part, one by where the part sits on its
parent. The two small graphs beside the number open them. A curve parameter -
a radius profile, a density along a branch, a presence over the year - is
drawn in place: drag a key, double-click to add one, right-click to remove
one.

**Pictures made from rules.** A plant material can generate its own leaf,
petal or bark picture from a shape, a serration, veins and a colour, so a
species needs no files at all and travels as one small folder. Give it your
own pictures instead whenever you have them.

Everything here is scriptable: `plant_species_new`, `plant_species_describe`,
`plant_species_set`, `plant_species_variation`, `plant_species_individuals`,
`plant_part_add`, `plant_species_preset`, `plant_species_save`,
`plant_species_load`, `plant_species_list` and `plant_species_export`, from
the assistant, the Python API and MCP alike.

Objects ▸ Add component ▸ **Plants**: *Pine*, *Juniper*, *Palm*, *Fern*,
*Grass tuft*, *Bush* and *Boulder*. They are built by the studio rather than
loaded from files, so a scene that uses them carries no model files; each
comes at its real size (a pine about 22 m, grass under a metre) with its bark
and foliage in their own colours. Scatter any of them over the terrain with a
Scatter points or Ecosystem layer node and *set_scatter*, as with any mesh.
The Primitive node's *Plant seed* grows another one of the same kind. Two
worked scenes build a whole population from them: File ▸ Run macro
`examples/macros/desert_junipers.json` (junipers, boulders and bunch grass on
sand) and `jungle_palms.json` (palms, bushes and ferns).

The offline engines get each plant's bark and leaves in their own colours
too, and an imported model shades smooth where its surface is smooth while
keeping its hard edges. Leaf cards keep their leaves with distance instead of
thinning to bare twigs. The built-in sphere, cylinder and cone shade smooth
at any detail.

---

## 9. Rendering

Render ▸ settings, or a camera's own render assignment.

**Engines.** The viewport's own renderer is instant and is what you have
been looking at. The offline engines — Mitsuba 3 as a path tracer, Cycles,
LuxCore — trace light properly: real shadows, real bounce, real depth of
field.

**What the offline engines are given** is the world the viewport draws: your
tile placed on its planet, the ground beyond it out to the horizon, the sea,
all on the world's curve, painted by the same palette. The tile wears the
material the viewport shows on it, giving way to the planet's palette at its
border exactly as on screen. A render through a camera frames exactly what
that camera's viewport framed. Cycles now receives the same world as Mitsuba -
the ground to the horizon, the sea as a mesh, the sky turned the right way.
LuxCore does too (`pip install pyluxcore`): the same world, sun and sky,
developed like Mitsuba's. Until 11 September both Mitsuba and Cycles hung the
viewport's sky half a turn round - the clouds and the sun's glow you framed
were behind the camera - and now put it where the viewport does.

**What you see is what you get.** The viewport and the render are developed
the same way: the same measured skylight, the same camera exposure and film,
the same sky. What remains is that the path tracer bounces light between
surfaces and the viewport does not, so a bright landscape is a little
brighter in the render. Environment ▸ Atmosphere ▸ **Skylight** is how much
of the sky's measured light reaches the ground - 1 is all of it, which is
what the render uses.

**Passes.** Depth, normal, albedo, direct, ambient, specular, shadow,
atmosphere and more, as EXR. **Panorama** renders a full sphere.
**Batch** renders every camera, or a named set, unattended.

---

## 10. Animation

Any numeric property can be animated. The small circle before a property is
its track: click to key, right-click for its menu. Turn on **Auto-key** and
editing a property that already has a track writes a key at the current
time — including flying a camera with the mouse.

The Timeline holds tracks, keys and sequences. See
[ANIMATION.md](ANIMATION.md).

---

## 11. Talking to it

Three surfaces, all equivalent:

- **The AI panel**, in every workspace: describe what you want in words.
- **The console**, for typed commands.
- **The scripting API and MCP tools**, for other programs — everything the
  interface can change, a script can change. See
  [AI_SCENE_GUIDE.md](AI_SCENE_GUIDE.md).

---

## 12. Files

Projects are `.gpxt`. They hold the graph, the scene, the world and the
settings — not the rendered images. Autosaves go to your local application
data folder and the application offers the most recent one if it did not
close cleanly last time.

Layouts, material libraries and assets live beside them and are shared
between projects.

---

## 13. When something looks wrong

**The ground stops on a hard line.** The view is drawing the world flat.
Turn *Planet curvature* on in that viewport's gear menu, or use a camera
view, which always curves.

**A square shows around the terrain.** Terrain ▸ Placement: raise *Corner
rounding* and *Edge wander*. If it persists, the tile's own material may be
fighting the palette around it.

**The render looks nothing like the preview.** The framing should match
exactly. If it does not, check that the render is aimed at the camera you
think — a camera render uses that camera's optics, a bare panel render uses
whatever the viewport is doing.

**Everything is grey and there are no stars.** A shader failed to compile.
The Console says which; the log in `logs/` keeps the message.

**It is slow.** [TERRAIN_PERFORMANCE.md](TERRAIN_PERFORMANCE.md) and
[LOD.md](LOD.md) explain what costs what. The usual levers are resolution
scale, shadows, cloud steps and whether clouds are volumetric.

---

## Where to go next

- [INTERFACE.md](INTERFACE.md) — every panel and control in detail.
- [NODES.md](NODES.md) — every node in the registry.
- [VOLUMETRICS.md](VOLUMETRICS.md) — fog, cloud and volumetric materials.
- [ECOSYSTEM.md](ECOSYSTEM.md), [GRASS.md](GRASS.md),
  [STONE_FIELDS.md](STONE_FIELDS.md) — populating the ground.
- [ANIMATION.md](ANIMATION.md) — the full animation system.
- [AI_SCENE_GUIDE.md](AI_SCENE_GUIDE.md) — building scenes by description.
- `../examples/` — worked projects and macros.
