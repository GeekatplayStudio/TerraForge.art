"""
Comprehensive Unit & Integration Test Suite for Geekatplay Studio NodeTerrain Core Solvers.
Tests noise kernels, physical erosion, structural geology, PBR splatmaps, Vue EcoSystems, atmosphere, and DAG.
"""

import pytest
import numpy as np
from core.noise import perlin_noise_2d, ridged_multifractal, voronoi_cellular_noise, billow_noise
from core.erosion import hydraulic_erosion, thermal_weathering
from core.geology import geological_strata, tectonic_fault, terrace_plateau
from core.materials import compute_slope_map, compute_curvature_map, compute_normal_map, generate_pbr_splatmap
from core.ecosystem import EcoSpeciesRule, populate_ecosystem
from core.atmosphere import AtmosphericEnvironment, generate_volumetric_cloud_layer
from core.dag import TerrainDAGSolver
from core.persistence import WALProjectManager


def test_procedural_noise_ranges():
    res = 64
    perlin = perlin_noise_2d(res, frequency=4.0, seed=42)
    assert perlin.shape == (res, res)
    assert 0.0 <= np.min(perlin) <= np.max(perlin) <= 1.0

    ridged = ridged_multifractal(res, octaves=4, seed=42)
    assert ridged.shape == (res, res)
    assert 0.0 <= np.min(ridged) <= np.max(ridged) <= 1.0

    voronoi = voronoi_cellular_noise(res, cell_count=8, seed=42)
    assert voronoi.shape == (res, res)
    assert 0.0 <= np.min(voronoi) <= np.max(voronoi) <= 1.0

    billow = billow_noise(res, octaves=4, seed=42)
    assert billow.shape == (res, res)
    assert 0.0 <= np.min(billow) <= np.max(billow) <= 1.0


def test_hydraulic_and_thermal_erosion():
    res = 64
    base_h = perlin_noise_2d(res, frequency=3.0, seed=123)

    # Hydraulic erosion
    eroded_h, flow, sed = hydraulic_erosion(base_h, iterations=15, rain_rate=0.01)
    assert eroded_h.shape == (res, res)
    assert flow.shape == (res, res)
    assert 0.0 <= np.min(eroded_h) <= np.max(eroded_h) <= 1.0

    # Thermal weathering
    weathered_h, talus = thermal_weathering(base_h, repose_angle_deg=34.5, iterations=10)
    assert weathered_h.shape == (res, res)
    assert talus.shape == (res, res)
    assert 0.0 <= np.min(weathered_h) <= np.max(weathered_h) <= 1.0


def test_structural_geology():
    res = 64
    base_h = perlin_noise_2d(res, frequency=2.5, seed=77)

    strata = geological_strata(base_h, layers_count=8, hardness_variance=0.5)
    assert strata.shape == (res, res)
    assert 0.0 <= np.min(strata) <= np.max(strata) <= 1.0

    faulted = tectonic_fault(base_h, fault_angle_deg=45.0, displacement_height=0.3)
    assert faulted.shape == (res, res)

    stepped = terrace_plateau(base_h, steps_count=5)
    assert stepped.shape == (res, res)


def test_pbr_materials_and_splatmaps():
    res = 64
    base_h = ridged_multifractal(res, octaves=4, seed=99)
    slope = compute_slope_map(base_h)
    assert slope.shape == (res, res)
    assert 0.0 <= np.min(slope) <= np.max(slope) <= 1.0

    curv = compute_curvature_map(base_h)
    assert curv.shape == (res, res)

    normal = compute_normal_map(base_h)
    assert normal.shape == (res, res, 3)
    assert 0.0 <= np.min(normal) <= np.max(normal) <= 1.0

    splat_dict = generate_pbr_splatmap(base_h)
    assert "splatmap_rgba" in splat_dict
    assert splat_dict["splatmap_rgba"].shape == (res, res, 4)


def test_ecosystem_populator():
    res = 64
    base_h = perlin_noise_2d(res, frequency=2.0, seed=55)
    rules = [
        EcoSpeciesRule(species_id=1, name="Alpine Pine", min_altitude=0.1, max_altitude=0.7, density=0.08),
        EcoSpeciesRule(species_id=2, name="Boulders", min_altitude=0.0, max_altitude=0.9, density=0.02),
    ]
    eco = populate_ecosystem(base_h, rules, seed=42)
    assert "total_instances" in eco
    assert eco["total_instances"] > 0
    assert eco["positions"].shape[1] == 3
    assert eco["rotations"].shape[1] == 3


def test_atmosphere_and_clouds():
    atmo = AtmosphericEnvironment(sun_elevation_deg=35.0, sun_azimuth_deg=120.0)
    coeffs = atmo.get_sky_coefficients()
    assert "sun_direction" in coeffs
    assert "sun_transmittance" in coeffs

    clouds = generate_volumetric_cloud_layer(res=64, coverage=0.5, seed=42)
    assert clouds.shape == (64, 64)
    assert 0.0 <= np.min(clouds) <= np.max(clouds) <= 1.0


def test_dag_solver_execution():
    solver = TerrainDAGSolver()
    graph = {
        "nodes": [
            {"id": "n1", "type": "ridged_multifractal", "params": {"octaves": 4, "frequency": 2.0}},
            {"id": "n2", "type": "hydraulic_erosion_gpu", "params": {"iterations": 5}},
            {"id": "n3", "type": "pbr_splatmap_generator", "params": {}},
        ],
        "edges": [
            {"source_node": "n1", "source_port": "height", "target_node": "n2", "target_port": "height_in"},
            {"source_node": "n2", "source_port": "height_out", "target_node": "n3", "target_port": "height_in"},
            {"source_node": "n2", "source_port": "sediment_flow", "target_node": "n3", "target_port": "flow_mask"},
        ]
    }
    outputs = solver.evaluate(graph, resolution=64)
    assert "n1" in outputs
    assert "n2" in outputs
    assert "n3" in outputs
    assert "pbr_material" in outputs["n3"]


def test_wal_persistence_and_restore(tmp_path):
    wal = WALProjectManager(str(tmp_path))
    wal.log_mutation("ADD_NODE", {"id": "test_node_1", "type": "perlin_noise"})
    wal.log_mutation("SET_PARAM", {"node_id": "test_node_1", "key": "frequency", "value": 8.0})

    restored = wal.restore_session()
    assert len(restored["nodes"]) == 1
    assert restored["nodes"][0]["id"] == "test_node_1"
    assert restored["nodes"][0]["params"]["frequency"] == 8.0
