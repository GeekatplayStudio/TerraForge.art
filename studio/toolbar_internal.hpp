// Geekatplay TerraForge - the command-palette vocabulary shared by the tool
// rows (toolbar_bars.cpp, toolbar_tools.cpp), the left tool column
// (toolbar_left.cpp), gizmo_tools.cpp and the viewport header: square icon
// buttons on the palette ladder, 3 px apart, with a thin rule and clear air
// between groups - Cinema 4D's palette, in short.
#pragma once
#include "icons.hpp"

namespace studio {

// The edge of a palette button: the glyph size the user chose plus the
// frame padding. Read every frame, so a change in Settings lands next frame.
float tool_size();

// The air at the start of a row or a column, so the first button never
// touches the window edge.
void tool_pad(float px = 8.f);

// A square icon button at tool_size(), followed by a 3 px gap along the
// palette. Active = accent frame and warm fill. Returns true when clicked.
// `expandable` draws the corner triangle that marks a button holding a group
// of tools; pair it with a BeginPopupContextItem right after the call.
bool tool_icon(Icon ic, const char *id, const char *tip, bool active = false,
               bool expandable = false);

// A text button drawn as the same tile as an icon button (the resolution
// presets: a number is its own icon). Same height, width to fit.
bool tool_text(const char *label, const char *tip, bool active = false);

// A 1 px rule between two groups of buttons, with 6 px of air either side.
void tool_sep();

// A dim caption before a combo or a slider, centred on the button height.
void tool_label(const char *fmt, ...);

// A row of items is finished: the next widget continues along the palette.
void tool_gap(float px = 3.f);

// Between tool_column_begin and tool_column_end the palette stacks downward
// instead of running along a row: the same calls build the left tool column.
void tool_column_begin();
void tool_column_end();
bool tool_vertical();

// gizmo_tools.cpp: Move / Rotate / Scale; the four deformers; the Gizmos
// switch. Each is a group of palette buttons drawn wherever the caller is.
void gizmo_transform_tools();
void gizmo_space_tools();
void gizmo_deform_tools();
void gizmo_visible_tool();

} // namespace studio
