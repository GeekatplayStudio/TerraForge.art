// Geekatplay TerraForge - state shared by the Timeline's files: the view
// (time at the left edge, pixels per second), the key selection, the
// clipboard, the drag in progress and which curves the Curve editor shows.
// One instance; the panels are the only users.
#pragma once
#include "anim_tracks.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace studio {

struct App;

struct KeySel {
  std::string track; // TrackRef::id
  float time = 0.f;  // keys are identified by time, which survives sorting
};

struct TimelineState {
  // view
  float t0 = 0.f;          // time at the left edge of the key area
  float px_per_s = 80.f;
  float area_x0 = 0.f, area_x1 = 0.f; // the key column, this frame
  float row_h = 20.f;
  bool animated_only = true;
  char filter[64] = "";
  // selection and edits
  std::vector<KeySel> sel;
  struct ClipTrack { std::string track; std::vector<gpx::Key> keys; };
  std::vector<ClipTrack> clipboard;
  bool dragging = false;       // moving selected keys
  bool drag_started = false;   // an undo step was pushed for this drag
  float drag_t0 = 0.f;         // time under the mouse when the drag began
  float drag_last_dt = 0.f;    // total shift applied so far
  bool boxing = false;         // box select
  ImVec2 box0, box1;
  int retime_handle = 0;       // 0 none, -1 left, 1 right (scaling the selection)
  float retime_pivot = 0.f, retime_ref = 0.f;
  bool scrubbing = false;
  int dragging_marker = -1;
  // curve editor
  std::vector<std::string> curves; // track ids shown
  bool curve_speed = false;
  bool curve_normalised = false;
  // the tracks listed this frame, in row order, for prev/next and box select
  std::vector<TrackRef> rows;
};

TimelineState &tl_state();

inline float tl_x_of(const TimelineState &s, float t) { return s.area_x0 + (t - s.t0) * s.px_per_s; }
inline float tl_t_of(const TimelineState &s, float x) { return s.t0 + (x - s.area_x0) / s.px_per_s; }

// Is this (track, time) selected?
bool tl_is_selected(const TimelineState &s, const std::string &track, float time);
void tl_select(TimelineState &s, const std::string &track, float time, bool add);
void tl_deselect(TimelineState &s, const std::string &track, float time);
void tl_clear_selection(TimelineState &s);

// The transport row (play, frame, range, fps, autokey, loop) and the ruler.
void tl_draw_transport(App &a);
// Ruler with ticks, the preview band, markers and the scrub area, at the
// top of the key column; height in pixels.
void tl_draw_ruler(App &a, ImVec2 p0, float width, float height);
// The current-time line over the key area.
void tl_draw_playhead(App &a, ImVec2 p0, float height);

// One track's key row in the key column: keys, hit-testing, drag start.
void tl_draw_key_row(App &a, const TrackRef &r, ImVec2 p0, float width);
// A summary row: every child's key time as a dim diamond.
void tl_draw_summary_row(App &a, const std::vector<const TrackRef *> &children, ImVec2 p0, float width);
// Called once per frame after the rows: finishes drags, box select,
// keyboard shortcuts (Delete, Ctrl+C/V, arrows).
void tl_finish_interaction(App &a);
// The right-click menu on keys and on the empty area.
void tl_key_context_menu(App &a, const TrackRef *r, float t_under_mouse);
// Apply an operation to every selected key, grouped by track.
void tl_selection_ease(App &a, gpx::anim::Ease e);
void tl_selection_interp(App &a, gpx::Interp i);
void tl_selection_delete(App &a);
void tl_selection_copy(App &a);
void tl_selection_paste(App &a, float at);
void tl_selection_mirror(App &a);
void tl_selection_snap(App &a);
void tl_fit_view(App &a, bool selection_only);

// The Curve editor window.
void draw_panel_curve_editor(App &a);

} // namespace studio
