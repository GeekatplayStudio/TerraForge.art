#!/usr/bin/env python3
"""Attach a tooltip to a node parameter, by key, without hand-matching text.

The node audit's biggest finding is parameters with no tooltip. Writing them
by exact-match patching does not scale: an `add_float` call may be on one
line or three, may or may not carry a group argument, and may already end in
`.tooltip`. Matching all that by hand fails about a third of the time and the
failure is a silent no-op or a mangled literal.

So this finds the call by its *key* - the one thing that is unambiguous - and
appends the tooltip to it, formatted as the concatenated C++ literals the
codebase uses. It refuses to touch a parameter that already has one.

    python scripts/add_tooltip.py engine/nodes/nodes_filters.cpp \
        radius "How far the blur reaches, as a fraction of the tile."

Or, for many at once, as a module:

    from add_tooltip import add_tooltips
    add_tooltips("engine/nodes/nodes_filters.cpp", {"radius": "...", ...})
"""
import re
import sys
import textwrap

# A tooltip line is indented to sit under `.tooltip = `, which is where the
# rest of the codebase puts it.
WRAP = 58


def _format(text, indent):
    """The C++ concatenated-literal form of a tooltip."""
    lines = textwrap.wrap(" ".join(text.split()), WRAP)
    pad = " " * (indent + 4)
    out = []
    for i, ln in enumerate(lines):
        esc = ln.replace("\\", "\\\\").replace('"', '\\"')
        nl = "\\n" if i + 1 < len(lines) else ""
        head = pad + ".tooltip = " if i == 0 else pad + " " * 11
        out.append(head + '"' + esc + nl + '"')
    return "\n".join(out) + ";"


def _node_span(src, node):
    """The character range of one REGISTER_NODE block.

    Several nodes in one file often share a parameter name - `strength`,
    `radius`, `amount` - so searching the whole file finds whichever comes
    first and silently leaves the others. Scoping to the node is the only way
    to be sure which one is being described.
    """
    m = re.search(r"REGISTER_NODE\(\s*\n?\s*" + re.escape(node) + r"\s*,", src)
    if not m:
        raise SystemExit(f"no REGISTER_NODE for {node}")
    nxt = src.find("REGISTER_NODE(", m.end())
    return m.start(), len(src) if nxt < 0 else nxt


def _find_call(src, key, span=None):
    """(start, end_of_call, indent) for the add_* call declaring `key`.

    `end_of_call` is the index just past the closing paren of the call, so the
    caller can see whether a `.tooltip` already follows it.
    """
    lo, hi = span if span else (0, len(src))
    # Two declaration styles in the codebase, both "call(attrset, "key", ...)":
    # the node files use add_float / add_choice / ... on `n.attrs`, and
    # engine/material_params.cpp uses one-letter helpers f/i/b/c on a bare
    # `a`. Matching the shape rather than the name covers both, and the key
    # in quotes is what makes it unambiguous either way.
    m = re.compile(r'^([ \t]*)(?:add_\w+|[a-z])\(\s*\w+(?:\.attrs)?\s*,\s*"'
                   + re.escape(key) + r'"', re.M).search(src, lo, hi)
    if not m:
        return None
    start, indent = m.start(), len(m.group(1))
    # walk to the matching close paren of the add_ call
    i = src.index("(", m.start())
    depth, in_str = 0, False
    while i < len(src):
        c = src[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return start, i + 1, indent
        i += 1
    return None


def add_tooltips(path, tips, quiet=False, node=None):
    """tips: {key: text}. `node` scopes the search to one REGISTER_NODE block,
    which is required whenever a key appears on more than one node in the same
    file. Returns the number actually added."""
    src = open(path, encoding="utf-8").read()
    added, skipped, missing = 0, [], []
    for key, text in tips.items():
        # recomputed each time: an insertion shifts everything after it
        span = _node_span(src, node) if node else None
        found = _find_call(src, key, span)
        if not found:
            missing.append(key)
            continue
        _, end, indent = found
        after = src[end:end + 40].lstrip()
        if after.startswith(".tooltip"):
            skipped.append(key)
            continue
        # the call ends with `);` (or `)` then a newline); replace the `;`
        tail = src[end:]
        semi = tail.index(";")
        if tail[:semi].strip():
            missing.append(key)  # something unexpected between ) and ;
            continue
        src = src[:end] + "\n" + _format(text, indent) + tail[semi + 1:]
        added += 1
    open(path, "w", encoding="utf-8", newline="").write(src)
    if not quiet:
        print(f"{path}: +{added}"
              + (f", already had {len(skipped)}" if skipped else "")
              + (f", NOT FOUND {missing}" if missing else ""))
    if missing:
        raise SystemExit(f"{path}: no add_ call for {missing}")
    return added


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    add_tooltips(sys.argv[1], {sys.argv[2]: sys.argv[3]})
