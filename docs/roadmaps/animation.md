# Animation — roadmap

## Purpose

Create frames and render animations with every keyed property on every
object: granular keyframes on objects, the world, materials and nodes;
curves, modifiers and expressions; sequences rendered through the render
presets.

## Where it stands (2026-09-10)

- **One table of what can be keyed** (`studio/anim_targets.cpp`): object
  properties by group (Transform, Display, Deform, Scatter, Light, Camera,
  Planet, Surface, **Nebula — added**), world properties (sun, sky, fog,
  water, clouds, terrain, **atmosphere height, stars, galaxy — added**),
  and every numeric node attribute through `Graph::apply_animation`
  (materials are node attributes, so they key).
- **Tracks** (`engine/gpx/animation.hpp`): Bezier/linear/step keys, ease,
  extrapolation, modifiers (noise, oscillator, offset, limit, smooth),
  expressions, markers, bake/simplify/snap/mirror/retime.
- **Timeline**: fps, range, preview range, loop modes, autokey, snap,
  markers; the track tree, key editing, the curve editor, motion paths,
  the animation circle on every keyable row; playback real-time or every
  frame.
- **Ops**: 26 in `anim_ops.cpp` + `set_time`, `set_camera_key`,
  `render_sequence`, `playblast`; 22 MCP anim tools plus the rest.
- Docs: `docs/ANIMATION.md` (spec + status + manual). Tests: `test_anim.cpp`,
  `test_anim_scene.cpp`.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | every module | `anim_apply` writes sampled values into objects, `RenderSettings` and node attributes each frame |
| in ← | Cameras | `CameraPath` rides a cloud; `set_camera_key` |
| out → | Render | `render_sequence`/`playblast` (viewport only today) |
| in ← | Terrain/Erosion (future) | an elapsed-time input |

## Scripting surface

Address a track by `{"object","prop"}`, `{"world"}`, `{"node","attr"}` or
`{"track"}`; `set_key` with `frame`/`time`, `value`, `interp`, `ease`;
`keys`; `set_range`; `play`/`stop`; modifiers and expressions. Guide:
recipe 8.

## Gaps (verified)

1. Not keyable: `Text`/`Filename`/`Gradient` attributes; nebula `type`,
   planet `seed`/rock colours, camera `format`/`film`/optics switches,
   object `enabled`/visibility dots/`side`/ground fields; world booleans
   (`anim_world_bool_ptr` returns null); the second cloud layer and the
   water foam/clarity; world shape; the backdrop; post-process.
2. Two `set_key` handlers with different vocabularies (`frame` vs `time`,
   `bezier` vs `smooth`) chosen by the presence of `"node"`.
3. `set_camera_key` keys the legacy `anim_eye/anim_target` path, not the
   `AnimProp` tracks.
4. Sequences render the viewport only, PNG, no passes, no resume, no engine;
   no menu entry; the docs describe a `Render ▸ Sequence` that does not
   exist.
5. No clips/NLA, no camera-switch track, no whole-object animation copy,
   weighted handles absent.
6. No time base for erosion, weather or ecosystems.

## Roadmap

### Phase 1 — every number keys
- Generate the object table from the fields: every `float`/`int`/`bool`/
  colour on `SceneObject`, `PlanetData`, `NebulaData`, `CameraData`,
  `RenderAssign` and every `env_fields` entry is a track by default (an
  exclusion list for the few that must not: ids, names, paths). World
  booleans keyable as step tracks. Accept: `keys` lists a track for every
  saved setting; a nebula's kind and a planet's rock colour key.
- One `set_key` (both address forms, `frame` and `time`, one interpolation
  vocabulary); `set_camera_key` writes the `AnimProp` tracks; the legacy
  path migrated on load.
- Render ▸ Sequence through the render presets and engines (render.md
  Phase 1), with a camera-switch track.

### Phase 2 — the editor
- Clips/NLA: a clip is a set of tracks with a range; blend and offset
  clips per object; a library of motion presets (orbit, flythrough, day
  cycle).
- Whole-object animation copy/paste; weighted Bezier handles; a dope sheet
  with per-object rows; onion skins for cameras (motion paths exist).

### Phase 3 — time as a first-class input
- The Timeline drives an `elapsed time` that erosion, weather, water and
  ecosystems consume (their Phase 3s), with per-node state caches so
  scrubbing is instant.
- Simulation baking to keys; physics settle of objects; procedural
  animation nodes (`Dynamics`, `KeyframeCurve`, `AnimationClip` are
  placeholders today).

Owner files: `studio/anim_targets.cpp`, `studio/anim_ops.cpp`,
`engine/gpx/animation.hpp`, `engine/node_graph_eval.cpp` (apply_animation),
`studio/panel_timeline*.cpp`, `studio/app_services.cpp` (sequences).
