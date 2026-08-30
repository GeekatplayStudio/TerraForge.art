"""
Geekatplay Studio — NodeTerrain Multi-Agent Command-Line Interface.
Run autonomous terrain synthesis, task delegation, and agent validation from the CLI.
"""

import sys
import json
from pathlib import Path
from .graph import MultiAgentGraph

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def main():
    print("=" * 70)
    print(" 🏔️  GEEKATPLAY STUDIO — NODETERRAIN MULTI-AGENT SUPER ORCHESTRATOR")
    print("=" * 70)
    
    if len(sys.argv) > 1:
        user_request = " ".join(sys.argv[1:])
    else:
        user_request = "Alpine glacial valley with steep granite arêtes, hanging cirque, and pine forest"

    print(f"\n🎯 [User Goal]: {user_request}")
    print("🚀 Dispatching Multi-Agent StateGraph...\n")

    graph = MultiAgentGraph()
    state = graph.run(user_request=user_request)

    print("\n" + "=" * 70)
    print(" 📜 AGENT EXECUTION LOG:")
    print("=" * 70)
    for msg in state.get("messages", []):
        print(f"\n{msg.get('content')}")

    print("\n" + "=" * 70)
    print(" 📊 SYNTHESIZED TERRAIN NODE GRAPH (JSON):")
    print("=" * 70)
    terrain_graph = state.get("terrain_graph")
    if terrain_graph:
        print(json.dumps(terrain_graph, indent=2))
        
        # Save to disk
        out_path = Path("generated_terrain_graph.json")
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(terrain_graph, f, indent=2)
        print(f"\n💾 Saved generated terrain node graph to: {out_path.resolve()}")

    print("\n" + "=" * 70)
    print(" 🛡️ QA VALIDATION REPORT:")
    print("=" * 70)
    val = state.get("validation_report", {})
    for k, v in val.items():
        print(f"  • {k}: {v}")

    print("\n✅ Multi-Agent Execution Finished Successfully.\n")


if __name__ == "__main__":
    main()
