# Stone fields: rocks that are functions, not texels

Why the raster stones could never look like stones, what replaced them, and
what it costs.

## The ceiling the raster nodes hit

`FakeStones` writes into the terrain's heightmap, so its stones are made of
texels. On a 5 km tile:

| resolution | metres per texel | stones per hectare a dome could occupy |
| :--- | ---: | ---: |
| 512 (the default) | 9.77 | ~12 |
| 1024 | 4.88 | ~46 |
| 4096 | 1.22 | ~745 |

and the node itself is stricter still: one stone per lattice cell, cell =
`stone_scale` x terrain width, minimum 0.004 - so **the smallest stone it
can express is about 14 m across, and at most 25 of them fit in a hectare**.
Authored at the default 0.03 they are 105 m across. That is not a tuning
problem; a picture of a fixed size cannot hold a field of pebbles.

Terragen states the same boundary from the other side (tg2 guide p4):
heightfields are "inherently limited by a finite resolution and detail",
procedurals "can be computed accurately at virtually any scale... you can
achieve theoretically 'infinite' detail", at the cost of being computed at
render time.

## What replaced them

`FieldStones` is a **field node**: a function of a position, with no
resolution at all. The GPU calls it per vertex while the tessellator
subdivides, and per pixel for the shading normal, so the same node makes a
3 m boulder and a 20 cm cobble and there is no ceiling on how many.

Feed it to `TerrainDisplacement` (strength 1 - the node emits world units,
so the metres you type are the metres you get) and to a material through
its `mask` and `shade` outputs.

## Materials at the stones' positions

The node has three outputs, and the two that are not the height are what
make a stone field look like stone rather than like bumps:

- **`mask`** - 1 on a stone, 0 on the ground between. Feed it to the
  `factor` of a `FieldColorMix` and you have earth under, stone on top.
- **`shade`** - a number of its own per stone, 0..1, constant across one
  stone and unrelated to its neighbour's. One colour over a whole field is
  the last thing that gives a procedural stone field away, so feed this to
  a second `FieldColorMix` between a dark and a pale stone colour and every
  stone takes its own shade.

Both go to a `TerrainSurface` node, which shades the terrain per pixel on
the GPU, so the colouring resolves as far down as the displacement does.
`examples/macros/stone_field_material.json` builds the whole graph in one
go (File > Run macro):

```
FieldStones.shade -> ColorMix(dark, pale).factor  -> stone colour
FieldStones.mask  -> ColorMix(earth, stone).factor -> TerrainSurface.color
FieldStones.out   -> TerrainDisplacement.field
```

The viewport must be in **Textured** shading for a surface graph to be
used at all; in Solid it is deliberately ignored, along with every other
albedo source.

**Sizes are metres and mean metres.** `Stone size` is the width across of
the biggest stone in the field, from **5 mm to 200 m**; every size below it
comes from the octaves, each half as wide and four times as many, and six
octaves span thirty-two to one. Useful settings, all measured in the
viewport:

| ground | stone size | octaves | drift |
| :--- | ---: | ---: | ---: |
| grit and gravel | 0.05 m | 3 | 0.8 m |
| a stony path | 0.12 m | 4 | 1.5 m |
| a scree slope | 0.25 m | 4 | 3 m |
| a boulder field | 3 m | 5 | 40 m |

### The six dials that decide what a field looks like

| | what it does |
| :--- | :--- |
| **Amount** | How much stone. Up to about three quarters it thins the field; past that every cell holds a stone and they grow into one another, so **1 paves the ground end to end with no bare earth left**. |
| **Stone size (m)** | The biggest stone's width across, in real metres. |
| **Size variation** | 0 every stone alike; 1 the power-law spectrum of scree - many small, a few large. |
| **Shape variation** | How much stones differ *from one another*. 0 breaks, flattens and pits every stone to the same degree, which is the look of a texture; 1 puts rounded cobbles and shattered blocks side by side. |
| **Cluster / repel** | Above zero, stones collect in drifts *and are pulled together inside a drift until they touch*. Below zero they push apart and stand off from one another. The count is unchanged either way. |
| **Drift size (m)** | How far across one clump is. |

**Clustering moves stones, it does not just count them.** Thinning a field
by a slow noise says how many stones a patch of ground gets and nothing
about where they sit, so the stones stay evenly spaced and merely become
rarer - which reads as scattered marbles, not as a clump. The drift field's
gradient (analytic: a bilinear patch differentiates to a few multiplies, so
no extra hashes) now also pulls each stone toward the side of its cell the
middle of the drift lies on. Stones in a drift end up shoulder to shoulder
and the ground between them is bare. The pull stays a blend of two points
inside the cell, so a stone can never leave its own cell and reach in from
outside the 3x3 window the field walks.

Below zero the same dial does the opposite: it takes the jitter out of the
placement, and the stones stand off from one another the way frost heave
and a slope's own sorting actually arrange a boulder field.

What makes a field of these read as stone rather than as bumps, each of
which the raster node lacks:

- **a power-law size spectrum** (`Size spread`) - many small, a few large,
  which is the distribution a scree slope actually has. FakeStones drew its
  radius from a uniform range;
- **a height per stone**, not one height for the whole field. Every stone
  FakeStones made was exactly as tall as every other; only the footprint
  varied, and that is its most visible tell;
