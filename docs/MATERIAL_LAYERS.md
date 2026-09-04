# Layered materials, fractal colour, and the Material Editor

## Why this exists

Two requests drove this work:

1. *"fractal colors, where we have fractal that can connect to the color map to
   produce texture"*
2. *"to any single terrain/object we can assign many multiple materials as
   layers and we can control those layers with functions, position, angle,
   orientation, maps ... like layers in Photoshop with alpha/mask options only
   now they can also be controlled by angle, orientation, roughness, or connect
   to the terrain shape"*

plus a dedicated editor screen rather than nodes alone.

The Vue Reference Manual was read for prior art (pages 683-770 for the Material
Editor and the layer/mix model, 851-856 and 902-906 for fractals and colour
creation, 1030-1041 for the filter and colour-map editors). What follows records
which of Vue's ideas were adopted, which were rejected, and what TerraForge does
that Vue does not.

## What already existed

Worth stating plainly so this document is not read as a bigger change than it is.

| Piece | Where | What it does |
| --- | --- | --- |
| `MaterialOutput` | `nodes_material_graph.cpp` | Six channel inputs; a scene object points at it |
| `MaterialStack` | `nodes_erosion_layers.cpp` | Six mask/albedo pairs, height-aware weighted mix, albedo + roughness out |
| `ErosionLayers` | `nodes_erosion_layers.cpp` | Erosion side channels to seven masks that sum to one |
| `FieldDistribution` | `nodes_field_material.cpp` | Altitude, slope and orientation bands in the field domain |
| `ColorizeGradient` | `nodes_texture.cpp` | A heightmap through a colour gradient |
| Fractal core | `gpx/fractal_core.hpp` | A Vue-grade fractal with base noise, landscape, profile and variable roughness |
| Materials panel | `panel_materials.cpp` | Library, preview sphere, channel table, AI generation |

The gap was not fractals and not gradients. It was that nothing joined them, and
that `MaterialStack`'s normalised weighted sum cannot express the one thing a
layer stack is for: *this layer is absent here, so fall through to the one
below.*

## The three pieces added

### 1. `FractalColor` — a fractal wired to a colour map

Vue's procedural colour pipeline, verbatim in structure (manual p711): for each
point a function gives a number, a filter reshapes it, and a colour map turns it
into colour **and alpha**. That last part is the interesting half. One fractal
drives both what a layer looks like and where it is present, which is why the
node has two outputs.

- **Inputs** `warp` and `mask`, both optional. `warp` displaces the sample
  position, which is Vue's "plug the Origin parameter into a Turbulence node"
  idiom. `mask` multiplies the output alpha.
- **Outputs** `texture` (RGBA, alpha straight from the gradient) and `mask`
  (the filtered scalar, so a downstream layer can key its presence on the same
  pattern that coloured it).
- **Filter** is `bias` and `gain`, the two-parameter shaping pair, rather than a
  full editable curve. A curve editor is a separate piece of UI and is not
  pretended at here.

### 2. `MaterialLayer` — one layer, chainable

Nodes chain bottom-up: each layer takes the accumulated result below it and
returns the result including itself. Vue describes the same operation top-down
with an early-out at full opacity; the arithmetic is identical.

**Presence** is the load-bearing concept, and it is deliberately kept separate
from both the mask and the channel data:

```
presence = enabled * opacity * mask * band(altitude) * band(slope) * band(orientation)
```

The three environment bands multiply. The manual never states how Vue combines
them, so this is a choice, not a reading: it matches `FieldDistribution`, which
already multiplies, and it gives the intersection semantics a user expects from
"steep **and** high".

Each band is a two-sided smoothstep whose edges soften by a fuzz parameter, and
a fuzz of zero gives a hard test. That is `FieldDistribution`'s `band()`, reused
rather than reinvented.

- **Altitude** comes from the terrain input, normalised over the map, or from
  world height when the frame is absolute.
- **Slope** is the terrain gradient in degrees, zero flat and ninety vertical.
- **Orientation** is the compass azimuth the surface faces, with a tightness
  controlling how wide the favoured arc is.

**Placement** is per-layer: tiling, offset and rotation applied to the sample
coordinates of that layer's own maps. This is the "position, angle, orientation"
half of the request.

**Blend modes** adapt Vue's blending methods to a raster pipeline:

| Mode | Behaviour |
| --- | --- |
| Normal | Alpha-over. The default and the honest one |
| Cover | Colour switches without a ramp inside the transition band; only the normal transitions |
| Colour only | Takes colour from this layer and keeps everything else from below |
| Add / Multiply | The usual two, for stains and tints |

**Normals accumulate or replace**, per layer, on a 0..1 dial. Vue makes the same
distinction for bump and displacement and it matters: a lichen layer's normal
should add to the rock beneath it, a snow layer's should flatten it.

### 3. The Material Editor screen

A dockable window, opened from `Window > Material Editor`. It reads the
`MaterialLayer` chain upstream of the selected object's `MaterialOutput` and
presents it as a stack, top layer first, the way every image editor does.

- Per row: visibility, name, opacity, blend mode, and a presence summary so the
  reason a layer is invisible is readable without opening it.
- Add, duplicate, delete, and move up or down, all of which rewire the node
  chain and push undo.
- The selected layer's full controls appear beneath the list, grouped as
  Presence, Placement and Channels.
- The existing turntable preview sits at the top, unchanged.

The nodes remain the truth. The editor is a second view of the same graph, so
anything done in one appears in the other.

## What was deliberately not copied from Vue

- **Shared layers.** A layer edited in one material changing it in every other
  material that uses it is a genuinely good feature and a genuinely large one.
  It needs a reference-counted sub-graph and a "make unique" operation. Not now.
- **Grouped materials.** The manual never says how a grouped material chooses
  among its members, so there is nothing to implement against.
- **The three-state visibility switch** (normal / hidden / flat highlight
  colour). Two states are implemented. The flat-colour debug view is worth
  having later.
- **Painted presence.** Vue lets you paint a layer's alpha with the terrain
  sculpt brushes. TerraForge has sculpt brushes and a `Field` attribute, so this
  is reachable, but it is its own piece of work.
- **A filter curve editor.** `bias` and `gain` cover most of what a curve is
  used for here.

## Known limits

- `MaterialLayer` composites albedo, normal and roughness. Metallic, height and
  ambient occlusion pass through untouched, because the viewport renderer does
  not consume metallic or AO maps yet.
- Presence is evaluated on the heightmap the layer is given, before any
  displacement. Vue makes re-evaluation after displacement an opt-in checkbox;
  there is no equivalent here.
- Orientation has a discontinuity at due south, as it does in Vue, because it is
  an angle mapped to a line.
