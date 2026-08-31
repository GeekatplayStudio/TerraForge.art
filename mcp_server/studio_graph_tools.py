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
                       "position and an `attrs` object of parameter values.",
        "params": {"type": "str", "x": "float", "y": "float", "attrs": "obj"},
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
    "studio_evaluate": "evaluate",
    "studio_verify_field_gpu": "verify_field_gpu",
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
