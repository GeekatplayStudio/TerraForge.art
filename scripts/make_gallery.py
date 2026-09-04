"""Drive TerraForge to build and photograph showcase terrains.

    python scripts/make_gallery.py            # everything
    python scripts/make_gallery.py alpine mesa

Requires a running TerraForge (start.ps1 / start.sh): the scenes are built
through the same action inbox the assistant and the MCP tools use, so every
picture on the website can be regenerated from this file.

Two things were learned the hard way and are load-bearing here:

  * Materials are chosen by measured luma, not by name. Rock035 sounds like a
    cliff and is a near-black wet slate; it is what made the first attempts
    look like coal. probe_mats.py prints the table.

  * The erosion chain is always Noise -> Hydraulic(method 1, the pipe model)
    -> ErosionLayers(method 3). The droplet solver and the explicit stream
    power both turned into fields of vertical needles at this resolution,
    exactly as AGENTS.md warns ("a solver that produces spikes will be
    normalised into a flat terrain"). Only the generator, the materials, the
    light and the camera change between biomes.

Measured palette (luma):
    Rock022  99 granite       Rock029  92 red sandstone   Rock030  77 basalt
    Rock031  46 near-black    Rock023 139 pale limestone  Gravel019 123 scree
    Ground047 67 dark earth   Ground033 194 pale sand     Ground054 141 tan
    Ground037 142 dry clay    Grass001  79 deep green     Snow006  200 snow
"""
import os, sys, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from mcp_server.studio_api import Studio

OUT = os.environ.get("TERRAFORGE_GALLERY_OUT", os.path.join(ROOT, "dist", "gallery"))
os.makedirs(OUT, exist_ok=True)
s = Studio()


def send(*acts, wait=1.0):
    s.send(*acts); time.sleep(wait)


def wait_eval(timeout=420):
    start, last, stable = time.time(), None, 0
    while time.time() - start < timeout:
        ev = s.state().get("eval", {})
        if not ev.get("running") and ev.get("serial") == last:
            stable += 1
            if stable >= 3:
                return True
        else:
            stable = 0
        last = ev.get("serial")
        time.sleep(1.0)
    print("   (eval timed out)")
    return False


def pbr(nid, asset, x, y, tiles):
    return {"id": nid, "type": "PBRMaterial", "pos": [x, y],
            "attrs": {"asset": asset, "tiles": tiles, "mapping": 1, "resolution": 1}}


def stack(layers, x=1200, blend=0.7, depth=0.28):
    nodes, links = [], []
    attrs = {"height_blend": blend, "blend_depth": depth}
    for i, (port, asset, tiles, rough) in enumerate(layers, start=1):
        nid = f"m{i}"
        nodes.append(pbr(nid, asset, x, (i - 1) * 190, tiles))
        links += [["el", port, "ms", f"mask {i}"], [nid, "albedo", "ms", f"albedo {i}"]]
        attrs[f"rough_{i}"] = rough
    nodes.append({"id": "ms", "type": "MaterialStack", "pos": [x + 300, 300], "attrs": attrs})
    return nodes, links


def shoot(name, nodes, links, env, cam, res=1024, size=(1920, 1080), settle=10):
    print(f"--- {name}", flush=True)
    send({"op": "graph", "replace": True, "spec": {"nodes": nodes, "links": links}},
         {"op": "set_resolution", "resolution": res}, wait=3)
    wait_eval(); time.sleep(settle); wait_eval()
    send(*env, wait=2)
    send(*cam, wait=3)
    wait_eval(); time.sleep(5)
    path = f"{OUT}/{name}.png"
    send({"op": "capture", "path": path, "width": size[0], "height": size[1]}, wait=6)
    print("   ", os.path.getsize(path) if os.path.exists(path) else "MISSING", flush=True)


def vp(detail=0.0016, scale=110):
    return {"op": "set_viewport", "tessellation": True, "tess_pixels": 5,
            "fractal_detail": detail, "fractal_scale": scale}


def cam(eye, target, focal=60):
    return {"op": "set_camera", "name": "Hero", "eye": eye, "look_at": target,
            "focal_mm": focal, "aperture": 8, "shutter": 0.008, "iso": 100}


