# Atmosphere — roadmap

## Purpose

The collector of everything in the air: sky, haze and fog, the cloud layers,
rain, snow, particles, wind, dust — with its own editor, ultra-realistic,
influenced by the planet it lies on (its shape, size, sun) and driving the
rest (light, wetness, snow cover, erosion rates).

## Where it stands (2026-09-10)

- **Sky**: a two-colour gradient graded against the surface's up, the sun
  disc, night; the HDR backdrop dome (7 mappings); the space transition.
- **The air as a layer**: `atmosphere_height` over the surface whatever the
  world's shape; `atm_path` composes space, sun and sky per pixel; the
  horizon haze follows the same path (`studio/shaders_sky.cpp`).
- **Fog**: Beer–Lambert over an exponential height profile, HG phase,
  single-scattering albedo, per-channel absorption, optional heterogeneous
  march with sun self-shadowing; measured from the surface on inside
  worlds; a two-layer column on the far shell (`FOG_FN`,
  `docs/VOLUMETRICS.md`).
- **Volumetric materials** share the model (AGENTS.md "Participating media").
- **Node**: `AtmosphereSettings` (density, height_km, ambient, zenith,
  horizon, fog block); the clouds chain into it.
- **Panel**: Environment ▸ Atmosphere, Fog/Haze, Clouds, Space; the
  Atmosphere object's tab.

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Planets | shape, radius, `pl_world_alt`; a sun inside |
| in ← | Lighting | sun direction and colour; `day` factor everywhere |
| out → | Clouds | the layer list and the shared march |
| out → | Space | visibility |
| out → | Terrain, Water, Objects | `fog_terms`/`apply_fog_terms` in every surface pass |
| out → | Render | the sky HDR, fog as a post pass in Mitsuba only |
| out → | Materials / Erosion (future) | rain, snow, wind |

## Scripting surface

Ops: `set_sky` (density, height, height_m, ambient, zenith, horizon),
`set_fog` (all eleven terms), `set_viewport atmosphere_height`; MCP
`studio_set_world`. Settings: 16 in `env_fields`. Guide: recipes 1, 2, 7.

## Gaps (verified)

1. The sky is an artistic gradient, not scattering: no Rayleigh/Mie, no sun
   colour from altitude, no aerial perspective LUT; the limb from space is
   a tuned band.
2. **No weather at all**: no rain, snow, particles, lightning, wind field
   beyond cloud drift, no humidity/temperature.
3. No CPU twin or GPU agreement check for `atm_path`, `fog_terms`, the
   cloud march or the water shader; no test of `atmosphere_height`
   persistence.
4. Five fog terms (`absorption_color`, `fog_albedo`, `fog_anisotropy`,
   `fog_heterogeneity`, `fog_steps`) have no node attribute.
5. One atmosphere per scene (planets.md Gap 1).
6. Cycles and LuxCore get no fog; Mitsuba gets it as a post pass.
7. Stale text: `docs/VOLUMETRICS.md` and README said the wrapped shells
   were not built.

## Roadmap

### Phase 1 — a physical sky and honest tests
- Bruneton/Hillaire precomputed scattering (transmittance + multiple
  scattering LUTs, per world radius and `atmosphere_height`): the zenith/
  horizon colours become an artistic tint over a physical base; sun colour
  and the limb fall out of it. Accept: the sky's zenith luminance ratio to
  the horizon at sun altitude 30° within 15% of a reference model.
- Aerial perspective: `fog_terms` gains an in-scatter from the LUT so far
  ridges go blue without a fog dial.
- CPU twins for `atm_path`, `layer_span` and `fog_terms` under
  `verify_field_gpu`; a persistence test for every atmosphere field; the
  five fog terms on the node.

### Phase 2 — weather
- A `Weather` node/settings block: precipitation type and rate, wind speed
  and direction (feeds clouds), temperature, humidity; keyable.
- Rain and snow as a GPU particle emitter around the camera (an owned
  emitter, not Effekseer — see `docs_private/roadmap/atmosphere.md` #23),
  with streaks, splash on water, wind drift; lightning as a light + flash.
- Outputs to other modules: wetness map to materials (darkening,
  puddles), snow depth to `Snow`/materials, rain rate to hydraulic erosion,
  wind to `Wind` erosion and cloud drift. Accept: turning rain on darkens
  the ground within a keyframe and the erosion `water_map` rises.

### Phase 3 — atmospheres per planet and the editor
- Per-planet atmosphere (planets.md Phase 1): composition presets (Earth,
  Mars, Titan, exotic), density by gravity, colour by composition.
- The Atmosphere editor: one window with sky, fog, clouds, weather, space
  tabs and a live preview strip (the Environment panel split into an
  editor like the Material Studio).
- Renderer parity: fog and clouds as media in Cycles (volume world) and
  LuxCore.

Owner files: `studio/shaders_sky.cpp`, `studio/shaders_terrain.cpp` (SKY_FN,
FOG_FN), `engine/nodes/nodes_atmosphere.cpp`, `studio/panel_environment.cpp`.
