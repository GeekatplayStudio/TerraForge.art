# Where the GPU is used, and where it is not

A short, honest audit, because "we use the GPU" is the sort of claim that
stays true in outline while being wrong about everything that costs time.

> **Update, 2026-09-07:** the first node family now runs on the GPU. The
> audit below is what prompted it; the results are at the foot of this page.

## What runs on the GPU today

| | |
| :--- | :--- |
| The viewport | GL 4.3 rasterisation, shadow maps, volumetric clouds, sky |
| Terrain geometry | Tessellation control/evaluation shaders — adaptive subdivision and per-patch frustum culling |
| Field-domain nodes | Transpiled to GLSL (`engine/field_glsl.cpp`) and evaluated per vertex and per fragment, so a fractal shapes a planet without ever being rasterised into a buffer |
| Displacement | Per-vertex on the GPU, from the same field graph |
| Instancing | Scattered objects, with LOD and billboards at distance |

## What does not

**Node evaluation. All of it.** Every raster node — noise, fractals, filters,
erosion, hydrology, morphology — runs on the CPU, multithreaded but on the
CPU. There is not one compute shader in the codebase:

```
$ grep -r "GL_COMPUTE_SHADER\|glDispatchCompute" .
(nothing)
```

The stack has been GL 4.3 since the beginning, and compute shaders arrived in
GL 4.3.

## What that costs

Measured 2026-09-07, `build/node_bench --res=N --slow`:

| Node | 512² | 1024² | 2048² |
| :--- | ---: | ---: | ---: |
| ErosionLayers | 414 ms | 1,396 ms | **16,626 ms** |
| FlowWarp | 71 ms | 228 ms | 1,525 ms |
| PathFind | 66 ms | 209 ms | 1,166 ms |
| Skeleton | 16 ms | 63 ms | 700 ms |
| TerrainFractal2 | 50 ms | 159 ms | 623 ms |
| GaborNoise | 31 ms | 74 ms | 336 ms |
| Landform | 24 ms | 69 ms | 319 ms |
| TerrainFractal | 20 ms | 68 ms | 300 ms |
| NoiseFractal | 27 ms | 63 ms | 253 ms |
| KMeans | 25 ms | 54 ms | 249 ms |

Set that against the entire render path, measured the same day: **0.07 to
1.25 ms** (`docs/TERRAIN_PERFORMANCE.md`).

A single erosion node at 2k costs about **thirteen thousand times** a frame
of rendering. The GPU is idle for seconds at a time while the CPU grinds, and
that wait is the one a person actually sits through.

Note too that ErosionLayers is *superlinear*: 1,580 ms per megapixel at 512
becomes 3,964 at 2048. Two and a half times worse per pixel. A ceiling
recorded at one resolution says nothing about another, which is why the bench
now sweeps.

## Why it has not been done

Two real constraints, not oversight.

**The engine is deliberately GL-free.** `libgeekatplay_nodeterrain` is linked
by `nodeterrain_cli` and by every headless test. A node compute function that
called GL directly would drag a window and a context into all of them.

**Evaluation runs on a worker thread.** `studio/app_eval.cpp` computes on a
dedicated thread so the UI never blocks, and a GL context belongs to one
thread at a time. The worker cannot touch the main context.

**And there is a standing requirement that outranks speed:** results must be
deterministic and bit-identical. GPU floating point is not bit-identical to
CPU floating point, and it varies by vendor and driver. A golden hash
computed on the GPU would be a golden hash for one machine.

## The shape of the answer

All three constraints point the same way: **the GPU is an accelerator for
interactive work, and the CPU stays the truth.**

- An injectable accelerator interface in the engine, defaulting to nothing.
  The engine stays GL-free; a node asks whether an accelerator is installed
  and falls back to its own CPU path when it is not.
- The studio installs a GL implementation, on a second context that shares
  objects with the main one and is made current on the eval worker.
- `nodeterrain_cli`, the tests and the regression goldens install nothing, so
  bakes and goldens stay bit-identical and deterministic exactly as they are
  today.
- A test proves the two paths agree within tolerance, so what you see while
  dragging is what you get when you bake. That is the same contract the field
  domain already keeps, verified by `verify_field_gpu` at 2e-4.

## Built: the fractal family

That plan, implemented for the first node family.

| | |
| :--- | :--- |
| `engine/gpx/accel.hpp` | The interface. The engine declares what it would like done and nothing about how. |
| `studio/gpu_compute.cpp` | A second, invisible GLFW window whose context shares objects with the main one, made current on the evaluation worker. Programs, buffers and textures are shared; nothing here uses a container object, which is the part that is not. |
| `studio/accel_gl_fractal.hpp` | The compute shader: a line-for-line mirror of `gpx::fractal::eval` and the noise it calls. |
| `studio/accel_gl.cpp` | Dispatch, readback, and the list of what the GPU may be trusted with. |
| `studio/accel_check.cpp` | The agreement check, behind `{"op":"verify_accel"}`. |

**Measured, three runs at each resolution, `scripts/bench_accel.py`.** A
`TerrainFractal` node; `eval_ms` from the studio's own telemetry, so both arms
are the same measurement:

| Resolution | CPU (multithreaded) | GPU | |
| :--- | ---: | ---: | ---: |
| 512² | 25.5 ms | 4.7 ms | **5.4×** |
| 1024² | 83.6 ms | 12.9 ms | **6.5×** |
| 2048² | 279.5 ms | 35.5 ms | **7.9×** |

The ratio grows with resolution because the dispatch and readback are a fixed
cost. The CPU column agrees with `build/node_bench --res=2048` (279.5 against
299.5 ms), which is the cross-check that says the two are measuring the same
thing.

The node's preview, GPU against CPU, at 112²: **zero pixels differ**, worst
channel delta 1 of 255.

### What the GPU is not trusted with, and why

`{"op":"verify_accel"}` runs 31 parameter combinations through both paths.
The first run reported **five disagreements**, and isolating them one
parameter at a time gave the same answer every time: the shader is right, and
four operations have unbounded sensitivity to the ~1e-7 difference between
two floating-point implementations.

| Declined | Because | Measured |
| :--- | :--- | ---: |
| `combine` MAX_ABS / MIN_ABS | picks an octave by magnitude; a near-tie flips, and the answer moves by the gap between two octaves | 9.5e-3 |
| `profile` Terraces | `floor()`; a value a millionth from a step lands on the far side of it | 1.6e-1 |
| `gain` ≠ 1 | `pow(|v|, 1/gain)` — near zero the slope is infinite | 5.6e-3 |
| `filter_steepness` ≠ 1 | the same `pow`, per octave | 6.9e-4 |
| Cellular bases | worley is not ported | — |

The mean difference in all five was the same ~3e-6 as in the cases that
agree; only the worst pixel ran away. So those five run on the CPU, and the
remaining 25 cases agree within **1.3e-4** against a 2e-4 tolerance. All four
declined parameters default to their neutral value, so the fast path covers
what people actually build.

Set `GPX_NO_GPU=1` to rule the GPU out without a rebuild. The scene state
publishes `viewport.accel`, `accel_taken` and `accel_declined`, because a
fast path nobody can see the use of is a fast path nobody trusts.

### Still to do

The erosion family, which is where the seconds are — 16.6 s at 2k against the
fractal's 0.3 s. Its per-iteration kernels are small and element-wise, and
each cell writes only its own output, so the GPU is a better fit than the
CPU's threading was. That is the next one, on infrastructure that now exists
and is proven.
