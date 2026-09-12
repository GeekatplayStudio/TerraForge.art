"""Geekatplay TerraForge — the LuxCoreRender backend.

Split from render_engines.py for the 500-line module rule. It builds the same
scene the other engines do from scene.json: the placed terrain with its
pictures, the ground out to the horizon, the viewport's sky panorama as the
environment, the sun, the sea as a mesh, every scene mesh by its parts with
each scattered copy, point lights and the camera. The picture is developed as
Mitsuba's is (tonemap.py: exposure, grade, ACES, and the height fog from a
depth pass), so the two engines agree on light and colour.

One frame for everything, and it is LuxCore's own. Its sky and sun models are
Z-up, so the scene goes in as Blender's importer turns a y-up file:
(x, y, z) -> (x, -z, y) (`CONV`, `to_lux`). Geometry keeps its y-up
coordinates and every object carries the turn as its transformation.

It had never run before pyluxcore was installed, and did not: DefineMesh was
called with too few arguments, and the sky and sun were set on y-up axes in a
renderer whose sky is z-up.
"""
from __future__ import annotations

import math
import os
import time

from . import tonemap
from .render_instances import instance_matrices, mat_mul, mesh_parts

# our y-up into LuxCore's z-up, column-major: x stays, y goes to z, z to -y
CONV = [1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, -1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0]

# The environment's turn about z. Our panorama's longitude is
# u = atan2(x, -z) / 2pi + 0.5 on y-up axes (the sky shader's panorama mode);
# LuxCore's infinite light reads its picture from its own spherical angles on
# z-up axes. Measured with a marked panorama: turned this way the mark at our
# +x lands in the middle of a camera looking along +x; unturned, the mark at
# our +z stood there.
ENV_TURN_DEG = 90.0


def to_lux(v) -> tuple:
    return (float(v[0]), -float(v[2]), float(v[1]))


def _path(p: str) -> str:
    """A file name LuxCore's property parser reads whole: forward slashes,
    quoted, since a Windows path's backslashes and spaces split it."""
    return '"' + p.replace("\\", "/") + '"'


def _mat_str(m) -> str:
    return " ".join(repr(float(v)) for v in m)


