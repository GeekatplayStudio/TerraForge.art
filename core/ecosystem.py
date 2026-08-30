"""
Geekatplay Studio — Vue-Class EcoSystem Procedural Distribution Solver.
Populates realistic vegetation, rocks, and trees according to altitude, slope, moisture, and sun exposure.
"""

import numpy as np
from typing import List, Dict, Any


class EcoSpeciesRule:
    """Rules for placing a specific plant or rock species."""
    def __init__(
        self,
        species_id: int,
        name: str,
        min_altitude: float = 0.0,
        max_altitude: float = 0.65,
        max_slope_deg: float = 35.0,
        min_moisture: float = 0.2,
        density: float = 0.05,
        scale_min: float = 0.8,
        scale_max: float = 1.3,
    ):
        self.species_id = species_id
        self.name = name
        self.min_altitude = min_altitude
        self.max_altitude = max_altitude
        self.max_slope_deg = max_slope_deg
        self.min_moisture = min_moisture
        self.density = density
        self.scale_min = scale_min
        self.scale_max = scale_max


def populate_ecosystem(
    heightfield: np.ndarray,
    species_rules: List[EcoSpeciesRule],
    moisture_map: np.ndarray = None,
    seed: int = 42,
    max_instances: int = 100000,
) -> Dict[str, Any]:
    """
    Populates procedural EcoSystem instances over a heightfield.
    Returns:
        Dict containing arrays of positions [X, Y, Z], rotations [Yaw, Pitch, Roll], scales, and species IDs.
    """
    h = heightfield.astype(np.float32)
    res_y, res_x = h.shape
    rng = np.random.default_rng(seed)

    # Compute slope
    dy, dx = np.gradient(h)
    slope_deg = np.degrees(np.arctan(np.sqrt(dx * dx + dy * dy)))

    moisture = moisture_map if moisture_map is not None else np.full_like(h, 0.5)

    all_positions = []
    all_rotations = []
    all_scales = []
    all_species_ids = []

    for rule in species_rules:
        # Create probability mask
        alt_mask = (h >= rule.min_altitude) & (h <= rule.max_altitude)
        slope_mask = slope_deg <= rule.max_slope_deg
        moist_mask = moisture >= rule.min_moisture

        valid_mask = alt_mask & slope_mask & moist_mask
        valid_indices = np.argwhere(valid_mask)

        if len(valid_indices) == 0:
            continue

        # Sample instances according to density
        num_samples = int(len(valid_indices) * rule.density)
        num_samples = min(num_samples, max_instances - len(all_positions))

        if num_samples <= 0:
            continue

        selected_idx = rng.choice(len(valid_indices), size=num_samples, replace=False)
        pts = valid_indices[selected_idx]

        # Add sub-pixel jitter
        jitter = rng.uniform(-0.45, 0.45, size=(num_samples, 2))
        grid_y = np.clip(pts[:, 0] + jitter[:, 0], 0, res_y - 1)
        grid_x = np.clip(pts[:, 1] + jitter[:, 1], 0, res_x - 1)

        # Bilinear sample height at jittered position
        ix = grid_x.astype(int)
        iy = grid_y.astype(int)
        z = h[iy, ix]

        norm_x = (grid_x / res_x).astype(np.float32)
        norm_y = (grid_y / res_y).astype(np.float32)

        pos = np.stack([norm_x, norm_y, z], axis=-1)
        yaw = rng.uniform(0.0, 360.0, size=num_samples).astype(np.float32)
        pitch = rng.uniform(-3.0, 3.0, size=num_samples).astype(np.float32)
        roll = rng.uniform(-3.0, 3.0, size=num_samples).astype(np.float32)
        rot = np.stack([yaw, pitch, roll], axis=-1)

        scale = rng.uniform(rule.scale_min, rule.scale_max, size=num_samples).astype(np.float32)
        sp_id = np.full(num_samples, rule.species_id, dtype=np.int32)

        all_positions.append(pos)
        all_rotations.append(rot)
        all_scales.append(scale)
        all_species_ids.append(sp_id)

    if not all_positions:
        return {
            "total_instances": 0,
            "positions": np.empty((0, 3), dtype=np.float32),
            "rotations": np.empty((0, 3), dtype=np.float32),
            "scales": np.empty((0,), dtype=np.float32),
            "species_ids": np.empty((0,), dtype=np.int32),
        }

    return {
        "total_instances": sum(len(p) for p in all_positions),
        "positions": np.concatenate(all_positions, axis=0),
        "rotations": np.concatenate(all_rotations, axis=0),
        "scales": np.concatenate(all_scales, axis=0),
        "species_ids": np.concatenate(all_species_ids, axis=0),
    }
