# What the terrain pass actually costs

The standing rule is that no performance claim ships without a number beside
it. This is the number for the surface — measured, not argued — and it is
here because a plan to rebuild the terrain's geometry was about to be started
on an architectural argument instead.

## How to take it yourself

```
build/geekatplay_studio          # leave it running
python scripts/bench_terrain.py --flat --json baseline.json
```

It builds a fixed terrain, flies a camera to six altitudes from standing on
the ground to orbit, and reads three things per view: the GPU time of the
terrain pass, the number of primitives the tessellator emitted, and the
viewport in pixels. All three are needed. Time alone cannot tell a
geometry-bound pass from a fragment-bound one, and neither number means
anything without the pixel count beside it.

To prove a change is invisible rather than assert it:

```
python scripts/shots_terrain.py before --setup    # ...change something...
python scripts/shots_terrain.py after
python scripts/compare_shots.py before after
```

## The baseline

Measured 2026-09-07, flat tile, 1024² heightmap, viewport 1063 × 476,
`tess_pixels 8`, `tess_min 8`, `tess_max 32`, frustum culling on.

| View | GPU | Triangles | Patches drawn |
| :--- | ---: | ---: | ---: |
| ground (standing on it) | 0.71 ms | 93,044 | 363 |
| low | 0.86 ms | 301,120 | 1,682 |
| mid | 1.25 ms | 625,968 | 3,864 |
| high | 0.07 ms | 663,552 | 4,096 |
| orbital | 0.10 ms | 663,552 | 4,096 |
| straight down | 0.91 ms | 663,552 | 4,096 |

Three things fall out of that table, and each of them contradicted something
that was believed before it was taken.

**The pass is fragment-bound, not geometry-bound.** The most expensive view
draws the fewest triangles and the cheapest draws the most: they are
anti-correlated. Standing on the ground costs 0.71 ms for 93,000 triangles;
from orbit, 663,000 triangles cost 0.10 ms. The cost follows how much of the
screen is ground, not how finely it is cut. **A geometry LOD that halved the
triangle count would save almost nothing** — the work is per pixel.

**The patch grid is already adaptive.** The 64 × 64 grid is 4,096 patches, but
frustum culling in the tessellation control shader leaves **363** of them at
ground level. A structure whose selling point is "4,096 constant patches
becomes 200–600 adaptive nodes" would be reinventing something that already
happens.

**The subdivision cap is never reached at the shipping settings.** Raising
`tess_max` from 32 to 64 changed the ground view by 824 triangles out of
93,044 — under one percent. Nothing was pressed against the ceiling. What
*was* binding is the floor, below.

## The floor was costing 81× at distance

`tess_min` is a floor under the screen-space subdivision metric. It exists for
a good reason: displacement and fractal relief are evaluated per vertex, so a
patch that subdivides to nothing loses them.

But it was flat, and a flat floor charges for relief on edges too small to
show any. At orbital range the metric asked for **8,192** triangles over the
whole tile and the floor delivered **663,552**.

The floor now tapers with the edge's length on screen — full strength on an
edge long enough to hold detail, giving way on one a handful of pixels long.
It still sits well above what the metric itself asks for, which is what keeps
displacement from vanishing at middle distances, and it still depends only on
the edge's two shared endpoints, which is what keeps the crack invariant
intact.

| View | Triangles before | after | |
| :--- | ---: | ---: | ---: |
| ground | 93,044 | 92,568 | −0.5% |
| low | 301,120 | 266,206 | −12% |
| mid | 625,968 | 319,586 | **−49%** |
| high | 663,552 | 75,756 | **−89%** |
| orbital | 663,552 | 14,804 | **−98%** |
| straight down | 663,552 | 87,674 | **−87%** |

And the picture, at 1280 × 720, `scripts/compare_shots.py`:

| View | Pixels differing | Worst channel |
| :--- | ---: | ---: |
| ground | **0** | 0 |
| low | 0.08% | 45 |
| mid | 0.42% | 69 |
| high | 0.71% | 90 |
| orbital | 0.29% | 58 |
| straight down | 0.12% | 29 |

Standing on the ground the frame is **bit-identical**: the near field was
never paying the floor, so nothing there changed. At distance a fraction of a
percent of pixels move, all of them on fine ridge detail where a coarser mesh
cuts the silhouette a pixel differently. Side by side the two frames are the
same frame.

Since the pass is fragment-bound this buys little time on this GPU — which is
the honest way to put it. It buys the 655,000 triangles a frame back for
somebody whose GPU is not this one, and for a viewport that is not half a
megapixel.

## What this means for a patch quadtree

The performance case is refuted. There is no performance problem: the pass
costs between 0.07 and 1.25 ms of a 16.7 ms frame, culling already adapts the
patch count, and the cost does not follow the triangle count.

The fidelity case survives, and is now measured rather than argued. Ask for
fine geometry — `tess_pixels 2` instead of 8, which is the regime a stone that
breaks a silhouette needs — and the cap starts binding hard:

| | cap 32 | cap 64 |
| :--- | ---: | ---: |
| ground | 516,982 | 826,148 (+60%) |
| low | 837,562 | 1,107,102 (+32%) |
| mid and beyond | 386,852 | 386,852 (no change) |

Patches in the near field are pinned against the ceiling, and 64 is the
hardware's own limit for a tessellation level. With a fixed 64 × 64 patch
grid the finest triangle is therefore `tile / 64 / 64` — **1.2 m on a 5 km
tile**, permanently, whatever any parameter is set to. Smaller patches are the
only way past it.

So a quadtree is worth building for what it lets the surface *show*, not for
what it saves. That is a different justification, a different success
criterion, and it is the one to hold it to.

## Why a quadtree cannot simply be dropped in

The terrain's subdivision rests on one invariant, and it is worth stating
plainly because everything else depends on it: **the level of an edge is
computed from that edge's two endpoints and nothing else.** Two patches
sharing an edge therefore compute the identical float, feed the tessellator
the identical number, and get identical vertices. No gap can open between
them.

A quadtree breaks the premise, because a coarse patch beside two finer ones
does not share an edge with either — it shares half an edge with each. The
usual answer is to constrain neighbouring levels and let the shared-endpoint
metric absorb the difference. It cannot, and the reason is arithmetic rather
than tuning.

`fractional_odd_spacing` always rounds up to an **odd** number of segments.
Two fine neighbours contribute odd + odd, which is even. The coarse edge
beside them contributes odd. An odd number can never equal an even one, so
the two sides cannot place the same vertices — at any level, at any distance,
on any hardware. On the natural case the coarse edge asks for 25 segments
where its two halves ask for 13 + 13, and the seam opens by 2% of an edge:
about 1.6 m on a 5 km tile.

Note that the 2:1 level relation does not have to be arranged. The coarse
edge is twice as long in pixels, so the metric already asks for twice the
level, unprompted. The levels were never the problem.

`tests/cpp/test_terrain_cracks.cpp` establishes all of that headlessly,
against the spacing rules the hardware actually uses, and confirms the
alternative: `equal_spacing` with an exact 2:1 relation is crack-free for
every level tried. That trades away the reason `fractional_odd_spacing` was
chosen — an integer level steps, and a stepping subdivision pops.

Deciding between the pop, a skirt that hides the seam, and CDLOD-style
morphing that closes it properly is the real design decision in front of a
patch quadtree. It was worth a day's arithmetic to find that out before
writing the shader.