def _rot_z(degrees: float) -> list[float]:
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return [c, s, 0.0, 0.0, -s, c, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def env_transformation() -> list[float]:
    """The sky picture's frame: turned about z, then mirrored across y. The
    infinite light runs its longitude the other way round from our panorama,
    which a turn cannot undo - turned alone, a camera looking between +x and
    +z saw the mark at +x on its left and the one at -z on its right."""
    mirror_y = [1.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    return mat_mul(mirror_y, _rot_z(ENV_TURN_DEG))


def _load_obj(path: str):
    """(points, triangles, normals, uvs) from an OBJ. Every exporter here
    writes a vertex, its normal and its texture coordinate at one index, so
    the corner's v index serves all three; normals and uvs are None when the
    file has none (or not one per vertex)."""
    pts, nrm, uvs, tris = [], [], [], []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("v "):
                x, y, z = line.split()[1:4]
                pts.append((float(x), float(y), float(z)))
            elif line.startswith("vn "):
                x, y, z = line.split()[1:4]
                nrm.append((float(x), float(y), float(z)))
            elif line.startswith("vt "):
                u, v = line.split()[1:3]
                uvs.append((float(u), float(v)))
            elif line.startswith("f "):
                idx = [int(tok.split("/")[0]) - 1 for tok in line.split()[1:]]
                for k in range(2, len(idx)):
                    tris.append((idx[0], idx[k - 1], idx[k]))
    return (pts, tris, nrm if len(nrm) == len(pts) else None,
            uvs if len(uvs) == len(pts) else None)


def _imagemap(name: str, path: str, gamma: float) -> list[str]:
    """A picture on a mesh's own texture coordinates. The exporters write
    v up from the picture's bottom row, as Blender and Mitsuba read it;
    LuxCore reads rows from the top, so the mapping flips v."""
    return [f"scene.textures.{name}.type = imagemap",
            f"scene.textures.{name}.file = {_path(path)}",
            f"scene.textures.{name}.gamma = {gamma}",
            f"scene.textures.{name}.mapping.type = uvmapping2d",
            f"scene.textures.{name}.mapping.uvscale = 1 -1",
            f"scene.textures.{name}.mapping.uvdelta = 0 1"]


def _disney(name: str, texture: str | None, color, mat: dict) -> list[str]:
    """The terrain's material: the principled surface Mitsuba renders it with."""
    lines = [f"scene.materials.{name}.type = disney",
             f"scene.materials.{name}.roughness = {float(mat.get('roughness', 0.85))}",
             f"scene.materials.{name}.metallic = {float(mat.get('metallic', 0.0))}",
             f"scene.materials.{name}.specular = {min(max(float(mat.get('specular', 0.35)), 0.0), 1.0)}"]
    if texture and os.path.isfile(texture):
        lines += _imagemap(f"{name}_tex", texture, 2.2)
        lines.append(f"scene.materials.{name}.basecolor = {name}_tex")
    else:
        c = color or (0.35, 0.32, 0.28)
        lines.append(f"scene.materials.{name}.basecolor = {c[0]} {c[1]} {c[2]}")
    return lines


def _part_material(name: str, p: dict) -> list[str]:
    """One mesh part's material: its picture or its colour, lit from both
    sides (a leaf is one sheet), cut where its alpha picture is clear."""
    lines = [f"scene.materials.{name}.type = matte"]
    tex = p.get("texture")
    if tex and os.path.isfile(tex):
        lines += _imagemap(f"{name}_tex", tex, 2.2)
        lines.append(f"scene.materials.{name}.kd = {name}_tex")
    else:
        c = p.get("color") or (0.6, 0.6, 0.6)
        lines.append(f"scene.materials.{name}.kd = {c[0]} {c[1]} {c[2]}")
    alpha = p.get("alpha")
    if alpha and os.path.isfile(alpha):
        # LuxCore's "transparency" is how much of the surface is there: 1 solid
        lines += _imagemap(f"{name}_alpha", alpha, 1.0)
        lines.append(f"scene.materials.{name}.transparency = {name}_alpha")
    return lines


def _progress(sc: dict, text: str) -> None:
    path = sc.get("progress_file")
    if not path:
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    except OSError:
        pass


def build_props(sc: dict) -> tuple[list[str], list[tuple]]:
    """The scene as LuxCore property lines, and the meshes to define as
    (name, obj path). Kept apart from pyluxcore so it can be tested without."""
    cam, sun = sc["camera"], sc["sun"]
    lines: list[str] = []
    meshes: list[tuple] = []

    eye, tgt = to_lux(cam["eye"]), to_lux(cam["target"])
    # LuxCore's field of view is across the picture's longer side (its screen
    # window runs -1..1 along it), ours always the vertical one. Measured: 40
    # on a 320x180 film spanned 40 degrees across and 22 up.
    w, h = int(sc["width"]), int(sc["height"])
    fov = float(cam["fov"])
    if w > h:
        fov = math.degrees(2.0 * math.atan(math.tan(math.radians(fov) * 0.5) * w / max(h, 1)))
    lines += ["scene.camera.type = perspective",
              f"scene.camera.lookat.orig = {eye[0]} {eye[1]} {eye[2]}",
              f"scene.camera.lookat.target = {tgt[0]} {tgt[1]} {tgt[2]}",
              "scene.camera.up = 0 0 1",
              f"scene.camera.fieldofview = {fov}",
              # A unit is a whole tile: the default near clip hides the first
              # stretch in front of the lens (the black waterline edge).
              "scene.camera.cliphither = 1e-5",
              "scene.camera.clipyon = 1e7"]

    # The sun as Mitsuba lights it: the same irradiance, from a disc the sun's
    # width for its soft shadow edge. A distant light's direction is the way
    # its light travels, and its colour is radiance over a cone of half-angle
    # theta: a white matte floor facing it returns colour x sin^2(theta)
    # (measured, 0.0302 at the default 10 degrees), so irradiance E needs a
    # radiance of E / (pi sin^2 theta). Taken as irradiance, the sun was
    # twenty thousand times too dim and cast no shadow at all.
    d = to_lux(sun["dir"])
    half = math.radians(max(float(sun.get("size_deg", 0.53)) * 0.5, 0.05))
    k = 1.0 / (math.pi * math.sin(half) ** 2)
    g = [float(c) * float(sun["intensity"]) * k for c in sun.get("color", [1, 1, 1])]
    lines += ["scene.lights.sun.type = distant",
              f"scene.lights.sun.direction = {-d[0]} {-d[1]} {-d[2]}",
              f"scene.lights.sun.color = {g[0]} {g[1]} {g[2]}",
              f"scene.lights.sun.theta = {math.degrees(half)}"]

    # the environment: the viewport's own sky and clouds
    if sc.get("sky_hdr") and os.path.isfile(sc["sky_hdr"]):
        lines += ["scene.lights.sky.type = infinite",
                  f"scene.lights.sky.file = {_path(sc['sky_hdr'])}",
                  "scene.lights.sky.gamma = 1.0",
                  f"scene.lights.sky.transformation = {_mat_str(env_transformation())}"]
    else:
        sky = sc["sky"]
        amb = [0.5 * (a + b) * sky["ambient"] for a, b in zip(sky["zenith"], sky["horizon"])]
        lines += ["scene.lights.sky.type = constantinfinite",
                  f"scene.lights.sky.color = {amb[0]} {amb[1]} {amb[2]}"]

    def obj(name: str, shape: str, material: str, matrix) -> None:
        lines.extend([f"scene.objects.{name}.shape = {shape}",
                      f"scene.objects.{name}.material = {material}",
                      f"scene.objects.{name}.transformation = {_mat_str(matrix)}"])

    mat = sc["material"]
    lines += _disney("terrain", sc.get("albedo"), None, mat)
    meshes.append(("terrain_mesh", sc["terrain_obj"]))
    obj("terrain", "terrain_mesh", "terrain", CONV)
    if sc.get("surround_obj") and os.path.isfile(sc["surround_obj"]):
        # the ground past the tile, in its own palette picture
        lines += _disney("surround", sc.get("surround_albedo"), None, mat)
        meshes.append(("surround_mesh", sc["surround_obj"]))
        obj("surround", "surround_mesh", "surround", CONV)

    water = sc.get("water", {})
    if water.get("enabled") and water.get("mesh") and os.path.isfile(water["mesh"]):
        dc = water["deep"]
        rough = max(float(water.get("roughness", 0.02)), 0.001)
        lines += ["scene.materials.sea.type = glossy2",
                  f"scene.materials.sea.kd = {dc[0]} {dc[1]} {dc[2]}",
                  "scene.materials.sea.ks = 0.04 0.04 0.04",
                  f"scene.materials.sea.uroughness = {rough}",
                  f"scene.materials.sea.vroughness = {rough}"]
        meshes.append(("sea_mesh", water["mesh"]))
        obj("sea", "sea_mesh", "sea", CONV)

    # scene meshes by their parts; each copy a transformed object with the
    # same composition the viewport draws it with (render_instances.py)
    for i, m in enumerate(sc.get("meshes", [])):
        parts = mesh_parts(m)
        for k, p in enumerate(parts):
            meshes.append((f"prop{i}_{k}_mesh", p["obj"]))
            lines += _part_material(f"propmat{i}_{k}", p)
        for n, t in enumerate(instance_matrices(m)):
            placed = mat_mul(CONV, t)
            for k in range(len(parts)):
                obj(f"prop{i}_{k}_{n}", f"prop{i}_{k}_mesh", f"propmat{i}_{k}", placed)

    # scene point lights, as Mitsuba's small bright spheres read
    for i, L in enumerate(sc.get("lights", [])):
        pos = to_lux(L["position"])
        c = [float(v) * float(L["intensity"]) for v in L["color"]]
        if L.get("type") == "spot":
            dr = to_lux(L.get("direction", [0, -1, 0]))
            cone = max(float(L.get("cone_deg", 40.0)) * 0.5, 1.0)
            lines += [f"scene.lights.light{i}.type = spot",
                      f"scene.lights.light{i}.position = {pos[0]} {pos[1]} {pos[2]}",
                      f"scene.lights.light{i}.target = {pos[0] + dr[0]} {pos[1] + dr[1]} {pos[2] + dr[2]}",
                      f"scene.lights.light{i}.coneangle = {cone}",
                      f"scene.lights.light{i}.conedeltaangle = {cone * 0.2}",
                      f"scene.lights.light{i}.color = {c[0] * 0.5} {c[1] * 0.5} {c[2] * 0.5}"]
        else:
            lines += [f"scene.lights.light{i}.type = point",
                      f"scene.lights.light{i}.position = {pos[0]} {pos[1]} {pos[2]}",
                      f"scene.lights.light{i}.color = {c[0]} {c[1]} {c[2]}"]
    return lines, meshes


def render_luxcore(sc: dict) -> int:
    try:
        import numpy as np
        import pyluxcore
    except ImportError:
        print("pyluxcore is not installed. Run: pip install pyluxcore")
        return 3
    pyluxcore.Init()
    t0 = time.time()
    _progress(sc, "luxcore: building the scene")
    lines, meshes = build_props(sc)
    scene = pyluxcore.Scene()
    loaded: dict[str, tuple] = {}
    for name, path in meshes:
        if path not in loaded:
            loaded[path] = _load_obj(path)
        pts, tris, nrm, uvs = loaded[path]
        scene.DefineMesh(name, pts, tris, nrm, uvs, None, None)
    props = pyluxcore.Properties()
    props.SetFromString("\n".join(lines))
    scene.Parse(props)

    w, h, spp = int(sc["width"]), int(sc["height"]), max(int(sc["spp"]), 1)
    cfg = pyluxcore.Properties()
    cfg.SetFromString("\n".join([
        f"renderengine.type = {os.environ.get('GPX_LUXCORE_ENGINE', 'PATHCPU')}",
        "sampler.type = SOBOL",
        f"film.width = {w}",
        f"film.height = {h}",
        "film.filter.type = BLACKMANHARRIS",
        f"batch.haltspp = {spp}",
        "path.pathdepth.total = 8",
        "film.outputs.0.type = RGB",
        "film.outputs.0.filename = rgb.exr",
        "film.outputs.1.type = DEPTH",
        "film.outputs.1.filename = depth.exr",
    ]))
    session = pyluxcore.RenderSession(pyluxcore.RenderConfig(cfg, scene))
    session.Start()

    fog = sc.get("fog", {})
    want_fog = fog.get("type", 0) != 0 and fog.get("density", 0) > 0
    cam, sun = sc["camera"], sc["sun"]
    preview = sc.get("preview") or (sc["output"] + ".preview.png")

    def develop(path: str) -> None:
        film = session.GetFilm()
        rgb = np.zeros((h * w * 3,), dtype=np.float32)
        film.GetOutputFloat(pyluxcore.FilmOutputType.RGB, rgb)
        img = rgb.reshape((h, w, 3))[::-1].copy()  # LuxCore's rows run bottom up
        if want_fog:
            depth = np.zeros((h * w,), dtype=np.float32)
            film.GetOutputFloat(pyluxcore.FilmOutputType.DEPTH, depth)
            depth = depth.reshape((h, w))[::-1]
            # a ray that met nothing is the sky, which the fog treats by angle
            depth = np.where(np.isfinite(depth) & (depth < 1.0e29), depth, 0.0)
            img = tonemap.apply_height_fog(img, depth, cam["eye"], cam["target"], cam["fov"],
                                           w, h, fog, sun)
        tonemap.save_png(img, path, sc.get("exposure", 1.0), sc.get("grade"),
                         sc.get("saturation", 1.0))

    last_preview = 0.0
    while not session.HasDone():
        time.sleep(0.5)
        session.UpdateStats()
        stats = session.GetStats()
        done = stats.Get("stats.renderengine.pass").GetInt()
        _progress(sc, f"luxcore {done}/{spp} spp  {time.time() - t0:.1f}s")
        if time.time() - last_preview > 3.0 and done > 0:
            develop(preview)
            last_preview = time.time()
    session.Stop()
    develop(sc["output"])
    _progress(sc, f"done  {spp} spp  {time.time() - t0:.1f}s")
    print("wrote", sc["output"])
    return 0
