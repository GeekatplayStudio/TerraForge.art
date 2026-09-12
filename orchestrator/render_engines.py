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
from .render_instances import instance_local as _instance_local
from .render_instances import mesh_parts as _mesh_parts
from .render_luxcore import render_luxcore


def _mitsuba_part_bsdf(p: dict) -> dict:
    """One mesh part's material: its picture or its colour, both faces
    shading (a leaf is one sheet), and cut where the picture is clear, as the
    viewport cuts a leaf card at half alpha."""
    if p.get("texture") and os.path.isfile(p["texture"]):
        refl = {"type": "bitmap", "filename": p["texture"]}
    else:
        refl = {"type": "rgb", "value": p["color"]}
    bsdf: dict = {"type": "twosided",
                  "material": {"type": "diffuse", "reflectance": refl}}
    if p.get("alpha") and os.path.isfile(p["alpha"]):
        bsdf = {"type": "mask", "material": bsdf,
                "opacity": {"type": "bitmap", "filename": p["alpha"], "raw": True}}
    return bsdf


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

    # The ground beyond the tile, out to the horizon. Without it a render is
    # one square of terrain floating in a void while the viewport through the
    # same camera shows a landscape - the single loudest difference between
    # the two pictures. Its own albedo, because its UVs are the surround
    # grid's and not the tile's.
    surround_bsdf: dict = dict(bsdf)
    if sc.get("surround_albedo"):
        surround_bsdf["base_color"] = {"type": "bitmap",
                                       "filename": sc["surround_albedo"]}

    scene_dict: dict = {
        "type": "scene",
        "integrator": {"type": "path", "max_depth": 8},
        "sensor": {
            "type": "perspective",
            "fov": cam["fov"],
            "fov_axis": "y",
            # A scene unit is a whole tile, kilometres across: Mitsuba's
            # default near clip of 0.01 cut away the first fifty metres in
            # front of the lens, and a camera by a shore looked under the sea
            # through the hole - the black edge along the waterline.
            "near_clip": 1e-5,
            "far_clip": 1e7,
            "to_world": mi.ScalarTransform4f().look_at(
                origin=cam["eye"], target=cam["target"], up=[0, 1, 0]),
            "film": {"type": "hdrfilm", "width": sc["width"],
                     "height": sc["height"],
                     "rfilter": {"type": "gaussian"}},
            "sampler": {"type": "independent", "sample_count": sc["spp"]},
        },
        # Both faces shade: a ground mesh seen edge-on shows the odd back
        # face along a crest or a cut, and Mitsuba's one-sided BSDFs return
        # nothing there - a black sliver where the viewport draws ground.
        "terrain": {"type": "obj", "filename": sc["terrain_obj"],
                    "bsdf": {"type": "twosided", "material": bsdf}},
        "sun": {
            "type": "directional",
            "direction": [-sun["dir"][0], -sun["dir"][1], -sun["dir"][2]],
            "irradiance": {"type": "rgb",
                           "value": [c * sun["intensity"] for c in sun["color"]]},
        },
    }
    if sc.get("surround_obj") and os.path.isfile(sc["surround_obj"]):
        scene_dict["surround"] = {"type": "obj",
                                  "filename": sc["surround_obj"],
                                  "bsdf": {"type": "twosided",
                                           "material": surround_bsdf}}

    # The viewport's own sky + volumetric clouds, as the environment light.
    # Mitsuba reads longitude from atan2(x, -z) with no half-turn offset, and
    # the panorama is written with one (u = atan2(x, -z)/2pi + 0.5, the sky
    # shader's panorama mode): unturned, every render's sky was the sky
    # behind the camera. Measured with a marked panorama - the mark at +x now
    # sits in the middle of a camera looking along +x.
    if sc.get("sky_hdr") and os.path.isfile(sc["sky_hdr"]):
        scene_dict["env"] = {"type": "envmap", "filename": sc["sky_hdr"],
                             "to_world": mi.ScalarTransform4f().rotate([0, 1, 0], 180.0)}
        print("using viewport sky panorama:", sc["sky_hdr"])
    else:
        amb = [0.5 * (a + b) * sky["ambient"]
               for a, b in zip(sky["zenith"], sky["horizon"])]
        scene_dict["env"] = {"type": "constant",
                             "radiance": {"type": "rgb", "value": amb}}

    # scene meshes, scattered copies included: base placement is the exported
    # model matrix; each copy swaps in its own translation, yaw and scale,
    # composed exactly as the viewport shader composes them. A mesh goes in
    # as its parts, each with its own colour and picture (a plant's bark and
    # leaves), since a shape takes one material.
    for i, m in enumerate(sc.get("meshes", [])):
        base = mi.ScalarTransform4f(
            [[m["model"][c * 4 + r] for c in range(4)] for r in range(4)])
        parts = {f"part{k}": {"type": "obj", "filename": p["obj"],
                              "bsdf": _mitsuba_part_bsdf(p)}
                 for k, p in enumerate(_mesh_parts(m))}
        insts = m.get("instances")
        if not insts:
            for key, shape in parts.items():
                scene_dict[f"mesh{i}_{key}"] = dict(shape, to_world=base)
            continue
        scene_dict[f"group{i}"] = dict({"type": "shapegroup"}, **parts)
        bx, by, bz = m["model"][12], m["model"][13], m["model"][14]
        for k, row in enumerate(insts):
            t = (mi.ScalarTransform4f().translate(
                    [row[0] - bx, row[1] - by, row[2] - bz])
                 @ base @ _instance_local(mi, row))
            scene_dict[f"inst{i}_{k}"] = {
                "type": "instance",
                "shapegroup": {"type": "ref", "id": f"group{i}"},
                "to_world": t}

    # scene lights: spots use Mitsuba's spot emitter, points a small
    # emissive sphere (which also reads as a visible bulb)
    for i, L in enumerate(sc.get("lights", [])):
        if L.get("type") == "spot":
            p = L["position"]
            d = L.get("direction", [0, -1, 0])
            scene_dict[f"light{i}"] = {
                "type": "spot",
                "cutoff_angle": max(L.get("cone_deg", 40.0) * 0.5, 1.0),
                "intensity": {"type": "rgb",
                              "value": [c * L["intensity"] * 0.5
                                        for c in L["color"]]},
                "to_world": mi.ScalarTransform4f().look_at(
                    origin=p,
                    target=[p[0] + d[0], p[1] + d[1], p[2] + d[2]],
                    up=[0, 1, 0] if abs(d[1]) < 0.95 else [1, 0, 0]),
            }
            continue
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
        # as wide as the ground it lies in: a sea cut off at the tile's edge
        # is a puddle, and the horizon behind it is empty sky
        water_bsdf = {"type": "roughplastic", "distribution": "ggx",
                      "alpha": float(water.get("roughness", 0.02)),
                      "diffuse_reflectance": {"type": "rgb",
                                              "value": water["deep"]}}
        if water.get("mesh") and os.path.isfile(water["mesh"]):
            # the sea on the world's own curve; a flat plane rises through
            # the land in the distance and draws a bright band at the horizon
            scene_dict["water"] = {"type": "obj", "filename": water["mesh"],
                                   "bsdf": water_bsdf}
        else:
            # mitsuba's rectangle is two units across before scaling, so the
            # scale is the half-width: the ground reaches `extent` either way
            half = max(float(water.get("extent", 0.5)), 0.5)
            scene_dict["water"] = {
                "type": "rectangle",
                "to_world": mi.ScalarTransform4f()
                .translate([0.5, water["level"], 0.5])
                .rotate([1, 0, 0], -90).scale(half),
                "bsdf": water_bsdf,
            }

    # panorama: no spherical sensor plugin ships with pip mitsuba, so render
    # six 90-degree cube faces at the same eye and remap them to one
    # equirectangular frame
    if sc.get("panorama"):
        import numpy as np
        eye = cam["eye"]
        face_res = max(sc["height"], 256)
        # forward vectors and their ups for the six faces
        faces = [((1, 0, 0), (0, 1, 0)), ((-1, 0, 0), (0, 1, 0)),
                 ((0, 0, 1), (0, 1, 0)), ((0, 0, -1), (0, 1, 0)),
                 ((0, 1, 0), (0, 0, -1)), ((0, -1, 0), (0, 0, 1))]
        imgs = []
        for k, (fwd, up) in enumerate(faces):
            d = dict(scene_dict)
            d["sensor"] = {
                "type": "perspective", "fov": 90.0, "fov_axis": "y",
                "near_clip": 1e-5, "far_clip": 1e7,  # as the main sensor
                "to_world": mi.ScalarTransform4f().look_at(
                    origin=eye,
                    target=[eye[0] + fwd[0], eye[1] + fwd[1], eye[2] + fwd[2]],
                    up=list(up)),
                "film": {"type": "hdrfilm", "width": face_res,
                         "height": face_res,
                         "rfilter": {"type": "gaussian"}},
                "sampler": {"type": "independent",
                            "sample_count": sc["spp"]},
            }
            imgs.append(np.array(mi.render(mi.load_dict(d)))[..., :3])
            _progress(sc, f"panorama face {k + 1}/6")
            print(f"panorama face {k + 1}/6", flush=True)
        W, H = sc["width"], sc["height"]
        ys, xs = np.mgrid[0:H, 0:W]
        lon = (xs / W) * 2 * np.pi - np.pi
        lat = np.pi / 2 - (ys / H) * np.pi
        dx = np.cos(lat) * np.sin(lon)
        dy = np.sin(lat)
        dz = -np.cos(lat) * np.cos(lon)
        out = np.zeros((H, W, 3), np.float32)
        ax = np.stack([np.abs(dx), np.abs(dy), np.abs(dz)])
        major = np.argmax(ax, axis=0)
        # face picking mirrors the render list above
        def put(mask, img, u, v):
            # bilinear: the nearest-texel version left visible stair-steps
            # along every face boundary
            fu = np.clip((u + 1) * 0.5 * (face_res - 1), 0, face_res - 1)
            fv = np.clip((1 - (v + 1) * 0.5) * (face_res - 1), 0,
                         face_res - 1)
            u0 = np.floor(fu).astype(int)
            v0 = np.floor(fv).astype(int)
            u1 = np.minimum(u0 + 1, face_res - 1)
            v1 = np.minimum(v0 + 1, face_res - 1)
            du = (fu - u0)[..., None]
            dv = (fv - v0)[..., None]
            m = mask
            out[m] = (img[v0[m], u0[m]] * (1 - du[m]) * (1 - dv[m]) +
                      img[v0[m], u1[m]] * du[m] * (1 - dv[m]) +
                      img[v1[m], u0[m]] * (1 - du[m]) * dv[m] +
                      img[v1[m], u1[m]] * du[m] * dv[m])
        m = (major == 0) & (dx > 0); put(m, imgs[0], -dz / np.abs(dx), dy / np.abs(dx))
        m = (major == 0) & (dx < 0); put(m, imgs[1], dz / np.abs(dx), dy / np.abs(dx))
        m = (major == 2) & (dz > 0); put(m, imgs[2], dx / np.abs(dz), dy / np.abs(dz))
        m = (major == 2) & (dz < 0); put(m, imgs[3], -dx / np.abs(dz), dy / np.abs(dz))
        m = (major == 1) & (dy > 0); put(m, imgs[4], -dz / np.abs(dy), -dx / np.abs(dy))
        m = (major == 1) & (dy < 0); put(m, imgs[5], -dz / np.abs(dy), dx / np.abs(dy))
        tonemap.save_png(out, sc["output"], sc.get("exposure", 1.0),
                         sc.get("grade"), sc.get("saturation", 1.0))
        _progress(sc, "panorama done")
        print("wrote", sc["output"])
        return 0

    scene = mi.load_dict(scene_dict)

    # depth once up front so every progressive frame can carry the fog
    depth = _mitsuba_depth(mi, scene_dict, sc) if want_fog else None

    def finish(rgb, path):
        out = rgb
        if depth is not None:
            out = tonemap.apply_height_fog(out, depth, cam["eye"], cam["target"],
                                           cam["fov"], sc["width"], sc["height"],
                                           fog, sun)
        # developed by the camera that took it: its exposure and its film
        tonemap.save_png(out, path, sc.get("exposure", 1.0),
                         sc.get("grade"), sc.get("saturation", 1.0))

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

    # render passes: one cheap AOV pass writes depth and normal EXRs beside
    # the image, for anyone compositing
    if sc.get("passes"):
        base = os.path.splitext(sc["output"])[0]
        aov_dict = dict(scene_dict)
        aov_dict["integrator"] = {
            "type": "aov",
            "aovs": "dd:depth,nn:sh_normal",
            "img": {"type": "path", "max_depth": 2},
        }
        aov_scene = mi.load_dict(aov_dict)
        img = mi.render(aov_scene, spp=4)
        try:
            import numpy as np
            arr = np.array(img)
            # channel layout: rgb(3) + depth(1) + normal(3)
            depth = arr[..., 3:4]
            normal = arr[..., 4:7]
            mi.util.write_bitmap(base + "_depth.exr",
                                 mi.Bitmap(np.ascontiguousarray(depth)))
            mi.util.write_bitmap(base + "_normal.exr",
                                 mi.Bitmap(np.ascontiguousarray(
                                     (normal + 1.0) * 0.5)))
            print("wrote", base + "_depth.exr", "and", base + "_normal.exr")
        except Exception as e:
            print("aov write failed:", e)
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
