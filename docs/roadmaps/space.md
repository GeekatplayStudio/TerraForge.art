# Space — the global environment — roadmap

## Purpose

Everything beyond the air: a star field, the galaxy band, nebulas and
galaxies as placeable objects, moons and planets in the sky — incredibly
realistic, every property adjustable, any count at any position, and seen
exactly where the atmosphere lets space through.

## Where it stands (2026-09-10)

- `studio/shaders_space.cpp` (`space_color`): a two-grid cube-mapped star
  field with a steep magnitude law and colour temperature; the galaxy band
  as a great circle with core, fractal structure and dust lanes; nebulas by
  kind — emission cloud, dark cloud, spiral galaxy, elliptical galaxy,
  planetary nebula — in their own tangent frame; eight in the sky pass.
- `SceneObject::Nebula` with `NebulaData` (azimuth, elevation, size, tilt,
  rotation, seed, brightness, density, detail, arms, two colours), a `Nebula`
  node, Add tile ▸ Space, Properties, keyable fields.
- Settings `space_stars`, `star_*`, `galaxy_*`; Environment ▸ Space section.
- Moons: `scene_add_moon` — a cratered, airless planet (layer type 4, bare
  rock shading).
- The atmosphere composes `space * visibility + sun + sky * scatter`
  (`shaders_sky.cpp`), so stars are outshone by day, not hidden, and the
  panorama export bakes space into the environment map.

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Atmosphere | `atm_path` decides how much space a pixel sees |
| in ← | Planets | `pl_world_flat`/`u_world_shape` — the sky's up follows the world |
| in ← | Lighting | the sun disc is drawn in space; night is the sun below the horizon |
| out → | Render | baked into the sky HDR for every offline engine |
| out → | Animation | `star_brightness`, `galaxy_intensity`, `galaxy_core`, nebula fields are tracks |

## Scripting surface

Ops: `add_nebula`, `set_nebula`, `set_space`, `add_moon`, `set_planet`.
MCP: `studio_add_nebula`, `studio_set_nebula`, `studio_set_space`,
`studio_add_moon`. State: `nebulas`, `space`. Guide: recipe 3.

## Gaps

1. No star catalogue: the field is procedural; no named stars,
   constellations, a real Milky Way orientation for a place and time, or
   magnitude-accurate brightness.
2. Eight nebulas at most in the sky pass; no nebula node for the star field
   or the galaxy band; `visible` not settable by op.
3. Moons have no phases from a real sun position beyond shading, no orbits,
   no eclipses; only one sun exists.
4. The Milky Way core is bright and broad by default; the night background
   sits lighter than black (the band's tint).
5. No auroras, meteors, comets, satellites, zodiacal light, airglow;
   no lens/atmospheric twinkle.
6. Space is baked into a 2k panorama for renderers: stars are blurred dots
   at render resolution.
7. `docs/NODES.md` does not yet list the `Nebula` node (regenerate).

## Roadmap

### Phase 1 — realism dials
- Star catalogue mode: Hipparcos/Yale bright stars (a compact table in
  `resources/`) placed by RA/Dec, with the equatorial frame set from the
  sun's `latitude`/`longitude`/date/time so the real sky stands over the
  scene; procedural stars fill the faint end. Accept: Orion's Belt at the
  right altitude for a given place and time within 1°.
- Galaxy band aligned to the catalogue frame automatically when the
  catalogue is on; darker default background; a `sky_darkness` dial.
- `Nebula` gallery presets (Orion, Lagoon, Helix, Andromeda, Sombrero…)
  as named seeds and colours; up to 32 nebulas.
- Moon phases: the lit side from the sun object; a `phase` readout.

### Phase 2 — the sky that moves
- Orbits for moons and planets (see planets.md); eclipses; the sky rotates
  with time of day and date; a real-time `set_time` scrub.
- Meteors, satellites, auroras (a curtain field over the magnetic pole),
  airglow and zodiacal light as `Sky` effect nodes with keyable rates.
- Star twinkle from the atmosphere's turbulence (cheap noise on brightness),
  off in space.

### Phase 3 — deep space as a place
- Camera in space: the planet as a sphere with its surround and air seen
  from orbit (needs planets Phase 2); the home world drawn among the
  planets; a system view.
- Render-resolution stars for the offline engines (a point-light list
  instead of a baked map).
- Volumetric nebulas when the camera is inside them (the participating
  media model), with a distance in light-years.

Owner files: `studio/shaders_space.cpp`, `studio/renderer_space.cpp`,
`engine/nodes/nodes_space.cpp`, `studio/panel_properties_object_space.cpp`.
