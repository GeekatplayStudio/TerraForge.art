# Planets — roadmap

## Purpose

An independent system: a planet is a collection of components — its surface
layers, its terrain tiles, its water, its atmosphere and clouds, its lights,
its populations — that has a global effect on its own collection and none on
another planet's. Any number of planets, at any size, with any shape.

## Where it stands (2026-09-10)

- **Maths**: `engine/gpx/planet_math.hpp` — layers by direction on the unit
  sphere (hills, ridged, billow, realistic terrain, craters), `Shape`
  {flat_x, flat_z, inside, flip, thick}, `sphere_place`, `world_alt`,
  `world_up`; GLSL twins verified by `planet_gpu_check.cpp`.
- **The home planet** is object 0 and the parent of the terrain, water,
  atmosphere and surface layers (`studio/scene.cpp`); its curvature is
  `planet_radius`, per view (`view_planet_radius`).
- **World shapes** (`studio/world_shape.*`): globe, ring, Dyson sphere, flat
  disc or square; thickness with a rim wall (`planet_rim.cpp`); ground on
  either face; a sun inside; far shell drawn by direction, now for outside
  faces too when the eye is aloft.
- **Planets in the sky**: `Planet` objects drawn on the GPU from parameters
  (three sphere LODs, no memory), with their own surface layers, sea, snow,
  atmosphere rim, spin; moons as cratered airless planets.
- **Placement**: the tile onto the planet with feathering, flatten, basins,
  clip modes; the surround continues the tile to the horizon.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | Terrain | placement (`planet_place.cpp`), curvature in every vertex stage (`pl_sphere_place`) |
| out → | Atmosphere, Clouds, Space | `pl_world_alt`/`pl_world_up`: the air band, cloud shells, star visibility follow the shape |
| out → | Water | the water plane and the surround ocean lie on the face |
| out → | Objects | `scene_planet_of(object)` — objects are placed against their planet's surface |
| out → | Lighting | a sun inside a ring/Dyson world; the sun body |
| out → | Distribution | camera-driven populations on infinite ground |
| in ← | Materials | the shared landscape palette; `SurfaceDisplacement` field graphs shape planets |

## Scripting surface

Ops: `add_planet`, `set_planet`, `add_moon`, `add_infinite_terrain`,
`set_viewport` (`world`, `world_shape`, `world_inside`, `world_width`,
`world_sun_inside`, `world_thickness`, `world_outline`, `planet_radius`,
`place_*`), `place_object side`. State: `planets`, `world`. Nodes: `Planet`,
`InfiniteTerrain`. Guide: recipes 2, 6.

## Gaps (verified)

1. **One environment per scene.** Sun, atmosphere, fog, water level and
   clouds are `RenderSettings` singletons: a second planet cannot have its
   own air, sea or sun. Planets in the sky get a limb-glow term only.
2. `Planet` node lacks the crater style, `spin`, `visible`, `home`;
   `InfiniteTerrain`'s style choice omits Craters; `add_infinite_terrain`
   omits `mask_scale`/`height_scale`.
3. No world-shape node: shape, thickness and outline persist through
   settings only.
4. No save/load test for the world-shape and thickness fields; no manifest
   entry for world shapes.
5. Not done from phase 34: shadow squares (a ring's night), a Dyson sphere's
   outside from space, planets in the sky with ring/flat shapes.
6. Tiles under a planet in the sky are not placed (nothing draws them there).
7. Small globes below ~1e-6 of the tile width vanish (float precision at
   x=0.5); the workaround is a smaller `terrain_size_m`.

## Roadmap

### Phase 1 — the planet as a collection
- `PlanetEnvironment` on `PlanetData`: its own atmosphere height/density/
  colours, fog, sea level and colour, sun (direction or a star object), and
  cloud layer list. The home planet's environment *is* the `RenderSettings`
  block (no behaviour change for old scenes); a planet in the sky renders
  its own air band, clouds band and sea from its block. Accept: two planets
  in one scene with different sky colours and sea levels, saved and
  reloaded.
- A `WorldShape` node (shape, radius, width, thickness, outline, inside,
  sun inside) driving the home planet, so a ring world is a graph.
- Fill the node/op gaps in 2; save/load test for every world field.

### Phase 2 — planets you can stand on
- Terrain tiles under a planet in the sky: placement against that planet's
  layers and drawing in its frame; fly to a planet and land.
- Planets with ring/flat/Dyson shapes; a Dyson sphere from space; ring
  shadow squares.
- Orbits: a planet or moon on a keyable orbit around another (period,
  inclination, phase) with the lit side following the star.

### Phase 3 — systems
- Star systems: several suns (see lighting.md), planets as children of a
  star, moons as children of planets; a system view that places them by
  distance and lets a scene be "on planet X looking at planet Y".
- Per-planet material palettes and biomes read from the planet's
  environment (temperature by distance from the star, water presence).
- Precision: tile-centred vertex maths and a double camera so millimetre
  globes and light-year systems coexist.

Owner files: `engine/gpx/planet_math.hpp`, `studio/world_shape.*`,
`studio/planet_renderer.cpp`, `studio/planet_rim.cpp`, `studio/planet_place.cpp`,
`studio/scene.cpp`.
