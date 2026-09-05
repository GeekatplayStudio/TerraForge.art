"""
Geekatplay TerraForge — the Material Studio and the asset manager as MCP
tools.

Materials: open one in the studio, list them with their types, set a type
(scaffolding the nodes it needs), save to and load from the library.

Assets: the index of every watched folder — materials, meshes, textures,
layouts — searched by name, folder, tag or note; open, tag, annotate, trash
(never delete), restore; manage the folders it watches.

Every tool is one action on the shared ``ai_apply_actions`` path; the table
is merged into ``GRAPH_TOOLS`` by studio_graph_tools, which dispatches them.
"""

from typing import Any, Dict

ASSET_TOOLS: Dict[str, Dict[str, Any]] = {
    "studio_open_material": {
        "description": "Open a project material in the Material Studio (the "
                       "Materials workspace): its live preview, channels, "
                       "surface and layers. By name or node id.",
        "params": {"material": "str|int"},
    },
    "studio_list_materials": {
        "description": "List every material in the project with its type: "
                       "simple, PBR textures, mixed, layered, distribution "
                       "(presence also places objects) or effector (a typed "
                       "influence field).",
        "params": {},
    },
    "studio_set_material_type": {
        "description": "Give a material a type, scaffolding the nodes it "
                       "needs and keeping what was already connected: simple, "
                       "pbr, mixed, layered, distribution or effector.",
        "params": {"material": "str|int", "type": "str"},
    },
    "studio_save_material": {
        "description": "Save a material to the library with a rendered "
                       "thumbnail.",
        "params": {"material": "str|int"},
    },
    "studio_load_material": {
        "description": "Load a library material into the project by name; "
                       "open it in the studio and/or assign it to the "
                       "selected object.",
        "params": {"name": "str", "open": "bool", "assign": "bool"},
    },
    "studio_asset_search": {
        "description": "Search the asset index - materials, meshes, textures, "
                       "layouts in every watched folder - by name, folder, tag "
                       "or note (tf-idf cosine with prefix matching). Returns "
                       "ids of the form kind/relative-path.",
        "params": {"query": "str", "kind": "str", "limit": "int",
                   "include_trashed": "bool"},
    },
    "studio_asset_open": {
        "description": "Open an asset by id: a material loads into the project "
                       "and opens in the Material Studio, a mesh imports as an "
                       "object, a layout applies.",
        "params": {"id": "str"},
    },
    "studio_asset_tag": {
        "description": "Add a tag to an asset; tags are searchable and kept "
                       "across rescans.",
        "params": {"id": "str", "tag": "str"},
    },
    "studio_asset_untag": {
        "description": "Remove a tag from an asset.",
        "params": {"id": "str", "tag": "str"},
    },
    "studio_asset_note": {
        "description": "Write a free-text note on an asset; its words become "
                       "searchable.",
        "params": {"id": "str", "text": "str"},
    },
    "studio_asset_trash": {
        "description": "Move an asset's file to the trash folder beside its "
                       "root (never deletes).",
        "params": {"id": "str"},
    },
    "studio_asset_restore": {
        "description": "Bring a trashed asset back.",
        "params": {"id": "str"},
    },
    "studio_asset_rescan": {
        "description": "Rescan every watched folder, keeping tags and notes.",
        "params": {},
    },
    "studio_asset_roots": {
        "description": "List the folders the asset index watches and their kinds.",
        "params": {},
    },
    "studio_asset_add_root": {
        "description": "Watch a folder as a kind (material, mesh, texture, "
                       "layout, macro, other) and index it.",
        "params": {"path": "str", "kind": "str"},
    },
    "studio_asset_remove_root": {
        "description": "Stop watching a folder.",
        "params": {"path": "str"},
    },
}

# tool name -> the action op it sends
ASSET_SIMPLE = {
    "studio_open_material": "open_material",
    "studio_list_materials": "list_materials",
    "studio_set_material_type": "set_material_type",
    "studio_save_material": "save_material",
    "studio_load_material": "load_material",
    "studio_asset_search": "asset_search",
    "studio_asset_open": "asset_open",
    "studio_asset_tag": "asset_tag",
    "studio_asset_untag": "asset_untag",
    "studio_asset_note": "asset_note",
    "studio_asset_trash": "asset_trash",
    "studio_asset_restore": "asset_restore",
    "studio_asset_rescan": "asset_rescan",
    "studio_asset_roots": "asset_roots",
    "studio_asset_add_root": "asset_add_root",
    "studio_asset_remove_root": "asset_remove_root",
}
