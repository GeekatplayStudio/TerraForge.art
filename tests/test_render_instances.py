"""The offline engines place a scattered copy the way Mitsuba does.

render_instances.py composes a copy's placement twice - as Mitsuba transforms
and as plain column-major lists for Cycles and LuxCore. These pin the plain
form against the Mitsuba one (when Mitsuba is installed) and against hand
arithmetic, and the part list's fallback for meshes exported without parts.
"""
import math

import pytest

from orchestrator.render_instances import (
    instance_matrices,
    instance_row,
    mat_mul,
    mat_rotate,
    mesh_parts,
)


def _apply(m, p):
    return [sum(m[c * 4 + r] * v for c, v in enumerate((*p, 1.0))) for r in range(3)]


def test_rows_of_five_take_neutral_extras():
    x, y, z, s, yaw, axes, lean, ground = instance_row([1, 2, 3, 0.5, 0.25])
    assert (x, y, z, s, yaw) == (1.0, 2.0, 3.0, 0.5, 0.25)
    assert axes == (1.0, 1.0, 1.0) and lean == 0.0 and ground == (0.0, 1.0, 0.0)


def test_rotate_is_right_handed():
    # a quarter turn about +y takes +x to -z
    p = _apply(mat_rotate([0, 1, 0], 90.0), (1.0, 0.0, 0.0))
    assert p == pytest.approx([0.0, 0.0, -1.0], abs=1e-9)
    # and composition applies the right-hand factor first
    m = mat_mul(mat_rotate([0, 0, 1], 90.0), mat_rotate([0, 1, 0], 90.0))
    assert _apply(m, (1.0, 0.0, 0.0)) == pytest.approx([0.0, 0.0, -1.0], abs=1e-9)


def test_copy_swaps_its_translation_into_the_model():
    model = [2.0, 0, 0, 0, 0, 2.0, 0, 0, 0, 0, 2.0, 0, 5.0, 6.0, 7.0, 1.0]
    mesh = {"model": model, "instances": [[1.0, 0.5, 2.0, 1.5, 0.0]]}
    (t,) = instance_matrices(mesh)
    # the model's size times the copy's, at the copy's own position
    assert _apply(t, (0.0, 0.0, 0.0)) == pytest.approx([1.0, 0.5, 2.0])
    assert _apply(t, (1.0, 0.0, 0.0)) == pytest.approx([4.0, 0.5, 2.0])


def test_no_instances_is_the_model_itself():
    model = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0.25, 0.5, 0.75, 1.0]
    assert instance_matrices({"model": model}) == [model]


def test_plain_matrices_match_mitsuba():
    mi = pytest.importorskip("mitsuba")
    mi.set_variant("scalar_rgb")
    from orchestrator.render_instances import instance_local

    model = [0.7, 0.1, 0.0, 0, -0.1, 0.7, 0.0, 0, 0.0, 0.0, 0.8, 0, 0.3, 0.1, 0.6, 1]
    row = [0.4, 0.05, 0.9, 1.3, 0.8, 1.1, 0.9, 1.2, 0.6, 0.2, 0.95, 0.1]
    (t,) = instance_matrices({"model": model, "instances": [row]})
    base = mi.ScalarTransform4f([[model[c * 4 + r] for c in range(4)] for r in range(4)])
    ref = (mi.ScalarTransform4f().translate([row[0] - model[12], row[1] - model[13],
                                             row[2] - model[14]])
           @ base @ instance_local(mi, row))
    for p in ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.2, -0.4, 1.0)):
        q = ref @ mi.ScalarPoint3f(*p)
        assert _apply(t, p) == pytest.approx([q[0], q[1], q[2]], abs=1e-5)


def test_parts_fall_back_to_the_whole_mesh():
    whole = {"obj": "mesh_0.obj", "color": [0.2, 0.3, 0.4]}
    assert mesh_parts(whole) == [{"obj": "mesh_0.obj", "color": [0.2, 0.3, 0.4]}]
    parted = dict(whole, parts=[{"obj": "mesh_0_part_0.obj", "color": [1, 0, 0]},
                                {"obj": "", "color": [0, 1, 0]}])
    assert [p["obj"] for p in mesh_parts(parted)] == ["mesh_0_part_0.obj"]
    assert math.isfinite(sum(parted["color"]))
