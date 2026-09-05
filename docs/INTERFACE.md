# The interface

How the studio's windows are laid out and what each control does — the
Objects panel (Cinema 4D's Object Manager), the tool palettes and their
icons, the viewport header, the status bar, languages. Panels that have their
own chapter (the Material Studio, the Timeline and Curve editor, the Settings
window) are linked from here.

## Layout

Three rows sit above the workspace:

1. **The menu bar** — File, Edit, Terrain, View, AI, Help — with the global
   tool palette on the same row: Undo / Redo · Recompute · Sculpt · Move,
   Rotate, Scale, Twist, Bend, Skew, Taper · Gizmos on/off · Console.
2. **The workspace tabs** — Terrain, Materials, Atmosphere, Render, All
   domains, Objects, Lighting, Cameras, Animation. Each workspace remembers
   its own arrangement of windows (`layouts/workspace-<name>.json`).
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
textured), the overlay group (sky, water, grid, outlines) and the gear for
that view's settings. When the viewport is too narrow the groups collapse to
two combos, then to the gear alone.

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
