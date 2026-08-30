"""
Geekatplay Studio — Model Context Protocol (MCP) Server for NodeTerrain.
Provides JSON-RPC tool endpoints for AI pair programming, autonomous agents, and IDE extensions.
"""

import sys
import json
from typing import Dict, Any, List
import numpy as np

from core.dag import TerrainDAGSolver, NodeRegistry
from core.persistence import WALProjectManager
from orchestrator.graph import MultiAgentGraph


class NodeTerrainMCPServer:
    """Model Context Protocol (MCP) Server implementation for Geekatplay NodeTerrain."""

    def __init__(self, workspace_dir: str = "."):
        self.workspace_dir = workspace_dir
        self.wal_manager = WALProjectManager(workspace_dir)
        self.dag_solver = TerrainDAGSolver()
        self.current_graph = self.wal_manager.restore_session()
        self.tools = {
            "terrain_get_state": self.tool_get_state,
            "terrain_create_node": self.tool_create_node,
            "terrain_connect_nodes": self.tool_connect_nodes,
            "terrain_set_param": self.tool_set_param,
            "terrain_evaluate": self.tool_evaluate,
            "terrain_generate_from_prompt": self.tool_generate_from_prompt,
            "terrain_export_heightfield": self.tool_export_heightfield,
        }

    def tool_get_state(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Returns the current node graph structure and metadata."""
        return {
            "status": "success",
            "graph": self.current_graph,
            "node_count": len(self.current_graph.get("nodes", [])),
            "edge_count": len(self.current_graph.get("edges", []))
        }

    def tool_create_node(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Creates a new terrain node in the active graph."""
        node_id = params.get("id", f"node_{len(self.current_graph.get('nodes', [])) + 1}")
        node_type = params.get("type", "perlin_noise")
        category = params.get("category", "generator")
        node_params = params.get("params", {})
        pos = params.get("position", {"x": 100.0, "y": 100.0})

        node_data = {
            "id": node_id,
            "type": node_type,
            "category": category,
            "params": node_params,
            "position": pos,
        }
        self.wal_manager.log_mutation("ADD_NODE", node_data)
        self.current_graph.setdefault("nodes", []).append(node_data)
        self.wal_manager.save_snapshot(self.current_graph)

        return {"status": "success", "created_node": node_data}

    def tool_connect_nodes(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Connects two node ports in the terrain graph."""
        edge_id = params.get("id", f"edge_{len(self.current_graph.get('edges', [])) + 1}")
        edge_data = {
            "id": edge_id,
            "source_node": params.get("source_node"),
            "source_port": params.get("source_port", "height_out"),
            "target_node": params.get("target_node"),
            "target_port": params.get("target_port", "height_in"),
        }
        self.wal_manager.log_mutation("CONNECT", edge_data)
        self.current_graph.setdefault("edges", []).append(edge_data)
        self.wal_manager.save_snapshot(self.current_graph)

        return {"status": "success", "created_edge": edge_data}

    def tool_set_param(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Updates a parameter slider for a specific node."""
        node_id = params.get("node_id")
        key = params.get("key")
        val = params.get("value")

        self.wal_manager.log_mutation("SET_PARAM", {"node_id": node_id, "key": key, "value": val})
        for n in self.current_graph.get("nodes", []):
            if n.get("id") == node_id:
                n.setdefault("params", {})[key] = val
        self.wal_manager.save_snapshot(self.current_graph)

        return {"status": "success", "updated": {key: val}}

    def tool_evaluate(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Evaluates the terrain graph at specified resolution."""
        res = int(params.get("resolution", 256))
        outputs = self.dag_solver.evaluate(self.current_graph, resolution=res)
        summary = {k: list(v.keys()) for k, v in outputs.items()}
        return {"status": "success", "evaluated_nodes": summary}

    def tool_generate_from_prompt(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Uses Multi-Agent Super Intelligence to synthesize a full terrain graph from natural language."""
        prompt = params.get("prompt", "Alpine glacial peaks")
        img_path = params.get("image_path")

        orchestrator = MultiAgentGraph()
        state = orchestrator.run(user_request=prompt, image_path=img_path)
        new_graph = state.get("terrain_graph")
        if new_graph:
            self.current_graph = new_graph
            self.wal_manager.save_snapshot(self.current_graph)

        return {
            "status": "success",
            "validation": state.get("validation_report"),
            "messages": [m["content"] for m in state.get("messages", [])],
            "graph": self.current_graph,
        }

    def tool_export_heightfield(self, params: Dict[str, Any]) -> Dict[str, Any]:
        """Bakes and exports the final terrain heightfield to a RAW/PNG array."""
        res = int(params.get("resolution", 256))
        outputs = self.dag_solver.evaluate(self.current_graph, resolution=res)
        
        # Locate output node or last evaluated node
        last_node = list(outputs.keys())[-1] if outputs else None
        if not last_node:
            return {"status": "error", "message": "Graph is empty"}

        last_out = outputs[last_node]
        h = last_out.get("height_out", last_out.get("height", last_out.get("final_heightfield")))
        if h is None:
            return {"status": "error", "message": "No heightfield output found"}

        return {
            "status": "success",
            "resolution": [int(h.shape[0]), int(h.shape[1])],
            "min_elevation": float(np.min(h)),
            "max_elevation": float(np.max(h)),
            "mean_elevation": float(np.mean(h)),
        }

    def handle_request(self, json_str: str) -> str:
        """Processes incoming JSON-RPC 2.0 MCP request."""
        try:
            req = json.loads(json_str)
            method = req.get("method")
            params = req.get("params", {})
            req_id = req.get("id")

            if method in self.tools:
                result = self.tools[method](params)
                return json.dumps({"jsonrpc": "2.0", "result": result, "id": req_id})
            elif method == "tools/list":
                tool_list = [
                    {"name": name, "description": fn.__doc__ or ""}
                    for name, fn in self.tools.items()
                ]
                return json.dumps({"jsonrpc": "2.0", "result": {"tools": tool_list}, "id": req_id})
            else:
                return json.dumps({"jsonrpc": "2.0", "error": {"code": -32601, "message": f"Method {method} not found"}, "id": req_id})
        except Exception as e:
            return json.dumps({"jsonrpc": "2.0", "error": {"code": -32000, "message": str(e)}, "id": None})
