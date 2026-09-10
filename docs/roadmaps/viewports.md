# Viewports — roadmap

## Purpose

An extremely flexible UI: up to eight dockable views, each with its own
projection, shading, camera, curvature and overlays; layouts per workspace
and by name; everything remembered between sessions; any view can become a
camera and any camera can be applied to any view.

## Where it stands (2026-09-10)

- Eight view slots as a set (`Prefs::view_mask`), each a `ViewConfig`
  (projection, shading, atmosphere, water, grid, outlines, ortho pan/zoom,
  look-through camera, curvature); split, arrange, close; docking through
  ImGui with the arrangement scaled to the window.
- **Layouts** (`layout_record.*`, `layout_store.cpp`): the ImGui ini, the
  view mask, every view's settings, **the free orbit camera and the ortho
  views (added)**, the extra node editors, the open panels, the workspace;
  named layouts; per-workspace layouts captured on switch **and at exit,
  and now restored at startup** (`workspace_layout_restore`).
- **View ↔ camera** (added): save this view as a new or existing camera;
  look through a camera here; move the free view to a camera's eye; ops
  `view_to_camera` / `camera_to_view`; `set_viewport` addresses one view's
  `scene_camera`, `projection`, `curved`, `atmosphere`, `water`, `grid`,
  `outlines`, `shading` (1-based `view`).
- Two viewport engines (rasterized PBR, cinematic raymarch); the governor
  scales quality by measured work; the perf watcher.
- Tests: `test_layout.cpp` (round trip, never zero viewports, bad file).

## Integrations

| Direction | With | Through |
|---|---|---|
| in ← | Cameras | look-through; `view_planet_radius` |
| in ← | Planets | curvature per view |
| out → | Render | the capture path uses a view config through the active camera |
| in ← | Prefs / layouts | persistence |

## Gaps (verified)

1. One free orbit camera for all perspective views (`Camera CAM` is a
   global): two free views always show the same eye.
2. The layout file name is the translated workspace name — switching the
   interface language loses the layout.
3. `layout_list()` shows the internal `workspace2-*` records beside the
   user's own layouts.
4. Viewport engine and layout are not saved with the project or the layout.
5. `show_panel` has no key for the Render output window, Assets, AI, Settings.
6. Docs: `INTERFACE.md` said `workspace-*.json` and "1-6 viewports" (fixed
   in this pass).
7. `settings_path()` prefers a file in the working directory, so a launch
   from another folder silently uses another set of prefs and layouts.

## Roadmap

### Phase 1 — every view its own
- A free orbit per view slot (`CAM[8]`), captured and restored per view;
  the ortho views already are. Accept: View 1 and View 2 orbit
  independently and both come back after a restart.
- Layout files keyed by workspace id, not name; internal records hidden
  from the Layouts menu; `show_panel` for every window; per-view engine.
- A "camera strip" in the view header: the camera list, save-as, link,
  copy, and the render preset of the linked camera with a Render button.

### Phase 2 — layouts as part of the work
- Layouts saved with the project (optional) so a scene opens on its own
  arrangement; layout presets per task (modelling, lighting, compositing).
- Split views synced (orbit one, others follow) and view bookmarks
  (numbered saved orbits per view).

### Phase 3 — editors everywhere
- Any panel as a viewport tab (material preview, node preview, timeline
  curve) so a layout is a composition of editors; multi-monitor windows
  remembered per display.

Owner files: `studio/layout_record.*`, `studio/layout_store.cpp`,
`studio/layout_workspace.cpp`, `studio/panel_viewport.cpp`,
`studio/renderer_camera.cpp` (the orbit), `studio/ai_ops_view.cpp`.
