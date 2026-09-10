# The interface

How the studio's windows are laid out and what each control does — the
Objects panel (Cinema 4D's Object Manager), the tool palettes and their
icons, the viewport header, the status bar, languages. Panels that have their
own chapter (the Material Studio, the Timeline and Curve editor, the Settings
window) are linked from here.

## Layout

Three rows sit above the workspace:

1. **The menu bar** — File, Edit, then one menu per workflow in the order
   the work runs: Terrain, Objects, Materials, Atmosphere, Animation,
   Render — then View, AI, Help. Every command on a tool row or in a panel
   is also in its workflow's menu. **View > Console** opens the terminal
   (shorthand `op key=value ...` or JSON; `help` prints the schema) and
   **View > Performance watcher** toggles the frame analyser that writes
   `logs/perf_watch.json`.
2. **The workspace tabs** — Terrain, Materials, Atmosphere, Render, All
   domains, Objects, Lighting, Cameras, Animation. Each workspace remembers
   its own arrangement of windows (`layouts/workspace2-<name>.json`,
   captured when you leave it and at exit, put back at startup).
3. **The tool row** — the commands that belong to the active workspace
   (resolution presets and sculpt for Terrain; primitives, planet, infinite
   terrain and mesh tools for Objects; the transport for Animation; …).

Below them the dockable windows: viewports (up to eight), the node editor,
Properties, Objects, the Timeline, the Preview panel, the Material Studio.
Every window floats or docks from the small button in its corner; named
layouts are saved from View > Layouts.

At the bottom, the **status bar**: the last message on the left, and on the
right the frame's health — FPS shown / possible, the frame breakdown, the
last evaluation, RAM, CPU, VRAM, the GPU, and the governor's state (see
"Health at a glance" in the README).

## Icons and their size

The tool palettes and the viewport header use one vector icon set drawn in
Cinema 4D's language: thin line glyphs, filled silhouettes for object types,
the four-way arrow for Move, the circular arrow for Rotate, the box with a
corner arrow for Scale. They are drawn from paths, so they are crisp at any
size and in any theme.

**Settings > General > Icon size** chooses Small (18 px), Medium (26 px) or
Large (36 px) — the three sizes Cinema 4D's palettes offer. Row heights in
the Objects panel follow the same choice (Show > Icon size).

Every icon button shows its name and shortcut in a tooltip; nothing is
icon-only without a hover.

## The Objects panel (Object Manager)

`Window > Objects` — the scene as a tree: terrain, water, sun, atmosphere,
meshes, groups, cameras, lights, planets and infinite-terrain layers.

### A row, left to right

```
[ connector lines ▸ type icon  Name ] │ ● ○  ✓ ⦿ │ [M] [N] [S]
                                        │ ●  ○      │
```

- **Connector lines** show the hierarchy; ▸/▾ folds a branch (Ctrl-click
  folds or unfolds the whole subtree).
- **Type icon**, then the **name**. Double-click, F2 or Return renames in
  place; Up/Down move to the next row while still editing.
- **Layer swatch** — the colour of the object's layer. Click it to move the
  object to another layer; layers and their colours are managed in
  Properties > Scene.
- **Two visibility dots**, upper = viewport, lower = render. Each has three
  states, exactly as in Cinema 4D:
  - grey — inherit from the parent (at the root: visible);
  - green — shown, even if the parent is hidden;
  - red — hidden, even if the parent is shown.
  Click cycles grey → green → red. Ctrl-click applies the state to every
  child. Hold the button and drag over other rows to paint the same state
  onto them.
- **Enabled tick / cross** — whether the object contributes at all.
  Alt-click toggles the whole subtree.
- Cameras get a **look-through** button.
- **Tags** on the right: `M` a material is assigned (click to jump to its
  node), `N` the object is driven by a graph node, `S` the mesh is scattered
  by a Points node.

Rows read their state: the selected row is highlighted, the last selected
one slightly brighter, unselected children of a selected object darker,
hidden objects faded.

### Selection

Click selects; Ctrl-click adds; Shift-click selects a range; middle-click
selects an object with its whole subtree. The Properties panel follows the
last selected object.

### The header strip

- **Search** filters rows by name (it does not select; X clears).
- **Filter** hides whole object types (an eye per type).
- **Show**: Flat tree · Group by layer · Sort by name · Show tags · Icon
  size.
- **Path bar** — with a big scene, *Set as root* (context menu, or Alt +
  double-click a group) descends into a subtree; the path bar shows where
  you are, Up one level and Home come back out.

### Drag and drop

Drag a row between two others to reorder (an insertion line shows where);
drop it on another to make it a child (the target row is coloured). Ctrl-drag
copies. A move that would put an object under its own child is refused.

### Context menu

Rename · Duplicate · Delete · Group selected · Unparent · Set as root · Move
to layer ▸ · Viewport visibility ▸ · Render visibility ▸ · Enable/Disable ·
Look through (cameras) · Fly here (planets) · Select children · Fold /
Unfold all.

## The viewport header

Each viewport has, at the right of its title: the projection group
(perspective, top, front, right), the shading group (wireframe, shaded,
textured, ID colours), the overlay group (sky, water, grid, outlines) and
the gear for that view's settings. When the viewport is too narrow the
groups collapse to two combos, then to the gear alone.

**Navigating.** Left drag orbits. Shift+left drag, middle drag or right
drag pans. Ctrl+left drag, Alt+right drag or the wheel dollies. The Maya
set — Alt+left orbit, Alt+middle pan, Alt+right dolly — works as well. A
right click that did not travel opens the view menu; a left click that did
not travel selects what is under it. Every drag is logged at trace level
(`[viewport] view N drag: ...`) so a navigation report can be read from
the console.

