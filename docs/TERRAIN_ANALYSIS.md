# Which way the ground faces, and what it can see

Three questions about a landscape that altitude and steepness cannot answer,
and that every reference application has an answer for.

## Aspect — which way a slope faces

**Node:** *Select aspect* (Mask)

Vue calls it Orientation, Gaea calls it Aspect, World Machine calls it Select
Orientation, and Terragen reaches it through the surface normal. It is the
difference between the north side of a mountain and the south side.

Nothing else in the mask set can stand in for it. Altitude gives you a snow
line at a constant height, which is why a snow line drawn that way runs
straight around a peak like a contour on a map instead of sagging on the sunny
side. Steepness gives you rock on the cliffs. Neither can tell one face of the
same ridge from the other.

| Control | What it does |
| :--- | :--- |
| Facing | The compass direction the selected slopes look towards: 0 north, 90 east, 180 south, 270 west |
| Spread | How wide an arc counts, in degrees either side |
| Ignore flatter than | Flat ground faces nowhere in particular; below this slope it is dropped |
| Feature scale | How large a feature has to be to count |

**Feature scale is the control that decides whether the mask is usable.** The
gradient of a fractal at one pixel reports the pebble under your foot, and the
mask comes out as speckle. Spreading the gradient's taps asks the question at
the scale of the hillside, which is the scale the question was about. The
default of 0.02 — one fiftieth of the tile — reads whole slopes.

Note that this node does not renormalise its output the way the other
selectors do. A selection that found nothing has to come back empty; stretched
to 0..1 it would come back selecting everything, and a mask that inverts itself
when you narrow the arc is worse than no mask.

## Sky exposure — how much sky a point can see

**Node:** *Sky exposure* (Analysis)

Open ridges near 1, valley floors shut in by their own walls near 0. This is
the long-range companion to *Select cavities*, which looks a few pixels out:
whether a valley floor sees the sky at all is decided by a ridge that may be a
thousand pixels away.

It drives the things that depend on openness rather than on height — snow that
survives in shade, moss on the sheltered side, the damp in a gorge, the way
ambient light actually falls on real ground.

## Sun exposure — how much direct sun reaches it

**Node:** *Sun exposure* (Analysis)

Set the sun's azimuth and altitude to match the scene's and the shadows agree
with the render. The **Sweep** control is what makes it a terrain property
rather than a screenshot: with the sweep at zero you get one instant, a hard
shadow map; at ninety degrees you get a morning, and the result is the
fraction of that journey each point was lit for. Soft edges, and a field that
says where snow survives rather than where a shadow falls right now.

## How the horizon is computed

Both exposure nodes rest on the same question: in a given compass direction,
how high does the ground rise before the sky begins?

Ray marching is the obvious way and the wrong one. At 1024 square with sixteen
directions and thirty-two samples it is half a billion samples, it is still an
approximation, and it misses precisely the thin far ridge that casts the
shadow.

`engine/gpx/horizon.hpp` uses the convex-hull sweep instead. Along a line of
cells, the horizon at cell *i* is the largest `(z[j] - z[i]) / (t[j] - t[i])`
over every *j* ahead of it. Walking backwards and keeping the upper convex hull
of the points ahead makes that one comparison per cell, amortised: the hull's
tangent from *i* is the answer, and as *i* moves back the tangent only moves
further away, so a hull point popped once is never wanted again.

It is exact, not an approximation, and it is linear. Measured at 1024 square:

| | |
| :--- | ---: |
| Sky exposure, 16 directions | 30 ms |
| Sun exposure, 90° sweep | 15 ms |
| Select aspect | 6 ms |

Two details in that file are worth knowing because they are visible in the
result:

- **The ray is sampled where it actually is, not at the nearest cell.** The two
  differ by up to half a cell, which is nothing at distance and everything
  close up — the neighbour one step away along a diagonal is timed at 1.12
  cells and would be sampled at 1.41, and the horizon it reports comes out 30%
  too steep. One lerp fixes it, and a round bowl then reads the same from its
  floor whichever way it looks.
- **Heights are scaled against the tile's width, not the cell.** *Vertical
  scale* is the terrain's height range as a fraction of the tile's width — 0.25
  means a kilometre of ground rising 250 m. That is what turns a heightmap into
  real angles, and what lets a mask authored at 512 survive being rendered at
  2048.

## What is tested

| What | Where |
| :--- | :--- |
| the sweep agrees exactly with brute force, over 400 random lines of four shapes | `tests/cpp/test_horizon.cpp` |
| a lone far ridge is seen, not stepped over | same |
| a spike subtends the right angle along an axis *and* along a diagonal | same |
| looking west is not looking east | same |
| a round bowl's floor sees the same rim in all 32 directions | same |
| every cell is visited exactly once, in every direction | same |
| the same terrain at 64 and at 256 gives the same angles | same |
| the tests would notice if any of that broke | `python scripts/mutate.py horizon` — 6 of 6 mutants killed |
| it stays fast | `tools/bench_nodes.cpp`, in the PerfGuard suite |
