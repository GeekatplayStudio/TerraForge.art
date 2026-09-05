// Geekatplay TerraForge — vector icons.
//
// Drawn from primitives rather than loaded from a font or an atlas: they scale
// with the UI without a second asset to keep in step, they inherit the theme's
// ink colour so nothing is ever a stray hue, and there is no licence attached
// to any of them.
//
// The visual language is Cinema 4D's: thin, even line icons with rounded
// joins, simple filled silhouettes for object types, and the 18 / 26 / 36 px
// ladder its palettes use (small / medium / large). Every icon is authored in
// a unit box and snapped to the pixel grid at draw time, so the 18 px set is
// crisp rather than a blurred-down copy of the large one.
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
  // objects: locked against edits / free
  Lock, Unlock,
  // deformers
  Twist, Bend, Skew, Taper,
  // timeline transport and keys
  Play, Pause, Stop, ToStart, ToEnd, PrevKey, NextKey, KeyAdd, KeyRemove,
  Autokey, Loop, Marker, Curve, Timeline,
  // object manager: visibility dots, states, tags
  Dot, DotRing, Check, Cross, Layer, Tag, Filter,
  // navigation
  Home, Up,
  // environment
  Sun, Atmosphere,
  // hierarchy and generators
  Group, Null, Expression, Modifier, Bake,
  // viewport helpers
  Fit, Snap, Magnet,
  Count
};

// The palette icon sizes, small / medium / large: {18, 26, 36}.
const int *icon_size_ladder();
// The toolbar glyph size the user chose in preferences (prefs().icon_size
// indexes the ladder).
float icon_toolbar_size();

// Paints an icon into the current window at `centre`, `size` pixels across.
void icon_draw(ImDrawList *dl, Icon ic, ImVec2 centre, float size, ImU32 col);

// An icon button. `size` is the square button edge; 0 means the toolbar
// size plus the frame padding. Returns true when clicked. `tip` is shown on
// hover — always give one, since an icon without a name is a puzzle.
bool IconButton(Icon ic, const char *id, const char *tip, bool active = false,
                float size = 0.f);

// Icon plus a label, for menus and lists.
bool IconMenuItem(Icon ic, const char *label, bool selected = false);

// Just the glyph, no button chrome — for table cells and tree rows.
void IconText(Icon ic, float size = 0.f, ImU32 col = 0);

} // namespace studio
