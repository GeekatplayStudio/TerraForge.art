"""
Geekatplay Studio — 3D Geologist & Terrain Realist Node.
Analyzes terrain prompts/images for geomorphology, drainage networks, rock strata, and erosion dynamics.
"""

from typing import Dict, Any
from ..state import MultiAgentState


def geologist_node(state: MultiAgentState) -> Dict[str, Any]:
    user_req = state.get("user_request", "").lower()
    has_image = bool(state.get("input_image_path"))
    
    # Geomorphological inference
    biome = "Alpine Mountain Range"
    erosion_types = ["Hydraulic Drainage (Stream Power)", "Thermal Talus Scree (34° Repose)", "Glacial Cirque"]
    rock_strata = "Sedimentary with Hard Granite Caprock"
    
    if "volcan" in user_req or "caldera" in user_req:
        biome = "Volcanic Caldera & Basalt Flow"
        erosion_types = ["Radial Fluvial Rills", "Lava Channel Incision", "Ash Deposition"]
        rock_strata = "Layered Basalt & Pyroclastic Tephra"
    elif "canyon" in user_req or "desert" in user_req or "mesa" in user_req:
        biome = "Arid Plateau & Mesa Canyon"
        erosion_types = ["Differential Strata Weathering", "Flash Flood Sapping", "Wind Deflation"]
        rock_strata = "Multi-tiered Sandstone & Shale Strata"
    elif "island" in user_req or "coast" in user_req:
        biome = "Coastal Arch & Wave-Cut Cliffs"
        erosion_types = ["Marine Wave Sapping", "Sub-aerial Gullying", "Tidal Alluvial Deposition"]
        rock_strata = "Limestone & Karstic Marine Formations"

    geo_analysis = {
        "inferred_biome": biome,
        "primary_rock_strata": rock_strata,
        "recommended_erosion_pipeline": erosion_types,
        "slope_equilibrium_angle": 34.5,
        "drainage_density_factor": 0.78,
    }

    msg = (
        f"🏔️ [3D Geologist]: Geomorphological analysis complete for biome '{biome}'. "
        f"Recommended rock strata: {rock_strata}. Configured natural erosion models: {', '.join(erosion_types)}."
    )

    return {
        "messages": [{
            "agent_id": "geologist",
            "role_name": "3D Geologist & Terrain Artist",
            "content": msg,
            "metadata": geo_analysis
        }],
        "active_agent": "geologist",
        "next_step": "gpu_master"
    }
