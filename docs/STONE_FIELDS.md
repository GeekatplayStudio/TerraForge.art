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
its `mask` output.

**Sizes are metres and mean metres.** `Largest stone` is the width across
of the biggest stone in the field, from **5 mm to 200 m**; every size below
it comes from the octaves, each half as wide and four times as many.
Useful settings, all measured in the viewport:

| ground | largest stone | octaves | drift |
| :--- | ---: | ---: | ---: |
| grit and gravel | 0.05 m | 3 | 0.8 m |
| a stony path | 0.35 m | 4 | 4 m |
| a boulder field | 3 m | 5 | 40 m |

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
- **drifts**: stones are not spread evenly. A slow field over the cell
  lattice gathers them into clusters with barer ground between, at a size
  given in metres. The mean is preserved, so clustering rearranges a field
  without thinning it;
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
evaluator over a 64x64 grid and compares every component: 23 stone cases
(density, spread, flattening, burial, lean, tallness, elongation, outline
roughness, facets, surface relief, clustering and its size, all five
octaves, and the mask output), 16,384 samples each. Worst case **4.6e-5**
against a 2e-4 bar, most at ~1e-6. Run it from the
API with `{"op":"verify_field_gpu"}`.

## Known limits

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
