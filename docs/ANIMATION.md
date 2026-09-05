# Animation in Geekatplay TerraForge

This is the specification and the user manual for the animation module in one
document. Part 1 says what a complete animation system needs, distilled from
the four applications whose animation tools are best regarded — Cinema 4D
(Timeline, Dope Sheet, F-Curve editor, the animation circle), Blender (Dope
Sheet, Graph Editor, NLA), After Effects (property stopwatch, Graph Editor,
easy-ease, expressions) and DaVinci Resolve (Fusion spline editor, keyframe
navigator). Part 2 is the state of TerraForge against that list. Part 3 is
how to use what is implemented. Every item in Part 1 has an id so tests, the
feature manifest (`tests/manifest/features.json`) and the roadmap can point
at it.

The one rule that governs everything: **nothing moves unless it has a key.**
An object, a light, a material, a node parameter — all static until a
keyframe is recorded on a property. Recording the first key enables
animation on that property (and shows it in the timeline); deleting the last
key makes it static again. There is no separate "enable animation" switch to
forget.

---

## Part 1 — What a complete animation module has

### A. The time model

| id | Feature | Reference |
|---|---|---|
| A1 | A document has a frame rate (24/25/30/60, or custom), a start and end frame, a preview range inside it, and a current frame. Time is stored as an integer frame plus the rate; a property is evaluated at fractional time for motion blur and sub-frame sampling. | C4D Project Settings; Blender Scene; AE Composition Settings |
| A2 | Time is displayed as frames, SMPTE timecode (`hh:mm:ss:ff`) or seconds, switchable; the ruler labels follow the display mode. | all four |
| A3 | The transport: go to start, previous key, previous frame, play/pause, next frame, next key, go to end; loop, ping-pong and play-once modes; play backwards; a playback rate (real-time, every frame, or a percentage). | C4D transport; AE preview panel |
| A4 | Playback in the active viewport at the document rate, dropping frames to stay in sync with real time, or every frame when "all frames" is on. Audio-off; no audio track in v1. | all |
| A5 | Scrubbing: dragging the ruler sets the current frame and the scene updates live; `Ctrl`-scrub snaps to keys. | C4D, Blender |
| A6 | A **keyframe navigator** in every property panel: previous key / add-remove key / next key, next to the property, and in the timeline header for the whole document. | Resolve keyframe navigator; AE |

### B. What can be animated

| id | Feature | Reference |
|---|---|---|
| B1 | **Every numeric, boolean, colour, vector and enum property of every object**: transform (position, rotation HPB, scale, per-axis squeeze), deformers (twist, bend, skew, taper), colour, visibility, lights (intensity, colour, radius, cone), cameras (focal length, aperture, focus distance, exposure, film), the sun (azimuth, altitude, intensity, colour), atmosphere (fog, haze, sky colours, cloud cover, wind), water (level, colours, waves), planets and infinite-terrain layers (every layer parameter). | Vue "animate anything"; C4D animation circle on every parameter |
| B2 | **Every node attribute in the graph** — terrain fractal seed/frequency/amplitude, erosion strength, material tint/roughness, layer presence bands. A key on a node attribute re-evaluates the affected part of the graph at that frame. | Vue graph animation p1148; C4D XPresso/Scene Nodes time inputs |
| B3 | Parameters of a whole material (tint, roughness, map offset — scrolling water) animate like any other; the material preview shows the current frame. | Vue material animation p1148–1150 |
| B4 | Non-numeric things are keyed as **step** tracks: an enum, a boolean, a texture file path, the active camera (a *camera switch* track). | AE hold keyframes; C4D Stage object |
| B5 | Groups: keys on a group move everything under it; children keep their own keys relative to the parent. | all |

### C. Keyframes

