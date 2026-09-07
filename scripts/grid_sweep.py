"""Does a finer uniform patch grid do the quadtree's job, without its risk?

The quadtree exists because a fixed 64x64 grid caps the finest triangle at
tile/64/64. A 128 or 256 grid lowers that cap by the same factor and keeps
the crack invariant exactly, because a uniform grid has no T-junctions at
all. The question is what it costs, and that is measurable.

Rebuilds the studio at each grid size, takes the benchmark, and prints the
comparison.
"""
import json
import os
import re
import subprocess
import sys
import time

SP = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SP)
HDR = os.path.join(ROOT, "studio", "terrain_cull.hpp")
SIZES = [64, 128, 256]


def set_grid(n):
    s = open(HDR, encoding="utf-8").read()
    s = re.sub(r"inline constexpr int TERRAIN_PATCHES_PER_EDGE = \d+;",
               f"inline constexpr int TERRAIN_PATCHES_PER_EDGE = {n};", s)
    open(HDR, "w", encoding="utf-8", newline="").write(s)


original = open(HDR, encoding="utf-8").read()
results = {}
try:
    for n in SIZES:
        set_grid(n)
        subprocess.run(["taskkill", "/IM", "geekatplay_studio.exe", "/F"],
                       capture_output=True)
        b = subprocess.run(["cmake", "--build", "build", "--target",
                            "geekatplay_studio"], cwd=ROOT,
                           capture_output=True, text=True)
        if b.returncode != 0:
            print(f"{n}: build failed\n{b.stdout[-1500:]}")
            continue
        subprocess.Popen([os.path.join(ROOT, "build", "geekatplay_studio.exe")],
                         cwd=ROOT)
        time.sleep(15)
        out = os.path.join(SP, f"grid_{n}.json")
        r = subprocess.run([sys.executable, os.path.join(SP, "bench_terrain.py"),
                            "--flat", "--json", out, "--label", f"grid {n}"],
                           capture_output=True, text=True)
        print(f"--- {n}x{n} ({n*n} patches) " + "-" * 30)
        print(r.stdout.rstrip())
        if os.path.exists(out):
            results[n] = json.load(open(out, encoding="utf-8"))
finally:
    open(HDR, "w", encoding="utf-8", newline="").write(original)
    subprocess.run(["taskkill", "/IM", "geekatplay_studio.exe", "/F"],
                   capture_output=True)

if results:
    print("\n" + "=" * 74)
    print(f"{'view':9s} " + "".join(f"{n:>20}" for n in results))
    for view in ("ground", "low", "mid", "high", "orbital", "top"):
        line = f"{view:9s} "
        for n, data in results.items():
            row = next((r for r in data["rows"] if r["view"] == view), None)
            line += (f"{row['gpu_ms_median']:7.2f}ms{row['primitives']:>11,}"
                     if row else " " * 20)
        print(line)
    print("\nfinest triangle at ground level, 5 km tile, cap 64:")
    for n in results:
        print(f"  {n}x{n}: {5000.0 / n / 64:.2f} m")
