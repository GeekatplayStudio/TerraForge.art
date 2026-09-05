"""Make QuadriFlow build with no Boost and no Lemon.

QuadriFlow's flow.hpp offers four max-flow solvers: Boykov (needs Boost.Graph),
network simplex (needs Lemon), Gurobi (commercial), and ECMaxFlowHelper, which
is QuadriFlow's own and needs nothing at all. Only the last one is used here.

That is the whole reason TerraForge can ship quad retopology: the licences were
never the obstacle - QuadriFlow is BSD-3 and Lemon is Boost Software License -
the obstacle was dragging Boost into a project that had no external build
dependencies. The exact solver changes, not the algorithm: EC and Boykov both
compute a maximum flow, and the maximum flow VALUE is unique. A different
optimal flow can give a slightly different quad layout on the same input, and
EC is slower on large problems.

Idempotent: run it as often as you like. Run it after cloning, before building.
"""

import re
import sys
from pathlib import Path

MARK = "// TerraForge: patched to build without Boost and Lemon"


def patch(root: Path) -> int:
    flow = root / "src" / "flow.hpp"
    if not flow.exists():
        print(f"not a QuadriFlow checkout: {flow} missing")
        return 1
    text = flow.read_text(encoding="utf-8")
    if MARK in text:
        print("QuadriFlow already patched")
        return 0

    # 1. the includes that pull the two libraries in
    text = re.sub(r"^#include <boost/[^\n]*\n", "", text, flags=re.M)
    text = re.sub(r"^#include <lemon/[^\n]*\n", "", text, flags=re.M)
    text = re.sub(r"^using namespace boost;\n", "", text, flags=re.M)

    # 2. the two solver classes that use them. Anchored on the next class in
    #    the file, which is stable because the checkout is pinned.
    def drop(a: str, b: str, why: str) -> None:
        nonlocal text
        start = text.find(a)
        end = text.find(b)
        if start < 0 or end < 0 or end <= start:
            raise SystemExit(f"cannot find the {why} block to remove")
        text = text[:start] + f"// removed: {why}\n\n" + text[end:]

    drop("class BoykovMaxFlowHelper : public MaxFlowHelper {",
         "class NetworkSimplexFlowHelper : public MaxFlowHelper {",
         "Boykov solver, which needs Boost.Graph")
    # End on the Gurobi guard, not on the class after it: the "#ifdef
    # WITH_GUROBI" line sits between the two, and removing it leaves a stray
    # "#endif" and a Gurobi class that is no longer conditional.
    drop("class NetworkSimplexFlowHelper : public MaxFlowHelper {",
         "#ifdef WITH_GUROBI",
         "network simplex solver, which needs Lemon")

    # 3. the names the rest of QuadriFlow asks for, now pointing at the solver
    #    that has no dependencies. The aliases go after ECMaxFlowHelper, which
    #    is the last class in the namespace.
    tail = "} // namespace qflow"
    idx = text.rfind(tail)
    if idx < 0:
        raise SystemExit("cannot find the end of namespace qflow")
    alias = (
        f"{MARK}\n"
        "// The two removed solvers keep their names, so optimizer.cpp needs no\n"
        "// edit; both resolve to QuadriFlow's own dependency-free max flow.\n"
        "using BoykovMaxFlowHelper = ECMaxFlowHelper;\n"
        "using NetworkSimplexFlowHelper = ECMaxFlowHelper;\n\n"
    )
    text = text[:idx] + alias + text[idx:]
    flow.write_text(text, encoding="utf-8", newline="\n")
    print(f"patched {flow}")

    # 4. Nothing to do with Boost, but in the way all the same: dedge.cpp
    #    reaches for an MSVC intrinsic on every _WIN32 build, and MinGW is
    #    _WIN32 with a GCC that has __sync_bool_compare_and_swap instead.
    #    Narrow the test to the compiler that actually has the intrinsic.
    dedge = root / "src" / "dedge.cpp"
    if dedge.exists():
        d = dedge.read_text(encoding="utf-8")
        old = "#if defined(_WIN32)"
        new = "#if defined(_WIN32) && defined(_MSC_VER)"
        if old in d and "_InterlockedCompareExchange" in d and new not in d:
            d = d.replace(old, new, 1)
            dedge.write_text(d, encoding="utf-8", newline="\n")
            print(f"patched {dedge}")
    return 0


if __name__ == "__main__":
    here = Path(__file__).resolve().parent.parent
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "external" / "quadriflow"
    raise SystemExit(patch(target))
