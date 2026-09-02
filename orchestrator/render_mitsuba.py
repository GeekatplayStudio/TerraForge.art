"""Geekatplay TerraForge — offline render via Mitsuba 3.

Reads the scene.json exported by the app's Render panel and renders a
physically-based path-traced image. Install once with:  pip install mitsuba
Exit codes: 0 ok, 2 bad scene, 3 mitsuba missing.
"""
from __future__ import annotations

import json
import sys


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    try:
        with open(sys.argv[1], "r", encoding="utf-8") as f:
            sc = json.load(f)
    except Exception as e:
        print("bad scene json:", e)
        return 2

    try:
        import mitsuba as mi
    except ImportError:
        print("mitsuba is not installed. Run: pip install mitsuba")
        return 3

    for variant in ("llvm_ad_rgb", "scalar_rgb"):
        try:
            mi.set_variant(variant)
            break
        except Exception:
            continue
    print("mitsuba variant:", mi.variant())

    cam = sc["camera"]
    sun = sc["sun"]
    sky = sc["sky"]

    terrain_bsdf: dict = {"type": "diffuse"}
    if sc.get("albedo"):
        terrain_bsdf["reflectance"] = {"type": "bitmap", "filename": sc["albedo"]}
    else:
        terrain_bsdf["reflectance"] = {"type": "rgb", "value": [0.35, 0.32, 0.28]}

    scene_dict = {
        "type": "scene",
        "integrator": {"type": "path", "max_depth": 8},
        "sensor": {
            "type": "perspective",
            "fov": cam["fov"],
            "fov_axis": "y",
            "to_world": mi.ScalarTransform4f().look_at(
                origin=cam["eye"], target=cam["target"], up=[0, 1, 0]
            ),
            "film": {
                "type": "hdrfilm",
                "width": sc["width"],
                "height": sc["height"],
                "rfilter": {"type": "gaussian"},
            },
            "sampler": {"type": "independent", "sample_count": sc["spp"]},
        },
        "terrain": {
            "type": "obj",
            "filename": sc["terrain_obj"],
            "bsdf": terrain_bsdf,
        },
        "sun": {
            "type": "directional",
            "direction": [-sun["dir"][0], -sun["dir"][1], -sun["dir"][2]],
            "irradiance": {
                "type": "rgb",
                "value": [c * sun["intensity"] for c in sun["color"]],
            },
        },
        "sky": {
            "type": "constant",
            "radiance": {
                "type": "rgb",
                "value": [
                    0.5 * (a + b) * sky["ambient"]
                    for a, b in zip(sky["zenith"], sky["horizon"])
                ],
            },
        },
    }
    # scene meshes: the base placement uses the exported model matrix; each
    # scattered copy swaps in its own translation, yaw and scale, exactly as
    # the viewport shader composes them
    for i, m in enumerate(sc.get("meshes", [])):
        base = mi.ScalarTransform4f(
            [[m["model"][c * 4 + r] for c in range(4)] for r in range(4)]
        )  # column-major in, row-major ctor
        bsdf = {"type": "diffuse",
                "reflectance": {"type": "rgb", "value": m["color"]}}
        insts = m.get("instances")
        if not insts:
            scene_dict[f"mesh{i}"] = {
                "type": "obj", "filename": m["obj"],
                "to_world": base, "bsdf": bsdf,
            }
            continue
        scene_dict[f"group{i}"] = {
            "type": "shapegroup",
            "child": {"type": "obj", "filename": m["obj"], "bsdf": bsdf},
        }
        bx, by, bz = m["model"][12], m["model"][13], m["model"][14]
        import math as _math
        for k, (x, y, z, s, yaw) in enumerate(insts):
            t = (
                mi.ScalarTransform4f()
                .translate([x - bx, y - by, z - bz])
                @ base
                @ mi.ScalarTransform4f()
                .rotate([0, 1, 0], -_math.degrees(yaw))
                .scale(s)
            )
            scene_dict[f"inst{i}_{k}"] = {
                "type": "instance",
                "shapegroup": {"type": "ref", "id": f"group{i}"},
                "to_world": t,
            }

    water = sc.get("water", {})
    if water.get("enabled"):
        lv = water["level"]
        scene_dict["water"] = {
            "type": "rectangle",
            "to_world": mi.ScalarTransform4f()
            .translate([0.5, lv, 0.5])
            .rotate([1, 0, 0], -90)
            .scale(0.5),
            "bsdf": {
                "type": "roughplastic",
                "distribution": "ggx",
                "alpha": 0.02,
                "diffuse_reflectance": {"type": "rgb", "value": water["deep"]},
            },
        }

    scene = mi.load_dict(scene_dict)
    img = mi.render(scene)
    mi.util.write_bitmap(sc["output"], img)
    print("wrote", sc["output"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
