"""Geekatplay TerraForge — scripting API and MCP tools for the studio.

The app writes its scene (cameras, sun, sky, water, render assignments) to a
JSON side file and watches an inbox file for action documents. Scripts, the
MCP server and the in-app AI assistant all speak the same action schema, so
anything you can do by typing a request in the UI can be done from code.

    from mcp_server.studio_api import Studio
    s = Studio()
    s.add_camera(name="Hero", focal_mm=50, aperture=2.8,
                 film="Kodak Portra 400", look_at="terrain", distance=2.2)
    s.set_sun(azimuth_deg=220, altitude_deg=8)
    s.render(samples=256, output="hero.png")
"""
from __future__ import annotations

import json
import os
import tempfile
import time
from typing import Any, Dict, List, Optional


def studio_dir() -> str:
    base = os.environ.get("LOCALAPPDATA") or tempfile.gettempdir()
    d = os.path.join(base, "GeekatplayTerraForge", "api")
    os.makedirs(d, exist_ok=True)
    return d


STATE_FILE = "scene_state.json"
INBOX_FILE = "actions_inbox.json"


class Studio:
    """Drives a running TerraForge instance through the shared action schema."""

    def __init__(self, directory: Optional[str] = None):
        self.dir = directory or studio_dir()

    # ---------------------------------------------------------------- state
    @property
    def state_path(self) -> str:
        return os.path.join(self.dir, STATE_FILE)

    @property
    def inbox_path(self) -> str:
        return os.path.join(self.dir, INBOX_FILE)

    def state(self) -> Dict[str, Any]:
        """Last scene snapshot the app published (cameras, world, render)."""
        try:
            with open(self.state_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}

    def cameras(self) -> List[Dict[str, Any]]:
        return self.state().get("cameras", [])

    # -------------------------------------------------------------- actions
    def send(self, *actions: Dict[str, Any]) -> Dict[str, Any]:
        """Queues action documents for the app to apply on its next frame."""
        doc = {"actions": list(actions)}
        tmp = self.inbox_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2)
        # The app opens the inbox on its own frame; a replace that lands in
        # that window is refused by Windows (sharing violation). It lasts a
        # millisecond, so wait it out rather than fail the send.
        for attempt in range(50):
            try:
                os.replace(tmp, self.inbox_path)
                break
            except PermissionError:
                if attempt == 49:
                    raise
                time.sleep(0.02)
        return doc

    # cameras
    def view_to_camera(self, **kw: Any) -> Dict[str, Any]:
        """kw: view (1-based), camera (existing name; omit for a new one),
        name, activate."""
        return self.send({"op": "view_to_camera", **kw})

    def camera_to_view(self, **kw: Any) -> Dict[str, Any]:
        """kw: camera (name; omit for the active one), view (1-based), link
        (True: the view looks through it; False: the free view moves there)."""
        return self.send({"op": "camera_to_view", **kw})

    def render_preset(self, **kw: Any) -> Dict[str, Any]:
        """kw: action (save|apply|delete|list), name, camera (or 'all'),
        engine, width, height, samples, output, passes, panorama."""
        return self.send({"op": "render_preset", **kw})

    def render_batch(self, **kw: Any) -> Dict[str, Any]:
        """kw: cameras (list of names; omit for every camera), preset."""
        return self.send({"op": "render_batch", **kw})

    def add_camera(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "add_camera", **kw})

    def set_camera(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_camera", **kw})

    # world
    def set_sun(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_sun", **kw})

    def set_sky(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_sky", **kw})

    def set_fog(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_fog", **kw})

    def set_clouds(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_clouds", **kw})

    def set_water(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_water", **kw})

    # objects and graphs
    def select(self, name: str) -> Dict[str, Any]:
        return self.send({"op": "select", "name": name})

    def place_object(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "place_object", **kw})

    def graph(self, spec: Dict[str, Any]) -> Dict[str, Any]:
        return self.send({"op": "graph", "spec": spec})

    # render
    def set_render(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_render", **kw})

    def render(self, **kw: Any) -> Dict[str, Any]:
        acts: List[Dict[str, Any]] = []
        if kw:
            acts.append({"op": "set_render", **kw})
        acts.append({"op": "render"})
        return self.send(*acts)

    # planets & infinite terrains
    def add_planet(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "add_planet", **kw})

    def set_planet(self, name: str, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_planet", "name": name, **kw})

    def add_infinite_terrain(self, **kw: Any) -> Dict[str, Any]:
        """kw: planet (name; omit for the home ground plane), style
        ('mountains'|'hills'|'dunes'), scale, amplitude, coverage, seed."""
        return self.send({"op": "add_infinite_terrain", **kw})

    # deep space
    def add_moon(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "add_moon", **kw})

    def add_nebula(self, **kw: Any) -> Dict[str, Any]:
        """kw: name, type (nebula|dark|galaxy|elliptical|planetary), azimuth,
        elevation, size_deg, tilt_deg, rotation_deg, seed, brightness,
        density, detail, arms, color1 [r,g,b], color2."""
        return self.send({"op": "add_nebula", **kw})

    def set_nebula(self, name: str, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "set_nebula", "name": name, **kw})

    def set_space(self, **kw: Any) -> Dict[str, Any]:
        """The star field, the milky band and the palette (studio_set_space)."""
        return self.send({"op": "set_space", **kw})

    def space_preset(self, name: str) -> Dict[str, Any]:
        """A whole sky at once: night, hubble, cinema, deep_field, nursery, void."""
        return self.send({"op": "space_preset", "name": name})

    def space_populate(self, count: int = 4, seed: int = 1,
                       style: str = "mixed") -> Dict[str, Any]:
        """Scatter nebulas and galaxies over the sky (mixed/nebulas/galaxies/dark)."""
        return self.send({"op": "space_populate", "count": count, "seed": seed,
                          "style": style})

    def planets(self) -> List[Dict[str, Any]]:
        return self.state().get("planets", [])

    # ---------------------------------------------------------- the graph
    # Reading the graph back is what turns this from a write-only interface
    # into one an agent can actually work with.
    def nodes(self) -> List[Dict[str, Any]]:
        return self.state().get("nodes", [])

    def links(self) -> List[Dict[str, Any]]:
        return self.state().get("links", [])

    def node(self, ident) -> Optional[Dict[str, Any]]:
        """Find a node by id or, failing that, by type name."""
        ns = self.nodes()
        for n in ns:
            if n.get("id") == ident:
                return n
        for n in reversed(ns):
            if n.get("type") == ident:
                return n
        return None

    def add_node(self, type: str, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "add_node", "type": type, **kw})

    def delete_node(self, node: Any) -> Dict[str, Any]:
        return self.send({"op": "delete_node", "node": node})

    def connect(self, frm: Any, to: Any, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "connect", "from": frm, "to": to, **kw})

    def disconnect(self, **kw: Any) -> Dict[str, Any]:
        return self.send({"op": "disconnect", **kw})

    def set_attr(self, node: Any, key: str = None, value: Any = None,
                 **kw: Any) -> Dict[str, Any]:
        act: Dict[str, Any] = {"op": "set_attr", "node": node}
        if key is not None:
            act["key"] = key
            act["value"] = value
        if kw:
            act["attrs"] = {**act.get("attrs", {}), **kw}
        return self.send(act)

    def bypass(self, node: Any, bypass: bool = True) -> Dict[str, Any]:
        return self.send({"op": "bypass", "node": node, "bypass": bypass})

    def move_node(self, node: Any, x: float, y: float) -> Dict[str, Any]:
        return self.send({"op": "move_node", "node": node, "x": x, "y": y})

    def clear_graph(self) -> Dict[str, Any]:
        return self.send({"op": "clear_graph"})

    def set_resolution(self, resolution: int) -> Dict[str, Any]:
        return self.send({"op": "set_resolution", "resolution": resolution})

    def view_node(self, node: Any = None) -> Dict[str, Any]:
        act: Dict[str, Any] = {"op": "view_node"}
        if node is not None:
            act["node"] = node
        return self.send(act)

    def select_node(self, node: Any) -> Dict[str, Any]:
        return self.send({"op": "select_node", "node": node})

    def set_workspace(self, workspace) -> Dict[str, Any]:
        return self.send({"op": "set_workspace", "workspace": workspace})

    def set_viewport(self, **kw: Any) -> Dict[str, Any]:
        """How the terrain surface is drawn: subdivision, per-patch culling,
        height scale, planetary radius, fractal relief, displacement strength,
        shadows, exposure. Only the keys given are changed."""
        return self.send({"op": "set_viewport", **kw})

    def render_passes(self, path: str, width: int = 1920, height: int = 1080,
                      fmt: int = 0, passes=None) -> Dict[str, Any]:
        """Render with the viewport engine: the beauty (fmt 0 PNG, 1 EXR,
        2 HDR) plus one linear EXR per pass name beside it."""
        act = {"op": "render_passes", "path": path, "width": width,
               "height": height, "format": fmt}
        if passes is not None:
            act["passes"] = passes
        return self.send(act)

    def capture(self, path: str, width: int = 1280,
                height: int = 720) -> Dict[str, Any]:
        """Render the active camera's viewport to a PNG. The only way a script
        can check what the renderer actually drew."""
        return self.send({"op": "capture", "path": path,
                          "width": width, "height": height})

    def evaluate(self) -> Dict[str, Any]:
        return self.send({"op": "evaluate"})

    def wait_for_eval(self, timeout: float = 60.0) -> bool:
        """Block until evaluation finishes. Watches the published eval serial,
        so this waits for a real event rather than for a file's timestamp."""
        start = time.time()
        begun = False
        while time.time() - start < timeout:
            ev = self.state().get("eval", {})
            if ev.get("running"):
                begun = True
            elif begun:
                return True
            time.sleep(0.15)
        return False

    def verify_field_gpu(self) -> Dict[str, Any]:
        return self.send({"op": "verify_field_gpu"})

    def verify_accel(self) -> Dict[str, Any]:
        return self.send({"op": "verify_accel"})

    def undo(self, steps: int = 1) -> Dict[str, Any]:
        """Revert the last change, including one made through this API."""
        return self.send({"op": "undo", "steps": steps})

    def redo(self, steps: int = 1) -> Dict[str, Any]:
        return self.send({"op": "redo", "steps": steps})

    def wait_for_state(self, timeout_s: float = 5.0) -> Dict[str, Any]:
        """Blocks until the app republishes its scene state."""
        start = os.path.getmtime(self.state_path) if os.path.exists(self.state_path) else 0
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if os.path.exists(self.state_path) and os.path.getmtime(self.state_path) != start:
                return self.state()
            time.sleep(0.1)
        return self.state()


# --------------------------------------------------------------- MCP tools
# Exposed so an agent can drive the studio with the same vocabulary.
MCP_TOOLS = {
    "studio_get_state": {
        "description": "Scene snapshot: cameras (lens, exposure, film, render "
                       "assignment), sun, sky, clouds, fog, water, selection.",
        "params": {},
    },
    "studio_add_camera": {
        "description": "Create a camera. Fields: name, focal_mm, format, "
                       "aperture, shutter, iso, film, look_at, distance, "
                       "height, azimuth_deg, activate.",
        "params": {"name": "str", "focal_mm": "float", "format": "str",
                   "aperture": "float", "shutter": "float", "iso": "float",
                   "film": "str", "look_at": "str|[x,y,z]", "distance": "float",
                   "height": "float", "azimuth_deg": "float",
                   "activate": "bool"},
    },
    "studio_set_camera": {
        "description": "Modify the named or active camera (same fields).",
        "params": {"name": "str"},
    },
    "studio_set_world": {
        "description": "Sun, sky, fog, clouds and water settings.",
        "params": {"sun": "obj", "sky": "obj", "fog": "obj", "clouds": "obj",
                   "water": "obj"},
    },
    "studio_place_object": {
        "description": "Move a scene object: name, position [x,y,z], scale, "
                       "rotation_deg. Terrain spans x 0..1, z 0..1. side "
                       "(world, outside, inside) puts a terrain tile or a "
                       "surface layer on the other face of the world.",
        "params": {"name": "str", "position": "[x,y,z]", "scale": "float",
                   "rotation_deg": "float", "side": "str"},
    },
    "studio_probe_height": {
        "description": "The ground's height at a point of the tile (x, z in "
                       "0..1): the terrain as displayed and the graph's own "
                       "heightmap, in heightmap units and metres. Read the "
                       "answer from the state's `reply`.",
        "params": {"x": "float", "z": "float"},
    },
    "studio_points_stats": {
        "description": "How many instances a Points node or an EcosystemLayer "
                       "placed, per species, and their mean scale. `node` is "
                       "an id, a macro alias or a type. Read the answer from "
                       "the state's `reply`.",
        "params": {"node": "str"},
    },
    "studio_save_node_preview": {
        "description": "Write out the 112 px thumbnail from a node's card as "
                       "a PNG: what that node actually made. A scatter that "
                       "clumped, a mask covering the wrong half, a fractal at "
                       "the wrong scale - all visible here, and far cheaper "
                       "than a render. The node must have been evaluated "
                       "first (studio_evaluate). `node` is an id, a macro "
                       "alias or a type.",
        "params": {"node": "str", "path": "str"},
    },
    "studio_place_on_terrain": {
        "description": "Make a mesh object stand on the terrain: it becomes a "
                       "child of the terrain, its base rides on the surface and "
                       "a TerrainImprint node moulds the ground to it.",
        "params": {"object": "str"},
    },
    "studio_set_ground": {
        "description": "A grounded object's settings, in metres: lock (base "
                       "follows the surface), offset_m the height over the "
                       "surface (negative for below it, and the same "
                       "number as the object's Y), sunk_m how far it is sunk "
                       "INTO the ground below the highest point under its "
                       "base - the ground does not react to this, so it is "
                       "the control for the gap under an object on a slope "
                       "and for burying one - margin_m the flat "
                       "patch past the walls, blend_m how far the ground "
                       "responds (0 = auto), sink_m a dead band before "
                       "digging starts. lift_m and dig_m cap how far the "
                       "ground may travel to meet the object - set both to 0 "
                       "and it holds the height you gave it, hanging in the "
                       "air or buried with the ground closed over it, which "
                       "is how an arch, a bridge deck or a half-sunk ruin is "
                       "placed. Omit them for the unlimited default, where "
                       "the ground always follows.",
        "params": {"object": "str", "lock": "bool", "offset_m": "float",
                   "margin_m": "float", "blend_m": "float", "sink_m": "float",
                   "lift_m": "float", "dig_m": "float", "sunk_m": "float"},
    },
    "studio_graph": {
        "description": "Merge a node-graph spec (terrain or material) into the "
                       "project.",
        "params": {"spec": "obj"},
    },
    "studio_set_render": {
        "description": "Configure the active camera's render output without "
                       "starting a render. Fields: engine, width, height, "
                       "samples, output.",
        "params": {"engine": "str", "width": "int", "height": "int",
                   "samples": "int", "output": "str"},
    },
    "studio_select": {
        "description": "Select a scene object by name.",
        "params": {"name": "str"},
    },
    "studio_render": {
        "description": "Render the active camera (or 'camera' by name), with "
                       "'preset' applied first if given. Fields: camera, preset, "
                       "engine, width, height, samples, output.",
        "params": {"camera": "str", "preset": "str", "engine": "str", "width": "int",
                   "height": "int", "samples": "int", "output": "str"},
    },
    "studio_view_to_camera": {
        "description": "Write a viewport's point of view (where it looks from and "
                       "at) into a camera: an existing one by name, or a new one. "
                       "Fields: view (1-based, default the focused view), camera, "
                       "name, activate.",
        "params": {"view": "int", "camera": "str", "name": "str", "activate": "bool"},
    },
    "studio_camera_to_view": {
        "description": "Put a camera into a viewport: link=true makes the view look "
                       "through it from now on, link=false moves the free orbit to "
                       "the camera's eye and target. Fields: camera (name; default "
                       "the active one), view (1-based), link.",
        "params": {"camera": "str", "view": "int", "link": "bool"},
    },
    "studio_render_preset": {
        "description": "Named render presets, saved with the project. action: save "
                       "(from a camera's assignment or the given fields), apply (to a "
                       "camera by name, 'all', or the active one), delete, list. "
                       "Fields: name, camera, engine, width, height, samples, output, "
                       "passes, panorama.",
        "params": {"action": "str", "name": "str", "camera": "str", "engine": "str",
                   "width": "int", "height": "int", "samples": "int", "output": "str",
                   "passes": "bool", "panorama": "bool"},
    },
    "studio_render_batch": {
        "description": "Render several cameras one after the other, each with its "
                       "own assignment and file. Fields: cameras (list of names; omit "
                       "for every camera), preset (applied to each first).",
        "params": {"cameras": "[str]", "preset": "str"},
    },
    "studio_add_planet": {
        "description": "Create a procedural planet (any number is fine - they "
                       "are generated on the GPU and cost no memory). Fields: "
                       "name, radius, relief, seed, position [x,y,z], "
                       "sea_level, snow_line, atmosphere, rock_low, "
                       "rock_high, water_color, atmo_color.",
        "params": {"name": "str", "radius": "float", "relief": "float",
                   "seed": "int", "position": "[x,y,z]", "sea_level": "float",
                   "snow_line": "float", "surface_node": "str", "atmosphere": "float"},
    },
    "studio_set_planet": {
        "description": "Modify an existing planet by name (same fields as "
                       "studio_add_planet).",
        "params": {"name": "str"},
    },
    "studio_add_moon": {
        "description": "Add a moon: a small airless cratered world in the sky "
                       "(a planet whose surface layer is craters). Fields: name, "
                       "radius, seed, position [x,y,z]; studio_set_planet edits it.",
        "params": {"name": "str", "radius": "float", "seed": "int", "position": "[x,y,z]"},
    },
    "studio_add_nebula": {
        "description": "Add a nebula or a galaxy in deep space, seen where the "
                       "atmosphere lets space through (night, high altitude, beyond "
                       "a ring's rim). Fields: name, type (nebula|dark|galaxy|"
                       "elliptical|planetary), azimuth and elevation in degrees, "
                       "size_deg (angular diameter), tilt_deg and rotation_deg "
                       "(galaxies), seed, brightness, density, detail 0-1, arms, "
                       "color1 (the ionised heart) and color2 (the gas around it) "
                       "[r,g,b], or palette 'auto' to take both from the realism "
                       "dial. A cloud is marched as a real volume, so it also takes "
                       "dust 0-1, warp 0-1.5 (how far it is pulled out of a ball), "
                       "glow, and sources 1-4: the hot stars inside whose glare "
                       "decides where it is teal and where it is hydrogen red. "
                       "Eight at most are drawn at once.",
        "params": {"name": "str", "type": "str", "azimuth": "float", "elevation": "float",
                   "size_deg": "float", "tilt_deg": "float", "rotation_deg": "float",
                   "seed": "int", "brightness": "float", "density": "float",
                   "detail": "float", "arms": "int", "dust": "float", "warp": "float",
                   "glow": "float", "sources": "int", "palette": "str",
                   "color1": "[r,g,b]", "color2": "[r,g,b]"},
    },
    "studio_set_nebula": {
        "description": "Modify a nebula or galaxy by name (same fields as studio_add_nebula).",
        "params": {"name": "str"},
    },
    "studio_set_space": {
        "description": "Deep space behind the atmosphere. The backdrop as a whole: "
                       "on (bool), brightness, realism 0-1 (1 a photograph - "
                       "hydrogen's crimson and ionised oxygen's teal; 0 what a film "
                       "paints - cyan, magenta and gold, with a glow round "
                       "everything), glow, quality 0-3 (how far the march through a "
                       "nebula steps). Stars: stars (bool), star_density 0-1, "
                       "star_brightness, star_size, star_temperature 0-1 (colour "
                       "spread), star_spikes (the diffraction arms on the brightest), "
                       "star_halo, star_clump (how strongly they gather into "
                       "associations), star_seed. The Milky Way band: galaxy (bool), "
                       "galaxy_intensity, galaxy_width (degrees), galaxy_yaw and "
                       "galaxy_pitch (where its pole points), galaxy_core (degrees "
                       "along the band), galaxy_dust 0-1 (dark rifts, which redden "
                       "what they dim), galaxy_grain 0-1 (the unresolved stars that "
                       "make it milky), galaxy_color [r,g,b], galaxy_seed. The "
                       "atmosphere's height (set_sky height_m) decides where space "
                       "shows.",
        "params": {"on": "bool", "brightness": "float", "realism": "float",
                   "glow": "float", "quality": "int",
                   "stars": "bool", "star_density": "float", "star_brightness": "float",
                   "star_size": "float", "star_temperature": "float",
                   "star_spikes": "float", "star_halo": "float", "star_clump": "float",
                   "star_seed": "int",
                   "galaxy": "bool", "galaxy_intensity": "float", "galaxy_width": "float",
                   "galaxy_yaw": "float", "galaxy_pitch": "float", "galaxy_core": "float",
                   "galaxy_dust": "float", "galaxy_grain": "float",
                   "galaxy_color": "[r,g,b]", "galaxy_seed": "int"},
    },
    "studio_space_preset": {
        "description": "A whole sky at once. 'night' is what the eye sees from a "
                       "dark place; 'hubble' a telescope's picture, teal hearts and "
                       "crimson outskirts; 'cinema' the sky a film paints; "
                       "'deep_field' a quiet star field and far galaxies; 'nursery' "
                       "one great cloud lit from inside; 'void' stars alone. Each "
                       "sets the star field, the band and the palette, and the ones "
                       "that want nebulas scatter them too.",
        "params": {"name": "str"},
    },
    "studio_space_populate": {
        "description": "Fill the sky: scatter `count` (0-8) nebulas and galaxies "
                       "over it, spread apart and varied in kind and size, coloured "
                       "from wherever the realism dial stands. `seed` changes the "
                       "arrangement; `style` is mixed, nebulas, galaxies or dark. "
                       "Replaces what a previous fill made and leaves anything "
                       "placed by hand alone.",
        "params": {"count": "int", "seed": "int", "style": "str"},
    },
    "studio_add_infinite_terrain": {
        "description": "Add an endless procedural terrain layer. With "
                       "'planet' it shapes that planet's surface; without it "
                       "the home ground plane extends to the horizon. Layers "
                       "stack. Fields: planet, style (mountains|hills|dunes), "
                       "scale, amplitude, coverage, seed.",
        "params": {"planet": "str", "style": "str", "scale": "float",
                   "amplitude": "float", "coverage": "float", "seed": "int"},
    },
    "studio_undo": {
        "description": "Revert the last change. Every edit is one step, "
                       "including changes made through this API, so a change "
                       "you just made can be taken back. Field: steps.",
        "params": {"steps": "int"},
    },
    "studio_redo": {
        "description": "Re-apply the last undone change. Field: steps.",
        "params": {"steps": "int"},
    },
}


def all_tools() -> Dict[str, Any]:
    """Every studio MCP tool: scene/world/camera plus the graph tools."""
    from .studio_graph_tools import GRAPH_TOOLS
    merged = dict(MCP_TOOLS)
    merged.update(GRAPH_TOOLS)
    return merged


def handle_mcp(tool: str, params: Dict[str, Any],
               studio: Optional[Studio] = None) -> Dict[str, Any]:
    """Dispatches an MCP tool call onto the shared action schema."""
    s = studio or Studio()
    # graph tools live in their own module so neither file grows unbounded
    from .studio_graph_tools import handle_graph_tool
    handled = handle_graph_tool(tool, params, s)
    if handled is not None:
        return handled
    if tool == "studio_get_state":
        return {"status": "success", "state": s.state()}
    if tool == "studio_add_camera":
        return {"status": "success", "sent": s.add_camera(**params)}
    if tool == "studio_set_camera":
        return {"status": "success", "sent": s.set_camera(**params)}
    if tool == "studio_set_world":
        acts = []
        for key, op in (("sun", "set_sun"), ("sky", "set_sky"),
                        ("fog", "set_fog"), ("clouds", "set_clouds"),
                        ("water", "set_water")):
            if key in params:
                acts.append({"op": op, **params[key]})
        if not acts:
            return {"status": "error", "message": "nothing to set"}
        return {"status": "success", "sent": s.send(*acts)}
    if tool == "studio_place_object":
        return {"status": "success", "sent": s.place_object(**params)}
    if tool == "studio_probe_height":
        return {"status": "success", "sent": s.send({"op": "probe_height", **params})}
    if tool == "studio_points_stats":
        return {"status": "success", "sent": s.send({"op": "points_stats", **params})}
    if tool == "studio_save_node_preview":
        return {"status": "success",
                "sent": s.send({"op": "save_node_preview", **params})}
    if tool == "studio_place_on_terrain":
        return {"status": "success", "sent": s.send({"op": "place_on_terrain", **params})}
    if tool == "studio_set_ground":
        return {"status": "success", "sent": s.send({"op": "set_ground", **params})}
    if tool == "studio_graph":
        return {"status": "success", "sent": s.graph(params.get("spec", {}))}
    if tool == "studio_render":
        return {"status": "success", "sent": s.render(**params)}
    if tool == "studio_set_render":
        # configure output without firing a render, which studio_render
        # cannot do because it always appends the render action
        return {"status": "success", "sent": s.set_render(**params)}
    if tool == "studio_select":
        return {"status": "success", "sent": s.select(params.get("name", ""))}
    if tool == "studio_view_to_camera":
        return {"status": "success", "sent": s.view_to_camera(**params)}
    if tool == "studio_camera_to_view":
        return {"status": "success", "sent": s.camera_to_view(**params)}
    if tool == "studio_render_preset":
        return {"status": "success", "sent": s.render_preset(**params)}
    if tool == "studio_render_batch":
        return {"status": "success", "sent": s.render_batch(**params)}
    if tool == "studio_add_planet":
        return {"status": "success", "sent": s.add_planet(**params)}
    if tool == "studio_set_planet":
        return {"status": "success", "sent": s.set_planet(**params)}
    if tool == "studio_add_moon":
        return {"status": "success", "sent": s.add_moon(**params)}
    if tool == "studio_add_nebula":
        return {"status": "success", "sent": s.add_nebula(**params)}
    if tool == "studio_set_nebula":
        return {"status": "success", "sent": s.set_nebula(**params)}
    if tool == "studio_set_space":
        return {"status": "success", "sent": s.set_space(**params)}
    if tool == "studio_space_preset":
        return {"status": "success", "sent": s.space_preset(params.get("name", "night"))}
    if tool == "studio_space_populate":
        return {"status": "success", "sent": s.space_populate(
            int(params.get("count", 4)), int(params.get("seed", 1)),
            str(params.get("style", "mixed")))}
    if tool == "studio_add_infinite_terrain":
        return {"status": "success", "sent": s.add_infinite_terrain(**params)}
    if tool == "studio_undo":
        return {"status": "success", "sent": s.undo(int(params.get("steps", 1)))}
    if tool == "studio_redo":
        return {"status": "success", "sent": s.redo(int(params.get("steps", 1)))}
    return {"status": "error", "message": f"unknown tool: {tool}"}
