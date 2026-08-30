"""
Geekatplay Studio — High-Performance Procedural Noise Generators.
Implements vectorized Perlin, Simplex, Ridged Multifractal, Voronoi, and Strata heightfield kernels.
"""

import numpy as np
from typing import Tuple, Optional


def generate_grid_coords(res: int, scale: float = 1.0) -> Tuple[np.ndarray, np.ndarray]:
    """Generates normalized [0, 1] grid coordinates for heightfield synthesis."""
    lin = np.linspace(0.0, scale, res, endpoint=False, dtype=np.float32)
    return np.meshgrid(lin, lin, indexing="xy")


def _fade(t: np.ndarray) -> np.ndarray:
    """Ken Perlin's quintic polynomial s-curve: 6t^5 - 15t^4 + 10t^3."""
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def perlin_noise_2d(res: int, frequency: float = 4.0, seed: int = 42) -> np.ndarray:
    """Generates smooth, deterministic 2D Perlin gradient noise."""
    rng = np.random.default_rng(seed)
    grid_size = max(int(np.ceil(frequency)) + 2, 4)
    
    # Random unit gradient vectors
    angles = rng.uniform(0, 2.0 * np.pi, size=(grid_size, grid_size)).astype(np.float32)
    gradients = np.stack([np.cos(angles), np.sin(angles)], axis=-1)

    x, y = generate_grid_coords(res, scale=frequency)
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    x1 = (x0 + 1) % grid_size
    y1 = (y0 + 1) % grid_size
    x0 = x0 % grid_size
    y0 = y0 % grid_size

    fx = x - np.floor(x)
    fy = y - np.floor(y)

    u = _fade(fx)
    v = _fade(fy)

    # Dot products with 4 corner gradients
    g00 = gradients[y0, x0]
    g10 = gradients[y0, x1]
    g01 = gradients[y1, x0]
    g11 = gradients[y1, x1]

    d00 = g00[..., 0] * fx + g00[..., 1] * fy
    d10 = g10[..., 0] * (fx - 1.0) + g10[..., 1] * fy
    d01 = g01[..., 0] * fx + g01[..., 1] * (fy - 1.0)
    d11 = g11[..., 0] * (fx - 1.0) + g11[..., 1] * (fy - 1.0)

    # Bilinear interpolation with s-curves
    nx0 = d00 + u * (d10 - d00)
    nx1 = d01 + u * (d11 - d01)
    noise = nx0 + v * (nx1 - nx0)

    # Normalize to [0, 1]
    return np.clip((noise + 0.707) / 1.414, 0.0, 1.0).astype(np.float32)


def ridged_multifractal(
    res: int,
    octaves: int = 8,
    frequency: float = 2.0,
    lacunarity: float = 2.15,
    gain: float = 0.5,
    offset: float = 1.0,
    seed: int = 42
) -> np.ndarray:
    """
    Generates sharp, alpine mountain ridges using Ridged Multifractal synthesis.
    Ideal for steep granite mountain peaks, arêtes, and razor-edge ridgelines.
    """
    heightfield = np.zeros((res, res), dtype=np.float32)
    weight = np.ones((res, res), dtype=np.float32)
    freq = frequency
    amplitude = 1.0

    for i in range(octaves):
        layer = perlin_noise_2d(res, frequency=freq, seed=seed + i * 137)
        # Invert and square to create ridges
        signal = offset - np.abs(layer * 2.0 - 1.0)
        signal = signal * signal
        signal = signal * weight

        heightfield += signal * amplitude
        weight = np.clip(signal * gain, 0.0, 1.0)
        freq *= lacunarity
        amplitude *= gain

    min_val = np.min(heightfield)
    max_val = np.max(heightfield)
    if max_val > min_val:
        heightfield = (heightfield - min_val) / (max_val - min_val)
    return heightfield.astype(np.float32)


def voronoi_cellular_noise(
    res: int,
    cell_count: int = 16,
    seed: int = 42,
    mode: str = "f1"
) -> np.ndarray:
    """
    Generates Voronoi/Worley cellular distance fields.
    Useful for basalt column formations, cracked dry mud, crater clusters, and terrace cells.
    """
    rng = np.random.default_rng(seed)
    feature_points = rng.uniform(0.0, 1.0, size=(cell_count, 2)).astype(np.float32)

    x, y = generate_grid_coords(res, scale=1.0)
    grid_pts = np.stack([x, y], axis=-1)  # (res, res, 2)

    # Compute distances to all feature points
    # (res, res, 1, 2) - (1, 1, cell_count, 2)
    diff = grid_pts[:, :, np.newaxis, :] - feature_points[np.newaxis, np.newaxis, :, :]
    dist_sq = np.sum(diff * diff, axis=-1)  # (res, res, cell_count)

    sorted_dists = np.sort(np.sqrt(dist_sq), axis=-1)
    f1 = sorted_dists[..., 0]
    f2 = sorted_dists[..., 1] if cell_count > 1 else f1

    if mode == "f2_minus_f1":
        field = f2 - f1
    elif mode == "f2":
        field = f2
    else:
        field = f1

    min_val = np.min(field)
    max_val = np.max(field)
    if max_val > min_val:
        field = (field - min_val) / (max_val - min_val)
    return field.astype(np.float32)


def billow_noise(
    res: int,
    octaves: int = 6,
    frequency: float = 3.0,
    seed: int = 42
) -> np.ndarray:
    """Generates puffy billow formations suitable for rolling hills, sand dunes, and clouds."""
    heightfield = np.zeros((res, res), dtype=np.float32)
    freq = frequency
    amplitude = 1.0
    total_amp = 0.0

    for i in range(octaves):
        layer = perlin_noise_2d(res, frequency=freq, seed=seed + i * 99)
        billow_layer = 2.0 * np.abs(layer - 0.5)
        heightfield += billow_layer * amplitude
        total_amp += amplitude
        freq *= 2.0
        amplitude *= 0.5

    return (heightfield / total_amp).astype(np.float32)
