// Geekatplay TerraForge — parameter animation: keys, curves, the timeline.
//
// The model every serious animation tool converges on (Cinema 4D's F-Curves,
// Blender's Graph Editor, After Effects' Graph Editor): a track is a sorted
// list of keys; each key has a value, an interpolation and a pair of
// tangents; the curve between two keys is a cubic Hermite; outside the keys
// an extrapolation rule applies; a stack of modifiers (noise, oscillator,
// offset, limit, smooth) post-processes the curve; and, optionally, an
// expression replaces the curve entirely. A parameter is animated when it
// owns a track with keys or an expression, and *nothing moves unless it has
// a key* — an un-animated parameter costs one empty vector.
//
// Times are seconds. The Timeline knows the frame rate and turns seconds
// into frames, timecode or seconds for display; keys snap to whole frames
// when the timeline says so. See docs/ANIMATION.md.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gpx {

// Per-key interpolation toward the NEXT key. The numeric values are part of
// the file format: 0..2 are the original three, Bezier was added later.
enum class Interp : uint8_t {
  Linear,   // straight line to the next key
  Smooth,   // smoothstep ease (legacy; a Bezier with flat tangents)
  Constant, // hold this value until the next key (a step / hold key)
  Bezier,   // cubic Hermite with the key's tangents — the editable curve
  Default = 255, // a key without its own choice: the track's `interp` applies
};

// How a key's tangents are decided.
enum class TangentMode : uint8_t {
  Auto,   // Catmull-Rom slope, clamped so a key between two equal neighbours
          // never overshoots (Blender's "auto clamped")
  User,   // dragged; in and out move together
  Broken, // dragged; in and out independent
};

// What the curve does before the first key and after the last.
enum class Extrapolate : uint8_t {
  Constant,    // hold the end value
  Linear,      // continue the end slope
  Cycle,       // repeat the keyed range
  CycleOffset, // repeat, each cycle starting where the last ended
  PingPong,    // repeat, reversing direction each cycle
};

struct Key {
  float time = 0.f;  // seconds
  float value = 0.f;
  Interp interp = Interp::Default;
  TangentMode tangent = TangentMode::Auto;
  float tan_in = 0.f;  // slope, value per second, arriving at this key
  float tan_out = 0.f; // slope leaving it (== tan_in unless Broken)
  bool operator<(const Key &o) const { return time < o.time; }
};

// A curve post-process. Fields are named generically so the struct stays one
// POD; the meaning per type is documented beside each type.
enum class ModType : uint8_t {
  Noise,      // a: amplitude, b: frequency (Hz), c: phase, octaves, seed
  Oscillator, // a: amplitude, b: frequency (Hz), c: phase, shape 0 sine 1 triangle 2 square
  Offset,     // a: value offset, b: time offset (seconds)
  Limit,      // a: minimum, b: maximum
  Smooth,     // a: window (seconds) — a box average of the curve
};
struct Modifier {
  ModType type = ModType::Noise;
  bool enabled = true;
  float a = 1.f, b = 1.f, c = 0.f;
  int shape = 0;
  int octaves = 1;
  uint32_t seed = 1;
};

// The context an expression may read: the current time and any other
// property by path ("Camera 1.focal_mm"). The studio supplies the lookup;
// the engine's tests supply a table.
struct ExprContext {
  float t = 0.f;
  float fps = 30.f;
  float value = 0.f; // the track's own curve at t, so "value * 2" works
  std::function<bool(const std::string &, float &)> lookup; // may be empty
};
// A tiny expression language: numbers, + - * / ^, parentheses, unary minus,
// variables t frame fps value pi, functions sin cos tan abs sqrt floor ceil
// round min max clamp lerp pow exp log smoothstep noise(x) , and dotted
// property paths resolved through the context. Returns false with a message
// on a syntax error; a missing path evaluates to 0 and sets the message.
bool expr_eval(const std::string &expr, const ExprContext &ctx, float &out,
               std::string *err = nullptr);

// One animated parameter. Values are floats because every animatable
// attribute reduces to one: an int or a choice rounds, a bool crosses 0.5, a
// vector or a colour is one track per component.
struct Track {
  std::vector<Key> keys;
  Interp interp = Interp::Smooth; // what a key with Interp::Default uses
  // The interpolation a key actually uses.
  Interp effective(const Key &k) const { return k.interp == Interp::Default ? interp : k.interp; }
  Extrapolate pre = Extrapolate::Constant, post = Extrapolate::Constant;
  std::vector<Modifier> modifiers;
  std::string expr; // when set, replaces the curve (modifiers still apply)

