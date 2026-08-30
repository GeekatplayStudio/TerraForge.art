"""
Geekatplay Studio — Spectral Atmospheric Scattering & Volumetric Sky Model.
Implements physically-based Rayleigh/Mie scattering parameters, sun lighting, and 3D volumetric clouds.
"""

import numpy as np
from typing import Dict, Any, Tuple


class AtmosphericEnvironment:
    """Atmospheric and solar environmental parameters (Bruneton / Eric Heitz model)."""

    def __init__(
        self,
        sun_elevation_deg: float = 25.0,
        sun_azimuth_deg: float = 135.0,
        rayleigh_turbidity: float = 1.0,
        mie_haze_density: float = 0.005,
        mie_anisotropy_g: float = 0.76,
        cloud_coverage: float = 0.45,
        cloud_altitude_km: float = 2.5,
        aerial_perspective_density: float = 0.0008,
    ):
        self.sun_elevation_deg = sun_elevation_deg
        self.sun_azimuth_deg = sun_azimuth_deg
        self.rayleigh_turbidity = rayleigh_turbidity
        self.mie_haze_density = mie_haze_density
        self.mie_anisotropy_g = mie_anisotropy_g
        self.cloud_coverage = cloud_coverage
        self.cloud_altitude_km = cloud_altitude_km
        self.aerial_perspective_density = aerial_perspective_density

    def get_sun_direction(self) -> np.ndarray:
        """Returns normalized 3D sun direction vector [X, Y, Z]."""
        el_rad = np.radians(self.sun_elevation_deg)
        az_rad = np.radians(self.sun_azimuth_deg)
        x = np.cos(el_rad) * np.sin(az_rad)
        y = np.sin(el_rad)
        z = np.cos(el_rad) * np.cos(az_rad)
        vec = np.array([x, y, z], dtype=np.float32)
        return vec / np.linalg.norm(vec)

    def get_sky_coefficients(self) -> Dict[str, Any]:
        """Calculates Rayleigh and Mie scattering wavelength coefficients."""
        # Standard sea-level Rayleigh coefficients (RGB: 680nm, 550nm, 440nm)
        beta_rayleigh = np.array([5.8e-6, 13.5e-6, 33.1e-6], dtype=np.float32) * self.rayleigh_turbidity
        beta_mie = np.array([21.0e-6, 21.0e-6, 21.0e-6], dtype=np.float32) * self.mie_haze_density

        # Solar zenith angle attenuation
        sun_dir = self.get_sun_direction()
        cos_zenith = max(sun_dir[1], 0.01)
        optical_depth = 1.0 / cos_zenith

        sun_transmittance = np.exp(-(beta_rayleigh + beta_mie) * optical_depth * 8000.0)

        return {
            "sun_direction": sun_dir.tolist(),
            "beta_rayleigh": beta_rayleigh.tolist(),
            "beta_mie": beta_mie.tolist(),
            "mie_anisotropy_g": self.mie_anisotropy_g,
            "sun_transmittance": sun_transmittance.tolist(),
            "cloud_coverage": self.cloud_coverage,
            "cloud_altitude_km": self.cloud_altitude_km,
        }


def generate_volumetric_cloud_layer(
    res: int = 256,
    coverage: float = 0.45,
    seed: int = 42
) -> np.ndarray:
    """
    Generates a 2D/3D density field representing a cumulus/stratus cloud deck.
    """
    from .noise import perlin_noise_2d
    base = perlin_noise_2d(res, frequency=3.0, seed=seed)
    detail = perlin_noise_2d(res, frequency=9.0, seed=seed + 101)
    combined = base * 0.7 + detail * 0.3
    
    # Threshold with coverage
    clouds = np.clip((combined - (1.0 - coverage)) / (coverage + 1e-6), 0.0, 1.0)
    # Smooth cloud borders
    return (clouds * clouds * (3.0 - 2.0 * clouds)).astype(np.float32)
