#!/usr/bin/env python3
"""Which parameters still have no tooltip, by category.

    python scripts/tooltip_gaps.py            # every category, summarised
    python scripts/tooltip_gaps.py Filter     # one category, in detail

Reads docs/node_index.json, so build and run the node_index_gen target first
if the registry has changed.
"""
import json
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX = os.path.join(ROOT, "docs", "node_index.json")


def gaps():
    d = json.load(open(INDEX, encoding="utf-8"))
    out = defaultdict(list)
    for n in d["nodes"]:
        if n["description"].startswith("[Planned]"):
            continue
        bad = [a["key"] for a in n["attrs"]
               if a["type"] != "seed" and not a.get("tooltip")]
        if bad:
            out[n["category"]].append((n["type"], bad))
    return out


def main():
    g = gaps()
    if len(sys.argv) > 1:
        cat = sys.argv[1]
        rows = g.get(cat, [])
        total = sum(len(b) for _, b in rows)
        print(f"{cat}: {len(rows)} nodes, {total} parameters")
        for t, b in sorted(rows):
            print(f"  {t:22s} {', '.join(b)}")
        return
    rows = sorted(g.items(), key=lambda kv: -sum(len(b) for _, b in kv[1]))
    total = sum(len(b) for _, v in g.items() for _, b in v)
    print(f"{sum(len(v) for v in g.values())} nodes, {total} parameters\n")
    for cat, v in rows:
        print(f"  {cat:18s} {len(v):3d} nodes  {sum(len(b) for _, b in v):4d} params")


if __name__ == "__main__":
    main()
