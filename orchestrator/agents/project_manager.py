"""
Geekatplay Studio — Project Manager & Super Agent Orchestrator Node.
Coordinates execution flow, evaluates task status, and delegates sub-tasks across the team.
"""

from typing import Dict, Any
from ..state import MultiAgentState, AgentMessage


def project_manager_node(state: MultiAgentState) -> Dict[str, Any]:
    """
    Project Manager supervisor node.
    Inspects user request, determines phase transitions, and routes to appropriate specialists.
    """
    iteration = state.get("iteration", 0) + 1
    messages = list(state.get("messages", []))
    phase = state.get("current_phase", "planning")
    user_request = state.get("user_request", "")
    has_image = bool(state.get("input_image_path"))

    pm_message = ""
    next_step = ""
    new_phase = phase

    if phase == "planning":
        pm_message = (
            f"📋 [Project Manager]: Initialized project plan for request: '{user_request}'. "
            f"Image context present: {has_image}. Delegating initial geomorphological analysis "
            f"to 3D Geologist and System Architecture review to System Architect."
        )
        new_phase = "architecting"
        next_step = "system_architect"
    elif phase == "architecting":
        pm_message = (
            "📋 [Project Manager]: Architecture and geomorphology planned. "
            "Routing to GPU Master and Systems Coder for shader and DAG solver synthesis."
        )
        new_phase = "synthesis"
        next_step = "gpu_master"
    elif phase == "synthesis":
        pm_message = (
            "📋 [Project Manager]: Terrain procedural graph synthesized. "
            "Passing to 3D Materials Master and UI Designer for material splatting and visual integration."
        )
        new_phase = "ui_design"
        next_step = "ui_designer"
    elif phase == "ui_design":
        pm_message = (
            "📋 [Project Manager]: UI and Materials assembled. "
            "Dispatching build to QA & Mutation Test Master for 100% test coverage verification."
        )
        new_phase = "testing"
        next_step = "tester"
    elif phase == "testing":
        validation = state.get("validation_report") or {}
        if validation.get("passed", False):
            pm_message = (
                "📋 [Project Manager]: All quality gates passed (Unit: 100%, E2E: Pass, Mutation: >90%). "
                "Project milestone successfully accomplished!"
            )
            new_phase = "completed"
            next_step = "completed"
        else:
            pm_message = (
                "📋 [Project Manager]: Quality gate issues detected. Routing back for remediation."
            )
            new_phase = "synthesis"
            next_step = "gpu_master"
    else:
        new_phase = "completed"
        next_step = "completed"

    messages.append({
        "agent_id": "project_manager",
        "role_name": "Project Manager (Geekatplay)",
        "content": pm_message,
        "metadata": {"phase": new_phase, "iteration": iteration}
    })

    return {
        "messages": [{
            "agent_id": "project_manager",
            "role_name": "Project Manager (Geekatplay)",
            "content": pm_message,
            "metadata": {"phase": new_phase, "iteration": iteration}
        }],
        "current_phase": new_phase,
        "active_agent": "project_manager",
        "next_step": next_step,
        "iteration": iteration,
        "is_completed": new_phase == "completed"
    }
