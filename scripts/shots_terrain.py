"""Capture the benchmark's views, so a geometry change can be proved invisible.

Cutting 98% of the triangles at orbital range is only allowed if the picture
is the same. This takes it; compare_shots.py says whether it is.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mcp_server"))
from studio_api import Studio  # noqa: E402

VIEWS = [("ground", 0.05, 0.04), ("low", 0.25, 0.10), ("mid", 1.00, 0.40),
         ("high", 3.00, 1.50), ("orbital", 8.00, 5.00), ("top", 0.01, 2.00)]

ap = argparse.ArgumentParser()
ap.add_argument("tag")
ap.add_argument("--setup", action="store_true")
a = ap.parse_args()

OUT = os.path.join(os.getcwd(), "shots_" + a.tag)
os.makedirs(OUT, exist_ok=True)
s = Studio()


def send(*acts):
    s.send(*acts)
    time.sleep(0.6)


if a.setup:
    send({"op": "clear_graph"})
    send({"op": "set_resolution", "resolution": 1024})
    send({"op": "add_node", "type": "TerrainFractal2", "alias": "terrain",
          "x": 40, "y": 40})
    send({"op": "set_attr", "node": "terrain", "key": "octaves", "value": 11})
    send({"op": "evaluate"})
    time.sleep(4.0)
    send({"op": "view_node", "node": "terrain"})
    send({"op": "set_viewport", "planet_radius": 0})
    time.sleep(1.5)

for name, dist, height in VIEWS:
    send({"op": "set_camera", "name": "Bench", "look_at": "terrain",
          "distance": dist, "height": height, "azimuth_deg": 210,
          "activate": True})
    time.sleep(1.5)
    send({"op": "capture", "path": os.path.join(OUT, name + ".png")
          .replace("\\", "/"), "width": 1280, "height": 720})
    time.sleep(1.2)
    print(f"  {name:8s} {s.state().get('status', '')[:70]}")
print("wrote", OUT)
