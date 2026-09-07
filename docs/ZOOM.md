# Zooming to the ground

What stopped a close approach working, what the fixes reach, and what is
still out of reach — with the arithmetic, because every one of these is a
number rather than an opinion.

## Why the zoom stopped

The orbit pivot's height was set once, at construction — `CAM.target[1] =
0.08` world units, which is **400 m** on a 5 km tile — and never written
again. Panning moves the pivot in x and z only. Since the eye converges on
the pivot as the distance shrinks, over any ground lower than 400 m the
zoom arrived at a point in the air and stayed there. Nothing in the code
looks like a limit: the distance clamp bottoms out at 1e-8 tile units,
0.05 mm, and never gets its turn.

Zooming in now keeps the pivot's height above the ground **proportional to
the distance** — halve one, halve the other — so a zoom converges on the
surface in the same hundred-odd notches it always took. Panning eases the
pivot toward the ground as it travels, so a zoom from anywhere lands on
the ground rather than above it. The dolly does what the wheel does.

The ground is read from the picking copy of the heightmap, which is 256
across — about 20 m a texel. That is what places the pivot, not what you
see, and it is four hundred metres better than the constant it replaced.

## Why the view went white

A 24-bit fixed-point depth buffer can tell a fragment from the cleared 1.0
only while `znear / z > 2^-24`. So the furthest thing that can write depth
at all is:

```
d_max = znear x 2^24
```

and `znear` was `cam_d x 0.002`, giving `d_max = cam_d x 33554`:

| camera distance | znear | furthest ground that can write depth |
| ---: | ---: | ---: |
| 9500 m (default) | 3.8e-3 | far past the horizon |
| **1.6 m** | 6.6e-7 | ~the horizon — **the threshold** |
| 1 cm | 4e-9 | **335 m** |
| 1 mm | 4e-10 | 33.5 m |

Below a camera distance of about 1.6 m, everything beyond a few hundred
metres quantised to exactly 1.0, failed `GL_LESS` against the clear, and
was never drawn. The sky showed through — and the sky paints the lower
half of its dome with 54% haze, which is the pale wash that got reported
as "the view goes white". At 1 m above the ground the terrain vanished
completely.

`znear` is now floored at **5e-7** tile units, which holds `d_max` at 8.4
tiles — past the horizon of the tile and its surround — at any zoom. It
costs the ability to see anything closer than **2.5 mm** to the eye, and
the world's `float` coordinates cannot express 0.3 mm at the tile's
position anyway (AGENTS.md, planets rule 9).

Reversed-Z with a floating-point depth buffer is the real answer and the
geometry roadmap specifies it (TF-PRE); this floor is what makes the
existing buffer behave, and it changes nothing above 1.6 m.

## Why it smoothed out

The fragment stage differences the fractal detail's normal over a step
frozen at a third of a heightmap texel — **3.4 m** on a 5 km tile at 512 —
while the octaves it is differencing reach 5 cm. Every one of those
octaves was computed and then averaged flat. The step now follows the
camera down. This is the same fault, and the same fix, as the
field-displacement normal in the stone-field work.

The fragment octave budget also goes to 12, the loop's own ceiling, which
is a 2.3 cm wavelength at the default frequency.

## What this reaches, measured

Walking a camera from 500 m down to 2 cm above the ground:

| | before | after |
| :--- | :--- | :--- |
| 1 m above ground | **terrain gone, blank sky** | renders |
| 5 m / 0.2 m / 0.02 m | one identical smooth blob | relief visible, refining |
| visible patches as the view narrows | erratic, sometimes none | 1247 -> 27 |
| GPU | — | ~1 ms throughout |

## What is still out of reach

**Geometry stops refining.** The patch grid is a fixed 64x64 over the
whole tile, built once with no camera term, so a patch is 78.125 m and the
finest triangle is:

```
78.125 m / 64 (the GL cap on tessellation levels) = 1.22 m
78.125 m / 32 (the shipping default)              = 2.44 m
```

however close the camera gets. Worse, the screen-space metric that chooses
the level *saturates*: `screen_of` clamps `|w|` at 1e-4, so every patch
within half a metre of the eye reports thousands of pixels an edge and
clamps to `tess_max`. **One metre and one centimetre subdivide
identically.** At centimetre range the ground's silhouette is visibly
faceted, and all remaining detail comes from the shading normal rather
than from geometry.

The fix is a **patch quadtree** — the number of patches adapting to the
view instead of only the subdivision inside a fixed patch count. It is
already specified, with its own acceptance gate on cracks, as TF-GEO-0 in
the geometry roadmap. It is not done.

**The albedo is still one texel wide up close.** At 512 across 5 km a
texel is 9.77 m, sampled once, bilinear, with no anisotropy and no detail
texture anywhere in the terrain shader, so a centimetre view sits inside a
single texel's gradient. Procedural surface colour (a `TerrainSurface`
field graph) is the mechanism that has no such limit; a baked albedo will
always end at its own resolution.
