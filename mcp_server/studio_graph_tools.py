"""
Geekatplay TerraForge — node-level graph tools for MCP.

The studio tools in ``studio_api`` cover cameras, world and planets. These
cover the graph itself: building it, rewiring it, tuning it and — the part
that was missing entirely — reading it back.

Before these, an agent could write to the graph and never read it. It could
not see what nodes existed, so it could only ever bolt more on: never inspect,
never tune, never repair.

Every tool here is a thin wrapper over one action on the shared
``ai_apply_actions`` path, so the assistant, the Python API and MCP stay one
implementation rather than three.
"""

from typing import Any, Dict, Optional

GRAPH_TOOLS: Dict[str, Dict[str, Any]] = {
    "studio_get_graph": {
        "description": "The node graph as it stands: every node with its id, "
                       "type, category, position, bypass state, last compute "
                       "time and error, every link, plus the selected and "
                       "viewed node. Read this before editing.",
        "params": {},
    },
    "studio_add_node": {
        "description": "Add a node. `type` is a registry name (Noise, "
                       "Hydraulic, FieldNoise, TerrainOutput...). Optional x/y "
                       "position and an `attrs` object of parameter values. "
                       "`alias` names the node for later calls: every op that "
                       "takes `node`/`from`/`to` accepts the alias.",
        "params": {"type": "str", "x": "float", "y": "float", "attrs": "obj",
                   "alias": "str"},
    },
    "studio_delete_node": {
        "description": "Delete a node and every link touching it. `node` is an "
                       "id or a type name.",
        "params": {"node": "int|str"},
    },
    "studio_connect": {
        "description": "Link two nodes. Ports default to the first output and "
                       "the first input, which is what most nodes want.",
        "params": {"from": "int|str", "to": "int|str",
                   "from_port": "str", "to_port": "str"},
    },
    "studio_disconnect": {
        "description": "Remove a link, by `link` id or by naming the input end "
                       "(`to` and optionally `to_port`).",
        "params": {"link": "int", "to": "int|str", "to_port": "str"},
    },
    "studio_set_attr": {
        "description": "Set parameters on a node: `key`+`value`, or an `attrs` "
                       "object of several. Choices accept an index or a label, "
                       "colours a [r,g,b] triple or a single grey.",
        "params": {"node": "int|str", "key": "str", "value": "any",
                   "attrs": "obj"},
    },
    "studio_bypass": {
        "description": "Bypass a node — the graph reads straight through it, "
                       "as if it were not there. Pass bypass:false to restore.",
        "params": {"node": "int|str", "bypass": "bool"},
    },
    "studio_move_node": {
        "description": "Move a node in the editor. Layout only.",
        "params": {"node": "int|str", "x": "float", "y": "float"},
    },
    "studio_clear_graph": {
        "description": "Empty the graph. Undoable like any other edit.",
        "params": {},
    },
    "studio_set_resolution": {
        "description": "Graph evaluation resolution, 64..8192.",
        "params": {"resolution": "int"},
    },
    "studio_view_node": {
        "description": "Choose which node the 3D viewport shows. Omit `node` "
                       "to go back to automatic (the terrain result).",
        "params": {"node": "int|str"},
    },
    "studio_select_node": {
        "description": "Select a node in the editor, so the Properties panel "
                       "shows it. Selection only — it does not change what the "
                       "viewport renders (use studio_view_node for that).",
        "params": {"node": "int|str"},
    },
    "studio_set_workspace": {
        "description": "Choose the workflow: terrain, materials, atmosphere or "
                       "render (or 0..3). This is what the second toolbar row "
                       "selects, and it decides which tools the third row "
                       "offers.",
        "params": {"workspace": "int|str"},
    },
    "studio_show_panel": {
        "description": "Open or close one of the studio's dockable panels: "
                       "Library, Nodes, Properties, Viewport, Toolbar, "
                       "Console, Timeline, Preview, or Material Editor. The "
                       "Material Editor shows the selected object's material "
                       "as a layer stack, top layer first, with each layer's "
                       "opacity, blend mode and environment constraints.",
        "params": {"panel": "str", "visible": "bool"},
    },
    "studio_arrange_views": {
        "description": "Rearrange the 3D viewport area into 1-8 cells "
                       "(2 side by side, quad 2x2, 3x2 and so on). Only the "
                       "viewport area changes; the node editor, properties "
                       "and console keep their places.",
        "params": {"count": "int"},
    },
    "studio_add_view": {
        "description": "Open another 3D viewport beside the one last worked "
                       "in. By default it arrives as a tab; split=true puts "
                       "it in half the space instead, vertical=true splitting "
                       "downwards rather than to the right. Up to 8 exist at "
                       "once, and each can be dragged anywhere or floated "
                       "onto another monitor.",
        "params": {"split": "bool", "vertical": "bool", "view": "int"},
    },
    "studio_close_view": {
        "description": "Close one 3D viewport by number (1-8). The last "
                       "remaining viewport is never closed.",
        "params": {"view": "int"},
    },
    "studio_save_layout": {
        "description": "Save the current window arrangement under a name: "
                       "every window's place, which viewports are open and "
                       "what each one shows. A layout never contains the "
                       "scene, so it can be loaded over any project.",
        "params": {"name": "str"},
    },
    "studio_load_layout": {
        "description": "Restore a saved window arrangement by name.",
        "params": {"name": "str"},
    },
    "studio_delete_layout": {
        "description": "Delete a saved window arrangement by name.",
        "params": {"name": "str"},
    },
    "studio_list_layouts": {
        "description": "List the saved window arrangements; the names come "
                       "back in the studio's status line.",
        "params": {},
    },
    "studio_reset_layout": {
        "description": "Put every window back to the default arrangement.",
        "params": {},
    },
    "studio_set_viewport": {
        "description": "How the terrain surface is drawn: adaptive "
                       "subdivision (tessellation, tess_pixels, tess_min, "
                       "tess_max), per-patch culling (frustum_cull), the "
                       "surface itself (height_scale, planet_radius, "
                       "fractal_detail, fractal_scale, field_displacement) "
                       "and shading (wireframe, shadows, shadow_softness, "
                       "exposure, use_albedo, layout, engine). Also "
                       "graph_memory_mb, the ceiling on cached node output "
                       "buffers (0 lifts it) — read memory.buffers_mb and "
                       "memory.released_mb back from the state. Send only the "
                       "settings you want to change. cloud_scatter_octaves (1-4) sets "
                       "how many multiple-scattering bounces the cloud march "
                       "approximates and cloud_scatter_depth (0.4-0.99) how "
                       "far each reaches into the cloud.",
        "params": {"tessellation": "bool", "tess_pixels": "float",
                   "tess_min": "float", "tess_max": "float",
                   "frustum_cull": "bool", "height_scale": "float",
                   "planet_radius": "float", "fractal_detail": "float",
                   "fractal_scale": "float", "field_displacement": "float",
                   "wireframe": "bool", "shadows": "bool",
                   "shadow_softness": "float", "exposure": "float",
                   "use_albedo": "bool", "layout": "int", "engine": "int",
                   "graph_memory_mb": "int",
                   "cloud_scatter_octaves": "int",
                   "cloud_scatter_depth": "float"},
    },
    "studio_capture": {
        "description": "Render the active camera's viewport to a PNG at "
                       "`path`, optionally `width` x `height` (default "
                       "1280x720). The one way to check what the renderer "
                       "actually put on screen.",
        "params": {"path": "str", "width": "int", "height": "int"},
    },
    "studio_evaluate": {
        "description": "Re-evaluate the whole graph. Poll studio_get_graph and "
                       "watch eval.running / eval.serial to know when it is "
                       "done.",
        "params": {},
    },
    "studio_verify_field_gpu": {
        "description": "Check that field graphs evaluate identically on the "
                       "CPU and the GPU, and report the terrain shader's "
                       "state. Writes a report beside the API state file.",
        "params": {},
    },
    "studio_render_passes": {
        "description": "Render with the viewport engine: the beauty image in "
                       "format 0 PNG / 1 EXR / 2 HDR plus one linear EXR per "
                       "pass (depth, normal, position, object_id, water_mask, "
                       "albedo, direct, shadow, ambient, specular, atmosphere, "
                       "environment), named <path stem>_<pass>.exr.",
        "params": {"path": "str", "width": "int", "height": "int",
                   "format": "int", "passes": "list[str]|int"},
    },
    "studio_set_time": {
        "description": "Move the animation clock; keyed attributes re-sample "
                       "and the graph re-evaluates. Field: time (seconds).",
        "params": {"time": "float"},
    },
    "studio_set_key": {
        "description": "Key an attribute on its animation track. Fields: node "
                       "(type or id), attr, value (defaults to the current), "
                       "time (defaults to the clock), interp "
                       "(constant/linear/smooth), remove (bool).",
        "params": {"node": "str", "attr": "str", "value": "float",
                   "time": "float", "interp": "str", "remove": "bool"},
    },
    "studio_render_sequence": {
        "description": "Capture the animation range as numbered PNGs through "
                       "the viewport engine. Fields: dir, fps, start, end, "
                       "width, height; optional camera_path (a Points node "
                       "the camera rides), camera_height, sun_from/sun_to "
                       "([azimuth, altitude] pairs for a day cycle).",
        "params": {"dir": "str", "fps": "float", "start": "float",
                   "end": "float", "width": "int", "height": "int",
                   "camera_path": "str", "camera_height": "float",
                   "sun_from": "list", "sun_to": "list"},
    },
    "studio_set_scatter": {
        "description": "Bind a mesh object to a Points node: instanced copies "
                       "stand on the terrain at every point. Fields: object, "
                       "node (empty unbinds), size, jitter, seed, sway, "
                       "size_from_value.",
        "params": {"object": "str", "node": "str", "size": "float",
                   "jitter": "float", "seed": "int", "sway": "float",
                   "size_from_value": "float"},
    },
    "studio_import_object": {
        "description": "Import an OBJ as a scene mesh. Fields: path, name, "
                       "position ([x,y,z]), scale.",
        "params": {"path": "str", "name": "str", "position": "list",
                   "scale": "float"},
    },
    "studio_save_project": {
        "description": "Save the project (.gpxt) to `path`.",
        "params": {"path": "str"},
    },
    "studio_open_project": {
        "description": "Open a project (.gpxt) from `path`.",
        "params": {"path": "str"},
    },
    "studio_set_camera_key": {
        "description": "Key (or remove) the named camera's whole pose - eye "
                       "and target - at the playhead or a given time. Move "
                       "the camera between keys and render_sequence plays "
                       "the move.",
        "params": {"camera": "str", "time": "float", "remove": "bool"},
    },
    "studio_run_macro": {
        "description": "Apply a saved JSON action document (any ops, one "
                       "batch). Field: path.",
        "params": {"path": "str"},
    },
    "studio_set_locked": {
        "description": "Lock an object in place (no gizmo, no dragging, "
                       "transform read-only) or free it. Fields: object "
                       "(name; omitted = selected), locked (bool).",
        "params": {"object": "str", "locked": "bool"},
    },
    "studio_assign_material": {
        "description": "Bind a MaterialOutput node to a scene object (the "
                       "terrain when object is omitted); node '' unbinds. "
                       "Closes the ErosionLayers -> MaterialStack -> "
                       "MaterialOutput pipeline.",
        "params": {"node": "str", "object": "str"},
    },
    "studio_add_light": {
        "description": "Add a point light. Fields: name, position ([x,y,z]), "
                       "color ([r,g,b]), intensity, reach. set_light edits "
                       "by name with the same fields.",
        "params": {"name": "str", "position": "list", "color": "list",
                   "intensity": "float", "reach": "float"},
    },
    "studio_set_light": {
        "description": "Edit a light by name with the add_light fields: "
                       "position ([x,y,z]), color ([r,g,b]), intensity, reach.",
        "params": {"name": "str", "position": "list", "color": "list",
                   "intensity": "float", "reach": "float"},
    },
    "studio_export_instances": {
        "description": "Write the scattered copies of a mesh object as CSV "
                       "transforms (x, y, z, scale, yaw_radians per line). "
                       "Fields: path, object (optional: one mesh by name).",
        "params": {"path": "str", "object": "str"},
    },
    "studio_open_node_editor": {
        "description": "Open another node editor window pinned to a domain: "
                       "terrain, materials, atmosphere, render, objects, "
                       "lighting, cameras, animation, or all.",
        "params": {"domain": "int|str"},
    },
    "studio_add_primitive": {
        "description": "Add a built-in mesh: cube, sphere, plane, cylinder "
                       "or cone. Fields: kind, name, position, scale, color.",
        "params": {"kind": "str", "name": "str", "position": "list",
                   "scale": "float", "color": "list"},
    },
}

