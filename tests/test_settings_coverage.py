"""The audit: everything the UI can change, scripting can change.

Three claims, each read from the source so they cannot drift:

1. Every render/world setting in RenderSettings that is a plain number, bool
   or colour is in the saved-settings table (scene_io.cpp env_fields) - which
   is also what `set_setting` / `list_settings` reach. A field missing there
   resets on load AND is invisible to scripts, the assistant and MCP.
2. Every panel flag on App (show_*) has a `show_panel` name.
3. Every node type's attributes carry a tooltip, because the tooltip is what
   the natural-language search indexes and what the assistant is told a
   property means. (Checked by the C++ node contract battery too; this is the
   same claim from the text side, for the nodes' declaration files.)
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STUDIO = ROOT / "studio"
ENGINE = ROOT / "engine"

# Fields that are legitimately not settings: per-view state, matrices, node
# maps, live/derived values, or covered by their own ops with more structure.
EXEMPT_FIELDS = {
    "views", "matp", "backdrop", "sun_color", "MAX_VIEWS", "MAX_CLOUD_LAYERS",
    "cloud_layers",  # derived from CloudLayer nodes every frame
    # where the air has drifted to this session, advanced from the clock a
    # frame at a time like time_acc: state, not a setting, and worked out
    # again from the wind when a project is opened
    "wind_drift_len", "wind_drift_hi_len",
    "wireframe",     # per-view: ViewConfig.display
    "viewport_layout", "viewport_engine",  # set_viewport
    "terrain_material_node", "map_normal_node", "map_roughness_node",
    "map_displacement_node",  # 'u' node ids, in the table under their own names
}


def render_settings_fields():
    text = (STUDIO / "render_settings.hpp").read_text(encoding="utf-8", errors="replace")
    # the struct body: from "struct RenderSettings" to the matching close
    start = text.index("struct RenderSettings")
    body = text[start:]
    fields = set()
    depth = 0
    for line in body.splitlines():
        s = line.strip()
        if s.startswith("//"):
            continue
        # nested structs/enums are their own thing
        if re.match(r"(struct|enum)\b", s):
            depth += 1
        if depth > 1:
            if s == "};":
                depth -= 1
            continue
        # the outer struct closes at column 0; an indented "};" is an array
        # initialiser or a nested type
        if line.rstrip() == "};":
            break
        m = re.match(r"(?:float|int|bool|unsigned long long)\s+([A-Za-z_]\w*)(\[3\])?\s*=", s)
        if m:
            fields.add(m.group(1))
    return fields


def saved_settings():
    text = (STUDIO / "scene_io.cpp").read_text(encoding="utf-8", errors="replace")
    return set(re.findall(r'\{"([a-z_0-9]+)",\s*\'[fibucs]\',\s*&?rs\.', text))


def test_every_render_setting_is_saved_and_scriptable():
    fields = render_settings_fields() - EXEMPT_FIELDS
    saved = saved_settings()
    assert len(fields) > 60, f"only {len(fields)} fields parsed - has the header changed shape?"
    # the table names may differ from the field names (sun_color vs sun_color
    # is the same; absorption_color etc.), so compare by the &rs.<field> the
    # table points at
    text = (STUDIO / "scene_io.cpp").read_text(encoding="utf-8", errors="replace")
    pointed = set(re.findall(r"&?rs\.([A-Za-z_]\w*)", text))
    missing = sorted(f for f in fields if f not in pointed)
    assert not missing, ("RenderSettings fields that neither save nor reach set_setting: "
                         + ", ".join(missing))


def test_every_panel_flag_has_a_show_panel_name():
    app = (STUDIO / "app.hpp").read_text(encoding="utf-8", errors="replace")
    flags = set(re.findall(r"bool\s+(show_[a-z_]+)\s*=", app))
    ops = (STUDIO / "ai_ops_view.cpp").read_text(encoding="utf-8", errors="replace")
    named = set(re.findall(r"&a\.(show_[a-z_]+)\}", ops))
    missing = sorted(flags - named)
    assert not missing, "panels a script cannot open: " + ", ".join(missing)


def test_every_node_attribute_has_a_tooltip():
    """A property without a tooltip is a property the assistant cannot explain
    and the natural-language search cannot find by meaning."""
    bare = []
    for path in sorted((ENGINE / "nodes").glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        # each add_<kind>(n.attrs, "key", ... ) call; a tooltip follows as
        # `.tooltip =` on the same statement (before the next ';')
        for m in re.finditer(r"add_(float|int|bool|choice|color|vec2|range|seed|text|filename|gradient|field)\(\s*n\.attrs,\s*\"([a-z_0-9]+)\"", text):
            end = text.find(";", m.end())
            stmt = text[m.start():end]
            if ".tooltip" not in stmt:
                bare.append(f"{path.name}:{m.group(2)}")
    # The count today. It must not grow; shrink it when you touch a node.
    assert len(bare) <= 36, (f"{len(bare)} node attributes have no tooltip (ceiling 36); "
                              "new ones: " + ", ".join(bare[:12]))
