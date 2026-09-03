// Geekatplay TerraForge — vector icons.
//
// Drawn from primitives rather than loaded from a font or an atlas: they scale
// with the UI without a second asset to keep in step, they inherit the theme's
// ink colour so nothing is ever a stray hue, and there is no licence attached
// to any of them.
//
// Every icon is drawn inside a unit box and scaled to the requested size, so
// one set works at 14 px in a toolbar and at 24 px in a header.
#pragma once
#include <imgui.h>

namespace studio {

enum class Icon {
  Undo, Redo, Refresh, Brush, Wireframe, Grid, Sky, Water,
  Camera, Planet, Terrain, Light, Cloud, Mesh, Folder, Eye, EyeOff,
  Plus, Minus, Trash, Gear, Search, Chevron, ChevronDown, Link, Unlink,
  Save, Open,
  Move, Rotate, Scale, Material, Node, Render, Scene, World, Object,
  // viewport: projection, then shading, then overlays
  ViewPersp, ViewTop, ViewFront, ViewRight, Shaded, Textured, Outline,
  // windows: float out of the main window / dock back into it
  Detach, Dock,
  Count
};

// Paints an icon into the current window at `centre`, `size` pixels across.
void icon_draw(ImDrawList *dl, Icon ic, ImVec2 centre, float size, ImU32 col);

// An icon button. `size` is the square button edge; the glyph is inset.
// Returns true when clicked. `tip` is shown on hover — always give one, since
// an icon without a name is a puzzle.
bool IconButton(Icon ic, const char *id, const char *tip, bool active = false,
                float size = 0.f);

// Icon plus a label, for menus and lists.
bool IconMenuItem(Icon ic, const char *label, bool selected = false);

// Just the glyph, no button chrome — for table cells and tree rows.
void IconText(Icon ic, float size = 0.f, ImU32 col = 0);

} // namespace studio
