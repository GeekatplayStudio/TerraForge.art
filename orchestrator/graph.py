"""
Geekatplay Studio — LangGraph Multi-Agent Workflow Graph.
Assembles the directed graph connecting all 13 specialized agent nodes with conditional execution edges.
"""

from typing import Dict, Any, Callable
from .state import MultiAgentState
from .agents import (
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
    build_engineer_node,
    qa_tester_node,
)


class MultiAgentGraph:
    """
    Autonomous Multi-Agent Directed Acyclic & Cyclical Graph Orchestrator.
    Supports both native Python state execution and standard LangGraph compilation.
    """

    def __init__(self):
        self.nodes: Dict[str, Callable[[MultiAgentState], Dict[str, Any]]] = {
            "project_manager": project_manager_node,
            "system_architect": system_architect_node,
            "art_director": art_director_node,
            "geologist": geologist_node,
            "gpu_master": gpu_master_node,
            "cpp_systems_coder": cpp_coder_node,
            "backend_engineer": backend_engineer_node,
            "ui_designer": ui_designer_node,
            "materials_master": materials_master_node,
            "three_engineer": three_engineer_node,
            "copywriter": copywriter_node,
            "scientist": scientist_node,
            "build_engineer": build_engineer_node,
            "tester": qa_tester_node,
        }

    def run(self, user_request: str, image_path: str = None, max_steps: int = 25) -> MultiAgentState:
        """Executes the full multi-agent orchestration graph until completion."""
        state: MultiAgentState = {
            "user_request": user_request,
            "input_image_path": image_path,
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

        current_node = "project_manager"
        step_count = 0

        while not state.get("is_completed", False) and step_count < max_steps:
            step_count += 1
            node_fn = self.nodes.get(current_node)
            if not node_fn:
                break

            result = node_fn(state)
            
            # Merge state updates
            for k, v in result.items():
                if k == "messages":
                    state["messages"] = list(state.get("messages", [])) + v
                else:
                    state[k] = v

            next_node = state.get("next_step")
            if next_node == "completed" or not next_node:
                state["is_completed"] = True
                break
            current_node = next_node

        return state


def build_langgraph_app():
    """Builds and compiles standard LangGraph StateGraph if langgraph is installed."""
    try:
        from langgraph.graph import StateGraph, END
        workflow = StateGraph(MultiAgentState)

        workflow.add_node("project_manager", project_manager_node)
        workflow.add_node("system_architect", system_architect_node)
        workflow.add_node("geologist", geologist_node)
        workflow.add_node("gpu_master", gpu_master_node)
        workflow.add_node("cpp_systems_coder", cpp_coder_node)
        workflow.add_node("backend_engineer", backend_engineer_node)
        workflow.add_node("ui_designer", ui_designer_node)
        workflow.add_node("materials_master", materials_master_node)
        workflow.add_node("three_engineer", three_engineer_node)
        workflow.add_node("copywriter", copywriter_node)
        workflow.add_node("art_director", art_director_node)
        workflow.add_node("scientist", scientist_node)
        workflow.add_node("tester", qa_tester_node)

        # Edges
        workflow.set_entry_point("project_manager")
        workflow.add_edge("project_manager", "system_architect")
        workflow.add_edge("system_architect", "geologist")
        workflow.add_edge("geologist", "gpu_master")
        workflow.add_edge("gpu_master", "cpp_systems_coder")
        workflow.add_edge("cpp_systems_coder", "backend_engineer")
        workflow.add_edge("backend_engineer", "ui_designer")
        workflow.add_edge("ui_designer", "materials_master")
        workflow.add_edge("materials_master", "three_engineer")
        workflow.add_edge("three_engineer", "copywriter")
        workflow.add_edge("copywriter", "art_director")
        workflow.add_edge("art_director", "scientist")
        workflow.add_edge("scientist", "tester")
        workflow.add_edge("tester", "project_manager")

        return workflow.compile()
    except ImportError:
        return MultiAgentGraph()
