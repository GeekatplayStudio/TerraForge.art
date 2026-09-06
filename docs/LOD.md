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

The shadow pass makes the same decision from the view camera, so a copy's
shadow is where the copy is. Adjacent cells drawn whole become one draw.

Defaults: full 150 m, far 1500 m, cull 6000 m, minimum share 0.15. The
performance governor (`studio/perf.cpp`) scales the far and cull
distances down (0.6, then 0.35) as it lightens the frame.

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

`scene_state.json` > `viewport` reports `instances_drawn` and
`instances_total` for the last frame, so "LOD is on" is a number: move the
camera away and drawn falls while total does not.

## Gates (tests/cpp/test_render.cpp, `render_tests`)

- the cells partition the population contiguously; each is sorted by key
  and its bounds hold its copies; a cell's first half has the lower keys;
- keep is 1 inside full, monotone non-increasing, `min_keep` at far and
  between far and cull, 0 past cull; the governor's scale pulls far in;
- survivors grow by `sqrt(1/keep)` capped at 2; the mesh level steps with
  distance.
