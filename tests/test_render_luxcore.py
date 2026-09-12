"""The LuxCore scene, without pyluxcore.

render_luxcore.build_props turns scene.json into LuxCore's property lines. It
had never run, and four of its conventions were each measured wrong once:
the frame (LuxCore is z-up), the field of view (across the longer side), the
sun (direction is the way the light travels, colour a radiance over the disc)
and the sky panorama's turn and mirror. These pin each one.
"""
import math

import pytest

from orchestrator import render_luxcore as rl


def _scene(tmp_path, **over):
    ground = tmp_path / "ground.obj"
    ground.write_text("v 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n")
    sc = {"width": 320, "height": 180, "spp": 4, "exposure": 1.0,
          "camera": {"eye": [0.0, 1.0, 2.0], "target": [0.0, 0.0, 0.0], "fov": 40.0},
          "sun": {"dir": [0.0, 1.0, 0.0], "intensity": 2.0, "color": [1.0, 1.0, 1.0]},
          "sky": {"ambient": 1.0, "zenith": [0.2, 0.3, 0.5], "horizon": [0.6, 0.6, 0.7]},
          "material": {"roughness": 0.8, "metallic": 0.0, "specular": 0.3},
          "terrain_obj": str(ground), "meshes": [], "lights": [], "water": {"enabled": False}}
    sc.update(over)
    return sc


def _props(lines):
    out = {}
    for line in lines:
        k, _, v = line.partition(" = ")
        out[k] = v
    return out


def _floats(s):
    return [float(t) for t in s.split()]


def test_our_y_up_goes_in_as_luxcores_z_up():
    assert rl.to_lux((1.0, 2.0, 3.0)) == (1.0, -3.0, 2.0)
    # CONV takes our +y to +z and our +z to -y, as a turn and not a mirror
    m = rl.CONV
    y_axis = [m[4], m[5], m[6]]
    z_axis = [m[8], m[9], m[10]]
    assert y_axis == [0.0, 0.0, 1.0] and z_axis == [0.0, -1.0, 0.0]


def test_the_field_of_view_spans_the_longer_side(tmp_path):
    p = _props(rl.build_props(_scene(tmp_path))[0])
    wide = 2.0 * math.degrees(math.atan(math.tan(math.radians(20.0)) * 320.0 / 180.0))
    assert float(p["scene.camera.fieldofview"]) == pytest.approx(wide)
    tall = _props(rl.build_props(_scene(tmp_path, width=180, height=320))[0])
    assert float(tall["scene.camera.fieldofview"]) == pytest.approx(40.0)
    assert p["scene.camera.up"] == "0 0 1"


def test_the_sun_shines_along_its_travel_with_the_irradiance_asked_for(tmp_path):
    p = _props(rl.build_props(_scene(tmp_path))[0])
    # the sun is straight up (our +y, LuxCore's +z): its light travels down -z
    assert _floats(p["scene.lights.sun.direction"]) == pytest.approx([0.0, 0.0, -1.0], abs=1e-9)
    theta = float(p["scene.lights.sun.theta"])
    radiance = _floats(p["scene.lights.sun.color"])[0]
    # radiance over the disc x pi sin^2(theta) is the irradiance: the intensity
    assert radiance * math.pi * math.sin(math.radians(theta)) ** 2 == pytest.approx(2.0)


def test_the_sky_panorama_is_turned_and_mirrored(tmp_path):
    hdr = tmp_path / "sky.hdr"
    hdr.write_bytes(b"#?RADIANCE\n")
    p = _props(rl.build_props(_scene(tmp_path, sky_hdr=str(hdr)))[0])
    assert p["scene.lights.sky.type"] == "infinite"
    m = _floats(p["scene.lights.sky.transformation"])
    assert m == pytest.approx(rl.env_transformation())
    # a mirror: the determinant of the upper 3x3 is -1
    a = [[m[c * 4 + r] for c in range(3)] for r in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
           - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    assert det == pytest.approx(-1.0)
    # no panorama: a constant sky of the ambient colour
    q = _props(rl.build_props(_scene(tmp_path))[0])
    assert q["scene.lights.sky.type"] == "constantinfinite"


def test_every_copy_of_a_part_is_an_object_with_its_material(tmp_path):
    part = tmp_path / "leaf.obj"
    part.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
    alpha = tmp_path / "leaf_alpha.png"
    alpha.write_bytes(b"png")
    mesh = {"obj": str(part), "parts": [{"obj": str(part), "color": [0.2, 0.5, 0.1], "alpha": str(alpha)}],
            "model": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.5, 0.1, 0.5, 1],
            "position": [0.5, 0.1, 0.5], "scale": 1.0, "scl": [1, 1, 1], "ypr": [0, 0, 0],
            "instances": [[0.2, 0.0, 0.2, 1.0, 0.0], [0.8, 0.0, 0.8, 1.0, 0.0]]}
    lines, meshes = rl.build_props(_scene(tmp_path, meshes=[mesh]))
    p = _props(lines)
    assert ("prop0_0_mesh", str(part)) in meshes
    assert p["scene.objects.prop0_0_0.material"] == "propmat0_0"
    assert p["scene.objects.prop0_0_1.shape"] == "prop0_0_mesh"
    assert p["scene.materials.propmat0_0.transparency"] == "propmat0_0_alpha"
