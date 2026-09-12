"""Geekatplay TerraForge — Blender Cycles backend.

Run by render_engines.py as:
    blender --background --factory-startup --python render_cycles.py -- scene.json
Builds the scene from the exported description: the placed terrain and the
ground out to the horizon with their pictures, the viewport's sky panorama as
the world, the sun, the sea as a mesh, scene meshes by their parts with every
scattered copy, point lights and the camera.

One frame for everything. Blender's OBJ importer turns a file's y-up into its
own z-up, (x, y, z) -> (x, -z, y), by giving the object a quarter turn about
x. Everything this script places itself goes through the same turn
(`to_blender`, `CONV`): it used to swap y and z instead - a mirror, not a
turn - so the camera looked at the terrain's reflection, and meshes lost the
importer's turn and lay on their sides.
"""
from __future__ import annotations

import json
import math
import os
import sys

import bpy  # provided by Blender
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_instances import instance_matrices, mesh_parts  # noqa: E402

# our y-up axes into Blender's z-up ones, the OBJ importer's own turn
CONV = Matrix(((1.0, 0.0, 0.0, 0.0),
               (0.0, 0.0, -1.0, 0.0),
               (0.0, 1.0, 0.0, 0.0),
               (0.0, 0.0, 0.0, 1.0)))


def to_blender(v) -> Vector:
    return Vector((float(v[0]), -float(v[2]), float(v[1])))


def scene_path_from_argv() -> str:
    argv = sys.argv
    if "--" in argv:
        return argv[argv.index("--") + 1]
    raise SystemExit("no scene json passed after --")


def import_obj(path: str):
    """The OBJ as one Blender object, still in the importer's turn."""
    bpy.ops.wm.obj_import(filepath=path)
    return bpy.context.selected_objects[0]