| id | Feature | Reference |
|---|---|---|
| C1 | A key is `(frame, value, interpolation, in-tangent, out-tangent, tangent-mode)`. Value has the property's type (float, int, bool, vec3, colour, string). | all |
| C2 | Record a key with: the **animation circle** next to the property label (click = add/remove at current frame; filled = key on this frame; hollow = animated but no key here; nothing = static); the `K` shortcut for the selected object's transform; **Autokey** — with it on, any edit to an animated property at a frame writes a key there. | C4D animation circle; Blender `I` and autokey; AE stopwatch |
| C3 | Key operations: select (click, box, all in track, all at frame), move (drag), copy/paste (same or different property with matching type), delete, duplicate, **retime** (scale a selection about a pivot), snap to whole frames, **mirror** (reverse a selection in time). | C4D Timeline; Blender Dope Sheet |
| C4 | Interpolation per key: **linear**, **bezier** (smooth), **step** (hold), and per-axis ease presets: ease in, ease out, ease in-out, **Easy Ease** (AE's F9). | AE, C4D |
| C5 | Tangent modes: **auto** (Catmull-Rom style, clamped so a value between two equal keys never overshoots — Blender's "auto-clamped"), **user** (dragged handles), **broken** (in/out independent), **weighted** (handle length changes the timing, AE style). | Blender, AE, C4D |
| C6 | Extrapolation before the first and after the last key: **constant**, **linear**, **cycle**, **cycle with offset**, **ping-pong** — so a spinning windmill needs two keys, not a thousand. | Blender F-curve modifiers; C4D Timeline |
| C7 | Keys on a vector or colour are stored as **one key per component** (X, Y, Z / R, G, B) so an X track can have a different key count from Y; the UI records all three at once unless a single component is chosen. | Blender, C4D |

### D. The Timeline (dope sheet)

| id | Feature | Reference |
|---|---|---|
| D1 | A dockable panel: ruler on top, a **track tree** on the left (object → property group → property → component), key rows on the right. Summary row per object and per group shows a diamond wherever any child has a key. | C4D Timeline, Blender Dope Sheet, AE layer bar |
| D2 | The track tree shows **only animated objects** by default ("Show animated only"); a toggle shows everything so keys can be added from the timeline; a filter box searches by name. | C4D Timeline filter |
| D3 | Key glyphs: diamond for bezier/auto, square for step, circle for linear; selected keys highlighted; a key that is off a whole frame is drawn hollow. | C4D key shapes |
| D4 | Ruler: current-frame marker, preview range as a shaded band, **markers** with names (right-click ruler → add marker; drag to move), start/end frame fields. | all four |
| D5 | Navigation: wheel zooms time about the cursor, middle-drag pans, `Home` fits all keys, `A` fits the selection. | Blender |
| D6 | **Motion paths**: with an object selected, its position track is drawn in the viewport as a curve with a dot per frame and a diamond per key; keys can be dragged in the viewport. | C4D, Blender, AE |
| D7 | Every track row has the same keyframe navigator as the property panel (A6). | Resolve |

### E. The Curve editor (F-Curve / Graph editor)

| id | Feature | Reference |
|---|---|---|
| E1 | Any track expands in place under the Timeline (or in its own window) as a value-against-time curve with a shared time axis. Several curves can be shown at once, each in its axis colour (X red, Y green, Z blue; scalars in the panel's accent colour). | C4D F-Curve; Blender Graph Editor; AE Graph Editor |
| E2 | Keys are draggable in both axes; tangent handles are visible and draggable; `Alt`-drag a handle breaks it; `Shift` constrains; the value ruler on the left shows the property's unit (metres, degrees, %). | all |
| E3 | Value view and **speed view** (the derivative) — AE's two graph modes; speed view is what makes an ease feel right. | AE |
| E4 | Normalised view: every curve scaled to 0..1 vertically so a 0–1 opacity and a 0–5000 m altitude can be compared on one graph. | Blender |
| E5 | Curve tools: smooth (average tangents), flatten, set extrapolation, bake (one key per frame), simplify (Ramer–Douglas–Peucker to a tolerance), apply an ease preset to the selection. | Blender, AE |
| E6 | Reference curve: a ghost of the curve before the current drag, so the change is visible. | Blender |

### F. Procedural motion and layering

| id | Feature | Reference |
|---|---|---|
| F1 | **Track modifiers** on a curve: noise (seeded, frequency, amplitude, octaves), oscillator (sine/triangle/square with phase), cycle, offset, limit (clamp), smooth. Stack order matters; each can be disabled. | Blender F-curve modifiers; C4D Vibrate tag; AE wiggle |
| F2 | **Expressions**: a property can be driven by a one-line expression over `t`, `frame`, other properties by path (`Camera 1.focal_mm`), and the node graph's published parameters — the driver model. | AE expressions; Blender drivers; C4D XPresso |
| F3 | **Clips (NLA)**: a range of keys on an object becomes a named clip that can be placed on a clip track, repeated, stretched, offset and blended with other clips; the underlying keys stay editable. | Blender NLA; C4D Motion Clips |
| F4 | **Time remap**: a curve mapping output time to source time on a clip or on the whole document — slow motion, freeze, reverse. | AE time remap; Vue TimeRemap |

### G. Camera and object behaviours

| id | Feature | Reference |
|---|---|---|
| G1 | **Target / look-at**: a camera or light tracks an object or a null; the constraint is a track that can itself be keyed (weight 0..1). | C4D Target tag; Vue tracking p1164 |
| G2 | **Path following**: an object rides a path (from the Points/Path nodes) with position along the path as the animated value, banking and look-ahead. | C4D Align to Spline; Vue look-ahead |
| G3 | **Camera switch track**: which camera renders at each frame. | C4D Stage; Vue camera switching p1145 |
| G4 | Motion blur from the animated transforms (shutter angle per camera) in the offline engines and as a post effect in the viewport. | Vue p1147 |

### H. Rendering an animation

| id | Feature | Reference |
|---|---|---|
| H1 | Render a frame range (or the preview range) to an image sequence with a padded frame number in the file name; every render pass (EXR AOVs) per frame; resume from the last written frame after a crash. | Vue animation render p1169–1180 |
| H2 | **Playblast / preview render**: the active viewport captured per frame to a sequence for a fast check. | Blender viewport render; C4D Make Preview |
| H3 | Per-frame deterministic evaluation: the same frame always renders bit-identically (the project's determinism rule applies to time too). | project rule |

### I. Editing ergonomics

| id | Feature | Reference |
|---|---|---|
| I1 | Keyframe selection colour on the property label (a property in the keyframe selection is tinted; Autokey records only those). | C4D Add Keyframe Selection |
| I2 | Right-click on any property: Add key · Remove key · Remove track · Show in timeline · Show F-curve · Copy/Paste animation · Add expression. | C4D Attribute Manager context menu |
| I3 | Undo covers every key edit as one step per gesture. | project rule |
| I4 | Copy animation between objects and paste with an offset. | Vue paste animation p1153 |
| I5 | The whole model is reachable through the API/MCP (`set_key`, `remove_key`, `keys`, `set_frame`, `play`, `set_range`, `bake`, `set_interpolation`, `add_marker`, `render_sequence`). | project rule |
| I6 | Every string in the module goes through `tr()`. | project rule |

---

## Part 2 — Where TerraForge stands (2026-09-05, phase 36)

Legend: ✅ done · 🟡 partial · ⬜ not started. The right column names the
code so the claim can be checked; the tests are `anim_tests`
(`tests/cpp/test_anim.cpp`) and the animation groups in `undo_tests`
(`tests/cpp/test_anim_scene.cpp`).

| id | State | Where |
|---|---|---|
| A1 | ✅ | `gpx::Timeline` (`engine/gpx/animation.hpp`) in `SceneState::timeline`; published as `timeline` in `scene_state.json` |
| A2 | ✅ | `Timeline::format/parse`; the transport's display combo |
| A3 | ✅ | `panel_timeline_transport.cpp`: start / prev key / prev frame / play / next frame / next key / end / stop; once, loop, ping-pong; the rate is the document fps |
| A4 | ✅ | `anim_playback.cpp`: real time (frames dropped to keep up) or "Every frame" |
| A5 | ✅ | ruler drag; `Ctrl` snaps to the nearest key |
| A6 | ✅ | `anim_key_nav()` in the transport and the Animation tool row |
| B1 | ✅ | `anim_targets.cpp`: transform, squeeze, colour, visibility, deformers, scatter, light, camera optics, planet, infinite-surface layers; sun, sky, fog, water, clouds, terrain height for the world |
| B2 | ✅ | node attributes through `Attribute::anim` (scalars) and `anim_v` (vector/colour components); `Graph::apply_animation` |
| B3 | ✅ | material properties are node attributes (B2) |
| B4 | ✅ | bool tracks are step tracks (`visible`, node bools/choices/seeds) |
| B5 | ✅ | transforms compose through the existing hierarchy |
| B4′ | 🟡 | a camera *switch* track (which camera renders per frame) is not a track yet; `CameraSwitch` node is the placeholder |
| C1 | ✅ | `gpx::Key` {time, value, interp, tangent mode, tan_in, tan_out} |
| C2 | ✅ | `anim_widgets.cpp`: the circle on every Properties row (`panel_properties_object.cpp`), `K`, Autokey |
| C3 | ✅ | `panel_timeline_keys.cpp`: click / Ctrl / box select, drag (frame-snapped, colliding keys merge), copy/paste/duplicate, delete, retime handles, mirror, snap |
| C4 | ✅ | linear / bezier / step per key; ease in / out / in-out / Easy Ease (F9) |
| C5 | ✅ | auto-clamped / user / broken tangents; weighted handles ⬜ (handle length is display only) |
| C6 | ✅ | constant / linear / cycle / cycle-offset / ping-pong, pre and post |
| C7 | ✅ | one track per component; the circle keys all unless a component is chosen |
| D1 | ✅ | `panel_timeline.cpp`: owner > group > component tree, summary diamonds |
| D2 | ✅ | "Animated only" toggle, filter box |
| D3 | ✅ | diamond / circle / square by interpolation; hollow when off a whole frame |
| D4 | ✅ | ruler ticks, preview band, named draggable markers, range fields |
| D5 | ✅ | Ctrl+wheel zoom, middle-drag pan, Home fit, A fit selection |
| D6 | ✅ | `renderer_motion_path.cpp`: the selected object's path with a cross per key (root objects; keys are not draggable in the viewport yet) |
| D7 | 🟡 | the navigator is in the transport, not on every row |
| E1 | ✅ | `panel_curve_editor.cpp`: chosen tracks in axis colours, shared time axis |
| E2 | ✅ | key and handle drags; Alt breaks; value ruler |
| E3 | ✅ | speed view |
| E4 | ✅ | normalised view |
| E5 | ✅ | Smooth / Flatten / Bake / Simplify / ease presets / extrapolation (`anim_curve.cpp`) |
| E6 | ✅ | ghost curve during a drag |
| F1 | ✅ | `gpx::Modifier`: noise, oscillator, offset, limit, smooth; stacked, each with an enable box |
| F2 | ✅ | `anim_expr.cpp`: arithmetic, functions, `t`/`frame`/`fps`/`value`, other properties by path via `anim_lookup` |
| F3 | ⬜ | clips / NLA — `AnimationClip` node is the placeholder |
| F4 | 🟡 | `TimeRemap` node remaps document time for the sequence renderer; not per clip |
| G1 | 🟡 | `CameraTarget` node (look-at); its weight is a node attribute, so keyable through B2 |
| G2 | 🟡 | `CameraPath` rides a path; the sequence fly-through samples it; no look-ahead control |
| G3 | 🟡 | see B4′ |
| G4 | 🟡 | per-camera `motion_blur` is keyable; the engines receive the shutter; no per-object blur |
| H1 | 🟡 | `render_sequence` op / Timeline capture: numbered PNGs through the viewport engine; EXR passes per frame and resume ⬜ |
| H2 | ✅ | `playblast` op |
| H3 | ✅ | `test_anim.cpp` determinism (noise/expressions hashed, never random); `anim_tests` |
| I1 | ⬜ | keyframe-selection tint on labels |
| I2 | ✅ | right-click on the circle: add/remove key, remove animation, show in timeline, show curve, extrapolation, interpolation |
| I3 | ✅ | one undo step per gesture |
| I4 | 🟡 | copy/paste of keys between tracks of the same object in the Timeline; whole-object animation copy ⬜ |
| I5 | ✅ | `anim_ops.cpp` + `mcp_server/studio_anim_tools.py` (22 tools); `tests/test_api_coverage.py` locks every op to a tool |
| I6 | ✅ | `tr()` throughout the new panels |

## Part 3 — Using it

### The animation circle

Every property that can be animated has a small circle to the left of its
label in the Properties panel — the same control Cinema 4D users know.

- **Empty circle**: the property is static.
- **Hollow ring**: the property is animated, but there is no key on the
  current frame.
- **Filled orange circle**: there is a key on this frame.
- **Click** the circle to add a key at the current frame, or to remove the
  key that is there. Adding the first key is what enables animation on the
  property; removing the last key makes it static again.
- **Ctrl+click** always adds; **Shift+click** always removes;
  **Ctrl+Shift+click** removes the whole track.
- **Right-click** the property for Show in timeline, Show F-curve, ease
  presets, extrapolation, Copy/Paste animation, Add expression.

### Recording a simple move

1. Select the object. Press `K` (or click the circle next to *Position*) at
   frame 0.
2. Drag the frame marker in the Timeline to frame 48.
3. Move the object with the gizmo or type a new position. With **Autokey**
   on (the red dot in the transport), the key is written for you; otherwise
   press `K` again.
4. Press **Play**. The object moves in the active viewport. The default
   interpolation is bezier with auto-clamped tangents, so it eases naturally.

### The Timeline

`Window > Timeline` (or the Animation workspace). The left tree lists
animated objects; expand one to see its property groups (Transform, Deform,
Light, Camera…) and each property's component tracks. A diamond is a key; a
square is a hold (step) key; a circle is linear.

- Drag keys to move them; box-select; `Ctrl+C`/`Ctrl+V`; `Delete`.
- **Retime**: select keys, drag the handles at either end of the selection.
- Right-click a key: interpolation, tangent mode, ease presets, mirror.
- Right-click the ruler: add a marker, set the preview range to the
  selection.
- The ▸ at the right of any track opens its **curve** below the rows.

### The Curve editor

Value against time. Drag keys, drag the tangent handles; `Alt`-drag breaks a
handle so in and out differ. **Speed** switches to the derivative. **Norm**
scales every visible curve to 0–1. The toolbar offers Smooth, Flatten,
Bake, Simplify, Ease presets and the extrapolation mode (Cycle for a
windmill, Cycle offset for a walking camera, Ping-pong for a swing).

### Modifiers and expressions

Right-click a track > **Add modifier**: Noise, Oscillator, Cycle, Offset,
Limit, Smooth. Each is a row under the track with its own controls and an
enable box. **Add expression** replaces the curve with a formula, e.g.
`sin(t * 2) * 0.3 + 1.2` or `Camera 1.focal_mm * 0.5`; the curve is drawn
from the expression so you can still see it.

### Rendering

`Render > Sequence` renders the frame range (or the preview range) with
the render settings of the active camera to `<name>_<frame>.png/.exr`, one
folder per sequence; every EXR pass comes out per frame. **Playblast**
captures the viewport instead — seconds, not minutes — for checking timing.

### From a script

```python
from mcp_server.studio_api import Studio
s = Studio()
s.send({"op": "set_range", "start": 0, "end": 120, "fps": 30})
s.send({"op": "set_key", "object": "Rock", "prop": "pos", "frame": 0,  "value": [0.5, 0.05, 0.5]})
s.send({"op": "set_key", "object": "Rock", "prop": "pos", "frame": 60, "value": [0.7, 0.05, 0.4], "ease": "easy"})
s.send({"op": "set_key", "node": "Terrain fractal", "attr": "seed", "frame": 0, "value": 1, "interp": "step"})
s.send({"op": "set_extrapolation", "object": "Rock", "prop": "rot", "mode": "cycle"})
s.send({"op": "render_sequence", "camera": "Camera 1", "output": "D:/out/shot", "start": 0, "end": 120})
```
