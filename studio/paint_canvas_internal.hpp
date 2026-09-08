// Geekatplay TerraForge - what the height painter's two halves share.
//
// The panel (paint_canvas.cpp) owns the window, the canvas and the input; the
// tools (paint_canvas_tools.cpp) own the toolbar and reading and writing the
// layer as a file. Both need the layer's field and the canvas texture, and
// nothing else does, so they meet here rather than in the public header.
#pragma once
#include "gpx/node_graph.hpp"
#include <cstdint>
#include <vector>

namespace studio {
struct App;
struct PaintCanvasState;

// The canvas texture. Rebuilt only when the painted layer can have changed -
// a stroke, a different node, or an evaluation (which is what an undo, a load
// and a script all end in) - because a 512 canvas is a megabyte a time.
struct CanvasTex {
  unsigned tex = 0;
  int w = 0, h = 0;
  uint64_t node = 0;
  uint64_t seen_eval = ~0ull;
  bool dirty = true;
  // The part of the picture a stroke has just changed, in texels, or empty.
  // A stroke covers a circle; rebuilding and re-uploading the whole canvas
  // for it is a megabyte a frame to redraw a few thousand pixels.
  int rx0 = 0, ry0 = 0, rx1 = -1, ry1 = -1;
  // A copy of the layer, so the readout under the canvas can name the value
  // beneath the pointer without holding the graph while it does it. Taken
  // when the picture is, which is the only time it can have changed.
  std::vector<float> value;
  bool has_rect() const { return rx1 >= rx0 && ry1 >= ry0; }
  void clear_rect() { rx0 = ry0 = 0; rx1 = ry1 = -1; }
};
CanvasTex &canvas_tex();

// The layer's own field, whichever kind of node it belongs to.
gpx::Attribute *layer_field(gpx::Node *n);

// The toolbar rows: tools, brush numbers, the shade ramp, and the layer row
// with its file buttons. Drawn from the panel, above the canvas.
void paint_toolbar(App &a, PaintCanvasState &C, const std::string &layer_name,
                   uint64_t layer_id);

} // namespace studio
