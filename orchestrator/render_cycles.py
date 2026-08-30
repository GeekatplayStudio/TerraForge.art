"""Geekatplay TerraForge — Blender Cycles backend.

Run by render_engines.py as:
    blender --background --factory-startup --python render_cycles.py -- scene.json
Builds the scene from the exported description: terrain mesh, albedo, the
viewport sky panorama as the world environment, sun, and water plane.
"""
from __future__ import annotations

import json
import math
import sys

import bpy  # provided by Blender


def scene_path_from_argv() -> str:
    argv = sys.argv
    if "--" in argv:
        return argv[argv.index("--") + 1]
    raise SystemExit("no scene json passed after --")


def main() -> None:
    with open(scene_path_from_argv(), "r", encoding="utf-8") as f:
        sc = json.load(f)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = int(sc["spp"])
    scene.render.resolution_x = int(sc["width"])
    scene.render.resolution_y = int(sc["height"])
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = sc["output"]
    scene.view_settings.view_transform = "Filmic"
    scene.view_settings.exposure = math.log2(max(float(sc.get("exposure", 1.0)), 1e-3))

    # GPU if available
    prefs = bpy.context.preferences.addons.get("cycles")
    if prefs:
        cprefs = prefs.preferences
        for backend in ("OPTIX", "CUDA", "HIP", "ONEAPI"):
            try:
                cprefs.compute_device_type = backend
                cprefs.get_devices()
                if any(d.type == backend for d in cprefs.devices):
                    scene.cycles.device = "GPU"
                    for d in cprefs.devices:
                        d.use = True
                    break
            except Exception:
                continue

    # terrain
    bpy.ops.wm.obj_import(filepath=sc["terrain_obj"])
    terrain = bpy.context.selected_objects[0]
    mat = bpy.data.materials.new("TerraForgeTerrain")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    m = sc["material"]
    bsdf.inputs["Roughness"].default_value = float(m["roughness"])
    bsdf.inputs["Metallic"].default_value = float(m["metallic"])
    if sc.get("albedo"):
        tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(sc["albedo"])
        mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    terrain.data.materials.append(mat)

    # environment: the viewport sky + clouds panorama
    world = bpy.data.worlds.new("TerraForgeWorld")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    if sc.get("sky_hdr"):
        env = world.node_tree.nodes.new("ShaderNodeTexEnvironment")
        env.image = bpy.data.images.load(sc["sky_hdr"])
        world.node_tree.links.new(env.outputs["Color"], bg.inputs["Color"])
    else:
        sky = sc["sky"]
        bg.inputs["Color"].default_value = (
            *[0.5 * (a + b) * sky["ambient"]
              for a, b in zip(sky["zenith"], sky["horizon"])], 1.0)

    # sun
    sun = sc["sun"]
    light = bpy.data.lights.new("Sun", type="SUN")
    light.energy = float(sun["intensity"])
    light.color = tuple(sun["color"])
    obj = bpy.data.objects.new("Sun", light)
    scene.collection.objects.link(obj)
    d = sun["dir"]
    obj.rotation_euler = (math.acos(max(min(d[1], 1.0), -1.0)),
                          0.0, math.atan2(d[0], d[2]))

    # water
    water = sc.get("water", {})
    if water.get("enabled"):
        bpy.ops.mesh.primitive_plane_add(size=1.0,
                                         location=(0.5, 0.5, water["level"]))
        wobj = bpy.context.active_object
        wmat = bpy.data.materials.new("TerraForgeWater")
        wmat.use_nodes = True
        wb = wmat.node_tree.nodes["Principled BSDF"]
        wb.inputs["Base Color"].default_value = (*water["deep"], 1.0)
        wb.inputs["Roughness"].default_value = float(water.get("roughness", 0.02))
        wobj.data.materials.append(wmat)

    # camera
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.sensor_fit = "VERTICAL"
    cam_data.angle_y = math.radians(float(sc["camera"]["fov"]))
    cam = bpy.data.objects.new("Camera", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    eye = sc["camera"]["eye"]
    tgt = sc["camera"]["target"]
    cam.location = (eye[0], eye[2], eye[1])   # Blender is Z-up
    terrain.rotation_euler = (math.radians(90), 0, 0)
    direction = (tgt[0] - eye[0], tgt[2] - eye[2], tgt[1] - eye[1])
    cam.rotation_euler = (
        math.atan2(math.hypot(direction[0], direction[1]), -direction[2]),
        0.0,
        math.atan2(direction[1], direction[0]) + math.radians(90),
    )

    bpy.ops.render.render(write_still=True)
    print("wrote", sc["output"])


main()