# tool name -> the action op it sends; None means it is handled specially
_SIMPLE = {
    "studio_add_node": "add_node",
    "studio_delete_node": "delete_node",
    "studio_connect": "connect",
    "studio_disconnect": "disconnect",
    "studio_set_attr": "set_attr",
    "studio_bypass": "bypass",
    "studio_move_node": "move_node",
    "studio_clear_graph": "clear_graph",
    "studio_set_resolution": "set_resolution",
    "studio_view_node": "view_node",
    "studio_select_node": "select_node",
    "studio_set_workspace": "set_workspace",
    "studio_show_panel": "show_panel",
    "studio_set_viewport": "set_viewport",
    "studio_arrange_views": "arrange_views",
    "studio_add_view": "add_view",
    "studio_close_view": "close_view",
    "studio_save_layout": "save_layout",
    "studio_load_layout": "load_layout",
    "studio_delete_layout": "delete_layout",
    "studio_list_layouts": "list_layouts",
    "studio_reset_layout": "reset_layout",
    "studio_capture": "capture",
    "studio_evaluate": "evaluate",
    "studio_verify_field_gpu": "verify_field_gpu",
    "studio_render_passes": "render_passes",
    "studio_set_time": "set_time",
    "studio_set_key": "set_key",
    "studio_render_sequence": "render_sequence",
    "studio_set_scatter": "set_scatter",
    "studio_import_object": "import_object",
    "studio_save_project": "save_project",
    "studio_open_project": "open_project",
    "studio_set_camera_key": "set_camera_key",
    "studio_run_macro": "run_macro",
    "studio_assign_material": "assign_material",
    "studio_set_locked": "set_locked",
    "studio_add_light": "add_light",
    "studio_set_light": "set_light",
    "studio_export_instances": "export_instances",
    "studio_open_node_editor": "open_node_editor",
    "studio_add_primitive": "add_primitive",
}


def handle_graph_tool(tool: str, params: Dict[str, Any],
                      studio: Optional[Any] = None) -> Optional[Dict[str, Any]]:
    """Dispatch one graph tool. Returns None when the tool is not ours, so the
    caller can keep looking."""
    if tool == "studio_get_graph":
        from .studio_api import Studio
        s = studio or Studio()
        st = s.state()
        return {
            "status": "success",
            "nodes": st.get("nodes", []),
            "links": st.get("links", []),
            "selected_node": st.get("selected_node"),
            "view_node": st.get("view_node"),
            "resolution": st.get("terrain", {}).get("resolution"),
            "eval": st.get("eval", {}),
        }

    op = _SIMPLE.get(tool)
    if op is None:
        return None
    from .studio_api import Studio
    s = studio or Studio()
    return {"status": "success", "sent": s.send({"op": op, **params})}
