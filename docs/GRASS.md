# Grass that is a function, not a texture

`FieldGrass` is the stones' argument one scale down. A heightmap on a 5 km
tile has texels metres across; a tuft of grass is centimetres, so a raster
node can no more hold grass than it could hold a pebble. This is a function
of a point: the GPU calls it per vertex while the tessellator subdivides and
per pixel for the shading normal, so the sward keeps resolving as the camera
comes down to it.

It is deliberately built on the stones' lattice
([STONE_FIELDS.md](STONE_FIELDS.md)), because the hard parts are the same -
a cell holds a thing, the thing has a size and a place and a shape of its
own, the things collect in patches, and the whole must be mirrored into GLSL
and agree with it to the last few ulps.

## What makes it grass and not small stones

| | |
| :--- | :--- |
| **Pointedness** | A tuft comes to a point; a stone is a shell. This is the single control that most decides whether the field reads as grass or as gravel, and no amount of blade detail rescues a field of domes. |
| **The wind is one direction for the whole field** | A stone leans whichever way it fell and its neighbour leans another. Grass all leans the same way, and that one fact is the strongest cue that what you are looking at is grass. **Tall blades bend further**, because they do. |
| **Blades** | An angular ripple around the tuft, strongest partway out where the blades splay and gone at the middle and the rim. **Blade count** blends across three harmonics rather than looping over a count - a variable-length loop inside the innermost loop of a per-pixel evaluation is not a thing to have. |
| **Bare ground** | Grass is not a carpet. It gives out where it is trodden, dry or shaded, at a scale far larger than a tuft, and the field is sharpened so it actually reaches zero - a sward that only ever thins to four fifths reads as a carpet, which is the thing grass never is. |

Everything else is the stones' vocabulary, because it was right there too:
amount as a coverage target that closes the sward at 1, size and shape
variation, height variation, size mix, clustering that gathers tufts into
patches *and pulls them together inside one*, and its negative that pushes
them apart.

## Three outputs

- **`out`** - the displacement, in world units. Feed it to
  `TerrainDisplacement` at strength 1.
- **`mask`** - 1 on grass, 0 on the bare ground between.
- **`shade`** - a number per tuft, constant across one tuft and unrelated to
  its neighbour's. Grass runs from dry to green within a stride, and one
  colour over a whole sward is what gives a procedural one away.

`examples/macros/grass_field_material.json` wires all three: earth under,
grass on top through `mask`, and every tuft its own colour between a dry and
a green through `shade`.

## Settings, measured in the viewport

| ground | tuft | blade | octaves | bare |
| :--- | ---: | ---: | ---: | ---: |
| a mown lawn | 0.04 m | 0.04 m | 2 | 0 |
| meadow grass | 0.14 m | 0.18 m | 3 | 0.25 |
| rough pasture | 0.3 m | 0.35 m | 3 | 0.4 |
| tussock | 0.9 m | 0.7 m | 2 | 0.5 |

Measured cost on a full screen of close ground: **9.5 ms** at three octaves,
3.9 ms at the lawn settings. As with the stones, the fragment stage
evaluates the field four times per pixel to central-difference the shading
normal, so an analytic gradient would turn those four calls into one. That
is the optimisation both nodes are waiting on.

## Known limits

- **The same precision floor as the stones.** Positions arrive as float32 in
  tile coordinates, so below about a centimetre across on a 5 km tile the
  tufts go blocky. Shrink the terrain rather than the grass.
- **Geometry stops at the tessellator.** At 5 km the finest triangle is
  about 1.2 m, so grass lives in the shading normal and not in the
  silhouette. That is the right place for it at any distance where you can
  see a whole sward at once.
- **This is not near grass.** A blade you can walk up to wants instances -
  see [ECOSYSTEM.md](ECOSYSTEM.md) for the population path, which places
  real meshes and billboards. The two are complementary: this covers the
  ground everywhere for the cost of a function, the instances handle what is
  close enough to have a silhouette.

## Where it is verified

`studio/field_gpu_check.cpp` runs the generated shader against the CPU
evaluator over a 64x64 grid: **31 grass cases**, 16,384 samples each -
amount either side of the packing knee, pointedness, blade relief and count,
wind, direction and bend, size, shape and height variation, size mix,
clustering both ways, bare ground, all five octaves and each of the three
outputs. Worst height case **5.9e-06** against a 2e-4 bar, and the per-tuft
`shade` - a step function, so the sharpest test of whether both sides pick
the same tuft - is **exactly zero** across all 16,384 samples.

The `mask` carries the same octave-dependent bar as the stones' and for the
same reason (1.8e-05 at one octave, 5.4e-05 at three); see
[STONE_FIELDS.md](STONE_FIELDS.md).
