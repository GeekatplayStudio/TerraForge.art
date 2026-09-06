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
  // animation: a key diamond (the animate toggle beside a property), a
  // sculpt brush set, and the view arrangement
  Key, Raise, Flatten, Smooth, Terrace, Noise, Erase, Console, Views,
  // the universal gizmo: arrows and a ring in one
  Transform,
  Count
};

// Cinema 4D's colour code, which is functional rather than decorative:
// blue for geometry, green for generators, purple for deformers, orange
// for tools and systems, yellow for lights, teal for cameras and views,
// red for record and delete, and the plain ink for everything neutral.
// The whole application reads the same code, so a purple icon is always
// a deformer wherever it appears.
namespace icon_hue {
ImU32 geometry();
ImU32 generator();
ImU32 deformer();
ImU32 tool();
ImU32 light();
ImU32 camera();
ImU32 sky();
ImU32 record();
ImU32 neutral();
} // namespace icon_hue

// The functional colour of an icon, per the code above.
ImU32 icon_color(Icon ic);

// The palette icon sizes, small / medium / large: {18, 26, 36}.
const int *icon_size_ladder();
// The toolbar glyph size the user chose in preferences (prefs().icon_size
// indexes the ladder).
float icon_toolbar_size();

// Paints an icon into the current window at `centre`, `size` pixels across,
// in one colour.
void icon_draw(ImDrawList *dl, Icon ic, ImVec2 centre, float size, ImU32 col);
// The same, in two tones: `base` for the lines and `hue` for the part that
// carries the meaning. Pass icon_color(ic) for the code colour.
void icon_draw_tinted(ImDrawList *dl, Icon ic, ImVec2 centre, float size,
                      ImU32 base, ImU32 hue);

// An icon button, drawn as Cinema 4D draws a palette button: a raised tile
// a shade lighter than the panel, the glyph in its functional colour, and -
// when the tool is on - an accent frame and a warm fill behind it. The
// glyph keeps its colour in every state, so the code stays readable.
// `size` is the square button edge; 0 means the toolbar size plus the frame
// padding. Returns true when clicked. `tip` is shown on hover — always give
// one, since an icon without a name is a puzzle.
bool IconButton(Icon ic, const char *id, const char *tip, bool active = false,
                float size = 0.f);

// Icon plus a label, for menus and lists.
bool IconMenuItem(Icon ic, const char *label, bool selected = false);

// Just the glyph, no button chrome — for table cells and tree rows.
void IconText(Icon ic, float size = 0.f, ImU32 col = 0);

} // namespace studio
