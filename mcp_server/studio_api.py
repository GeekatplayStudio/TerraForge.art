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
        os.replace(tmp, self.inbox_path)
        return doc

    # cameras
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
                       "rotation_deg. Terrain spans x 0..1, z 0..1.",
        "params": {"name": "str", "position": "[x,y,z]", "scale": "float",
                   "rotation_deg": "float"},
    },
    "studio_graph": {
        "description": "Merge a node-graph spec (terrain or material) into the "
                       "project.",
        "params": {"spec": "obj"},
    },
    "studio_render": {
        "description": "Render the active camera. Fields: engine, width, "
                       "height, samples, output.",
        "params": {"engine": "str", "width": "int", "height": "int",
                   "samples": "int", "output": "str"},
    },
}


def handle_mcp(tool: str, params: Dict[str, Any],
               studio: Optional[Studio] = None) -> Dict[str, Any]:
    """Dispatches an MCP tool call onto the shared action schema."""
    s = studio or Studio()
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
    if tool == "studio_graph":
        return {"status": "success", "sent": s.graph(params.get("spec", {}))}
    if tool == "studio_render":
        return {"status": "success", "sent": s.render(**params)}
    return {"status": "error", "message": f"unknown tool: {tool}"}
