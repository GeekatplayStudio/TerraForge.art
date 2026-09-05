// Geekatplay TerraForge - the command-palette vocabulary shared by the tool
// rows (toolbar_bars.cpp, toolbar_tools.cpp, gizmo_tools.cpp) and the
// viewport header: square icon buttons on the palette ladder, 2 px apart,
// with a thin 1 px rule between groups - Cinema 4D's palette, in short.
#pragma once
#include "icons.hpp"

namespace studio {

// The edge of a palette button: the glyph size the user chose plus the
// frame padding. Read every frame, so a change in Settings lands next frame.
float tool_size();

// A square icon button at tool_size(), followed by a 2 px gap on the same
// line. Active = accent fill. Returns true when clicked.
bool tool_icon(Icon ic, const char *id, const char *tip, bool active = false);

// A 1 px vertical rule between two groups of buttons, on the same line.
void tool_sep();

// A dim caption before a combo or a slider, centred on the button height.
void tool_label(const char *fmt, ...);

// A row of items is finished: the next widget continues on the same line.
inline void tool_gap(float px = 2.f) { ImGui::SameLine(0, px); }

} // namespace studio
