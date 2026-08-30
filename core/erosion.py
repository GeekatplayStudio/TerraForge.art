"""
Geekatplay Studio — Physical Terrain Erosion Solvers.
Implements Eulerian/Lagrangian hydraulic erosion and thermal weathering (talus scree angle-of-repose).
"""

import numpy as np
from typing import Tuple, Dict


def hydraulic_erosion(
    heightfield: np.ndarray,
    iterations: int = 40,
    rain_rate: float = 0.012,
    evaporation_rate: float = 0.025,
    sediment_capacity_factor: float = 4.0,
    dissolution_rate: float = 0.3,
    deposition_rate: float = 0.3,
    gravity: float = 9.81,
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Simulates physical hydraulic fluvial erosion over a 2D heightfield.
    Returns:
        (eroded_heightfield, flow_accumulation_map, sediment_map)
    """
    h = heightfield.astype(np.float32).copy()
    res_y, res_x = h.shape
    water = np.zeros_like(h)
    sediment = np.zeros_like(h)
    flow_accum = np.zeros_like(h)

    # 4-neighbor directional offsets: [dy, dx]
    dirs = np.array([[-1, 0], [1, 0], [0, -1], [0, 1]], dtype=int)

    for _ in range(iterations):
        # 1. Precipitation
        water += rain_rate

        # 2. Compute height differences with 4 neighbors
        h_up = np.pad(h, ((1, 0), (0, 0)), mode="edge")[:-1, :]
        h_down = np.pad(h, ((0, 1), (0, 0)), mode="edge")[1:, :]
        h_left = np.pad(h, ((0, 0), (1, 0)), mode="edge")[:, :-1]
        h_right = np.pad(h, ((0, 0), (0, 1)), mode="edge")[:, 1:]

        # Slopes
        slope_up = np.maximum(0.0, (h + water) - h_up)
        slope_down = np.maximum(0.0, (h + water) - h_down)
        slope_left = np.maximum(0.0, (h + water) - h_left)
        slope_right = np.maximum(0.0, (h + water) - h_right)

        total_slope = slope_up + slope_down + slope_left + slope_right + 1e-6
        flow_up = (slope_up / total_slope) * water
        flow_down = (slope_down / total_slope) * water
        flow_left = (slope_left / total_slope) * water
        flow_right = (slope_right / total_slope) * water

        net_flow = flow_up + flow_down + flow_left + flow_right
        flow_accum += net_flow

        # 3. Erosion & Sediment transport capacity
        velocity = np.sqrt(np.maximum(0.0, 2.0 * gravity * (total_slope / (res_x * 0.5))))
        capacity = np.maximum(0.01, total_slope) * water * velocity * sediment_capacity_factor

        # Erode if under capacity, deposit if over capacity
        erode_amount = np.maximum(0.0, capacity - sediment) * dissolution_rate
        deposit_amount = np.maximum(0.0, sediment - capacity) * deposition_rate

        h -= np.clip(erode_amount, 0.0, h * 0.1)
        h += deposit_amount

        sediment += erode_amount - deposit_amount

        # 4. Water evaporation & settling
        water = np.maximum(0.0, water * (1.0 - evaporation_rate) - 0.001)

    # Deposit remaining suspended sediment
    h += sediment * 0.5

    # Normalize outputs
    flow_norm = flow_accum / (np.max(flow_accum) + 1e-6)
    return np.clip(h, 0.0, 1.0), flow_norm.astype(np.float32), sediment.astype(np.float32)


def thermal_weathering(
    heightfield: np.ndarray,
    repose_angle_deg: float = 34.5,
    talus_rate: float = 0.45,
    iterations: int = 25,
) -> Tuple[np.ndarray, np.ndarray]:
    """
    Simulates thermal weathering and freeze-thaw crumbling based on material angle of repose.
    Creates realistic talus scree slopes and crumbling cliff edges.
    Returns:
        (weathered_heightfield, talus_scree_map)
    """
    h = heightfield.astype(np.float32).copy()
    res_y, res_x = h.shape
    talus_map = np.zeros_like(h)

    # Convert angle of repose to critical height threshold
    crit_threshold = np.tan(np.radians(repose_angle_deg)) / res_x

    for _ in range(iterations):
        # Calculate height differentials
        h_up = np.pad(h, ((1, 0), (0, 0)), mode="edge")[:-1, :]
        h_down = np.pad(h, ((0, 1), (0, 0)), mode="edge")[1:, :]
        h_left = np.pad(h, ((0, 0), (1, 0)), mode="edge")[:, :-1]
        h_right = np.pad(h, ((0, 0), (0, 1)), mode="edge")[:, 1:]

        d_up = np.maximum(0.0, h - h_up - crit_threshold)
        d_down = np.maximum(0.0, h - h_down - crit_threshold)
        d_left = np.maximum(0.0, h - h_left - crit_threshold)
        d_right = np.maximum(0.0, h - h_right - crit_threshold)

        total_excess = d_up + d_down + d_left + d_right
        slippage = (total_excess * 0.25) * talus_rate

        h -= slippage
        talus_map += slippage

        # Distribute fallen material to lower neighbors
        h_up += d_up * 0.25 * talus_rate
        h_down += d_down * 0.25 * talus_rate
        h_left += d_left * 0.25 * talus_rate
        h_right += d_right * 0.25 * talus_rate

    talus_norm = talus_map / (np.max(talus_map) + 1e-6)
    return np.clip(h, 0.0, 1.0), talus_norm.astype(np.float32)