  bool empty() const { return keys.empty() && expr.empty(); }
  bool animated() const { return !empty(); }
  // Value at `t`: curve, extrapolation, expression, then modifiers.
  float sample(float t) const;
  float sample(float t, const ExprContext &ctx) const;
  // The bare curve: keys, tangents and extrapolation only.
  float curve(float t) const;
  // Set (or replace) the key at `t`. Returns true if a key was added. A new
  // key takes the track's default interpolation and Auto tangents.
  bool set_key(float t, float value);
  bool remove_key(float t);
  bool has_key_at(float t) const;
  int index_at(float t) const; // -1 when there is no key at t
  void clear() { keys.clear(); expr.clear(); modifiers.clear(); }
  // Recompute every Auto tangent from its neighbours. Called by set_key,
  // remove_key and the curve operations; call it after editing keys directly.
  void update_tangents();
  // The first and last key time, or 0 when empty.
  float first_time() const { return keys.empty() ? 0.f : keys.front().time; }
  float last_time() const { return keys.empty() ? 0.f : keys.back().time; }
};

// Serialization, kept beside the type so the format stays in one place. The
// old "interp;t,v;t,v" form still loads; the new form starts with "K2".
std::string track_to_string(const Track &t);
bool track_from_string(Track &t, const std::string &s);

// ------------------------------------------------------------------ editing
// Curve operations on a selection of key indices (anim_curve.cpp). Every one
// leaves the track sorted with tangents updated.
namespace anim {
enum class Ease { In, Out, InOut, Easy, Linear, Hold };
void set_interp(Track &t, const std::vector<int> &sel, Interp i);
void set_ease(Track &t, const std::vector<int> &sel, Ease e);
void set_tangent_mode(Track &t, const std::vector<int> &sel, TangentMode m);
// Move keys in time and value; keys that would land on another are merged.
void move_keys(Track &t, std::vector<int> &sel, float dt, float dv);
// Scale the selection's times about `pivot` (retime); factor 2 = twice as slow.
void retime(Track &t, std::vector<int> &sel, float pivot, float factor);
// Reverse the selection in time about its own centre.
void mirror(Track &t, std::vector<int> &sel);
// Average each selected key's tangents with its neighbours' slopes.
void smooth(Track &t, const std::vector<int> &sel);
// Flat tangents on the selection.
void flatten(Track &t, const std::vector<int> &sel);
// One Linear key per frame between the first and last key (modifiers and
// expression baked in, then removed).
void bake(Track &t, float fps);
// Remove keys whose absence changes the curve by less than `tolerance`
// (Ramer–Douglas–Peucker on the sampled curve).
void simplify(Track &t, float tolerance, float fps);
// Snap every key to the nearest whole frame; keys that collide merge.
void snap_to_frames(Track &t, float fps);
// Copy of the selected keys, shifted so the first sits at 0.
std::vector<Key> copy_keys(const Track &t, const std::vector<int> &sel);
// Paste keys at `at` (replacing keys that land on the same frames).
void paste_keys(Track &t, const std::vector<Key> &keys, float at);
} // namespace anim

// ----------------------------------------------------------------- timeline
struct Marker {
  float time = 0.f;
  std::string name;
};
enum class TimeDisplay : uint8_t { Frames, Timecode, Seconds };
enum class LoopMode : uint8_t { Once, Loop, PingPong };

struct Timeline {
  float fps = 30.f;
  float start = 0.f, end = 10.f; // seconds
  bool preview = false;          // play and render the preview range instead
  float preview_start = 0.f, preview_end = 10.f;
  TimeDisplay display = TimeDisplay::Frames;
  LoopMode loop = LoopMode::Loop;
  bool autokey = false;    // an edit to an animated property writes a key
  bool all_frames = false; // play every frame rather than keep real time
  bool snap = true;        // keys land on whole frames
  std::vector<Marker> markers;

  float frame_of(float t) const { return t * fps; }
  float time_of(float frame) const { return frame / fps; }
  float snap_time(float t) const; // to the nearest frame when snap is on
  float play_start() const { return preview ? preview_start : start; }
  float play_end() const { return preview ? preview_end : end; }
  // "48", "00:00:01:18" or "1.600 s" per the display mode.
  std::string format(float t) const;
  // Parse what format() writes (and plain numbers as frames).
  bool parse(const std::string &s, float &t) const;
};

} // namespace gpx
