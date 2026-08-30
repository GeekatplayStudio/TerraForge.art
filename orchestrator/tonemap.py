"""Geekatplay TerraForge — shared tone mapping and fog matching.

The viewport applies ACES + exposure + sRGB gamma, and an analytic height
fog. Offline renders run the identical math here so the two images match.
"""
from __future__ import annotations

import numpy as np


def to_numpy(img) -> np.ndarray:
    """Mitsuba TensorXf / bitmap -> float32 numpy array."""
    arr = np.array(img)
    return arr.astype(np.float32)


def aces(x: np.ndarray) -> np.ndarray:
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14
    return np.clip((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)


def apply_height_fog(rgb: np.ndarray, depth, eye, target, fov_deg: float,
                     width: int, height: int, fog: dict, sun: dict) -> np.ndarray:
    """Same exponential height fog the terrain shader uses.

    Falls back to the unmodified image when no depth pass was available.
    """
    if depth is None:
        return rgb
    eye = np.asarray(eye, dtype=np.float32)
    target = np.asarray(target, dtype=np.float32)
    fwd = target - eye
    fwd /= max(np.linalg.norm(fwd), 1e-9)
    up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    right = np.cross(fwd, up)
    right /= max(np.linalg.norm(right), 1e-9)
    up = np.cross(right, fwd)

    aspect = width / float(height)
    ty = np.tan(np.radians(fov_deg) * 0.5)
    ys, xs = np.mgrid[0:height, 0:width].astype(np.float32)
    ndc_x = (xs + 0.5) / width * 2.0 - 1.0
    ndc_y = 1.0 - (ys + 0.5) / height * 2.0
    dirs = (fwd[None, None, :]
            + right[None, None, :] * (ndc_x * ty * aspect)[..., None]
            + up[None, None, :] * (ndc_y * ty)[..., None])
    dirs /= np.maximum(np.linalg.norm(dirs, axis=2, keepdims=True), 1e-9)

    dist = np.asarray(depth, dtype=np.float32)
    if dist.ndim == 3:
        dist = dist[:, :, 0]
    sky = dist <= 0
    dist = np.where(sky, 1e3, dist)
    world_y = eye[1] + dirs[:, :, 1] * dist

    level = float(fog.get("level", 0.0))
    falloff = float(fog.get("falloff", 6.0))
    ftype = int(fog.get("type", 0))
    density = float(fog.get("density", 0.0)) * (
        0.35 if ftype == 1 else (1.0 if ftype == 2 else 1.8))

    fy0 = max(eye[1] - level, 0.0)
    fy1 = np.maximum(world_y - level, 0.0)
    dY = fy1 - fy0
    a = np.exp(-falloff * fy0)
    b = np.exp(-falloff * fy1)
    with np.errstate(divide="ignore", invalid="ignore"):
        od = np.abs(dist * (a - b) / (falloff * dY))
    od = np.where(np.abs(falloff * dY) < 1e-3, dist * a, od)
    f = np.clip(1.0 - np.exp(-od * density), 0.0, 1.0)[..., None]

    sun_dir = np.asarray(sun["dir"], dtype=np.float32)
    sunward = np.clip(np.sum(dirs * sun_dir[None, None, :], axis=2), 0, 1) ** 6
    fog_color = np.asarray(fog.get("color", [0.55, 0.63, 0.75]), dtype=np.float32)
    sun_color = np.asarray(sun.get("color", [1, 1, 1]), dtype=np.float32)
    scatter = float(fog.get("scatter", 0.5))
    fogc = fog_color[None, None, :] * (
        1.0 + (sun_color[None, None, :] * 1.6 - 1.0) * (sunward * scatter)[..., None])
    if ftype == 3:
        fogc = fogc * np.array([0.85, 0.75, 0.6], dtype=np.float32)

    absorb = np.asarray(fog.get("absorb", [1, 1, 1]), dtype=np.float32)
    rgb3 = rgb[:, :, :3]
    rgb3 = rgb3 * (1.0 + (absorb[None, None, :] - 1.0) * f)
    return rgb3 * (1.0 - f) + fogc * f


def save_png(rgb: np.ndarray, path: str, exposure: float = 1.0) -> None:
    img = np.asarray(rgb, dtype=np.float32)[:, :, :3]
    img = aces(img * float(exposure))
    img = np.clip(img, 0.0, 1.0) ** (1.0 / 2.2)
    out = (img * 255.0 + 0.5).astype(np.uint8)
    try:
        from PIL import Image
        Image.fromarray(out).save(path)
        return
    except Exception:
        pass
    _write_png(out, path)


def _write_png(rgb8: np.ndarray, path: str) -> None:
    """Minimal PNG writer so the pipeline works without Pillow."""
    import struct
    import zlib
    h, w, _ = rgb8.shape
    raw = b"".join(b"\x00" + rgb8[y].tobytes() for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
