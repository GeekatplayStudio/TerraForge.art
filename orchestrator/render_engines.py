"""Geekatplay TerraForge — offline render backends.

One scene description (scene.json exported by the Render panel) can be
rendered by several engines. The goal is parity with the viewport: the same
HDR sky/cloud environment, the same PBR material, sun, water and height fog,
and the same ACES tone mapping.

Usage:
    python -m orchestrator.render_engines scene.json
    python -m orchestrator.render_engines --probe
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys

from . import tonemap


# ----------------------------------------------------------------- detection
def _has_module(name: str) -> bool:
    try:
        __import__(name)
        return True
    except Exception:
        return False


def find_blender() -> str | None:
    exe = shutil.which("blender")
    if exe:
        return exe
    candidates = []
    for base in (os.environ.get("ProgramFiles", r"C:\Program Files"),
                 os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")):
        root = os.path.join(base, "Blender Foundation")
        if os.path.isdir(root):
            for entry in sorted(os.listdir(root), reverse=True):
                p = os.path.join(root, entry, "blender.exe")
                if os.path.isfile(p):
                    candidates.append(p)
    return candidates[0] if candidates else None


def probe() -> dict:
    blender = find_blender()
    return {
        "mitsuba": {"available": _has_module("mitsuba"),
                    "how": "pip install mitsuba"},
        "cycles": {"available": blender is not None,
                   "how": "install Blender", "path": blender or ""},
        "luxcore": {"available": _has_module("pyluxcore"),
                    "how": "pip install pyluxcore"},
        "appleseed": {"available": _has_module("appleseed"),
                      "how": "no maintained release since 2019"},
    }


def print_probe() -> int:
    for name, info in probe().items():
        mark = "OK      " if info["available"] else "missing "
        extra = info.get("path") or info["how"]
        print(f"{mark}{name:10s} {extra}")
    return 0


# -------------------------------------------------------------- mitsuba 3
def _progress(sc: dict, text: str) -> None:
    """Writes a one-line status the app polls while rendering."""
    path = sc.get("progress_file")
    if not path:
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    except Exception:
        pass


def _passes(total_spp: int) -> list[int]:
    """Progressive schedule: a quick first image, then doubling refinements."""
    out, done, chunk = [], 0, 4
    while done < total_spp:
        step = min(chunk, total_spp - done)
        out.append(step)
        done += step
        chunk = min(chunk * 2, 64)
    return out


def render_mitsuba(sc: dict) -> int:
    try:
        import mitsuba as mi
    except ImportError:
        print("mitsuba is not installed. Run: pip install mitsuba")
        return 3
    for variant in ("cuda_ad_rgb", "llvm_ad_rgb", "scalar_rgb"):
        try:
            mi.set_variant(variant)
            break
        except Exception:
            continue
    print("mitsuba variant:", mi.variant())

    cam, sun, sky, mat = sc["camera"], sc["sun"], sc["sky"], sc["material"]
    fog = sc.get("fog", {})
    want_fog = fog.get("type", 0) != 0 and fog.get("density", 0) > 0

    bsdf: dict = {
        "type": "principled",
        "roughness": float(mat["roughness"]),
        "metallic": float(mat["metallic"]),
        "specular": float(max(min(mat["specular"], 1.0), 0.0)),
    }
    if sc.get("albedo"):
        bsdf["base_color"] = {"type": "bitmap", "filename": sc["albedo"]}
    else:
        bsdf["base_color"] = {"type": "rgb", "value": [0.35, 0.32, 0.28]}

    scene_dict: dict = {
        "type": "scene",
        "integrator": {"type": "path", "max_depth": 8},
        "sensor": {
            "type": "perspective",
            "fov": cam["fov"],
            "fov_axis": "y",
            "to_world": mi.ScalarTransform4f().look_at(
                origin=cam["eye"], target=cam["target"], up=[0, 1, 0]),
            "film": {"type": "hdrfilm", "width": sc["width"],
                     "height": sc["height"],
                     "rfilter": {"type": "gaussian"}},
            "sampler": {"type": "independent", "sample_count": sc["spp"]},
        },
        "terrain": {"type": "obj", "filename": sc["terrain_obj"], "bsdf": bsdf},
        "sun": {
            "type": "directional",
            "direction": [-sun["dir"][0], -sun["dir"][1], -sun["dir"][2]],
            "irradiance": {"type": "rgb",
                           "value": [c * sun["intensity"] for c in sun["color"]]},
        },
    }

    # the viewport's own sky + volumetric clouds, as the environment light
    if sc.get("sky_hdr") and os.path.isfile(sc["sky_hdr"]):
        scene_dict["env"] = {"type": "envmap", "filename": sc["sky_hdr"]}
        print("using viewport sky panorama:", sc["sky_hdr"])
    else:
        amb = [0.5 * (a + b) * sky["ambient"]
               for a, b in zip(sky["zenith"], sky["horizon"])]
        scene_dict["env"] = {"type": "constant",
                             "radiance": {"type": "rgb", "value": amb}}

    # scene meshes, scattered copies included: base placement is the exported
    # model matrix; each copy swaps in its own translation, yaw and scale,
    # composed exactly as the viewport shader composes them
    import math as _math
    for i, m in enumerate(sc.get("meshes", [])):
        base = mi.ScalarTransform4f(
            [[m["model"][c * 4 + r] for c in range(4)] for r in range(4)])
        mesh_bsdf = {"type": "diffuse",
                     "reflectance": {"type": "rgb", "value": m["color"]}}
        insts = m.get("instances")
        if not insts:
            scene_dict[f"mesh{i}"] = {"type": "obj", "filename": m["obj"],
                                      "to_world": base, "bsdf": mesh_bsdf}
            continue
        scene_dict[f"group{i}"] = {
            "type": "shapegroup",
            "child": {"type": "obj", "filename": m["obj"], "bsdf": mesh_bsdf}}
        bx, by, bz = m["model"][12], m["model"][13], m["model"][14]
        for k, (x, y, z, s, yaw) in enumerate(insts):
            t = (mi.ScalarTransform4f().translate([x - bx, y - by, z - bz])
                 @ base
                 @ mi.ScalarTransform4f()
                 .rotate([0, 1, 0], -_math.degrees(yaw)).scale(s))
            scene_dict[f"inst{i}_{k}"] = {
                "type": "instance",
                "shapegroup": {"type": "ref", "id": f"group{i}"},
                "to_world": t}

    # scene point lights
    for i, L in enumerate(sc.get("lights", [])):
        # a small emissive sphere reads as a physical light in a path tracer;
        # radiance scaled so intensity roughly matches the viewport's falloff
        scene_dict[f"light{i}"] = {
            "type": "sphere",
            "center": L["position"],
            "radius": 0.01,
            "emitter": {
                "type": "area",
                "radiance": {"type": "rgb",
                             "value": [c * L["intensity"] * 40.0
                                       for c in L["color"]]},
            },
        }

    water = sc.get("water", {})
    if water.get("enabled"):
        scene_dict["water"] = {
            "type": "rectangle",
            "to_world": mi.ScalarTransform4f()
            .translate([0.5, water["level"], 0.5])
            .rotate([1, 0, 0], -90).scale(0.5),
            "bsdf": {"type": "roughplastic", "distribution": "ggx",
                     "alpha": float(water.get("roughness", 0.02)),
                     "diffuse_reflectance": {"type": "rgb",
                                             "value": water["deep"]}},
        }

    scene = mi.load_dict(scene_dict)

    # depth once up front so every progressive frame can carry the fog
    depth = _mitsuba_depth(mi, scene_dict, sc) if want_fog else None

    def finish(rgb, path):
        out = rgb
        if depth is not None:
            out = tonemap.apply_height_fog(out, depth, cam["eye"], cam["target"],
                                           cam["fov"], sc["width"], sc["height"],
                                           fog, sun)
        tonemap.save_png(out, path, sc.get("exposure", 1.0))

    import time
    total_spp = int(sc["spp"])
    schedule = _passes(total_spp)
    preview = sc.get("preview") or (sc["output"] + ".preview.png")
    accum = None
    done = 0
    t0 = time.time()
    for i, step in enumerate(schedule):
        img = tonemap.to_numpy(mi.render(scene, spp=step, seed=i))
        accum = img * step if accum is None else accum + img * step
        done += step
        avg = accum / float(done)
        # progressive: refine the same image so the viewer sees it converge
        finish(avg, preview)
        _progress(sc, f"pass {i + 1}/{len(schedule)}  {done}/{total_spp} spp  "
                      f"{time.time() - t0:.1f}s")
        print(f"pass {i + 1}/{len(schedule)} - {done}/{total_spp} spp", flush=True)
    finish(accum / float(done), sc["output"])
    _progress(sc, f"done  {total_spp} spp  {time.time() - t0:.1f}s")
    print("wrote", sc["output"])
    return 0


def _mitsuba_depth(mi, scene_dict: dict, sc: dict):
    """Second, cheap pass that yields per-pixel distance for the fog match."""
    try:
        d = dict(scene_dict)
        d["integrator"] = {"type": "aov", "aovs": "dd.y:depth",
                           "img": {"type": "path", "max_depth": 2}}
        d["sensor"] = dict(scene_dict["sensor"])
        d["sensor"]["sampler"] = {"type": "independent", "sample_count": 4}
        scene = mi.load_dict(d)
        img = tonemap.to_numpy(mi.render(scene))
        # aov output packs [rgb..., depth, ...]; depth is the 4th channel
        if img.ndim == 3 and img.shape[2] >= 4:
            return img[:, :, 3]
    except Exception as e:  # pragma: no cover - engine dependent
        print("depth pass unavailable, skipping fog:", e)
    return None


# ----------------------------------------------------------- blender cycles
CYCLES_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "render_cycles.py")


def render_cycles(sc: dict) -> int:
    blender = find_blender()
    if not blender:
        print("Blender not found. Install it from blender.org (or put "
              "blender.exe on PATH) to use Cycles.")
        return 3
    scene_path = sc["__scene_path"]
    cmd = [blender, "--background", "--factory-startup", "--python",
           CYCLES_SCRIPT, "--", scene_path]
    print("running:", " ".join(cmd))
    rc = subprocess.call(cmd)
    return 0 if rc == 0 and os.path.isfile(sc["output"]) else (rc or 1)


# ------------------------------------------------------------- luxcorerender
def render_luxcore(sc: dict) -> int:
    try:
        import pyluxcore
    except ImportError:
        print("pyluxcore is not installed. Run: pip install pyluxcore")
        return 3
    pyluxcore.Init()
    cam, sun, mat = sc["camera"], sc["sun"], sc["material"]
    props = pyluxcore.Properties()
    props.SetFromString(f"""
        scene.camera.type = perspective
        scene.camera.lookat.orig = {cam['eye'][0]} {cam['eye'][1]} {cam['eye'][2]}
        scene.camera.lookat.target = {cam['target'][0]} {cam['target'][1]} {cam['target'][2]}
        scene.camera.up = 0 1 0
        scene.camera.fieldofview = {cam['fov']}
        scene.materials.terrain.type = roughmatte
        scene.materials.terrain.sigma = {float(mat['roughness']) * 90.0}
        scene.lights.sun.type = sun
        scene.lights.sun.dir = {-sun['dir'][0]} {-sun['dir'][1]} {-sun['dir'][2]}
        scene.lights.sun.gain = {sun['intensity']} {sun['intensity']} {sun['intensity']}
        scene.lights.sky.type = sky2
        scene.lights.sky.dir = {sun['dir'][0]} {sun['dir'][1]} {sun['dir'][2]}
    """)
    scene = pyluxcore.Scene()
    scene.Parse(props)
    scene.DefineMesh("terrain_mesh", *_load_obj(sc["terrain_obj"]))
    obj = pyluxcore.Properties()
    obj.SetFromString("""
        scene.objects.terrain.material = terrain
        scene.objects.terrain.shape = terrain_mesh
    """)
    scene.Parse(obj)
    # scene meshes, scattered copies as transformed object entries: LuxCore
    # takes a per-object 4x4, so each copy is base-model x yaw x scale with
    # its own translation - the same composition the viewport shader uses
    import math as _math
    for i, m in enumerate(sc.get("meshes", [])):
        mesh_id = f"prop{i}_mesh"
        scene.DefineMesh(mesh_id, *_load_obj(m["obj"]))
        matp = pyluxcore.Properties()
        col = m["color"]
        matp.SetFromString(f"""
            scene.materials.propmat{i}.type = matte
            scene.materials.propmat{i}.kd = {col[0]} {col[1]} {col[2]}
        """)
        scene.Parse(matp)
        M = m["model"]  # column-major 4x4

        def compose(tx, ty, tz, s, yaw):
            c, sn = _math.cos(yaw), _math.sin(yaw)
            # R*S in column-major, then base model, then the translation swap
            rs = [c * s, 0, -sn * s, 0, 0, s, 0, 0, sn * s, 0, c * s, 0,
                  0, 0, 0, 1]
            out = [0.0] * 16
            for r in range(4):
                for cc in range(4):
                    out[cc * 4 + r] = sum(M[k * 4 + r] * rs[cc * 4 + k]
                                          for k in range(4))
            out[12] += tx - M[12]
            out[13] += ty - M[13]
            out[14] += tz - M[14]
            return out

        insts = m.get("instances") or [
            (M[12], M[13], M[14], 1.0, 0.0)]
        for k, (x, y, z, s, yaw) in enumerate(insts):
            t = compose(x, y, z, s, yaw)
            op = pyluxcore.Properties()
            op.SetFromString(f"""
                scene.objects.prop{i}_{k}.material = propmat{i}
                scene.objects.prop{i}_{k}.shape = {mesh_id}
                scene.objects.prop{i}_{k}.transformation = {' '.join(str(v) for v in t)}
            """)
            scene.Parse(op)

    cfg = pyluxcore.Properties()
    cfg.SetFromString(f"""
        renderengine.type = PATHCPU
        film.width = {sc['width']}
        film.height = {sc['height']}
        batch.haltspp = {sc['spp']}
        film.outputs.1.type = RGB_IMAGEPIPELINE
        film.outputs.1.filename = {sc['output']}
    """)
    session = pyluxcore.RenderSession(pyluxcore.RenderConfig(cfg, scene))
    session.Start()
    session.WaitForDone()
    session.Stop()
    session.GetFilm().SaveOutputs()
    print("wrote", sc["output"])
    return 0


def _load_obj(path: str):
    """Minimal OBJ reader returning (points, triangles, None, None, None)."""
    pts, tris = [], []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("v "):
                x, y, z = line.split()[1:4]
                pts.append((float(x), float(y), float(z)))
            elif line.startswith("f "):
                idx = [int(tok.split("/")[0]) - 1 for tok in line.split()[1:]]
                for k in range(2, len(idx)):
                    tris.append((idx[0], idx[k - 1], idx[k]))
    return pts, tris, None, None, None


def render_appleseed(sc: dict) -> int:
    print("appleseed has had no maintained release since 2019 and no Windows "
          "Python package; use Mitsuba 3, Cycles or LuxCore instead.")
    return 3


ENGINES = {
    "mitsuba": render_mitsuba,
    "cycles": render_cycles,
    "luxcore": render_luxcore,
    "appleseed": render_appleseed,
}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    if sys.argv[1] == "--probe":
        return print_probe()
    scene_path = sys.argv[1]
    with open(scene_path, "r", encoding="utf-8") as f:
        sc = json.load(f)
    sc["__scene_path"] = os.path.abspath(scene_path)
    engine = sc.get("engine", "mitsuba")
    fn = ENGINES.get(engine)
    if not fn:
        print("unknown engine:", engine)
        return 2
    return fn(sc)


if __name__ == "__main__":
    sys.exit(main())
