"""
Geekatplay TerraForge — the plant library as MCP tools.

The Plants workspace's library: the built-in plants, the free CC0 plants
downloaded from Poly Haven and the user's own recorded models. List them,
put one in the scene (or scatter it), download the free set, record a model.

Every tool is one action on the shared ``ai_apply_actions`` path; the table
is merged into ``GRAPH_TOOLS`` by studio_graph_tools, which dispatches them.
"""

from typing import Any, Dict

PLANT_TOOLS: Dict[str, Dict[str, Any]] = {
    "studio_plant_library": {
        "description": "List the plant library in the reply: every plant's id, "
                       "name, source (builtin, polyhaven, user), group (trees, "
                       "shrubs, ground cover, flowers, grass, deadwood, rocks), "
                       "height in metres, licence and, for a file that holds a "
                       "set, its variants. Filter by query words, group or "
                       "source; rescan reads the library folder again.",
        "params": {"query": "str", "group": "str", "source": "str", "limit": "int",
                   "rescan": "bool"},
    },
    "studio_plant_add": {
        "description": "Put a library plant in the scene at its real size, "
                       "standing on the ground, with the node that drives it: "
                       "under the view's pivot, or at position [x, y, z] in "
                       "tile units. plant is its id (polyhaven/fern_02, "
                       "builtin/pine); variant picks one plant of a set; size "
                       "multiplies its own size; scatter also covers the "
                       "terrain with count copies through a Scatter points node.",
        "params": {"plant": "str", "variant": "str", "position": "list", "size": "float",
                   "heading_deg": "float", "scatter": "bool", "count": "int", "name": "str"},
    },
    "studio_plant_fetch": {
        "description": "Download the free CC0 plants from Poly Haven into the "
                       "plant library in the background (the curated set, or "
                       "the given asset ids); files already there are kept. "
                       "cancel stops a running download.",
        "params": {"ids": "list", "res": "str", "cancel": "bool"},
    },
    "studio_plant_record": {
        "description": "Record a plant model the user owns (glTF, FBX, OBJ) in "
                       "the plant library without copying it. height_m sets the "
                       "size it is placed at; 0 reads its units from the file.",
        "params": {"path": "str", "name": "str", "height_m": "float"},
    },
    "studio_plant_species_new": {
        "description": "Grow a new plant species from rules and stand it in the "
                       "scene: from words (a plant's name and modifiers such as "
                       "'old weeping willow in autumn' - over a hundred plants are "
                       "known without a model), from a description object, or from "
                       "an archetype. ai asks the configured text model to "
                       "describe the plant first. individuals makes several "
                       "plants of the species, each its own seed.",
        "params": {"words": "str", "description": "dict", "archetype": "str", "height_m": "float",
                   "name": "str", "place": "bool", "position": "list", "size": "float",
                   "heading_deg": "float", "scatter": "bool", "count": "int",
                   "individuals": "int", "ai": "bool", "image": "str"},
    },
    "studio_plant_species_describe": {
        "description": "The botanical description a plant's name gives (archetype, "
                       "height, crown, bark, leaves, flowers, fruit, seasons) in the "
                       "reply, to edit and pass to studio_plant_species_new.",
        "params": {"words": "str"},
    },
    "studio_plant_species_set": {
        "description": "Change a species' age, max age, health (0 dying .. 1 "
                       "thriving), season (0 midwinter, 0.25 spring, 0.5 summer, "
                       "0.75 autumn), seed, wind strength and direction, and "
                       "meshing detail.",
        "params": {"species": "str", "age": "float", "max_age": "float", "health": "float",
                   "season": "float", "seed": "int", "wind_strength": "float",
                   "wind_direction": "float", "detail": "float", "receive_wind": "bool"},
    },
    "studio_plant_species_variation": {
        "description": "Another individual of a species: a new seed (-1) or a given "
                       "one; flag keeps the current seed in the species' flagged list.",
        "params": {"species": "str", "seed": "int", "flag": "bool"},
    },
    "studio_plant_species_individuals": {
        "description": "More individuals of a species sharing its parts, each with "
                       "its own seed and its own object standing beside the first.",
        "params": {"species": "str", "count": "int"},
    },
    "studio_plant_part_add": {
        "description": "Add a part to a species from a node preset (trunk, branch, "
                       "stem, palm, twig, leaf, billboard leaf, growth, flower, "
                       "cutout leaf, fruit, bark material, leaf material) onto a "
                       "parent part: 'trunk', 'root', a node id or a node type.",
        "params": {"species": "str", "parent": "str", "preset": "str", "slot": "int"},
    },
    "studio_plant_species_preset": {
        "description": "Store, apply, delete or list a species' named presets (age, "
                       "health, season, seed and published parameter values).",
        "params": {"species": "str", "action": "str", "name": "str"},
    },
    "studio_plant_species_save": {
        "description": "Save a species into the plant library (its graph, its "
                       "pictures, a thumbnail), where Add and Scatter place it.",
        "params": {"species": "str", "name": "str", "group": "str", "note": "str"},
    },
    "studio_plant_species_load": {
        "description": "Load a species from the plant library (plant id such as "
                       "species/weeping_willow) or a species.json path into the "
                       "graph and stand its plant in the scene.",
        "params": {"plant": "str", "path": "str", "place": "bool", "position": "list",
                   "size": "float", "scatter": "bool", "count": "int"},
    },
    "studio_spray_biome": {
        "description": (
            "Lay out a whole biome as a stack of populations, in the order ground assembles: "
            "boulders first, then what stands on them, then what lives among that, then the "
            "cover, then the litter. Each layer is placed against whichever group its strongest "
            "ecological rule is about, so a pine wood comes out with moss and lichen on the "
            "boulders, needle litter under the conifers, fungi on the dead wood and no grass. "
            "One brush paints all of it."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "biome": {"type": "string", "description": "its id, e.g. pine_wood, sonoran, mars_plain, reef"},
                "area_m": {"type": "number"},
                "unbounded": {"type": "boolean"},
            },
            "required": ["biome"],
        },
    },
    "studio_spray_biomes": {
        "description": "Every biome, or every one of a family (forest, field, farm, desert, water, underwater, alien, built), with the groups each is made of.",
        "inputSchema": {"type": "object", "properties": {"family": {"type": "string"}}},
    },
    "studio_spray_groups": {
        "description": (
            "The ecology's vocabulary: every group, its tier in the assembly order, its "
            "footprint, and every rule about how it behaves near another group with the "
            "observation each came from. Rules are about groups, not models, so anything "
            "assigned to a group is placed correctly everywhere that group appears."
        ),
        "inputSchema": {"type": "object", "properties": {}},
    },
    "studio_spray_new": {
        "description": (
            "The spray brush: a painted population of several kinds at once. Makes a paint mask "
            "feeding a population layer - paint the mask in the viewport to say where - and takes "
            "a list of components saying what. Each component is a plant grown from its name or an "
            "object already in the scene, with its percent of the whole, its scale, how much that "
            "size varies for that kind alone, and how far it leans with the slope."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "components": {"type": "array", "description": "each {plant|object, percent, scale, size_variation, lean}"},
                "density_ha": {"type": "number", "description": "instances per hectare where the paint is full"},
                "spacing_m": {"type": "number", "description": "the least distance between two of them"},
                "clumping": {"type": "number", "description": "0 evenly spread, 1 tight groups"},
                "size_variation": {"type": "number", "description": "the layer's own, for kinds that do not set their own"},
                "area_m": {"type": "number"},
                "unbounded": {"type": "boolean"},
            },
        },
    },
    "studio_spray_add": {
        "description": "Add a kind to a spray: a plant by name or an object in the scene, with its percent of the whole.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "spray": {"type": "integer", "description": "the spray's node id; omit when there is only one"},
                "plant": {"type": "string"},
                "object": {"type": "string"},
                "percent": {"type": "number"},
                "scale": {"type": "number"},
                "size_variation": {"type": "number"},
                "lean": {"type": "number"},
            },
        },
    },
    "studio_spray_list": {
        "description": "Every spray in the scene, its components, and each one's share as a percentage of the whole.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    "studio_spray_set": {
        "description": (
            "Change one kind of a spray (with \"slot\"), or the brush itself (without it: "
            "density_ha, spacing_m, clumping, size_variation)."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "spray": {"type": "integer"},
                "slot": {"type": "integer", "description": "which kind, 0-based"},
                "percent": {"type": "number"},
                "scale": {"type": "number"},
                "size_variation": {"type": "number"},
                "lean": {"type": "number"},
                "density_ha": {"type": "number"},
                "spacing_m": {"type": "number"},
                "clumping": {"type": "number"},
            },
        },
    },
    "studio_plant_forest": {
        "description": (
            "Plant a wood of one kind. The species is grown `individuals` times, each with its "
            "own seed so no two trees are alike, and one population layer splits its points "
            "among them: the wood costs what those few trees cost however many stand in it. "
            "An altitude band keeps them out of the water, a slope band off the cliffs, and "
            "clumping gathers them into stands. Give `words` (a plant's name) or `species`."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "words": {"type": "string", "description": "the plant to grow, e.g. 'scots pine'"},
                "species": {"type": "string", "description": "an existing species instead of words"},
                "individuals": {"type": "integer", "description": "different trees the wood is made of, 1..8 (default 5)"},
                "area_m": {"type": "number", "description": "how far the wood reaches (default 1200)"},
                "spacing_m": {"type": "number", "description": "metres between trees; 0 takes it from their height"},
                "density_ha": {"type": "number", "description": "trees per hectare; 0 works it out from spacing"},
                "clumping": {"type": "number", "description": "0 an even orchard, 1 tight stands (default 0.55)"},
                "altitude_lo": {"type": "number", "description": "lowest ground it grows on, 0..1 of the height range"},
                "altitude_hi": {"type": "number", "description": "highest ground it grows on, 0..1"},
                "max_slope_deg": {"type": "number", "description": "steepest ground it grows on (default 34)"},
                "size_variation": {"type": "number", "description": "0 all one size, 1 half to twice (default 0.35)"},
                "unbounded": {"type": "boolean", "description": "follow the camera over the whole ground (default true)"},
            },
        },
    },
    "studio_plant_species_list": {
        "description": "The plant species in the graph (with height and triangle "
                       "counts) and in the library, the archetypes and the part "
                       "presets, in the reply.",
        "params": {},
    },
    "studio_plant_species_export": {
        "description": "Export a species' grown plant as a mesh file: .glb or .gltf "
                       "with its pictures embedded, or .obj with .mtl.",
        "params": {"species": "str", "path": "str"},
    },
}

# tool name -> the action op it sends
PLANT_SIMPLE = {
    "studio_plant_library": "plant_library",
    "studio_plant_add": "plant_add",
    "studio_plant_fetch": "plant_fetch",
    "studio_plant_record": "plant_record",
    "studio_plant_species_new": "plant_species_new",
    "studio_plant_species_describe": "plant_species_describe",
    "studio_plant_species_set": "plant_species_set",
    "studio_plant_species_variation": "plant_species_variation",
    "studio_plant_species_individuals": "plant_species_individuals",
    "studio_plant_part_add": "plant_part_add",
    "studio_plant_species_preset": "plant_species_preset",
    "studio_plant_species_save": "plant_species_save",
    "studio_plant_species_load": "plant_species_load",
    "studio_spray_biome": "spray_biome",
    "studio_spray_biomes": "spray_biomes",
    "studio_spray_groups": "spray_groups",
    "studio_spray_new": "spray_new",
    "studio_spray_add": "spray_add",
    "studio_spray_list": "spray_list",
    "studio_spray_set": "spray_set",
    "studio_plant_forest": "plant_forest",
    "studio_plant_species_list": "plant_species_list",
    "studio_plant_species_export": "plant_species_export",
}
