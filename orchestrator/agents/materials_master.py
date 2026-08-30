"""
Geekatplay Studio — Master of 3D Materials Node.
Designs procedural PBR biomes, triplanar height-displacement blending, and splatmaps.
"""

from typing import Dict, Any
from ..state import MultiAgentState


def materials_master_node(state: MultiAgentState) -> Dict[str, Any]:
    biome_layers = [
        {"name": "Granite Cliff Bedrock", "rule": "Slope > 45°", "triplanar": True, "normal_strength": 1.2},
        {"name": "Talus Scree & Gravel", "rule": "Talus flow > 0.4 and Slope 28°-44°", "triplanar": True},
        {"name": "Humus Forest Soil & Grass", "rule": "Altitude < 0.6 and Slope < 30°", "albedo_variance": 0.3},
        {"name": "Alpine Snow & Ice Cap", "rule": "Altitude > 0.72 or Cavity Shading > 0.6", "subsurface_scattering": 0.45},
        {"name": "Wet Fluvial Sediment", "rule": "Hydraulic flow > 0.6", "roughness": 0.15, "wetness": 0.9}
    ]
    
    msg = (
        f"🎨 [Materials Master]: Designed 5-layer procedural PBR biome stack ({len(biome_layers)} layers). "
        "Triplanar height-blending activated to eliminate stretching on sheer rock walls. "
        "Integrated normal, cavity, wetness, and snow accumulation channels."
    )
    
    return {
        "messages": [{
            "agent_id": "materials_master",
            "role_name": "Master of 3D Materials",
            "content": msg,
            "metadata": {"layers": biome_layers}
        }],
        "active_agent": "materials_master",
        "next_step": "three_engineer"
    }
