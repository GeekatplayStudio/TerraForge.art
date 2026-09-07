"""The fixed-grid terrain baseline, at the altitudes TF-GEO-0's gate names.

The quadtree's pass/stop gate is written as "terrain GPU p95 not worse than
the fixed grid at any altitude". That sentence is unfalsifiable without this
table, so this table comes first.

Reads viewport.terrain_gpu_ms and viewport.terrain_primitives, which is the
pair that says whether the pass is geometry-bound (many triangles, time
tracking them) or fragment-bound (few triangles, time regardless), and
view_w x view_h, without which neither number compares to anything.

    python bench_terrain.py --json baseline.json
    python bench_terrain.py --tess-max 64      # what the cap is costing us
"""
import argparse
import json
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mcp_server"))
from studio_api import Studio  # noqa: E402

PAUSE = 0.6      # the inbox holds one document; faster overwrites it
SETTLE = 2.5     # the GPU timer smooths over ~7 frames and the counter rings 4
SAMPLES = 10

# distance and height are in tile widths; the tile spans 0..1 with about 0.25
# of relief, so 0.05 away and 0.04 up is a person standing on it.
VIEWS = [
    ("ground",  0.05,  0.04, 210),
    ("low",     0.25,  0.10, 210),
    ("mid",     1.00,  0.40, 210),
    ("high",    3.00,  1.50, 210),
    ("orbital", 8.00,  5.00, 210),
    ("top",     0.01,  2.00, 210),   # straight down: the whole tile on screen
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--label", default="fixed 64x64 grid")
    ap.add_argument("--tess-max", type=float, default=None)
    ap.add_argument("--tess-min", type=float, default=None)
    ap.add_argument("--tess-px", type=float, default=None)
    ap.add_argument("--flat", action="store_true",
                    help="planet_radius 0: the tile as a flat world")
    ap.add_argument("--no-setup", action="store_true")
    args = ap.parse_args()

    s = Studio()

    def send(*acts):
        s.send(*acts)
        time.sleep(PAUSE)

    if not args.no_setup:
        # A fixed terrain, so two runs on different days compare.
        send({"op": "clear_graph"})
        send({"op": "set_resolution", "resolution": 1024})
        send({"op": "add_node", "type": "TerrainFractal2", "alias": "terrain",
              "x": 40, "y": 40})
        send({"op": "set_attr", "node": "terrain", "key": "octaves",
              "value": 11})
        send({"op": "evaluate"})
        time.sleep(4.0)
        send({"op": "view_node", "node": "terrain"})
        time.sleep(1.5)
    if args.flat:
        send({"op": "set_viewport", "planet_radius": 0})
    if args.tess_max is not None:
        send({"op": "set_viewport", "tess_max": args.tess_max})
    if args.tess_min is not None:
        send({"op": "set_viewport", "tess_min": args.tess_min})
    if args.tess_px is not None:
        send({"op": "set_viewport", "tess_pixels": args.tess_px})
    time.sleep(1.5)

    rows = []
    for name, dist, height, az in VIEWS:
        send({"op": "set_camera", "name": "Bench", "look_at": "terrain",
              "distance": dist, "height": height, "azimuth_deg": az,
              "activate": True})
        time.sleep(SETTLE)
        ms, prims, patches = [], [], []
        for _ in range(SAMPLES):
            v = s.state().get("viewport", {})
            if v.get("terrain_gpu_ms"):
                ms.append(v["terrain_gpu_ms"])
                prims.append(v.get("terrain_primitives", 0))
                patches.append(v.get("patches_visible", -1))
            time.sleep(0.25)
        if not ms:
            print(f"  {name:8s} no measurement came back")
            continue
        ms.sort()
        p = int(statistics.median(prims))
        pv = int(statistics.median(patches))
        med = round(statistics.median(ms), 3)
        rows.append({
            "view": name, "distance": dist, "height": height,
            "gpu_ms_median": med,
            "gpu_ms_p95": round(ms[min(len(ms) - 1, int(len(ms) * 0.95))], 3),
            "primitives": p,
            "patches_visible": pv,
            "tris_per_patch": round(p / pv, 1) if pv > 0 else 0,
            "samples": len(ms),
        })
        print(f"  {name:8s} {med:7.3f} ms  {p:>10,} tris  {pv:>5} patches  "
              f"{p / max(pv, 1):7.0f} tris/patch")

    st = s.state()
    v = st.get("viewport", {})
    out = {
        "label": args.label,
        "when": time.strftime("%Y-%m-%d %H:%M"),
        "settings": {k: v.get(k) for k in
                     ("tess_pixels", "tess_min", "tess_max", "frustum_cull",
                      "patches_total", "terrain_lod", "tessellation",
                      "planet_radius", "view_w", "view_h")},
        "rows": rows,
    }
    print("\n " + json.dumps(out["settings"]))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=1)
        print(" wrote", args.json)


if __name__ == "__main__":
    main()