- **elongation and a turn** - a stone lies the way it fell. Round in plan
  is the signature of a procedural field;
- **an irregular outline**, two harmonics per stone, so no stone is an
  ellipse;
- **broken faces**: two hashed planes cut flat facets into each stone, so a
  field of them reads as rock rather than as droplets. A stone is a broken
  thing, not a bubble, and this is the single control that most decides
  whether the field looks like stone;
- **surface relief** across each stone, so its own shell is not polished;
- **stones that differ from one another** and not only from the average
  (`Shape variation`): rounded cobbles and shattered blocks in the same
  field. The multiplier averages to one, so turning it up widens the range
  without changing the field's character, and the two draws are used
  against each other so the flat slabs come out smooth and the lumpy ones
  pitted rather than everything drifting the same way at once;
- **drifts that touch**: stones are not spread evenly, and a clump is
  stones that *touch*, not merely more of them in one place. See
  Cluster / repel above;
- **a number per stone** (`shade`), so a material can colour each one
  differently from its neighbour;
- **a lean**, so the high point is off centre and a stone has a downhill
  side;
- **burial** (`Settled into the ground`), so a stone shows only its top and
  the ground cuts its outline instead of meeting it tangentially;
- **flattening into a plateau** rather than a scaled-down dome, which is
  what a slab is;
- **octaves**: each halves the stone and quadruples the count, so boulders,
  cobbles and gravel come out of one function - and distance takes octaves
  away again, so the ceiling is not the cost.

## The other half of the fix

A field displacement was already possible before this node; it just could
not be *seen*. The terrain's shading normal was central-differenced over
`u_texel` - one heightmap texel, about 10 m - so a field could move the
geometry into stones and then shade as though the ground were flat. The
step is now taken from the distance to the camera instead
(`studio/shaders_terrain_frag.cpp`), floored so it cannot underflow and
capped at a texel so distant ground is no noisier than it was.

## What it costs

Measured on a full screen of close ground, 3 m stones through 4 octaves:

| setting | GPU ms |
| :--- | ---: |
| no stone field | 1.0 |
| stones, tessellation floor (max 8) | 5.25 |
| stones, shipping tessellation (8 px, max 32) | 5.6 |
| stones, maximum tessellation (3 px, max 64) | 6.3 |

The cost barely moves with tessellation, which says where it lives: the
**fragment stage evaluates the field four times per pixel** to central
difference the shading normal. Making the stone function return its own
analytic gradient would turn those four calls into one; that is the
optimisation this node is waiting on, and it is not done yet.

The inner loop is already free of transcendentals: a stone's turn, its
lean and its outline harmonics all come from hashed unit vectors and the
multiple-angle identities, because nine cells an octave times five octaves
times four evaluations a pixel is no place for a sine.

## Where it is verified

`studio/field_gpu_check.cpp` runs the generated shader against the CPU
evaluator over a 64x64 grid and compares every component: **33 stone cases**
(amount either side of the packing knee, size and shape variation,
flattening, burial, lean, tallness, elongation, outline roughness, facets,
surface relief, clustering both ways and its size, all six octaves, and
each of the three outputs), 16,384 samples each.

The height is worst at **1.2e-5** against a 2e-4 bar, most at ~1e-6, and
the per-stone `shade` - a step function, so the sharpest test in the
harness of whether both sides pick the same winning stone - is **exactly
zero** across all 16,384 samples.

**The mask's bar widens with the octave count, and only the mask's.** It is
a 0..1 step across a stone's rim whose steepness in world terms is set by
the stone, so a field whose finest octave is a hundredth of the cell
divides the input coordinate's own float quantisation up by the square of
that. Measured, all else equal: **1.9e-05 at one octave, 1.8e-04 at four,
4.2e-04 at six** - doubling per octave, which is exactly the cell halving,
and nothing to do with whether the two implementations agree. The
one-octave case is held to the same 2e-4 as everything else, which is what
pins the formula; the wider cases print the amplification as a record. Run
it all from the API with `{"op":"verify_field_gpu"}`.

## Known limits

- **There is a floor on how small a stone can be, and it is the tile's
  fault, not the node's.** Positions arrive as float32 in tile
  coordinates, so on a 5 km tile a point is known to about half a
  micrometre of the tile - which is a third of a millimetre of ground. A
  stone needs a few hundred distinct positions across it to keep a smooth
  outline, so **below about a centimetre across on a 5 km tile the outlines
  go blocky**, and the finest octave is what hits it first. The `mask`
  measurements above are that same quantisation, made visible. Shrink the
  terrain rather than the stone.
- **The displacement is GPU-only.** Objects placed on the terrain, the
  imprint and the ecosystems all read the heightmap, so they sit on the
  ground *under* the stones rather than on them.
- **Geometry stops at the tessellator.** At 5 km the finest triangle is
  about 1.2 m, so stones smaller than that exist in the shading normal but
  not in the silhouette. That is the right place for them - it is how the
  fractal micro-relief has always worked - but a stone on the horizon of
  the terrain will not break its outline.
- **Grass is not this node.** Blade-scale relief wants a different
  function; near grass wants instances (see [ECOSYSTEM.md](ECOSYSTEM.md)).