def sky(az, alt, si, sc, zen, hor, fogd, fogc, clouds=None, water=None,
        ambient=0.62, density=1.15, fog_level=0.04):
    return [
        {"op": "set_sun", "azimuth_deg": az, "altitude_deg": alt, "intensity": si, "color": sc},
        {"op": "set_sky", "density": density, "ambient": ambient, "zenith": zen, "horizon": hor},
        {"op": "set_fog", "type": "haze", "density": fogd, "level": fog_level, "color": fogc},
        {"op": "set_clouds", "enabled": bool(clouds), **(clouds or {})},
        {"op": "set_water", "enabled": bool(water), **(water or {})},
    ]


CUMULUS = {"type": "cumulus", "coverage": 0.5, "density": 1.0,
           "altitude": 1.5, "thickness": 0.9}


def eroded(seed, ntype=1, kw=2.6, octaves=11, gain=0.52, particles=260000,
           erode=0.42, deposit=0.28, el=None):
    """The one stable chain: ridged/fBm noise, pipe-model water, layered erosion."""
    e = {"method": 3, "thermal_iters": 90, "strength": 0.65, "talus": 1.3,
         "snowline": 0.60, "relief": 0.32, "rock_slope": 0.34, "grass_slope": 0.22}
    e.update(el or {})
    return ([{"id": "n", "type": "Noise", "pos": [0, 0],
              "attrs": {"type": ntype, "octaves": octaves, "kw": kw, "gain": gain,
                        "seed": seed, "ridge_weight": 0.9}},
             {"id": "hy", "type": "Hydraulic", "pos": [280, 0],
              "attrs": {"method": 1, "particles": particles,
                        "erode_rate": erode, "deposit_rate": deposit}},
             {"id": "el", "type": "ErosionLayers", "pos": [560, 0], "attrs": e},
             {"id": "out", "type": "TerrainOutput", "pos": [1900, 0]}],
            [["n", "output", "hy", "input"], ["hy", "output", "el", "input"],
             ["el", "output", "out", "heightmap"], ["ms", "albedo", "out", "texture"]])


# ---------------------------------------------------------------- the scenes

def alpine():
    mn, ml = stack([("bedrock", "Rock022", 6, 0.85), ("scree", "Gravel019", 9, 0.9),
                    ("soil", "Ground047", 7, 0.85), ("grass", "Grass001", 11, 0.8),
                    ("snow", "Snow006", 5, 0.45)])
    tn, tl = eroded(12)
    shoot("alpine", tn + mn, tl + ml,
          sky(250, 17, 3.1, [1.0, 0.93, 0.84], [0.09, 0.23, 0.58], [0.55, 0.68, 0.84],
              0.35, [0.66, 0.74, 0.86], clouds=CUMULUS),
          [vp(), cam([0.12, 0.24, 0.90], [0.86, 0.06, 0.16], 70)])


def mesa():
    mn, ml = stack([("bedrock", "Rock029", 7, 0.9), ("scree", "Ground054", 10, 0.9),
                    ("soil", "Ground033", 8, 0.85), ("grass", "Grass004", 16, 0.8),
                    ("sediment", "Ground037", 9, 0.85)])
    tn, tl = eroded(31, ntype=0, kw=2.0, octaves=10, gain=0.46, particles=220000,
                    erode=0.5, deposit=0.22,
                    el={"snowline": 1.0, "relief": 0.24, "rock_slope": 0.26,
                        "grass_slope": 0.09, "talus": 2.1, "thermal_iters": 60})
    shoot("mesa", tn + mn, tl + ml,
          sky(285, 12, 3.4, [1.0, 0.86, 0.66], [0.15, 0.29, 0.58], [0.82, 0.71, 0.57],
              0.45, [0.86, 0.75, 0.62],
              clouds={"type": "cumulus", "coverage": 0.32, "density": 0.9,
                      "altitude": 1.6, "thickness": 0.8}),
          [vp(0.0014, 100), cam([0.10, 0.23, 0.92], [0.88, 0.03, 0.18], 62)])


def badlands():
    mn, ml = stack([("bedrock", "Rock023", 5, 0.9), ("scree", "Ground054", 8, 0.9),
                    ("soil", "Rock029", 6, 0.88), ("grass", "Grass004", 16, 0.82),
                    ("sediment", "Ground033", 7, 0.85)])
    tn, tl = eroded(77, ntype=1, kw=3.4, octaves=10, gain=0.46, particles=300000,
                    erode=0.58, deposit=0.18,
                    el={"snowline": 1.0, "relief": 0.22, "rock_slope": 0.28,
                        "grass_slope": 0.10, "talus": 1.8, "thermal_iters": 120})
    shoot("badlands", tn + mn, tl + ml,
          sky(268, 12, 3.3, [1.0, 0.90, 0.74], [0.13, 0.28, 0.60], [0.72, 0.72, 0.70],
              0.35, [0.78, 0.76, 0.74],
              clouds={"type": "cumulus", "coverage": 0.4, "density": 0.9,
                      "altitude": 1.5, "thickness": 0.85}),
          [vp(0.0012, 140), cam([0.14, 0.22, 0.90], [0.86, 0.03, 0.18], 78)])