**ID colours** paints every object (or every material — the gear chooses)
in one flat bright colour, for reading masks and distributions.

**A pinned view.** Pinning a node to a view (the node's context menu, or
`view_node` from a script) makes the view show that node's result instead of the terrain output. A
padlock button appears in the header and a badge in the overlay while the
pin is on; click the padlock to return to the terrain. A view that seems
not to follow your edits is usually pinned.

**New viewport through camera** in the gear's menu opens another view
looking through any scene camera; **Free orbit** releases it. The tab's own
context menu belongs to the docking system, so these live in the gear.

## Languages

Every visible string goes through the translation table. **Settings >
General > Language** lists English (built in) and every
`resources/lang/<code>.json` found next to the application; German and
French ship. A tag that a language file does not translate falls back to
English, and the Settings window reports how many are missing so a
translation can be completed.

## See also

- [Animation](ANIMATION.md) — the Timeline, the Curve editor, the animation
  circle.
- [Layered materials](MATERIAL_LAYERS.md) — the Material Studio.
- [AI services](AI_SERVICES.md) — the Settings window's service tabs.

## World shape

Objects ▸ Home planet ▸ *World shape*: **Shape** (Globe, Ring world, Dyson
sphere, Flat world - a preset that sets the switches below), **Ring width**
(a ring only: how far it reaches north and south of the tile; beyond its
rim is space), **Outline** and **Width** (a flat world only: a disc or a
square that wide, with nothing beyond its edge), **Thickness** (0 is a
skin; above it the other face lies that far below the ground - a ring's
outside, a flat world's underside, a globe's inner crust - and a ring or a
flat world is drawn as a body: both faces and the rim wall between them),
**Ground on the inside** (the centre is above the tile: the surface rises
away from you and the far side of the world is drawn overhead; every view
curves an inside world) and **Sun inside** (the sun is a body on the ring's
axis or at the sphere's centre, straight above the tile; the Sun object's
angles are not used while it is on). The atmosphere, the cloud layers,
their shadows and the water lie on the surface whatever its shape: on a
ring the clouds are a band round the inside of the ring, ending at its rim,
and the sky stands over the ring's ground. A terrain tile's Placement
section and a surface layer's tab carry **Side of the world**: the world's
own face, or the other face of the same shell, where the heights go the
other way - a globe's other face is the inside of its crust, seen from
within the hollow planet. A tile on the other face is placed against that
face's own layers. The Placement section's **Blend** also offers **Clip
low** (the tile stands only where it rises above the planet's own ground;
its low edges are cut away, so a mountain sits in the landscape without a
skirt) and **Clip high** (only where it lies below; a basin is carved into
the ground). Script: `set_viewport` with `world`, `world_shape`,
`world_inside`, `world_width`, `world_sun_inside`, `world_thickness`,
`world_outline`, `place_mode` (3 clip low, 4 clip high); `place_object`
with `side`.

## Atmosphere height and deep space

Environment ▸ Atmosphere ▸ **Height** is how high the air reaches over the
world's surface, whatever its shape; beyond it is space. From high enough
the sky thins to stars and the world shows a blue rim; on a ring the air
is a band inside the ring, seen from outside through its opening. 0 keeps
the old rule of sky everywhere thinning with distance. Environment ▸
**Space** is the whole backdrop. *A whole sky at once* holds six named
skies - **A dark night sky**, **Hubble**, **Science fiction**, **Deep
field**, **Star nursery**, **Empty** - each of which sets the star field,
the band and the palette together, and **Fill the sky**, which scatters
*How many* (up to eight) nebulas and galaxies over it, of a chosen kind,
spread apart and varied; *Arrangement* is the seed. Filling again replaces
what the last fill made and leaves anything placed by hand.

*The look*: **Realism** is the dial the whole backdrop turns on - 1 is a
photograph, hydrogen's crimson with doubly-ionised oxygen's teal where the
gas is hardest lit; 0 is what a film paints, cyan and magenta with gold
cores. It grades what is already in the sky and chooses the colours
anything new is born with. **Brightness**, **Glow** (the halo a long
exposure spreads round bright gas) and **Quality** (how far the march
through a nebula steps: 12, 22, 40 or 72 samples) sit beside it. Only the
pixels a nebula covers pay for the march, and only where space is visible
at all, so a daylit scene costs nothing.

*Stars*: density, brightness, size, colour spread, **Diffraction spikes**
(the four arms a telescope's vanes cut across the brightest), **Halo**,
**Clumping** (stars gather into associations rather than sprinkling
evenly), seed. *Galaxy band*: intensity, width, pole heading and
elevation, core position, **Dark rifts** (dust that reddens as well as
dims), **Star haze** (the unresolved stars that make it milky), tint,
seed.

Nebulas and galaxies are objects: Add tile ▸ Space ▸ Nebula, Dark nebula,
Spiral galaxy, Elliptical galaxy, Planetary nebula, and Moon - a small
airless cratered world (a planet whose surface layer style is Craters). A
nebula's Properties: kind, azimuth, elevation, size, rotation, tilt
(galaxies), seed, brightness, density, detail, arms, and - for the clouds,
which are marched as real volumes - **Dust**, **Shape** (how far it is
pulled out of a ball), **Glow** and **Hot stars** (how many young stars
inside light it; their glare is what decides where it is teal and where it
is red). *Colours from the Realism dial* takes both colours from where
that dial stands. Eight are drawn at once. Script: `set_sky` with `height`
(tile units) or `height_m`; `space_preset`; `space_populate`; `set_space`;
`add_nebula` / `set_nebula` (with `palette:"auto"`); `add_moon`;
`add_infinite_terrain` with style `craters`.
