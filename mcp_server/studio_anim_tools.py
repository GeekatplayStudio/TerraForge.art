"""
Geekatplay TerraForge — animation tools for MCP.

Every tool is a thin wrapper over one action on the shared
``ai_apply_actions`` path (studio/anim_ops.cpp), so the assistant, the
Python API and MCP stay one implementation. Tracks are addressed the same
way everywhere: ``object`` + ``prop`` (``comp`` 0..2 or "x"/"y"/"z" for one
component), ``world`` (a RenderSettings path such as ``sun_azimuth``),
``node`` + ``attr`` (a graph attribute), or a raw ``track`` id as returned by
``studio_keys``. Times are given as ``frame`` (preferred) or ``time`` in
seconds; both default to the current frame. See docs/ANIMATION.md.
"""

from typing import Any, Dict

_ADDR = {"object": "str", "prop": "str", "comp": "str", "world": "str",
         "node": "str", "attr": "str", "track": "str"}

ANIM_TOOLS: Dict[str, Dict[str, Any]] = {
    "studio_set_key": {
        "description": "Record a key. Address a track (object+prop, world, "
                       "node+attr or track); `value` (number, bool or [x,y,z]) "
                       "is written to the property too, else the live value "
                       "is keyed. Optional `interp` linear|smooth|step|bezier "
                       "and `ease` easy|in|out|linear|hold. The first key on a "
                       "property is what makes it animated.",
        "params": {**_ADDR, "frame": "float", "time": "float", "value": "any",
                   "interp": "str", "ease": "str"},
    },
    "studio_remove_key": {
        "description": "Remove the key at `frame` (or now) from the addressed "
                       "track(s). Removing the last key makes the property "
                       "static again.",
        "params": {**_ADDR, "frame": "float", "time": "float"},
    },
    "studio_remove_animation": {
        "description": "Remove every key, modifier and expression from the "
                       "addressed track(s) so the property is static.",
        "params": _ADDR,
    },
    "studio_keys": {
        "description": "List animation: every animated track (owner, group, "
                       "label, component, extrapolation, expression, modifier "
                       "count, keys with frame/value/interp/tangents), or "
                       "only the addressed track. The answer is published in "
                       "the state's `reply`.",
        "params": _ADDR,
    },
    "studio_set_frame": {
        "description": "Move the clock to a frame.",
        "params": {"frame": "float"},
    },
    "studio_play": {
        "description": "Start (or with playing=false pause) playback in the "
                       "active viewport.",
        "params": {"playing": "bool"},
    },
    "studio_stop": {
        "description": "Stop playback and return to the start of the range.",
        "params": {},
    },
    "studio_set_range": {
        "description": "The document timeline: `fps`, `start`/`end` in frames "
                       "(or start_s/end_s seconds), `preview_start`/`preview_end`, "
                       "`preview` on/off, `loop` once|loop|pingpong, `autokey`, "
                       "`display` frames|timecode|seconds.",
        "params": {"fps": "float", "start": "float", "end": "float",
                   "start_s": "float", "end_s": "float", "preview": "bool",
                   "preview_start": "float", "preview_end": "float",
                   "loop": "str", "autokey": "bool", "display": "str"},
    },
    "studio_add_marker": {
        "description": "Add a named marker on the ruler at `frame` (or now).",
        "params": {"frame": "float", "name": "str"},
    },
    "studio_remove_marker": {
        "description": "Remove the marker named `name`, or the one at `frame`.",
        "params": {"frame": "float", "name": "str"},
    },
    "studio_key_transform": {
        "description": "Key the selected object's position, rotation and size "
                       "at the current frame (the K shortcut).",
        "params": {},
    },
    "studio_set_extrapolation": {
        "description": "What the curve does outside its keys: `pre` and/or "
                       "`post` (or `mode` for both) constant|linear|cycle|"
                       "cycle_offset|pingpong. Cycle turns two keys into a "
                       "spinning windmill.",
        "params": {**_ADDR, "pre": "str", "post": "str", "mode": "str"},
    },
    "studio_set_ease": {
        "description": "Interpolation or ease of the key at `frame` (or every "
                       "key on the track): `interp` linear|smooth|step|bezier, "
                       "`ease` easy|in|out|linear|hold.",
        "params": {**_ADDR, "frame": "float", "interp": "str", "ease": "str"},
    },
    "studio_bake_track": {
        "description": "One linear key per frame between the first and last "
                       "key; modifiers and the expression are folded in.",
        "params": _ADDR,
    },
    "studio_simplify_track": {
        "description": "Remove keys whose absence changes the curve by less "
                       "than `tolerance`.",
        "params": {**_ADDR, "tolerance": "float"},
    },
    "studio_snap_keys": {
        "description": "Snap every key of the track to whole frames.",
        "params": _ADDR,
    },
    "studio_mirror_keys": {
        "description": "Reverse the track in time about its own centre.",
        "params": _ADDR,
    },
    "studio_retime": {
        "description": "Scale the track's key times by `factor` about `pivot` "
                       "(frame; default the first key). 2 = twice as slow.",
        "params": {**_ADDR, "factor": "float", "pivot": "float"},
    },
    "studio_set_expression": {
        "description": "Drive the track by an expression over t, frame, fps, "
                       "value (the curve), pi, sin/cos/abs/min/max/clamp/lerp/"
                       "pow/noise and other properties by path "
                       "(\"Camera 1\".cam.focal_mm, world.sun_azimuth). Empty "
                       "removes it.",
        "params": {**_ADDR, "expr": "str"},
    },
    "studio_add_modifier": {
        "description": "Add a curve modifier: `type` noise|oscillator|offset|"
                       "limit|smooth with amplitude/frequency/phase/octaves/"
                       "seed/shape (0 sine 1 triangle 2 square), min/max for "
                       "limit, window for smooth.",
        "params": {**_ADDR, "type": "str", "amplitude": "float",
                   "frequency": "float", "phase": "float", "octaves": "int",
                   "seed": "int", "shape": "int", "min": "float",
                   "max": "float", "window": "float"},
    },
    "studio_clear_modifiers": {
        "description": "Remove every modifier from the track.",
        "params": _ADDR,
    },
    "studio_playblast": {
        "description": "Capture the active viewport for every frame of the "
                       "play range (or start..end frames) into `dir`, one PNG "
                       "per frame at width x height - a fast timing check.",
        "params": {"dir": "str", "width": "int", "height": "int",
                   "start": "float", "end": "float"},
    },
}

ANIM_SIMPLE = {
    "studio_set_key": "set_key",
    "studio_remove_key": "remove_key",
    "studio_remove_animation": "remove_animation",
    "studio_keys": "keys",
    "studio_set_frame": "set_frame",
    "studio_play": "play",
    "studio_stop": "stop",
    "studio_set_range": "set_range",
    "studio_add_marker": "add_marker",
    "studio_remove_marker": "remove_marker",
    "studio_key_transform": "key_transform",
    "studio_set_extrapolation": "set_extrapolation",
    "studio_set_ease": "set_ease",
    "studio_bake_track": "bake",
    "studio_simplify_track": "simplify",
    "studio_snap_keys": "snap_keys",
    "studio_mirror_keys": "mirror_keys",
    "studio_retime": "retime",
    "studio_set_expression": "set_expression",
    "studio_add_modifier": "add_modifier",
    "studio_clear_modifiers": "clear_modifiers",
    "studio_playblast": "playblast",
}