def material(name: str, color=None, texture: str | None = None,
             alpha: str | None = None, roughness: float = 0.6,
             metallic: float = 0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = float(roughness)
    bsdf.inputs["Metallic"].default_value = float(metallic)
    if color is not None:
        bsdf.inputs["Base Color"].default_value = (*[float(c) for c in color], 1.0)
    if texture and os.path.isfile(texture):
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(texture)
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if alpha and os.path.isfile(alpha):
        # the viewport cuts a leaf card where its picture is clear
        mask = nodes.new("ShaderNodeTexImage")
        mask.image = bpy.data.images.load(alpha)
        mask.image.colorspace_settings.name = "Non-Color"
        links.new(mask.outputs["Color"], bsdf.inputs["Alpha"])
    return mat


def look_rotation(direction: Vector):
    """The rotation that points an object's -z along `direction`, its y up."""
    return direction.to_track_quat("-Z", "Y").to_euler()


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

    m = sc["material"]
    # the placed terrain, and the ground beyond it out to the horizon in its
    # own palette picture (its UVs are the surround grid's, not the tile's)
    terrain = import_obj(sc["terrain_obj"])
    terrain.data.materials.append(material(
        "TerraForgeTerrain", texture=sc.get("albedo"),
        roughness=m["roughness"], metallic=m["metallic"]))
    if sc.get("surround_obj") and os.path.isfile(sc["surround_obj"]):
        surround = import_obj(sc["surround_obj"])
        surround.data.materials.append(material(
            "TerraForgeSurround", texture=sc.get("surround_albedo"),
            roughness=m["roughness"], metallic=m["metallic"]))

    # environment: the viewport sky + clouds panorama. Its longitude starts
    # three quarters of a turn from Blender's (u = atan2(x, -z)/2pi + 0.5 on
    # our axes), so the mapping turns it back and the clouds and the sun's
    # glow stand where the viewport drew them. It was turned a quarter, which
    # matched Mitsuba - and Mitsuba was itself half a turn out: both engines
    # rendered the sky behind the camera (measured with a marked panorama).
    world = bpy.data.worlds.new("TerraForgeWorld")
    scene.world = world
    world.use_nodes = True
    wn, wl = world.node_tree.nodes, world.node_tree.links
    bg = wn["Background"]
    if sc.get("sky_hdr") and os.path.isfile(sc["sky_hdr"]):
        env = wn.new("ShaderNodeTexEnvironment")
        env.image = bpy.data.images.load(sc["sky_hdr"])
        coord = wn.new("ShaderNodeTexCoord")
        mapping = wn.new("ShaderNodeMapping")
        mapping.inputs["Rotation"].default_value[2] = math.radians(270.0)
        wl.new(coord.outputs["Generated"], mapping.inputs["Vector"])
        wl.new(mapping.outputs["Vector"], env.inputs["Vector"])
        wl.new(env.outputs["Color"], bg.inputs["Color"])
    else:
        sky = sc["sky"]
        bg.inputs["Color"].default_value = (
            *[0.5 * (a + b) * sky["ambient"]
              for a, b in zip(sky["zenith"], sky["horizon"])], 1.0)

    # sun: a sun lamp shines down its -z, so its +z is the way to the sun
    sun = sc["sun"]
    light = bpy.data.lights.new("Sun", type="SUN")
    light.energy = float(sun["intensity"])
    light.color = tuple(sun["color"])
    obj = bpy.data.objects.new("Sun", light)
    scene.collection.objects.link(obj)
    obj.rotation_euler = to_blender(sun["dir"]).to_track_quat("Z", "Y").to_euler()

    # scene meshes by their parts, each part its own material. A scattered
    # population is a collection drawn by one instancing empty per copy.
    inv_conv = CONV.inverted()
    for i, mesh in enumerate(sc.get("meshes", [])):
        objs = []
        for k, p in enumerate(mesh_parts(mesh)):
            ob = import_obj(p["obj"])
            ob.data.materials.append(material(
                f"TerraForgeMesh{i}_{k}", color=p.get("color"),
                texture=p.get("texture"), alpha=p.get("alpha"), roughness=0.7))
            objs.append(ob)
        mats = instance_matrices(mesh)
        placements = [Matrix([[t[c * 4 + r] for c in range(4)] for r in range(4)])
                      for t in mats]
        if not mesh.get("instances"):
            for ob in objs:
                ob.matrix_world = CONV @ placements[0]
            continue
        coll = bpy.data.collections.new(f"TerraForgeMesh{i}")
        for ob in objs:
            for c in list(ob.users_collection):
                c.objects.unlink(ob)
            coll.objects.link(ob)
        for n, place in enumerate(placements):
            empty = bpy.data.objects.new(f"copy{i}_{n}", None)
            empty.instance_type = "COLLECTION"
            empty.instance_collection = coll
            # the parts keep the importer's turn inside the collection
            empty.matrix_world = CONV @ place @ inv_conv
            scene.collection.objects.link(empty)

    # scene point lights
    for L in sc.get("lights", []):
        is_spot = L.get("type") == "spot"
        pl = bpy.data.lights.new("L", type="SPOT" if is_spot else "POINT")
        pl.energy = float(L["intensity"]) * 50.0
        pl.color = tuple(L["color"])
        if is_spot:
            pl.spot_size = math.radians(float(L.get("cone_deg", 40.0)))
        ob = bpy.data.objects.new("L", pl)
        scene.collection.objects.link(ob)
        ob.location = to_blender(L["position"])
        if is_spot:
            ob.rotation_euler = look_rotation(to_blender(L.get("direction", [0, -1, 0])))

    # the sea: the exported mesh on the world's own curve, as wide as the
    # ground it lies in; a plane only when the export carried no mesh
    water = sc.get("water", {})
    if water.get("enabled"):
        if water.get("mesh") and os.path.isfile(water["mesh"]):
            wobj = import_obj(water["mesh"])
        else:
            half = max(float(water.get("extent", 0.5)), 0.5)
            bpy.ops.mesh.primitive_plane_add(
                size=2.0 * half, location=to_blender((0.5, water["level"], 0.5)))
            wobj = bpy.context.active_object
        wobj.data.materials.append(material(
            "TerraForgeWater", color=water["deep"],
            roughness=float(water.get("roughness", 0.02))))

    # camera
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.sensor_fit = "VERTICAL"
    cam_data.angle_y = math.radians(float(sc["camera"]["fov"]))
    # A unit is a whole tile: Blender's default clip start of 0.1 hides the
    # first half kilometre in front of the lens (the black waterline edge).
    cam_data.clip_start = 1e-5
    cam_data.clip_end = 1e7
    cam = bpy.data.objects.new("Camera", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    eye, tgt = sc["camera"]["eye"], sc["camera"]["target"]
    cam.location = to_blender(eye)
    cam.rotation_euler = look_rotation(to_blender(tgt) - to_blender(eye))

    bpy.ops.render.render(write_still=True)
    print("wrote", sc["output"])


main()
