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
    "studio_set_material": {
        "description": "Set one property of a material by key: tint, "
                       "color_gain, saturation, roughness, specular, "
                       "highlight_model, highlight_color, transparency, ior, "
                       "reflection, reflect_min, reflect_angle, metallic, "
                       "translucency, sss_color, backlight, diffuse, ambient, "
                       "luminous, contrast, cast_shadows... (the Material "
                       "Studio's tabs).",
        "params": {"material": "str|int", "key": "str", "value": "any"},
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
    "studio_preset_material": {
        "description": "Make one of the base materials in the graph - Basic "
                       "Color, Mirror, Wax, Slime, Metal, Copper, Gold, Iron, "
                       "Glass, Water, Ice, Smoke, Glow, Ground, Sand, Rock, "
                       "Snow, Swamp, Displacement rock, Displacement ground - "
                       "and optionally assign it to a named object.",
        "params": {"name": "str", "object": "str"},
    },
    "studio_paint_save": {
        "description": "Save the Height Paint layer (the terrain's painted "
                       "sculpt, as 16-bit greyscale PNG) to a path.",
        "params": {"path": "str"},
    },
    "studio_paint_load": {
        "description": "Load a greyscale image into the Height Paint layer: mid "
                       "grey leaves the terrain alone, darker carves, lighter "
                       "raises.",
        "params": {"path": "str"},
    },
    "studio_paint_clear": {
        "description": "Reset the Height Paint layer to mid grey (no effect).",
        "params": {},
    },
    "studio_list_settings": {
        "description": "Every saved render/world setting by name with its "
                       "current value (sun, sky, fog, clouds, water, terrain "
                       "size and shape, LOD, render editor...), in the state's "
                       "reply.",
        "params": {},
    },
    "studio_set_setting": {
        "description": "Set one render/world setting by the name "
                       "list_settings reports: a number, a bool, a [r,g,b] "
                       "colour or a string as the setting takes.",
        "params": {"key": "str", "value": "any"},
    },
    "studio_perf_report": {
        "description": "The performance watcher's report: frame phases, event "
                       "rates (evaluations, uploads, view draws, lock misses), "
                       "memory, and findings about work done for nothing. The "
                       "JSON comes back in the state's reply; the findings in "
                       "status. Also written to logs/perf_watch.json every 10 s.",
        "params": {},
    },
    "studio_crash_reports": {
        "description": "Every crash report, hang report (the watchdog caught the "
                       "main thread standing still) and session killed without a "
                       "clean exit in logs/, newest first, each with a one-line "
                       "summary and whether it has been dealt with. JSON in the "
                       "state's reply. Check this first in a development session.",
        "params": {},
    },
    "studio_crash_mark_fixed": {
        "description": "Close one crash/hang report by its file name, with a note "
                       "saying what fixed it; it leaves the startup warning and "
                       "the open count.",
        "params": {"file": "str", "note": "str"},
    },
    "studio_ai_generate_texture": {
        "description": "Generate a seamless tileable texture from a prompt with "
                       "the configured image provider (ComfyUI, OpenAI Images, "
                       "Stability, Google Imagen); apply connects it to a "
                       "material's channel. Runs as a background job.",
        "params": {"prompt": "str", "negative": "str", "provider": "str",
                   "width": "int", "seed": "int", "apply": "bool",
                   "material": "str|int", "channel": "str", "workflow": "str"},
    },
    "studio_ai_generate_skydome": {
        "description": "Generate a 2:1 equirectangular 360 skydome from a "
                       "prompt; apply sets it as the sky backdrop.",
        "params": {"prompt": "str", "negative": "str", "provider": "str",
                   "width": "int", "seed": "int", "apply": "bool"},
    },
    "studio_ai_generate_image": {
        "description": "Generate a plain image from a prompt into the texture library.",
        "params": {"prompt": "str", "negative": "str", "provider": "str",
                   "width": "int", "height": "int", "seed": "int"},
    },
    "studio_ai_generate_model": {
        "description": "Generate a 3D model from a prompt and/or a reference "
                       "picture with Meshy, Tripo or Hitem3D; import places it "
                       "in the scene when it arrives.",
        "params": {"prompt": "str", "image": "str", "provider": "str", "import": "bool"},
    },
    "studio_ai_describe": {
        "description": "Build the scene, the terrain or the atmosphere from a "
                       "natural-language description: the text model plans a "
                       "list of studio actions and they are applied when the "
                       "answer arrives (a background job).",
        "params": {"prompt": "str", "scope": "str", "image": "str"},
    },
    "studio_ai_ask": {
        "description": "Ask the configured text model (Ollama, OpenAI, Anthropic, "
                       "Gemini) a question, optionally about a picture.",
        "params": {"prompt": "str", "system": "str", "image": "str", "provider": "str"},
    },
    "studio_ai_jobs": {
        "description": "List AI generation jobs with state, progress and results.",
        "params": {},
    },
    "studio_config_set_service": {
        "description": "Set an AI service's key, endpoint, model or enabled flag "
                       "(openai, anthropic, google, stability, openai_image, "
                       "google_image, comfyui, ollama, meshy, tripo, hitem3d, "
                       "replicate, fal). Keys are stored protected.",
        "params": {"service": "str", "key": "str", "endpoint": "str",
                   "model": "str", "enabled": "bool"},
    },
    "studio_ai_job_cancel": {
        "description": "Cancel a running AI generation job by id.",
        "params": {"id": "int"},
    },
    "studio_config_set_defaults": {
        "description": "Choose the default providers (text_provider, "
                       "image_provider, model_provider) and the ComfyUI address "
                       "or installation folder.",
        "params": {"text_provider": "str", "image_provider": "str",
                   "model_provider": "str", "comfy_url": "str", "comfy_install": "str"},
    },
    "studio_config_check_comfy": {
        "description": "Probe the configured ComfyUI server: node types and "
                       "checkpoints it has, or why it cannot be reached.",
        "params": {},
    },
    "studio_config_status": {
        "description": "Which AI services are ready, and the ComfyUI address.",
        "params": {},
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
    "studio_set_material": "set_material",
    "studio_save_material": "save_material",
    "studio_load_material": "load_material",
    "studio_preset_material": "preset_material",
    "studio_paint_save": "paint_save",
    "studio_paint_load": "paint_load",
    "studio_paint_clear": "paint_clear",
    "studio_perf_report": "perf_report",
    "studio_crash_reports": "crash_reports",
    "studio_crash_mark_fixed": "crash_mark_fixed",
    "studio_list_settings": "list_settings",
    "studio_set_setting": "set_setting",
    "studio_ai_generate_texture": "ai_generate_texture",
    "studio_ai_generate_skydome": "ai_generate_skydome",
    "studio_ai_generate_image": "ai_generate_image",
    "studio_ai_generate_model": "ai_generate_model",
    "studio_ai_describe": "ai_describe",
    "studio_ai_ask": "ai_ask",
    "studio_ai_jobs": "ai_jobs",
    "studio_config_set_service": "config_set_service",
    "studio_ai_job_cancel": "ai_job_cancel",
    "studio_config_set_defaults": "config_set_defaults",
    "studio_config_check_comfy": "config_check_comfy",
    "studio_config_status": "config_status",
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
