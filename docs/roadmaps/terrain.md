# Terrain — roadmap

## Purpose

Create terrain shapes ultra fast and photo-real: science-fiction or from real
maps, at any scale from a quarry to a planet, procedurally or from a DEM, and
always as a graph that can be re-run at another resolution.

## Where it stands (2026-09-10)

- **Generators**: 60+ nodes across `engine/nodes/nodes_primitives.cpp`,
  `nodes_fractals*.cpp` (Vue-class fractals with roughness output),
  `nodes_surface*.cpp` (PowerFractal, Stratify, Craggy, Landform, Crater,
  Dunes, Snow, BasaltField), `nodes_terrain_fx*.cpp`, `nodes_masks.cpp`,
  `nodes_filters*.cpp`, `nodes_path*.cpp`, the field domain
  (`nodes_field*.cpp`, resolution-free, GPU-transpiled), and the planet
  layer types 0–4 (`engine/gpx/planet_math.hpp`).
- **Editor**: Vue's Terrain Editor as graph ops (`studio/terrain_editor.cpp`,
  `terrain_styles.cpp`): nine styles, eight erosions with one Rock-hardness
  dial, twelve global effects, clipping, picture import, sculpt brushes into
  a `TerrainSculpt` delta layer, height painting.
- **Placement**: the tile blends onto the planet (`studio/planet_place.cpp`)
  with five join modes including clip low / clip high; multi-tile scenes
  (`terrain_tiles.cpp`); per-tile transform and deformers.
- **Real maps**: `HeightmapFile` reads uncompressed GeoTIFF (int16/uint16/
  float32), SRTM `.hgt`, 16-bit PNG, 8-bit images; `terrain_import_picture`
  blends one into the chain.
- **GPU**: the raster fractal family runs on a compute shader (5–8× measured,
  `docs/GPU_USE.md`); the field domain runs entirely on the GPU with a
  CPU/GPU agreement check at 2e-4.
- **Tests**: goldens for nine terrain projects, determinism across thread
  counts, GeoTIFF and `.hgt` import, shape/lake, cracks, transforms.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | Planets | `planet_place_tile` composites the tile onto the surround's relief; `terrain_base` feeds the surround |
| out → | Materials | `ErosionLayers` masks, `TerrainOutput`'s `albedo`, `MaterialLayer` altitude/slope/orientation read the heightmap |
| out → | Distribution | slope/altitude/mask presence, `TerrainImprint`'s `objects` mask |
| out → | Water | `Lake`, `Flood`, `Rivers` carve and mask; the water level is a height on the tile |
| out → | Animation | `height_scale` is a world track; sculpt deltas are not keyable |
| in ← | Objects | `TerrainImprint` moulds the ground under placed meshes |
| in ← | Cameras/Viewports | evaluation resolution and interactive low-res eval follow the drag |

## Scripting surface

Ops: `graph`, `add_node`/`connect`/`set_attr`/…, `terrain_style`,
`terrain_effect`, `terrain_clip`, `terrain_global`, `terrain_import_picture`,
`set_sculpt`, `paint_*`, `set_resolution`, `probe_height`, `add_component
kind:terrain`, `place_object`. MCP: one tool per op. Settings: `height_scale`,
`terrain_size_m`, `terrain_shape`, `terrain_aspect`, the `place_*` block.
Guide: [`../AI_SCENE_GUIDE.md`](../AI_SCENE_GUIDE.md) recipes 1, 5.

## Gaps (verified)

1. **DEM import loses the world.** `nodes_modeling.cpp:164` normalises every
   import to 0..1; no metres, no CRS, no lat/lon, no tile stitching,
   compressed GeoTIFFs refused; the dialog offers `.exr/.r16/.raw` that do
   not load.
2. **No window model.** `gpx::Heightmap` is `{w,h,vector}` with no origin,
   extent, cell size or halo; one graph resolution; three sampling
   conventions (node-centred, cell-centred, GL) disagree by half a texel.
3. **Per-buffer min/max normalisation** in erosion blocks tiling: two
   adjacent tiles run different physics.
4. **Picking** uses a 256² downsample (≈20 m/texel at 5 km).
5. **Patch grid fixed at 64×64**: the finest triangle at 5 km is ~1.2 m
   however close the camera is (`docs_private/ROADMAP.md` TF-GEO-0).
6. `nodes_scene.cpp` still lists `TerrainObject` as `[Planned]` although
   multi-tile terrain shipped.

## Roadmap

### Phase 1 — real maps that stay real (next)
- `HeightmapFile` gains `keep_elevation` (metres in, metres out, the height
  scale derived), a CRS/extent read from GeoTIFF tags, and a `crop` window
  in lat/lon or metres; `terrain_import_picture` sets `terrain_size_m` from
  the file's extent when asked. Accept: a 30 m SRTM tile imports with the
  summit at its true height (`probe_height` in metres within 1%).
- Compressed GeoTIFF through a bundled minimal LZW/Deflate decoder; `.r16`,
  `.raw`, `.exr` either load or leave the dialog.
- Online DEM fetch (Copernicus 30 m / SRTM) as an `ai_generate`-style job
  with a cache; a `DemFetch` node with lat/lon/size.
- Remove the stale `TerrainObject` placeholder; document the tiles.

### Phase 2 — the window model
- `Heightmap` carries origin, extent, cell size and a halo; nodes evaluate a
  requested window; `Sample`/`Rasterize` already speak this way. One
  sampling convention, documented and tested. Accept: two adjacent windows
  of a fractal chain agree along the seam to 1e-6 without `MakeTileable`.
- Physical units in erosion (see erosion Phase 1) so tiles share physics.
- Per-node resolution and a cache keyed by window: 8k terrains without 8k
  everywhere.

### Phase 3 — photoreal at every distance
- Patch quadtree (TF-GEO-0): triangles follow the camera to centimetres.
- Centimetre picking from the full-resolution map.
- Photo → heightfield (ONNX depth model) feeding `HeightmapFile`/`Stamp`.
- Style presets as reproducible recipes with reference photographs in
  `docs/` (the gallery images already come from the graph).

Owner files: `engine/nodes/nodes_modeling.cpp`, `engine/gpx/heightmap.hpp`,
`studio/terrain_editor.cpp`, `studio/planet_place.cpp`, `studio/terrain_tiles.cpp`.
