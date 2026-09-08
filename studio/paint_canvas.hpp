// Geekatplay TerraForge — the height painter: a sculpt layer as a flat canvas.
//
// A TerrainSculpt layer is already a paintable heightfield living in the
// graph: mid grey does nothing, darker carves, lighter raises, and everything
// downstream - erosion, filters, the materials that read erosion - recomputes
// as the strokes land. What was missing was a way to paint it as an image
// rather than by brushing the terrain in perspective, which is hopeless for
// drawing a river's course or a canyon's line.
//
// So this is the same layer, the same brushes and the same live evaluation,
// shown flat and painted like a greyscale picture. Nothing here owns the
// data: the strokes go into the node's field through the sculpt brush, which
// is what keeps hand edits and the procedural chain in one graph.
#pragma once
#include <string>

namespace studio {
struct App;

struct PaintCanvasState {
  // The node being painted. 0 means "whatever the sculpt brush would pick",
  // which is a selected MaskPaint, else the existing sculpt layer, else a new
  // one spliced in before the terrain output.
  unsigned long long node = 0;
  float zoom = 1.f;       // 1 fits the canvas to the window
  // Where the view is centred, in 0..1 canvas coordinates.
  float pan_x = 0.5f, pan_y = 0.5f;
  bool show_terrain = true;   // tint the canvas with the terrain underneath
  // Whether the window was up last frame, so opening it can raise it above
  // whatever it shares a dock with.
  bool was_shown = false;
  std::string last_error;
};
PaintCanvasState &paint_canvas();

// The window. Toggled by App::show_paint_canvas.
void draw_panel_paint_canvas(App &a);

// Write the painted layer out as a 16-bit greyscale PNG, and read one back.
// Mid grey is the value that does nothing, so a height map round-trips through
// any image editor.
bool paint_canvas_save_image(App &a, const std::string &path, std::string &err);

// Tell the canvas its picture is stale - for anything that changes the layer
// from outside the panel (a script, an undo, a load).
void paint_canvas_invalidate();

// Load a greyscale picture into the painted layer, resampled to the field.
// Returns false and fills `err` if the file cannot be read.
bool paint_canvas_load_image(App &a, const std::string &path, std::string &err);

} // namespace studio
