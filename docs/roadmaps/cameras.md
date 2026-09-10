# Cameras — roadmap

## Purpose

Cameras are the render viewpoints with their own properties: real optics,
exposure, film, an animation path, and a render assignment, so any camera
can be looked through in any viewport, take a viewport's view, carry a
render preset and start a render from there.

## Where it stands (2026-09-10)

- `CameraData` (`studio/scene.hpp`): eye/target, focal length, sensor
  format (7), aperture, shutter, ISO, film stock (6), optics simulation
  (distortion auto/manual, vignette, chromatic, flare, motion blur),
  `RenderAssign` (engine, size, samples, output, passes, panorama, preset).
- `engine/gpx/camera_math.hpp`: FOV, EV100, exposure multiplier, lens
  distortion; `studio/camera_optics.cpp`, `renderer_post.cpp` apply them.
- Panels: `panel_camera.cpp` (activation, through-the-lens preview, lens,
  exposure with a shutter solver, film, optics, transform, "Frame the
  terrain", render assignment with a preset combo and a Render button),
  `panel_camera_optics.cpp` (copy the lens to other cameras).
- Viewports: look-through per view (`scene_camera` −2 active / −1 free /
  index), new viewport through a camera, **save this view as a camera (new
  or existing), look through a camera here, move the free view to a camera**
  (added in this audit, ops `view_to_camera` / `camera_to_view`).
- Render: per-camera assignment, presets (new), batch over cameras (new),
  `render camera:"…"`.
- Nodes: `SceneCamera`, `CameraPath`; `CameraTarget`, `DepthOfField`,
  `MotionBlur`, `CameraSwitch` are `[Planned]`.
- Animation: eye, target, focal, aperture, shutter, ISO, distortion,
  vignette, chromatic, flare, motion blur are tracks; `set_camera_key`.
- Tests: lens distortion, camera persistence, undo.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | Viewports | look-through; `view_planet_radius` treats a camera view as curved |
| out → | Render | `RenderAssign`; `request_camera_render`; the batch queue |
| in ← | Animation | tracks; `CameraPath` rides a Points cloud |
| in ← | Planets | a camera view always curves the world |
| out → | Atmosphere/Space | the eye's altitude decides what the sky pass sees |

## Scripting surface

Ops: `add_camera`, `set_camera` (position/eye, look_at, distance, height,
azimuth_deg, optics, render), `set_camera_key`, `view_to_camera`,
`camera_to_view`, `render camera preset`, `render_preset`, `render_batch`.
MCP: one tool each. State: `cameras`. Guide: recipes 1, 7.

## Gaps (verified)

1. `passes` and `panorama` on `RenderAssign` have no UI on the camera and
   no MCP parameter.
2. No camera-switch track (`CameraSwitch` is a placeholder): a sequence
   renders one camera.
3. Depth of field and motion blur exist as post/optics dials, not as nodes
   with a focus target.
4. `CameraData::anim_eye/anim_target` is a second, legacy animation path
   beside the `AnimProp` tracks.
5. No camera rig helpers: orbit/dolly/crane paths as parametric nodes,
   look-at targets, camera shake.
6. Free orbit and scene camera share nothing: FOV of the free view is fixed.

## Roadmap

### Phase 1 — the camera as the render's owner
- `passes` and `panorama` in the camera panel and the MCP params; a per-
  camera pass mask.
- `CameraSwitch` track: which camera renders at each frame; sequences and
  batches honour it. Accept: a 3-shot sequence renders each shot through
  its own camera and file pattern.
- Retire `anim_eye/anim_target`: migrate on load into the `AnimProp` tracks.

### Phase 2 — cinematography nodes
- `CameraTarget` (look-at with weight), `DepthOfField` (focus object or
  distance, in the viewport and exported), `MotionBlur` (shutter-driven,
  per object in the offline engines), camera shake, dolly/crane/orbit rigs
  as `CameraPath` presets.
- Free-view FOV and "match viewport to camera" both ways with optics.

### Phase 3 — the shot list
- A Shots panel: named shots = camera + frame range + preset; batch renders
  the shot list; thumbnails per shot; contact sheets.
- Real-lens presets (a lens library with distortion/vignette profiles).

Owner files: `studio/scene.hpp` (CameraData), `studio/panel_camera*.cpp`,
`studio/camera_optics.cpp`, `studio/layout_store.cpp` (view↔camera),
`engine/nodes/nodes_camera.cpp`.
