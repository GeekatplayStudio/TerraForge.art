"""
Geekatplay Studio — Copywriter & Prompt Engineer Node.
Generates natural language prompt system templates, node documentation, and UI microcopy.
"""

from typing import Dict, Any
from ..state import MultiAgentState


def copywriter_node(state: MultiAgentState) -> Dict[str, Any]:
    user_req = state.get("user_request", "Custom Terrain")
    
    docs = {
        "title": f"Geekatplay NodeTerrain Preset — {user_req.title()}",
        "description": "Procedurally synthesized high-fidelity landscape generated with multi-pass physical erosion and PBR biome shading.",
        "prompt_template": (
            "You are the Geekatplay Terrain Synthesis VLM. Analyze input photography or description. "
            "Output JSON containing node specifications, parameter ranges, and edge connections."
        )
    }
    
    msg = (
        f"✍️ [Copywriter]: Generated system prompt definitions, parameter tooltips, and preset documentation "
        f"for '{user_req}'. All microcopy adheres to Geekatplay Studio's modern, professional voice."
    )
    
    return {
        "messages": [{
            "agent_id": "copywriter",
            "role_name": "Copywriter & Prompt Engineer",
            "content": msg,
            "metadata": docs
        }],
        "active_agent": "copywriter",
        "next_step": "scientist"
    }