def volcanic():
    mn, ml = stack([("bedrock", "Rock031", 7, 0.9), ("scree", "Rock030", 10, 0.9),
                    ("soil", "Ground047", 9, 0.88), ("grass", "Grass001", 16, 0.8),
                    ("sediment", "Ground054", 11, 0.85)])
    tn, tl = eroded(44, ntype=1, kw=2.2, octaves=10, gain=0.5, particles=200000,
                    erode=0.36, deposit=0.3,
                    el={"snowline": 1.0, "relief": 0.34, "rock_slope": 0.30,
                        "grass_slope": 0.13, "talus": 1.5})
    # a caldera cut into the eroded relief, before the layers are worked out
    tn.insert(2, {"id": "cr", "type": "Crater", "pos": [420, 0],
                  "attrs": {"scale": 0.5, "depth": 0.55, "lip": 0.4, "floor": 0.25,
                            "irregular": 0.45, "count": 1, "seed": 3}})
    tl = [l for l in tl if l != ["hy", "output", "el", "input"]]
    tl += [["hy", "output", "cr", "input"], ["cr", "output", "el", "input"]]
    shoot("volcanic", tn + mn, tl + ml,
          sky(108, 7, 4.0, [1.0, 0.68, 0.42], [0.10, 0.16, 0.33], [0.86, 0.48, 0.28],
              0.55, [0.74, 0.46, 0.34], ambient=0.45, density=1.3,
              clouds={"type": "cumulonimbus", "coverage": 0.42, "density": 1.2,
                      "altitude": 1.6, "thickness": 1.1}),
          [vp(0.0016, 120), cam([-0.25, 0.42, 1.30], [0.55, 0.02, 0.42], 55)])


def island():
    mn, ml = stack([("bedrock", "Rock022", 8, 0.85), ("scree", "Gravel019", 10, 0.9),
                    ("soil", "Ground047", 8, 0.85), ("grass", "Grass001", 13, 0.8),
                    ("sediment", "Ground033", 9, 0.8)])
    shoot("island",
          [{"id": "lf", "type": "Landform", "pos": [0, 0],
            "attrs": {"type": 0, "relief": 0.85, "radius": 0.44}},
           {"id": "hy", "type": "Hydraulic", "pos": [280, 0],
            "attrs": {"method": 1, "particles": 200000, "erode_rate": 0.36,
                      "deposit_rate": 0.3}},
           {"id": "el", "type": "ErosionLayers", "pos": [560, 0],
            "attrs": {"method": 3, "thermal_iters": 60, "strength": 0.55,
                      "talus": 1.5, "snowline": 0.95, "relief": 0.26,
                      "rock_slope": 0.34, "grass_slope": 0.32}},
           {"id": "out", "type": "TerrainOutput", "pos": [1900, 0]}] + mn,
          [["lf", "output", "hy", "input"], ["hy", "output", "el", "input"],
           ["el", "output", "out", "heightmap"], ["ms", "albedo", "out", "texture"]] + ml,
          sky(240, 21, 3.0, [1.0, 0.95, 0.88], [0.10, 0.26, 0.62], [0.60, 0.74, 0.86],
              0.4, [0.68, 0.78, 0.88],
              clouds={"type": "cumulus", "coverage": 0.55, "density": 1.1,
                      "altitude": 1.4, "thickness": 1.0},
              water={"level": 0.075, "deep": [0.02, 0.10, 0.16],
                     "shallow": [0.10, 0.34, 0.42], "foam": True}),
          [vp(0.0015, 120), cam([-0.30, 0.30, 1.25], [0.62, 0.02, 0.34], 52)])



# The five that made the gallery. A dune field was tried and dropped: the
# Dunes generator needs a camera and a wavelength this recipe never found.
SCENES = {"alpine": alpine, "mesa": mesa, "badlands": badlands,
          "volcanic": volcanic, "island": island}

if __name__ == "__main__":
    for w in (sys.argv[1:] or list(SCENES)):
        SCENES[w]()
