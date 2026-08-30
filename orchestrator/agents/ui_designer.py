"""
Geekatplay Studio — Modern UI/UX Designer Node.
Assembles the signature Geekatplay glassmorphic node graph canvas, radial palette, and micro-animations.
"""

from typing import Dict, Any
from ..state import MultiAgentState


def ui_designer_node(state: MultiAgentState) -> Dict[str, Any]:
    ui_spec = {
        "design_system": "Geekatplay Dark Glassmorphism 2026",
        "theme": {
            "background": "#07090e",
            "card_glass": "rgba(15, 23, 42, 0.75)",
            "border_glow": "rgba(56, 189, 248, 0.35)",
            "accent_cyan": "#06b6d4",
            "accent_amber": "#f59e0b",
            "accent_emerald": "#10b981",
        },
        "components": [
            "Infinite Node Canvas (ReactFlow / XYFlow engine)",
            "Radial Context Quick-Add Menu (from LogiBoard design)",
            "Natural Language & Vision Prompt Bar in Header",
            "Real-time 3D Viewport Split-Pane with PBR Shadows",
            "Bi-directional Node Inspector with Interactive Curve Editor",
            "History / WAL Snapshot Timeline with Instant Scrubbing"
        ]
    }
    
    msg = (
        "✨ [UI Designer]: Constructed Geekatplay Studio glassmorphic node interface. "
        "Configured radial node spawn menus, glowing bezier wire flows, and embedded natural language / vision bar."
    )
    
    return {
        "messages": [{
            "agent_id": "ui_designer",
            "role_name": "Modern UI/UX Designer",
            "content": msg,
            "metadata": ui_spec
        }],
        "active_agent": "ui_designer",
        "next_step": "materials_master"
    }
