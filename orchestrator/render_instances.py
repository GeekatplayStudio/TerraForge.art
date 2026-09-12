"""Geekatplay TerraForge — a scattered copy's placement, for the offline engines.

scene.json writes each copy of a scattered mesh as a row of numbers: x, y, z,
scale, yaw (radians), then - in files written since the copies learned to
lean - the per-axis scale, the lean into the ground and the ground's normal.
Every engine used to unpack exactly five, and threw on every population.

The same placement is here twice: as Mitsuba transforms, and as plain
column-major 4x4 lists for the engines that take a matrix of numbers
(Cycles, LuxCore). Both compose it the way the viewport does (INSTANCE_FN in
studio/shaders_scene.cpp), so the three pictures stand their copies alike.
"""
from __future__ import annotations

import math


def instance_row(row) -> tuple:
    """(x, y, z, scale, yaw, axes, lean, ground) from one row; older files
    carry only the first five, and the rest take their neutral values."""
    x, y, z, s, yaw = (float(v) for v in row[:5])
    axes = tuple(float(v) for v in row[5:8]) if len(row) >= 8 else (1.0, 1.0, 1.0)
    lean = float(row[8]) if len(row) >= 9 else 0.0
    ground = tuple(float(v) for v in row[9:12]) if len(row) >= 12 else (0.0, 1.0, 0.0)
    return x, y, z, s, yaw, axes, lean, ground


def _lean_rotation(lean: float, ground) -> tuple[list[float], float] | None:
    """The axis and angle (degrees) a copy leans by toward the ground's
    normal, or None when it stands straight."""
    if lean <= 0.0:
        return None
    up = [ground[0] * lean, 1.0 + (ground[1] - 1.0) * lean, ground[2] * lean]
    n = math.sqrt(sum(c * c for c in up)) or 1.0
    up = [c / n for c in up]
    axis = [up[2], 0.0, -up[0]]  # (0, 1, 0) x up
    sn = math.sqrt(axis[0] ** 2 + axis[2] ** 2)
    if sn <= 1e-5:
        return None
    return [axis[0] / sn, 0.0, axis[2] / sn], math.degrees(math.atan2(sn, up[1]))


def instance_local(mi, row):
    """The copy's own Mitsuba transform before the model's, as the viewport
    builds it: turned about y, scaled per axis, then leaned toward the
    ground's normal."""
    _, _, _, s, yaw, axes, lean, ground = instance_row(row)
    t = (mi.ScalarTransform4f().scale([s * axes[0], s * axes[1], s * axes[2]])
         @ mi.ScalarTransform4f().rotate([0, 1, 0], -math.degrees(yaw)))
    tilt = _lean_rotation(lean, ground)
    if tilt:
        t = mi.ScalarTransform4f().rotate(tilt[0], tilt[1]) @ t
    return t


# ------------------------------------------------ column-major 4x4 matrices
def mat_identity() -> list[float]:
    return [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


def mat_mul(a: list[float], b: list[float]) -> list[float]:
    """a * b: b applied first."""
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return out


def mat_translate(t) -> list[float]:
    m = mat_identity()
    m[12], m[13], m[14] = float(t[0]), float(t[1]), float(t[2])
    return m


def mat_scale(s) -> list[float]:
    m = mat_identity()
    m[0], m[5], m[10] = float(s[0]), float(s[1]), float(s[2])
    return m


def mat_rotate(axis, degrees: float) -> list[float]:
    """Right-handed turn about a unit axis, as Mitsuba's rotate()."""
    x, y, z = (float(v) for v in axis)
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    k = 1.0 - c
    rows = [[c + x * x * k, x * y * k - z * s, x * z * k + y * s],
            [y * x * k + z * s, c + y * y * k, y * z * k - x * s],
            [z * x * k - y * s, z * y * k + x * s, c + z * z * k]]
    m = mat_identity()
    for r in range(3):
        for col in range(3):
            m[col * 4 + r] = rows[r][col]
    return m


def instance_local_matrix(row) -> list[float]:
    """instance_local as a column-major list."""
    _, _, _, s, yaw, axes, lean, ground = instance_row(row)
    t = mat_mul(mat_scale([s * axes[0], s * axes[1], s * axes[2]]),
                mat_rotate([0, 1, 0], -math.degrees(yaw)))
    tilt = _lean_rotation(lean, ground)
    if tilt:
        t = mat_mul(mat_rotate(tilt[0], tilt[1]), t)
    return t


def instance_matrices(m: dict) -> list[list[float]]:
    """Every world matrix a scene mesh is drawn with: its model matrix alone,
    or one per scattered copy - the copy's translation swapped in for the
    model's, over the model, over the copy's own turn, size and lean."""
    model = [float(v) for v in m["model"]]
    insts = m.get("instances")
    if not insts:
        return [model]
    base = (model[12], model[13], model[14])
    out = []
    for row in insts:
        x, y, z = (float(v) for v in row[:3])
        t = mat_translate([x - base[0], y - base[1], z - base[2]])
        out.append(mat_mul(t, mat_mul(model, instance_local_matrix(row))))
    return out


def mesh_parts(m: dict) -> list[dict]:
    """A scene mesh's parts, each {obj, color, texture?, alpha?}: its own
    materials when the export wrote them, else the whole mesh in one colour."""
    parts = [p for p in m.get("parts") or [] if p.get("obj")]
    if parts:
        return parts
    return [{"obj": m["obj"], "color": m.get("color", [0.6, 0.6, 0.6])}]
