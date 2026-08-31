"""
API / MCP coverage lock.

AGENTS.md states that the in-app assistant, the Python API and the MCP tools
all execute the same ``ai_apply_actions`` path, so adding an operation once
gives all three. That was not true: the shipped MCP server talked to a
Python-side prototype and exposed none of the application's operations, and
nothing tested the claim, so it drifted silently.

These tests read the operations straight out of the C++ source and assert that
every one of them is reachable from Python and from MCP. If someone adds an op
and forgets to expose it, this fails and names it.
"""

import json
import re
from pathlib import Path

import pytest

from mcp_server.server import NodeTerrainMCPServer
from mcp_server.studio_api import all_tools, handle_mcp, MCP_TOOLS

ROOT = Path(__file__).resolve().parents[1]
STUDIO = ROOT / "studio"

# Ops that are deliberately not their own MCP tool.
#   undo/redo    - exposed as studio_undo / studio_redo
#   set_sun etc. - folded into the single studio_set_world tool
#   render       - studio_render fires it as part of configuring output
EXEMPT = {
    "undo", "redo", "render",
    "set_sun", "set_sky", "set_fog", "set_clouds", "set_water",
}

# op -> the MCP tool that covers it, where the names differ
ALIASES = {
    "graph": "studio_graph",
    "select": "studio_select",
    "add_camera": "studio_add_camera",
    "set_camera": "studio_set_camera",
    "place_object": "studio_place_object",
    "add_planet": "studio_add_planet",
    "set_planet": "studio_set_planet",
    "add_infinite_terrain": "studio_add_infinite_terrain",
    "set_render": "studio_set_render",
}


def handled_ops():
    """Every op the C++ actually accepts, read from the source."""
    ops = set()
    for name in ("ai_assist.cpp", "ai_ops_graph.cpp"):
        text = (STUDIO / name).read_text(encoding="utf-8", errors="replace")
        ops |= set(re.findall(r'op == "([a-z_]+)"', text))
    return ops


def test_source_declares_operations():
    ops = handled_ops()
    # a sanity floor: if the regex stops matching, the rest of this file would
    # pass vacuously, which is worse than failing
    assert len(ops) >= 25, f"only found {len(ops)} ops - has the dispatch changed shape?"
    for expected in ("add_node", "connect", "set_attr", "graph", "set_sun"):
        assert expected in ops, f"{expected} not found in the C++ dispatch"


def test_every_op_is_reachable_over_mcp():
    """The claim in AGENTS.md, enforced."""
    tools = all_tools()
    missing = []
    for op in sorted(handled_ops() - EXEMPT):
        tool = ALIASES.get(op, "studio_" + op)
        if tool not in tools:
            missing.append(f"{op} (expected tool {tool})")
    assert not missing, "operations with no MCP tool: " + ", ".join(missing)


def test_shipped_mcp_server_exposes_the_studio_tools(tmp_path):
    """The server that mcp_server actually exports must drive the app.

    It used to expose only an in-process Python prototype, so an agent
    connecting over MCP could not touch the running application at all.
    """
    server = NodeTerrainMCPServer(workspace_dir=str(tmp_path))
    resp = json.loads(server.handle_request(
        json.dumps({"jsonrpc": "2.0", "method": "tools/list", "id": 1})))
    names = {t["name"] for t in resp["result"]["tools"]}
    for tool in all_tools():
        assert tool in names, f"{tool} is not exposed by the shipped MCP server"


def test_every_tool_dispatches(tmp_path, monkeypatch):
    """No tool may be advertised and then rejected as unknown."""
    sent = []

    class FakeStudio:
        def state(self):
            return {"nodes": [], "links": [], "terrain": {}, "eval": {}}

        def send(self, *actions):
            sent.append(actions)
            return {"ok": True}

        def __getattr__(self, name):
            def call(*a, **kw):
                sent.append((name, a, kw))
                return {"ok": True}
            return call

    for tool in all_tools():
        out = handle_mcp(tool, {}, studio=FakeStudio())
        assert out.get("status") != "error" or "unknown tool" not in out.get(
            "message", ""), f"{tool} is advertised but not dispatched"


def test_graph_tools_are_documented():
    """Every tool carries a description an agent can act on."""
    for name, spec in all_tools().items():
        assert spec.get("description"), f"{name} has no description"
        assert len(spec["description"]) > 20, f"{name}'s description is too thin"
