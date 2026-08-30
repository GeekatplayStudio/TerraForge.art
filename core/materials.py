"""
Geekatplay Studio — PBR Materials, Normal Baking & Biome Splatmaps.
Computes slope, curvature, flow masks, triplanar blending, and high-fidelity PBR splatmaps.
"""

import numpy as np
from typing import Dict, Tuple, Optional


def compute_slope_map(heightfield: np.ndarray) -> np.ndarray:
    """Computes terrain surface slope angles in normalized [0, 1] range (0 to 90 degrees)."""
    h = heightfield.astype(np.float32)
    dy, dx = np.gradient(h)
    slope = np.arctan(np.sqrt(dx * dx + dy * dy)) / (np.pi * 0.5)
    return np.clip(slope, 0.0, 1.0).astype(np.float32)


def compute_curvature_map(heightfield: np.ndarray) -> np.ndarray:
    """Computes Laplacian surface curvature (ridge vs valley / cavity)."""
    h = heightfield.astype(np.float32)
    laplacian = (
        np.pad(h, ((1, 0), (0, 0)), mode="edge")[:-1, :] +
        np.pad(h, ((0, 1), (0, 0)), mode="edge")[1:, :] +
        np.pad(h, ((0, 0), (1, 0)), mode="edge")[:, :-1] +
        np.pad(h, ((0, 0), (0, 1)), mode="edge")[:, 1:] -
        4.0 * h
    )
    # Normalize centered at 0.5
    norm_curv = np.clip(laplacian * 10.0 + 0.5, 0.0, 1.0)
    return norm_curv.astype(np.float32)


def compute_normal_map(heightfield: np.ndarray, strength: float = 8.0) -> np.ndarray:
    """
    Computes tangent-space normal map RGB [0, 1] from heightfield gradients.
    """
    h = heightfield.astype(np.float32)
    dy, dx = np.gradient(h)

    # Tangent space normal vectors: (-dx * strength, -dy * strength, 1.0)
    nx = -dx * strength
    ny = -dy * strength
    nz = np.ones_like(h)

    length = np.sqrt(nx * nx + ny * ny + nz * nz) + 1e-6
    nx /= length
    ny /= length
    nz /= length

    # Remap from [-1, 1] to [0, 1] RGB
    r = (nx + 1.0) * 0.5
    g = (ny + 1.0) * 0.5
    b = (nz + 1.0) * 0.5

    return np.stack([r, g, b], axis=-1).astype(np.float32)


def generate_pbr_splatmap(
    heightfield: np.ndarray,
    flow_map: Optional[np.ndarray] = None,
    snow_line: float = 0.72,
    cliff_slope_threshold: float = 0.45,
) -> Dict[str, np.ndarray]:
    """
    Generates multi-channel PBR biome splatmaps:
    - Channel 0 (Red): Steep Rock & Granite Cliffs
    - Channel 1 (Green): Talus Scree & Valley Gravel
    - Channel 2 (Blue): Lowland Grass & Humus Soil
    - Channel 3 (Alpha): High-altitude Alpine Snow & Ice
    - Wetness / River Flow channel
    """
    h = heightfield.astype(np.float32)
    slope = compute_slope_map(h)
    curv = compute_curvature_map(h)
    flow = flow_map if flow_map is not None else np.zeros_like(h)

    # 1. Rock Mask (Steep slopes)
    rock = np.clip((slope - cliff_slope_threshold * 0.7) / (cliff_slope_threshold * 0.6 + 1e-6), 0.0, 1.0)

    # 2. Snow Mask (High elevation, gathers in low slope depressions)
    snow = np.clip((h - snow_line) / 0.18, 0.0, 1.0) * (1.0 - rock * 0.6)

    # 3. Wetness / River Mask
    wetness = np.clip(flow * 1.5, 0.0, 1.0)

    # 4. Soil / Vegetation Mask (Low slope, below snow line)
    soil = (1.0 - rock) * (1.0 - snow) * np.clip(1.0 - h * 1.2, 0.0, 1.0)

    # 5. Scree / Gravel Mask (Remaining transition areas)
    gravel = np.clip(1.0 - (rock + snow + soil), 0.0, 1.0)

    splat_rgba = np.stack([rock, gravel, soil, snow], axis=-1)

    return {
        "splatmap_rgba": splat_rgba.astype(np.float32),
        "rock_mask": rock.astype(np.float32),
        "snow_mask": snow.astype(np.float32),
        "soil_mask": soil.astype(np.float32),
        "gravel_mask": gravel.astype(np.float32),
        "wetness_mask": wetness.astype(np.float32),
    }
