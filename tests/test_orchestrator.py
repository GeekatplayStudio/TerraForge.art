"""
Unit and Integration tests for Geekatplay Studio NodeTerrain Multi-Agent Orchestrator.
"""

import pytest
from orchestrator.state import MultiAgentState
from orchestrator.graph import MultiAgentGraph
from orchestrator.agents import (
    project_manager_node,
    system_architect_node,
    art_director_node,
    geologist_node,
    gpu_master_node,
    cpp_coder_node,
    backend_engineer_node,
    ui_designer_node,
    materials_master_node,
    three_engineer_node,
    copywriter_node,
    scientist_node,
    qa_tester_node,
)


def test_project_manager_planning_phase():
    state: MultiAgentState = {
        "user_request": "Alpine Fjord with sheer cliffs",
        "input_image_path": None,
        "current_phase": "planning",
        "active_agent": "project_manager",
        "iteration": 0,
        "messages": [],
        "terrain_graph": None,
        "system_plan": None,
        "validation_report": None,
        "next_step": "project_manager",
        "approval_required": False,
        "is_completed": False,
    }
    result = project_manager_node(state)
    assert result["current_phase"] == "architecting"
    assert result["next_step"] == "system_architect"
    assert len(result["messages"]) == 1


def test_system_architect_blueprint():
    state: MultiAgentState = {
        "user_request": "Desert Mesa Canyon",
        "input_image_path": None,
        "current_phase": "architecting",
        "active_agent": "system_architect",
        "iteration": 1,
        "messages": [],
        "terrain_graph": None,
        "system_plan": None,
        "validation_report": None,
        "next_step": "system_architect",
        "approval_required": False,
        "is_completed": False,
    }
    result = system_architect_node(state)
    assert "system_plan" in result
    assert result["system_plan"]["mcp_compliance"] is not None
    assert result["next_step"] == "geologist"


def test_geologist_biome_inference():
    state: MultiAgentState = {
        "user_request": "Volcanic caldera with active crater lake",
        "input_image_path": None,
        "current_phase": "architecting",
        "active_agent": "geologist",
        "iteration": 1,
        "messages": [],
        "terrain_graph": None,
        "system_plan": None,
        "validation_report": None,
        "next_step": "geologist",
        "approval_required": False,
        "is_completed": False,
    }
    result = geologist_node(state)
    assert "messages" in result
    meta = result["messages"][0]["metadata"]
    assert "Volcanic" in meta["inferred_biome"]


def test_gpu_master_and_cpp_coder_dag():
    state: MultiAgentState = {
        "user_request": "Glacial Horn Peak",
        "input_image_path": None,
        "current_phase": "synthesis",
        "active_agent": "gpu_master",
        "iteration": 2,
        "messages": [],
        "terrain_graph": None,
        "system_plan": None,
        "validation_report": None,
        "next_step": "gpu_master",
        "approval_required": False,
        "is_completed": False,
    }
    gpu_res = gpu_master_node(state)
    assert "terrain_graph" in gpu_res
    graph = gpu_res["terrain_graph"]
    assert len(graph["nodes"]) > 0

    state["terrain_graph"] = graph
    cpp_res = cpp_coder_node(state)
    assert cpp_res["messages"][0]["metadata"]["has_cycle"] is False
    assert len(cpp_res["messages"][0]["metadata"]["sorted_order"]) == len(graph["nodes"])


def test_tester_validation_and_pass():
    state: MultiAgentState = {
        "user_request": "Highlands plateau",
        "input_image_path": None,
        "current_phase": "testing",
        "active_agent": "tester",
        "iteration": 3,
        "messages": [],
        "terrain_graph": {
            "nodes": [{"id": "n1", "category": "generator"}, {"id": "n2", "category": "erosion"}],
            "edges": [{"source_node": "n1", "target_node": "n2"}]
        },
        "system_plan": None,
        "validation_report": None,
        "next_step": "tester",
        "approval_required": False,
        "is_completed": False,
    }
    res = qa_tester_node(state)
    val = res["validation_report"]
    assert val["passed"] is True
    assert val["unit_test_coverage"] == 100.0


def test_full_graph_orchestration_run():
    graph = MultiAgentGraph()
    state = graph.run("Coastal sea stack arch with crashing waves and limestone cliffs")
    assert state["is_completed"] is True
    assert state["terrain_graph"] is not None
    assert state["validation_report"]["passed"] is True
    assert len(state["messages"]) >= 10
