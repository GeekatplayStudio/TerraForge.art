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

**Workspaces** run along the top: Terrain, Materials, Objects, Atmosphere,
Lighting, Cameras, Animation, Render. Each is a saved arrangement of panels
for one job. Switching between them does not change your scene, only what
you are looking at.

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

**Water** is a level, not an object: everywhere the ground is below it fills.
Depth colour, clarity, roughness and foam are in its properties.

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

**Fog** is a participating medium, not a colour wash: extinction,
scattering, and a height profile. See [VOLUMETRICS.md](VOLUMETRICS.md).

**Deep space** beyond the air: stars, a Milky Way, and nebulas marched as
volumes. Environment ▸ Space has the count, brightness and how realistic
against how dramatic you want them.

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

---

## 9. Rendering

Render ▸ settings, or a camera's own render assignment.

**Engines.** The viewport's own renderer is instant and is what you have
been looking at. The offline engines — Mitsuba 3 as a path tracer, Cycles,
LuxCore — trace light properly: real shadows, real bounce, real depth of
field.

**What the offline engines are given** is the world the viewport draws: your
tile placed on its planet, the ground beyond it out to the horizon, the sea,
all on the world's curve, painted by the same palette. A render through a
camera frames exactly what that camera's viewport framed.

**One honest difference.** The path tracer lights the ground from the whole
sky; the viewport multiplies one averaged sky colour by the ambient dial.
The physical answer is several times brighter, so a path-traced frame comes
out lighter than its preview. The preview is the approximation, not the
render.

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
