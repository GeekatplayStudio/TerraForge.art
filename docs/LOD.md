# Level of detail

What is drawn at less than full quality when it is far away, what the
dials are, and how to check the numbers from a script.

## Scattered copies

A population (an `EcosystemLayer`, or any Points node a mesh is bound to)
is bucketed into a 16 x 16 grid of cells over the tile when it is rebuilt
(`studio/scatter_lod.cpp`, pure, tested in `render_tests`). Inside a cell
the copies are sorted by a per-instance hash, so any prefix of a cell is a
uniform subsample of it. Each frame, per view:

1. a cell outside the view's frustum is skipped (the shadow pass keeps
   every cell the sun sees);
2. the cell's distance from the camera picks a **keep fraction**: 1 inside
   `scatter_lod_full_m`, falling smoothly to `scatter_lod_min_keep` at
   `scatter_lod_far_m`, 0 past `scatter_lod_cull_m`; the pass draws that
   prefix of the cell with `glDrawArraysInstancedBaseInstance`;
3. the survivors of a thinned cell grow by `sqrt(1 / keep)` (capped at 2)
   so the ground stays as covered as it was - the far crowd is fewer, not
   sparser;
4. the mesh level follows the distance too: the mesh itself inside
   `full`, a quarter of its faces to the midpoint of `far`, a sixteenth
   beyond (`studio/mesh_lod.cpp`, the same quadric reducer the mesh tools
   use). A mesh with material parts - a plant's textured leaf cards - keeps
   its geometry at every level and relies on thinning alone; a reducer
   would tear its pictures.
5. past `scatter_lod_billboard_m` a copy is a **card**: four vertices
   facing the camera, textured with the mesh rasterised straight on
   (`studio/billboard.cpp`, the same CPU rasteriser that draws the node
   thumbnails, with the model's own pictures and their cut-outs kept and
   the background left transparent). The card turns about the world's up
   axis only, so a tree stays upright when the camera looks down at it,
   and it takes the same wind lean the geometry does so nothing jumps at
   the boundary. Measured on a meadow of 20,387 drawn copies: 3.54 ms of
   GPU as geometry, 2.02 ms as cards. Cards are skipped in the render
   passes (an AOV wants geometry, not a picture of it) and in the shadow
   map, where at that distance a copy's shadow is smaller than a texel.

The shadow pass makes the same decision from the view camera, so a copy's
shadow is where the copy is. Adjacent cells drawn whole become one draw.

Defaults: full 150 m, far 1500 m, cards past 2500 m, cull 6000 m, minimum
share 0.15. The performance governor (`studio/perf.cpp`) scales the far,
card and cull distances down (0.6, then 0.35) as it lightens the frame.
A mesh always falls back down the ladder to what it actually has: no card
baked, no reduced copies, and it is drawn as itself.

## The terrain's relief

Stones and grass made as displacement layers are baked into the tile's
heightmap. Far from the camera a vertex used to read a single texel of
that relief, so a stone field at the horizon shimmered as the tessellation
moved. The height texture is now mipmapped and the vertex reads
`textureLod(u_height, uv, lod)` with `lod = log2(distance * k)` clamped to
0..4, `k = 32 * terrain_lod`: at the default 0.5 the relief is averaged
over two texels an eighth of the terrain's width away and over sixteen at
the far edge. Near ground keeps every texel. The shadow map's terrain
reads the same level from the same camera, so the shadow lies on the
surface the viewer sees. `terrain_lod` 0 turns it off.

## Dials and API

Environment panel > Surface detail: **Relief detail by distance**
(`terrain_lod`). The scatter distances are project settings
(`scatter_lod_full_m`, `scatter_lod_far_m`, `scatter_lod_cull_m`,
`scatter_lod_min_keep`), saved with the scene and set from a script with
`set_viewport` (MCP: `studio_set_viewport`).

`scene_state.json` > `viewport` reports `instances_drawn`,
`instances_total` and `instances_cards` for the last frame, so "LOD is on"
is a number: move the camera away and drawn falls while total does not,
and pull `scatter_lod_billboard_m` in and cards climbs to meet drawn. The
Environment panel prints the same line under Surface detail.

## Gates (tests/cpp/test_render.cpp, `render_tests`)

- the cells partition the population contiguously; each is sorted by key
  and its bounds hold its copies; a cell's first half has the lower keys;
- keep is 1 inside full, monotone non-increasing, `min_keep` at far and
  between far and cull, 0 past cull; the governor's scale pulls far in;
- survivors grow by `sqrt(1/keep)` capped at 2; the level ladder steps
  with distance and only ever coarsens, ending at the card;
- the cells a camera asks for are inside its radius, nearest first, the
  same set in the same order every time, a budget keeps the nearest, and
  a step of one cell keeps most of the previous set.
