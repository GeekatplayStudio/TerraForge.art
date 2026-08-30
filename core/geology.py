"""
Geekatplay Studio — Structural Geology & Tectonic Solvers.
Simulates geological rock strata, differential lithology hardness, tectonic faults, and plateaus.
"""

import numpy as np
from typing import Tuple


def geological_strata(
    heightfield: np.ndarray,
    layers_count: int = 12,
    hardness_variance: float = 0.6,
    folding_angle_deg: float = 15.0,
    seed: int = 42
) -> np.ndarray:
    """
    Applies multi-layered geological sedimentary strata with differential erosion resistance.
    Creates realistic cliff ledges, stepped plateau canyon walls, and rock banding.
    """
    h = heightfield.astype(np.float32).copy()
    res_y, res_x = h.shape
    rng = np.random.default_rng(seed)

    # Hardness profiles for distinct strata bands
    layer_hardness = rng.uniform(1.0 - hardness_variance, 1.0 + hardness_variance, size=layers_count)

    # Compute strata phase including dip angle
    rad = np.radians(folding_angle_deg)
    lin_x = np.linspace(0.0, 1.0, res_x, dtype=np.float32)
    lin_y = np.linspace(0.0, 1.0, res_y, dtype=np.float32)
    grid_x, grid_y = np.meshgrid(lin_x, lin_y)
    
    dip_offset = (grid_x * np.cos(rad) + grid_y * np.sin(rad)) * 0.15
    strata_coord = np.clip((h + dip_offset) * layers_count, 0.0, layers_count - 1.001)

    layer_idx = np.floor(strata_coord).astype(int)
    frac = strata_coord - layer_idx

    # S-curve terrace step shaping based on layer hardness
    hardness = layer_hardness[layer_idx]
    shaped_frac = np.where(hardness > 1.0, np.power(frac, 0.5), np.power(frac, 2.0))

    strata_h = (layer_idx + shaped_frac) / layers_count
    blended = h * 0.7 + strata_h * 0.3
    return np.clip(blended, 0.0, 1.0).astype(np.float32)


def tectonic_fault(
    heightfield: np.ndarray,
    fault_angle_deg: float = 45.0,
    displacement_height: float = 0.25,
    fault_position: float = 0.5,
    drag_width: float = 0.08,
) -> np.ndarray:
    """
    Simulates tectonic fault displacement (normal / reverse faulting).
    Creates sheer escarpments, fault rifts, and tectonic mountain uplifts.
    """
    h = heightfield.astype(np.float32).copy()
    res_y, res_x = h.shape

    rad = np.radians(fault_angle_deg)
    nx = np.cos(rad)
    ny = np.sin(rad)

    lin_x = np.linspace(0.0, 1.0, res_x, dtype=np.float32)
    lin_y = np.linspace(0.0, 1.0, res_y, dtype=np.float32)
    gx, gy = np.meshgrid(lin_x, lin_y)

    dist_from_fault = (gx * nx + gy * ny) - fault_position

    # Smooth sigmoid transition along the fault drag zone
    displacement = displacement_height * (1.0 / (1.0 + np.exp(-dist_from_fault / max(drag_width, 1e-4))))
    uplifted = h + displacement

    min_val = np.min(uplifted)
    max_val = np.max(uplifted)
    if max_val > min_val:
        uplifted = (uplifted - min_val) / (max_val - min_val)
    return uplifted.astype(np.float32)


def terrace_plateau(
    heightfield: np.ndarray,
    steps_count: int = 6,
    smoothness: float = 0.15
) -> np.ndarray:
    """Carves terraced plateau steps with customizable cliff sharpness."""
    h = heightfield.astype(np.float32)
    scaled = h * steps_count
    base = np.floor(scaled)
    frac = scaled - base

    # Smoothstep interpolation
    t = np.clip((frac - (0.5 - smoothness)) / (2.0 * smoothness + 1e-6), 0.0, 1.0)
    stepped = base + t * t * (3.0 - 2.0 * t)
    return (stepped / steps_count).astype(np.float32)
