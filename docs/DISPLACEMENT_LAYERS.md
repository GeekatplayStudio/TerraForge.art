# Rocks and grass as displacement layers

How Terragen builds a rocky, grassy surface without placing a single object,
what TerraForge does about it, and how to build the same thing here.

## What Terragen does

Terragen's surface is a stack of **Surface Layers**. A layer decides *where*
it is — by altitude and slope, each with a fuzzy zone, and by an optional
mask — and *what* it does there: its colour, and, through a child
**displacement shader**, its relief. The relief is applied to the actual
surface at render time, so a layer does not merely paint rocks; it raises
them, and everything above it (shadows, further layers, populations) sees the
raised ground.

Three shaders do most of the work:

- **Fake Stones** — a displacement shader that scatters boulders: a density,
  a stone scale, a pancake (flattening) factor and a size variation. Put it
  as the child of a surface layer restricted to slopes and you have scree
  where scree belongs.
- **Power Fractal** as displacement — at metre scale it is rock; at a few
  centimetres, ridged, with a small amplitude and "displacement direction:
  along normal", it is the texture of ground and the base of *displacement
  grass*: a dense high-frequency relief on the flats, coloured green, that
  reads as grass from any distance a population would be too heavy for.
- **Displacement spike limit** and **Intersect underlying** on the layer —
  the first stops high frequencies on steep ground, the second lets a
  layer's coverage favour the hollows or the crests of the relief below it.

The ordering rule is the point: a layer's relief is composited by the
layer's coverage, on top of whatever the layers below already raised.

## What TerraForge does

The same model, on the node graph:

- **`MaterialLayer` carries a displacement channel.** Inputs *displacement*
  (this layer's own relief, in heightmap units) and *below displacement*;
  output *displacement*. The relief is composited exactly as the colour is:
  by the layer's **presence** — opacity, mask, altitude, slope and
  orientation bands — and on the *Add to displacement below* dial it either
  stacks on the layers below (1) or replaces theirs where it is present (0).
  *Displacement* multiplies the input.
- **`MaterialOutput` has a *displacement* input.** Connect the top layer's
  output there and the material's relief travels with the material: assign
  it to the terrain and the studio adds the relief to the ground the
  viewport shows — *after* the tile has been placed on its planet, because
  placement only keeps tile relief larger than its `presence` threshold and
  would swallow every stone — so the stones cast shadows, an object placed
  on the terrain stands on the raised ground, and every viewport agrees.
- **Two displacement sources are made for layers:**
  - **`FakeStones`** now has a *displacement* output — the stones alone,
    output minus input — beside its displaced *output* and its
    *stone_mask*. Terragen's Fake Stones, as a layer's child.
  - **`GrassDisplacement`** is new: a field of tufts — domes on a jittered
    cellular lattice, gathered into clumps by a larger noise, ragged at the
    tops by a finer one — raised only on ground whose slope is inside a
    band. Outputs *displacement* (the tufts), *grass_mask* (where they are,
    for the layer's colour and mask) and *output* (the ground plus the
    tufts). Tuft size, height, density, clumping, clump size, raggedness,
    seed, slope band and slope fade. Every value is a hash of position and
    seed: a frame is bit-identical on any thread count.
  - Any other relief works too — a `PowerFractal`, a `Craggy`, a
    `Stratify` — through a `Math` subtract of the node's input, since a
    layer wants the *delta*, not the displaced terrain.

### Building the rocks-and-grass surface

`examples/macros/rocks_grass_layers.json` builds it in one go (File > Run
macro, or send the actions through the API):

```
Noise ──────────────┬──> TerrainOutput
                    ├──> FakeStones ──(displacement, stone_mask)──┐
                    ├──> GrassDisplacement ──(displacement, grass_mask)──┐
                    │                                              │      │
FlatColor(rock)  ───┼──> MaterialLayer "rock"  <── slope 12..60°, mask = stone_mask
FlatColor(grass) ───┴──> MaterialLayer "grass" <── slope 0..14°,  mask = grass_mask,
                                                    below = rock layer
                         MaterialOutput <── albedo, roughness, displacement of "grass"
                         assign_material
```

The rock layer lives on slopes and is masked by the stones themselves, so
its grey colour appears exactly on the boulders; the grass layer lives on the
flats and is masked by the tufts, so the green sits on the tufts and the
bare ground between clumps shows what is below. Both layers add their
relief; the material's *displacement* output is the sum, composited by
presence, and the terrain rises accordingly.

In the Material Studio the layer's **Displacement** tab shows whether the
input is connected and holds the *Displacement* multiplier and the *Add to
displacement below* dial.

## Scale, and what a heightmap can and cannot do

A raster layer's relief has the terrain's resolution. On a 5 km tile at
1024², a texel is about 5 m: boulders and tufted meadows are real geometry
at that scale; individual blades are not, on any heightmap. Terragen has the
same boundary — its displacement grass is a *field*, and it reaches blade
scale only because the renderer subdivides to micropolygons at render time.
TerraForge's equivalent is the field domain: `TerrainSurface` and
`SurfaceDisplacement` evaluate procedurally on the GPU at whatever density
the camera needs, and are the right place for blade-scale fuzz; the raster
layers above are the right place for the clumps, the tufts and the stones
that shape the ground.

Two things Terragen has that are not here yet:

- **Intersect underlying** — a layer's presence favouring the hollows or the
  crests of the relief below it. Presence is evaluated on the *terrain*
  input; feeding a layer the ground plus the relief below it as its
  *terrain* gets most of the effect by hand.
- **Displacement along the normal.** A heightmap moves the surface
  vertically; overhangs and sideways bulges belong to the field domain.

## Where the relief is applied, and why

The terrain tile the graph produces is composited onto the procedural
planet before it is drawn (`docs` in `studio/planet_place.hpp`): a texel
that departs from the tile's ground by less than `place_presence` is not a
feature, and the planet shows through it. That is right for a lone
mountain stamped into a flat tile and wrong for a boulder field, whose
relief is smaller than the threshold by design. So the material's
displacement, and the mould the ground makes under placed objects, are
applied *after* placement, on the ground that is actually shown
(`studio/surface_features.cpp`), and the graph's own `TerrainOutput` keeps
the un-displaced ground for the sculpt, the scatter and the exports.
`probe_height` reports both: `placed_m` is what you see, `graph_m` is the
graph.

## API

```python
s.send(*json.load(open("examples/macros/rocks_grass_layers.json"))["actions"])
s.set_attr("grass", "height", 0.006)         # taller tufts
s.set_attr("rock_layer", "disp_amount", 1.5) # bigger boulders
s.set_attr("grass_layer", "disp_add", 0.0)   # grass replaces the stones' relief where it grows
```
