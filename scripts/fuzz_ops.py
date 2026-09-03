"""Bounded fuzz of the TerraForge actions inbox: valid-ish random ops in
random order, watching that the app stays alive. Deterministic seed."""
import json, os, random, subprocess, time

random.seed(20260902)
API = os.path.expandvars(r"%LOCALAPPDATA%\GeekatplayTerraForge\api")
NODES = ["Noise", "Hydraulic", "Thermal", "Terrace", "Blend", "ScatterPoints",
         "PointsToPath", "PathSpline", "PathCarve", "KMeans", "Morphology",
         "Landform", "BasaltField", "GaborNoise", "LineNoise", "FlowWarp",
         "Quilt", "WaveletNoise", "DetailEqualizer", "HydraulicBlur",
         "SelectMidrange", "SelectBorder", "TerrainMetrics", "MakeTileable",
         "Flood", "PointsSDF", "TerrainOutput"]

def rand_action():
    r = random.random()
    if r < 0.35:
        return {"op": "add_node", "type": random.choice(NODES),
                "x": random.uniform(0, 900), "y": random.uniform(0, 700)}
    if r < 0.55:
        return {"op": "connect", "from": random.choice(NODES),
                "to": random.choice(NODES)}
    if r < 0.65:
        return {"op": "set_attr", "node": random.choice(NODES),
                "key": random.choice(["seed", "octaves", "gain", "radius",
                                      "count", "level", "strength"]),
                "value": random.choice([0, 1, 3, 0.5, 12, -1, 999999])}
    if r < 0.72:
        return {"op": "delete_node", "node": random.choice(NODES)}
    if r < 0.78:
        return {"op": "set_time", "time": random.uniform(-5, 50)}
    if r < 0.84:
        return {"op": "set_key", "node": random.choice(NODES),
                "attr": "gain", "time": random.uniform(0, 10),
                "value": random.uniform(-2, 2)}
    if r < 0.88:
        return {"op": "add_light", "position": [random.random(), random.random(), random.random()],
                "intensity": random.uniform(-5, 50), "cone": random.uniform(-10, 400)}
    if r < 0.92:
        return {"op": "add_primitive", "kind": random.choice(
            ["cube", "sphere", "plane", "cone", "bogus"])}
    if r < 0.96:
        return {"op": random.choice(["undo", "redo"])}
    return {"op": "evaluate"}

def send(actions):
    doc = {"actions": actions}
    with open(os.path.join(API, "actions_inbox.json"), "w", encoding="utf-8") as f:
        json.dump(doc, f)

def main(rounds=25, per=10):
    for i in range(rounds):
        send([rand_action() for _ in range(per)])
        time.sleep(1.2)
        # is the app still with us?
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "(Get-Process geekatplay_studio -ErrorAction SilentlyContinue).Count"],
            capture_output=True, text=True)
        alive = out.stdout.strip()
        print(f"round {i+1}/{rounds}: alive={alive}", flush=True)
        if alive != "1":
            print("APP DIED")
            return 1
    print("FUZZ SURVIVED")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
