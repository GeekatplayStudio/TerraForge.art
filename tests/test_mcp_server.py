"""
Unit & Integration Tests for NodeTerrain Model Context Protocol (MCP) Server.
"""

import json
import pytest
from mcp_server.server import NodeTerrainMCPServer


def test_mcp_tools_list(tmp_path):
    server = NodeTerrainMCPServer(workspace_dir=str(tmp_path))
    req = json.dumps({"jsonrpc": "2.0", "method": "tools/list", "id": 1})
    resp_raw = server.handle_request(req)
    resp = json.loads(resp_raw)
    assert resp["id"] == 1
    assert "tools" in resp["result"]
    assert len(resp["result"]["tools"]) >= 6


def test_mcp_node_creation_and_connect(tmp_path):
    server = NodeTerrainMCPServer(workspace_dir=str(tmp_path))

    # 1. Create generator node
    req1 = json.dumps({
        "jsonrpc": "2.0",
        "method": "terrain_create_node",
        "params": {"id": "n1", "type": "perlin_noise", "params": {"frequency": 4.0}},
        "id": 2
    })
    resp1 = json.loads(server.handle_request(req1))
    assert resp1["result"]["status"] == "success"

    # 2. Create erosion node
    req2 = json.dumps({
        "jsonrpc": "2.0",
        "method": "terrain_create_node",
        "params": {"id": "n2", "type": "hydraulic_erosion_gpu", "params": {"iterations": 10}},
        "id": 3
    })
    resp2 = json.loads(server.handle_request(req2))
    assert resp2["result"]["status"] == "success"

    # 3. Connect nodes
    req3 = json.dumps({
        "jsonrpc": "2.0",
        "method": "terrain_connect_nodes",
        "params": {"source_node": "n1", "source_port": "height", "target_node": "n2", "target_port": "height_in"},
        "id": 4
    })
    resp3 = json.loads(server.handle_request(req3))
    assert resp3["result"]["status"] == "success"

    # 4. Evaluate graph
    req4 = json.dumps({
        "jsonrpc": "2.0",
        "method": "terrain_evaluate",
        "params": {"resolution": 64},
        "id": 5
    })
    resp4 = json.loads(server.handle_request(req4))
    assert resp4["result"]["status"] == "success"
    assert "n1" in resp4["result"]["evaluated_nodes"]
    assert "n2" in resp4["result"]["evaluated_nodes"]
