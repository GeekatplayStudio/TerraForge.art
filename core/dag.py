"""
Geekatplay Studio — High-Throughput Directed Acyclic Graph (DAG) Execution Engine.
Manages topological sorting, dirty-node sub-graph caching, and parallel node execution.
"""

from typing import Dict, List, Any, Optional, Tuple
import hashlib
import json
import numpy as np

from .noise import perlin_noise_2d, ridged_multifractal, voronoi_cellular_noise, billow_noise
from .erosion import hydraulic_erosion, thermal_weathering
from .geology import geological_strata, tectonic_fault, terrace_plateau
from .materials import generate_pbr_splatmap, compute_normal_map


class NodeRegistry:
    """Registry of mathematical execution handlers for all terrain node types."""

    @staticmethod
    def execute_node(node_type: str, params: Dict[str, Any], inputs: Dict[str, Any], res: int) -> Dict[str, Any]:
        """Executes a single terrain node given its inputs and parameters."""
        if node_type == "perlin_noise":
            freq = float(params.get("frequency", 4.0))
            seed = int(params.get("seed", 42))
            h = perlin_noise_2d(res, frequency=freq, seed=seed)
            return {"height": h}

        elif node_type == "ridged_multifractal":
            octaves = int(params.get("octaves", 8))
            freq = float(params.get("frequency", 2.0))
            lac = float(params.get("lacunarity", 2.15))
            gain = float(params.get("gain", 0.5))
            seed = int(params.get("seed", 42))
            h = ridged_multifractal(res, octaves=octaves, frequency=freq, lacunarity=lac, gain=gain, seed=seed)
            return {"height": h}

        elif node_type == "voronoi_cellular":
            cells = int(params.get("cell_count", 16))
            mode = str(params.get("mode", "f1"))
            seed = int(params.get("seed", 42))
            h = voronoi_cellular_noise(res, cell_count=cells, seed=seed, mode=mode)
            return {"height": h}

        elif node_type == "geological_strata":
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            layers = int(params.get("layers", 12))
            variance = float(params.get("hardness_variance", 0.6))
            angle = float(params.get("folding_angle", 15.0))
            h = geological_strata(in_h, layers_count=layers, hardness_variance=variance, folding_angle_deg=angle)
            return {"height_out": h}

        elif node_type == "tectonic_fault":
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            angle = float(params.get("fault_angle", 45.0))
            disp = float(params.get("displacement", 0.25))
            h = tectonic_fault(in_h, fault_angle_deg=angle, displacement_height=disp)
            return {"height_out": h}

        elif node_type == "terrace_plateau":
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            steps = int(params.get("steps", 6))
            smooth = float(params.get("smoothness", 0.15))
            h = terrace_plateau(in_h, steps_count=steps, smoothness=smooth)
            return {"height_out": h}

        elif node_type in ["hydraulic_erosion_gpu", "hydraulic_erosion"]:
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            iters = int(params.get("iterations", 30))
            rain = float(params.get("rain_rate", 0.012))
            h, flow, sed = hydraulic_erosion(in_h, iterations=iters, rain_rate=rain)
            return {"height_out": h, "sediment_flow": flow, "sediment": sed}

        elif node_type in ["thermal_weathering_gpu", "thermal_weathering"]:
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            angle = float(params.get("repose_angle", 34.5))
            iters = int(params.get("iterations", 20))
            h, talus = thermal_weathering(in_h, repose_angle_deg=angle, iterations=iters)
            return {"height_out": h, "talus_map": talus}

        elif node_type == "pbr_splatmap_generator":
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            flow = inputs.get("flow_mask", None)
            snow = float(params.get("altitude_snow_line", 0.72))
            cliff = float(params.get("slope_threshold", 0.45))
            splat = generate_pbr_splatmap(in_h, flow_map=flow, snow_line=snow, cliff_slope_threshold=cliff)
            norm = compute_normal_map(in_h)
            return {"pbr_material": splat, "normal_map": norm}

        elif node_type == "viewport_3d_output":
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            mat = inputs.get("material_in", {})
            return {"final_heightfield": in_h, "final_material": mat}

        else:
            in_h = inputs.get("height_in", np.zeros((res, res), dtype=np.float32))
            return {"height_out": in_h}


class TerrainDAGSolver:
    """Evaluates terrain node graphs with dirty-node caching and dependency resolution."""

    def __init__(self):
        self.node_cache: Dict[str, Dict[str, Any]] = {}
        self.cache_hashes: Dict[str, str] = {}

    def _compute_hash(self, node: Dict[str, Any], input_hashes: Dict[str, str]) -> str:
        data = {
            "type": node.get("type"),
            "params": node.get("params", {}),
            "input_hashes": input_hashes,
        }
        return hashlib.sha256(json.dumps(data, sort_keys=True).encode()).hexdigest()

    def evaluate(self, graph_dict: Dict[str, Any], resolution: int = 512) -> Dict[str, Any]:
        nodes = graph_dict.get("nodes", [])
        edges = graph_dict.get("edges", [])
        
        # Build dependency graph
        node_map = {n["id"]: n for n in nodes}
        in_degrees = {n["id"]: 0 for n in nodes}
        adj_list = {n["id"]: [] for n in nodes}
        incoming_edges: Dict[str, List[Dict[str, Any]]] = {n["id"]: [] for n in nodes}

        for e in edges:
            src = e["source_node"]
            tgt = e["target_node"]
            if src in adj_list and tgt in in_degrees:
                adj_list[src].append(tgt)
                in_degrees[tgt] += 1
                incoming_edges[tgt].append(e)

        # Kahn's Algorithm for Topological Sort
        queue = [n_id for n_id, deg in in_degrees.items() if deg == 0]
        sorted_nodes = []
        while queue:
            curr = queue.pop(0)
            sorted_nodes.append(curr)
            for neighbor in adj_list[curr]:
                in_degrees[neighbor] -= 1
                if in_degrees[neighbor] == 0:
                    queue.append(neighbor)

        if len(sorted_nodes) != len(nodes):
            raise ValueError("Cyclic dependency detected in terrain node graph.")

        node_outputs: Dict[str, Dict[str, Any]] = {}
        node_hashes: Dict[str, str] = {}

        for node_id in sorted_nodes:
            node = node_map[node_id]
            node_type = node.get("type", "")
            params = node.get("params", {})

            # Gather inputs
            inputs: Dict[str, Any] = {}
            input_hashes: Dict[str, str] = {}
            for e in incoming_edges[node_id]:
                src_id = e["source_node"]
                src_port = e["source_port"]
                tgt_port = e["target_port"]
                src_out = node_outputs.get(src_id, {})
                if src_port in src_out:
                    inputs[tgt_port] = src_out[src_port]
                input_hashes[tgt_port] = node_hashes.get(src_id, "0")

            node_hash = self._compute_hash(node, input_hashes)
            node_hashes[node_id] = node_hash

            # Check cache
            if node_id in self.node_cache and self.cache_hashes.get(node_id) == node_hash:
                outputs = self.node_cache[node_id]
            else:
                outputs = NodeRegistry.execute_node(node_type, params, inputs, resolution)
                self.node_cache[node_id] = outputs
                self.cache_hashes[node_id] = node_hash

            node_outputs[node_id] = outputs

        return node_outputs
