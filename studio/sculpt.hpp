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
};

SculptState &sculpt_state();

// Apply one frame of brushing at terrain uv (tx, tz). dt is the frame time.
// Returns true if the terrain changed (caller requests an eval).
bool sculpt_apply(App &a, float tx, float tz, float dt);

// Called when the mouse is released: ends the stroke.
void sculpt_end_stroke(App &a);

// The strip of sculpt controls, drawn inside the viewport panel.
void sculpt_toolbar(App &a);

// Find the TerrainSculpt node strokes go into, creating one on demand wired
// after the current terrain chain. Returns 0 if the graph is empty or busy.
// Caller must hold graph_mtx.
unsigned long long sculpt_target_node(App &a);
} // namespace studio
