// Geekatplay TerraForge — terrain sculpting.
//
// Brushes paint into the `delta` field of a TerrainSculpt node, so hand edits
// live in the graph as their own layer (the way Vue's "User Touch-up" node
// works) instead of destroying the procedural chain under them.
#pragma once

namespace studio {
struct App;

enum class SculptTool {
  Raise = 0, // add/dig relief
  Flatten,   // pull toward the height under the first click
  Smooth,    // relax the sculpted layer plus local terrain shape
  Terrace,   // quantize altitudes under the brush
  Noise,     // stamp fractal detail
  Erase,     // remove sculpted strokes only
  // Paint a chosen value, the way a heightfield is painted in an image
  // editor: mid grey is the layer doing nothing, black is the deepest
  // carve, white the highest rise. Appended last so saved tool indices
  // keep meaning what they meant.
  Shade,
};

struct SculptState {
  bool active = false;        // sculpt mode on/off (viewport toolbar)
  SculptTool tool = SculptTool::Raise;
  float radius = 0.08f;       // fraction of terrain width
  float flow = 0.5f;          // strength per second of brushing
  float falloff = 2.f;        // edge hardness of the brush profile
  bool invert = false;        // Raise digs, etc. (also held Alt)
  bool stroking = false;      // mouse currently down on the terrain
  float flatten_target = 0.f; // captured on stroke start
  bool have_target = false;
  // Shade: the grey being painted, 0 black .. 1 white, with 0.5 the value
  // that leaves the terrain alone. It is stored as a fraction of the field's
  // own range rather than in field units, so the same swatch means the same
  // thing whether the layer runs -1..1 (a sculpt) or 0..1 (a mask).
  float shade = 0.75f;
};

SculptState &sculpt_state();

// Apply one frame of brushing at terrain uv (tx, tz). dt is the frame time.
// Returns true if the terrain changed (caller requests an eval).
bool sculpt_apply(App &a, float tx, float tz, float dt);

// The same, along the segment the pointer covered this frame. Prefer it
// wherever the previous position is known: it stamps the whole segment under
// one lock with one dirty mark and one evaluation request, where calling
// sculpt_apply per point pays all three per dab.
bool sculpt_apply_segment(App &a, float u0, float v0, float u1, float v1,
                          float dt);

// Called when the mouse is released: ends the stroke.
void sculpt_end_stroke(App &a);

// Switch sculpt mode on or off. On makes sure the TerrainSculpt layer
// exists so the first stroke lands instantly.
void sculpt_set_active(App &a, bool on);

// The brush's size, strength and edge, and the invert switch: the settings
// row for the Terrain workflow while sculpt mode is on (toolbar_tools.cpp).
// The brushes themselves are palette buttons in the left tool column.
void sculpt_params_row(App &a);

// Find the TerrainSculpt node strokes go into, creating one on demand wired
// after the current terrain chain. Returns 0 if the graph is empty or busy.
// Caller must hold graph_mtx.
unsigned long long sculpt_target_node(App &a);
} // namespace studio
