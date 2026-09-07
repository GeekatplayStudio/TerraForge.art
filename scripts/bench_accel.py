#!/usr/bin/env python3
"""How much does the GPU accelerator actually save?

The same graph, the same script, the studio's own eval_ms, three times at
each resolution. Run it twice - once normally and once with GPX_NO_GPU=1 -
and compare. Anything else is comparing two different measurements.

    build/geekatplay_studio &            python scripts/bench_accel.py gpu
    GPX_NO_GPU=1 build/geekatplay_studio &   python scripts/bench_accel.py cpu
"""
import json
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mcp_server"))
from studio_api import Studio  # noqa: E402

RUNS = 3
RESOLUTIONS = (512, 1024, 2048)
s = Studio()
label = sys.argv[1] if len(sys.argv) > 1 else "run"


def send(*a):
    s.send(*a)
    time.sleep(0.6)


def wait_idle(limit=180):
    for _ in range(limit):
        if not s.state().get("eval", {}).get("running", True):
            return True
        time.sleep(0.25)
    return False


send({"op": "clear_graph"})
send({"op": "add_node", "type": "TerrainFractal", "alias": "f", "x": 40, "y": 40})

rows = {}
for res in RESOLUTIONS:
    send({"op": "set_resolution", "resolution": res})
    times = []
    for _ in range(RUNS):
        # a fresh seed forces a real recompute rather than a cache hit
        send({"op": "set_attr", "node": "f", "key": "seed",
              "value": int(time.time() * 1000) % 100000})
        send({"op": "evaluate"})
        wait_idle()
        time.sleep(0.8)
        ms = s.state().get("perf", {}).get("eval_ms", 0.0)
        if ms > 0:
            times.append(ms)
    if times:
        rows[res] = round(statistics.median(times), 1)
        print(f"  {res:5d}  {rows[res]:9.1f} ms   (of {len(times)}: "
              f"{', '.join(f'{t:.0f}' for t in times)})")

v = s.state().get("viewport", {})
print(f"\n  accel={v.get('accel')} taken={v.get('accel_taken')} "
      f"declined={v.get('accel_declined')}")
out = os.path.join(os.getcwd(), f"accel_{label}.json")
json.dump({"label": label, "eval_ms": rows,
           "accel": v.get("accel"), "taken": v.get("accel_taken")},
          open(out, "w", encoding="utf-8"), indent=1)
print("  wrote", out)
