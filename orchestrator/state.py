"""
Geekatplay Studio — NodeTerrain Multi-Agent State Definition.
Defines the shared LangGraph state schema across all 13 specialized agent personas.
"""

from typing import Dict, List, Optional, Any, TypedDict, Annotated
import operator
from pydantic import BaseModel, Field


class AgentMessage(BaseModel):
    agent_id: str
    role_name: str
    content: str
    timestamp: Optional[str] = None
    metadata: Dict[str, Any] = Field(default_factory=dict)


class TerrainNodeSpec(BaseModel):
    id: str
    type: str  # e.g., "perlin_noise", "hydraulic_erosion", "terrace", "strata", "splatmap"
    category: str  # "generator", "filter", "erosion", "geology", "material", "output"
    position: Dict[str, float] = Field(default_factory=lambda: {"x": 0.0, "y": 0.0})
    params: Dict[str, Any] = Field(default_factory=dict)


class TerrainEdgeSpec(BaseModel):
    id: str
    source_node: str
    source_port: str
    target_node: str
    target_port: str


class TerrainGraphModel(BaseModel):
    name: str = "Geekatplay_Terrain"
    version: str = "1.0.0"
    resolution: int = 2048
    nodes: List[TerrainNodeSpec] = Field(default_factory=list)
    edges: List[TerrainEdgeSpec] = Field(default_factory=list)
    metadata: Dict[str, Any] = Field(default_factory=dict)


class ValidationReport(BaseModel):
    passed: bool = True
    unit_test_coverage: float = 100.0
    integration_passed: bool = True
    mutation_score: float = 95.0
    issues: List[str] = Field(default_factory=list)
    suggestions: List[str] = Field(default_factory=list)


class MultiAgentState(TypedDict):
    """LangGraph multi-agent shared state dictionary."""
    user_request: str
    input_image_path: Optional[str]
    current_phase: str  # "planning", "architecting", "synthesis", "ui_design", "testing", "completed"
    active_agent: str
    iteration: int
    messages: Annotated[List[Dict[str, Any]], operator.add]
    terrain_graph: Optional[Dict[str, Any]]
    system_plan: Optional[Dict[str, Any]]
    validation_report: Optional[Dict[str, Any]]
    next_step: Optional[str]
    approval_required: bool
    is_completed: bool
