# Render — roadmap

## Purpose

Multiple render presets across several engines: a camera carries a preset
and renders from it; the Render menu shows every preset, makes new ones and
starts a single render or a batch; sequences render through the same path;
every viewport feature reaches every engine.

## Where it stands (2026-09-10)

- **Per-camera assignment** (`RenderAssign`: engine, size, samples, output,
  passes, panorama, preset) and **named presets saved with the project**
  (`studio/render_presets.*`, added in this audit): apply/save on the Render
  tab and the camera's properties; the Render menu lists every preset with
  apply / render with it / apply to every camera / batch / delete, and
  "save the active camera's settings as a new preset"; a **render queue**
  runs cameras one after another, each to its own file.
- **Engines** (`orchestrator/render_engines.py`): Mitsuba 3 (full: sky
  HDR, meshes, instances, progressive preview, panorama, depth/normal
  AOVs, volumetric materials), Blender Cycles (terrain, albedo, sky HDR,
  sun, water plane, lights), LuxCore (minimal), appleseed (a stub), the
  OpenGL viewport with **12 AOVs** in EXR (`renderer_aov.cpp`).
- The sky HDR export bakes atmosphere, clouds, world shape and deep space
  into the environment map.
- Ops: `render` (camera, preset), `set_render`, `render_preset`,
  `render_batch`, `render_passes`, `render_sequence`, `capture`,
  `export_instances`; nodes `RenderOutput`, `RenderPasses`,
  `RenderBackdrop`, `PostProcess`, `RenderCamera`, `RenderQuality`;
  `RenderRegion`/`RenderLayers` `[Planned]`.
- Post: exposure, saturation, tint, vignette; ACES; the camera's optics.

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Cameras | `RenderAssign`, `request_camera_render`, the queue |
| in ← | Atmosphere/Clouds/Space | the sky HDR; fog terms (Mitsuba post) |
| in ← | Materials | the shared material params; volumetric media (Mitsuba) |
| in ← | Objects/Distribution | OBJ + matrices + instances |
| in ← | Animation | `render_sequence`/`playblast` (viewport frames only) |
| out → | Viewports | the progressive preview panel and the render window |

## Scripting surface

MCP: `studio_render`, `studio_set_render`, `studio_render_preset`,
`studio_render_batch`, `studio_render_passes`, `studio_render_sequence`,
`studio_capture`. State: `render_presets`, `render_queue`. Guide: recipe 7.

## Gaps (verified)

1. **Parity**: Cycles has no fog; LuxCore has no HDR sky, water, fog or
   point lights; appleseed is a stub still offered; nothing but Mitsuba
   gets participating media; planets, world shape and space reach engines
   only baked; only Mitsuba writes AOVs (2 of 12).
2. Sequences (`render_sequence`, `playblast`) write viewport PNGs only:
   no engine choice, no per-frame EXR passes, no resume; there is no menu
   item for them though the docs describe one.
3. `passes`/`panorama` have no UI on the camera; panorama is Mitsuba-only.
4. Settings carry no descriptions (`env_fields`).
5. No render region, no render layers, no denoiser hook, no network/queue
   across machines.

## Roadmap

### Phase 1 — parity and sequences
- Cycles: height fog as a volume world, layered materials mapped to a
  Principled node tree, AOVs; LuxCore: the sky HDR, water, lights, fog as
  a homogeneous volume, or drop it from the combos until it matches; remove
  appleseed from the combos. Accept: the default scene rendered by Mitsuba
  and Cycles agrees in mean luminance within 10%.
- Render ▸ Sequence: the Timeline's range through the active camera's
  assignment (any engine), EXR passes per frame, a frame pattern, resume
  from the last frame; the shot list (cameras.md Phase 3) feeds it.
- `passes`/`panorama` in the camera panel and MCP; presets carry the pass
  mask; a `docs/SETTINGS.md` generated from `env_fields` with descriptions.

### Phase 2 — the render editor
- Render layers (objects/materials per layer with holdouts) and regions;
  a denoiser (OIDN) on the viewport AOVs; the progressive panel for every
  engine.
- A settings-description table (`EnvField` label/unit/range) so
  `list_settings` explains itself to an agent.

### Phase 3 — scale
- A render farm: queue entries serialised as `scene.json` + preset, workers
  on other machines, results collected; cloud engines behind the same
  preset.
- Unbaked sky for the engines (procedural sky and clouds as Mitsuba/Cycles
  plugins) so stars and clouds render at output resolution.

Owner files: `studio/render_presets.*`, `studio/panel_render.cpp`,
`studio/toolbar_menus.cpp`, `studio/render_scene_export.cpp`,
`studio/renderer_aov.cpp`, `orchestrator/render_engines.py`, `render_cycles.py`.
