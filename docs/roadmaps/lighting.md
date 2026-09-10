# Sun and lighting — roadmap

## Purpose

Ultra-realistic light distribution: one or several suns controlling rays,
shadows and colour; sky light; point, spot and area lights; god rays; the
sun inside a ring or Dyson world; every light reaching every renderer.

## Where it stands (2026-09-10)

- **The sun**: manual azimuth/altitude or geographic (latitude, longitude,
  UTC offset, date, time — `compute_sun_dir`), colour, intensity; the `Sun`
  object and gizmo; the `SunLight` node; a sun inside a world as a body on
  the axis/centre lighting the far shell per pixel.
- **Shadows**: a sun shadow map with softness (`pass_shadow`), the governor
  turning them off when far; cloud shadows (cinematic engine).
- **Point/spot lights**: up to eight (`upload_scene_lights`), reach, cone,
  colour, intensity, heading/pitch; `LightSource` node; exported to
  Mitsuba/Cycles.
- **Sky light**: `ambient_intensity`, the sky colour in the ambient term, the
  HDR backdrop dome lighting reflections and ambient.
- `[Planned]`: `AreaLight`, `Skylight`, `LightGel`, `VolumetricLight`,
  `LensFlare`.
- Animation: sun azimuth/altitude/intensity/colour/hour tracks; light
  intensity/reach/cone.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | every pass | sun direction/colour; `day` factor; shadow map |
| out → | Atmosphere/Clouds/Space | sun disc, night, cloud light march |
| in ← | Planets | `world_sun_inside`; `pl_world_up_at` |
| out → | Water | specular and reflection |
| out → | Render | sun emitter and lights in Mitsuba/Cycles; sun only in LuxCore |
| in ← | Animation | tracks; `hour` for a day cycle |

## Scripting surface

Ops: `set_sun` (azimuth_deg, altitude_deg, intensity, color — forces manual
mode), `add_light`/`set_light`, `set_viewport shadows shadow_softness
exposure`, `set_setting` for the geographic fields. MCP: `studio_set_world
sun`, `studio_add_light`/`studio_set_light`. Guide: recipes 1, 8.

## Gaps (verified)

1. **One sun.** `RenderSettings` holds a single sun; `SceneObject::Sun` is a
   proxy for it; N `SunLight` nodes overwrite each other; no per-sun data
   in any shader.
2. `set_sun` cannot set the geographic mode; `add_light` cannot set
   `enabled`/`shadows`; the MCP light tool omits `type`, `cone`, `heading_deg`,
   `pitch_deg`.
3. Viewport point/spot lights cast no shadows; no area lights; no gels; no
   god rays; no lens flare beyond the camera's flare dial.
4. Ambient is one scalar: no sky-light term with directionality, no
   ground bounce, no ambient occlusion beyond the material's AO channel.
5. `compute_sun_dir`'s solar maths has no test.
6. LuxCore gets no point lights; cloud shadows only in the cinematic engine.

## Roadmap

### Phase 1 — more than one sun
- `Star` objects: direction or a position in space, colour temperature,
  intensity, angular size; the home sun becomes Star 0 (no behaviour
  change); shaders take up to four suns (direction, colour, disc, shadow
  map for the first two). Binary sunsets, a red giant and a white dwarf,
  the sun inside a ring as a Star at the axis. Accept: two suns cast two
  shadows of a cube in opposite directions.
- `set_sun` gains `mode`, the geographic fields and `shadows`; the light
  ops and tools gain every field; a test for the solar position at four
  known places and times.

### Phase 2 — the rest of the light
- Point/spot shadows (cube/2D shadow maps for the nearest lights); area
  lights (rect/disc) with soft shadows; `Skylight` as a real term (sky
  colour by direction, ground bounce); `LightGel` (texture on a spot);
  `VolumetricLight` god rays through the fog and cloud gaps; `LensFlare`
  driven by the star's screen position.
- Cloud shadows in both viewport engines from every layer (clouds.md).

### Phase 3 — physically consistent light
- Spectral sun colour from altitude through the physical sky
  (atmosphere.md Phase 1), consistent between the viewport and the
  renderers; IES profiles for lights; exposure in real EV with the camera's
  exposure triangle as the only tone control.

Owner files: `studio/render_settings.cpp` (compute_sun_dir),
`studio/renderer_scene.cpp` (upload_scene_lights, sun body),
`studio/renderer_passes.cpp` (pass_shadow), `engine/nodes/nodes_lighting.cpp`,
`nodes_atmosphere.cpp` (SunLight).
