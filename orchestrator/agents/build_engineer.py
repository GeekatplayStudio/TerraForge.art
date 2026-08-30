"""
Geekatplay Studio — DevOps & Build Automation Master Node.
Automates CMake compilation, build verification, binary artifact packaging, and test harness execution.
"""

import subprocess
import shutil
import sys
from pathlib import Path
from typing import Dict, Any, Tuple
from ..state import MultiAgentState


def run_cmake_build(workspace_dir: str = ".") -> Tuple[bool, str]:
    """Configures and builds the native C++20 NodeTerrain engine using CMake."""
    ws = Path(workspace_dir).resolve()
    build_dir = ws / "build"

    cmake_bin = shutil.which("cmake") or "cmake"
    
    # 1. Configure
    cfg_cmd = [cmake_bin, "-B", str(build_dir), "-G", "MinGW Makefiles", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    cfg_res = subprocess.run(cfg_cmd, cwd=str(ws), capture_output=True, text=True, encoding="utf-8", errors="replace")
    if cfg_res.returncode != 0:
        return False, f"CMake Configure Failed:\n{cfg_res.stderr}\n{cfg_res.stdout}"

    # Copy compile_commands.json to root
    comp_json = build_dir / "compile_commands.json"
    if comp_json.exists():
        shutil.copy(comp_json, ws / "compile_commands.json")

    # 2. Build
    build_cmd = [cmake_bin, "--build", str(build_dir)]
    build_res = subprocess.run(build_cmd, cwd=str(ws), capture_output=True, text=True, encoding="utf-8", errors="replace")
    if build_res.returncode != 0:
        return False, f"CMake Build Failed:\n{build_res.stderr}\n{build_res.stdout}"

    return True, "Build Succeeded"


def run_cpp_test_suite(workspace_dir: str = ".") -> Tuple[bool, str]:
    """Runs the compiled native C++ test executable."""
    ws = Path(workspace_dir).resolve()
    test_exe = ws / "build" / "nodeterrain_tests.exe"
    if not test_exe.exists():
        test_exe = ws / "build" / "nodeterrain_tests"

    if not test_exe.exists():
        return False, f"Test executable not found at: {test_exe}"

    run_res = subprocess.run([str(test_exe)], cwd=str(ws), capture_output=True, text=True, encoding="utf-8", errors="replace")
    passed = run_res.returncode == 0
    return passed, run_res.stdout if passed else f"{run_res.stderr}\n{run_res.stdout}"


def build_engineer_node(state: MultiAgentState) -> Dict[str, Any]:
    """
    Build & DevOps Engineer agent node for LangGraph orchestrator.
    Builds the C++ binaries and executes the C++ test suite.
    """
    success, build_log = run_cmake_build()
    test_passed, test_log = run_cpp_test_suite() if success else (False, "Skipped due to build failure.")

    status_icon = "✅" if (success and test_passed) else "❌"
    msg = (
        f"⚙️ [DevOps & Build Master]: {status_icon} Automated Build & Test Pipeline Executed.\n"
        f"  • C++20 Build Status: {'SUCCESS' if success else 'FAILED'}\n"
        f"  • C++ Test Suite Status: {'PASSED (100%)' if test_passed else 'FAILED'}\n"
        f"  • Artifacts: libgeekatplay_nodeterrain.a, nodeterrain_cli.exe, nodeterrain_tests.exe\n"
        f"  • IntelliSense Database: compile_commands.json exported."
    )

    build_report = {
        "build_success": success,
        "test_passed": test_passed,
        "build_log": build_log,
        "test_log": test_log
    }

    return {
        "messages": [{
            "agent_id": "build_engineer",
            "role_name": "DevOps & Build Automation Master",
            "content": msg,
            "metadata": build_report
        }],
        "active_agent": "build_engineer",
        "next_step": "tester"
    }
