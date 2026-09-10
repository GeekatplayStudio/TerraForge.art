# Clouds — roadmap

## Purpose

Volumetric cloud layers as part of the atmosphere: any number, each with its
own kind, height, coverage, density and motion, wrapped to the world's
shape, casting shadows, seen from below, inside and above, and driven by
weather.

## Where it stands (2026-09-10)

- **Layers**: the main layer (`cloud_*` settings), a fixed second layer
  (`cloud2_*`), and every further `CloudLayer` node as its own layer (eight
  in the sky pass); stratus / cumulus / cumulonimbus with anvil.
- **March** (`studio/shaders_sky.cpp`): Perlin–Worley shape eroded by a
  Worley detail volume, blue-noise start jitter, dual-lobe HG, a five-step
  sun march, normalised multiple-scattering octaves, powder, empty-space
  skipping, early exit, far-to-near compositing; **the layer is a band over
  the surface** (`layer_span` intersects two shells about the centre or a
  ring's axis, cut at a ring's rim).
- **On the ground**: cloud shadows from the main layer (cinematic viewport
  engine); the far shell of an inside world carries a cloud band sampled
  from the same shape texture.
- Node: `CloudLayer` (enabled, type, coverage, density, altitude,
  thickness, detail, anvil, wind, ambient, colour, quality); four
  `[Planned]`: `CloudZone`, `CloudDensityField`, `CloudMaterial`,
  `SpectralClouds`.
- Panel: Environment ▸ Clouds (main + second layer + quality/scattering).

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Atmosphere | the sky pass; `atm_path`; the fog after the clouds |
| in ← | Planets | `pl_world_alt`, `u_world_shape`, ring width |
| in ← | Lighting | `sun_at` (a sun inside shines from the axis); `day` |
| out → | Terrain | `cloud_shadow` in the terrain frag |
| out → | Planets | the far-shell band |
| out → | Render | baked into the sky HDR |
| out → | Animation | coverage, density, altitude, thickness, detail, wind, colour tracks |

## Scripting surface

Ops: `set_clouds` (enabled, type, coverage, density, altitude, thickness,
wind_speed, layer2_*), `set_viewport cloud_scatter_*`, `add_node CloudLayer`.
MCP: `studio_set_world clouds`. Guide: recipes 1, 2.

## Gaps (verified)

1. `set_clouds` cannot set `detail`, `anvil`, `wind_dir`, `color`,
   `ambient`, `quality`.
2. The extra layers (3..8) have no panel; only the graph reaches them; the
   second layer is not node-driven.
3. Cloud shadows come from the main layer only and only in the cinematic
   engine; `clouds_ok` switches clouds off above y = 3 tiles; a flat view
   cannot look down on a deck (`rd.y < 0.015` rejection).
4. The far side of an inside world gets no marched clouds (the 30-tile cap);
   the band on the shell is an impostor.
5. Ambient sampled straight up only; the cloud volume has no mipmaps
   (aliasing at distance); no temporal reconstruction (no TAA).
6. Clouds cannot occlude terrain (drawn in the sky pass without depth).
7. No weather field (`CloudDensityField` is a stub), no cirrus/spectral
   layer, no zones, no cloud material.

## Roadmap

### Phase 1 — every layer, everywhere
- `set_clouds` gains the six fields; a `layer` index addresses any layer;
  the panel lists every `CloudLayer` node with its dials; the second layer
  becomes a node like the rest.
- Shadows from every layer, in both viewport engines; no altitude cut-off
  (`clouds_ok` by distance to the band, not y); the flat-world branch marches
  downward rays. Accept: from a 4-tile peak on a flat world the deck below
  is visible and shadows the valley.
- Mipmapped shape volume; ambient from the sky at the sample's up.

### Phase 2 — the far side and the weather
- Marched clouds on the far side of a ring/Dyson world (a second march
  window at the far band); clouds occlude terrain through a depth-aware
  composite.
- `CloudDensityField` from a weather field (coverage, type, height per
  world cell) driven by the weather block (atmosphere.md Phase 2) and
  keyable; fronts, storms, clearing.
- `SpectralClouds` (cirrus), `CloudZone` (a soft box confining a layer).

### Phase 3 — inside the cloud
- Camera inside and above decks with temporal reconstruction (TAA) for
  quality at speed; god rays through gaps (lighting.md).
- `CloudMaterial`: shading through the material system; exported as media
  to Cycles/Mitsuba rather than baked.

Owner files: `studio/shaders_sky.cpp`, `studio/cloud_noise.cpp`,
`engine/nodes/nodes_atmosphere.cpp` (CloudLayer), `nodes_clouds.cpp`,
`studio/shaders_terrain_frag.cpp` (cloud_shadow), `studio/planet_shaders.cpp` (far band).
