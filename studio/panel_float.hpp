// Geekatplay TerraForge — windows that leave the main window and come back.
//
// Every panel is a dockable ImGui window, and with platform viewports on it
// can already be dragged out onto another monitor. What was missing was a
// button: one click to float a panel out (onto a second screen when there
// is one), one click to dock it back into the main layout, without having
// to find a drop target with the mouse.
#pragma once

namespace studio {
struct App;

// Call immediately before the panel's ImGui::Begin(name): applies a pending
// float or dock request for that window.
void panel_float_prepare(App &a, const char *name);

// Call right after a successful Begin(name): draws the float/dock button in
// the window's top-right corner and records the request for the next frame.
// Leaves the cursor where it found it, so the panel's own first row is not
// disturbed; that row should leave the last ~30 px free.
void panel_float_controls(App &a, const char *name);

} // namespace studio
