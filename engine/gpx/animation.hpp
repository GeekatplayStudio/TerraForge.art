// Geekatplay TerraForge — parameter animation (P0.4 hooks, P7 timeline).
//
// Terragen puts an animation button next to essentially every setting (guide
// p15): set a key, remove a key, delete the animation, import a curve. Vue is
// the same. The timeline itself is phase P7, but the *hook* has to exist on
// every parameter now — retrofitting it after terrain, materials, lighting,
// atmosphere, clouds and render would mean touching every node a second time.
//
// The model is deliberately small: a parameter is animated when it owns a
// track, and a track is a sorted list of keys with an interpolation mode. An
// un-animated parameter costs one empty vector.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpx {

enum class Interp : uint8_t {
  Linear,   // straight lines between keys
  Smooth,   // eased in and out — the sane default for camera and light moves
  Constant, // hold each value until the next key (steps)
};

struct Key {
  float time = 0.f; // seconds
  float value = 0.f;
  bool operator<(const Key &o) const { return time < o.time; }
};

// One animated parameter. Values are floats because every animatable attribute
// reduces to one: an int or a choice rounds, a bool crosses 0.5, a vector is
// three tracks.
struct Track {
  std::vector<Key> keys;
  Interp interp = Interp::Smooth;

  bool empty() const { return keys.empty(); }
  // Value at `t`. Before the first key holds the first value, after the last
  // holds the last — an animation should never fall off a cliff at its edges.
  float sample(float t) const;
  // Set (or replace) the key at `t`. Returns true if a key was added.
  bool set_key(float t, float value);
  // Remove the key at `t` (within a small tolerance). Returns true if removed.
  bool remove_key(float t);
  bool has_key_at(float t) const;
  void clear() { keys.clear(); }
};

// Serialization helpers, kept beside the type so the format stays in one place.
std::string track_to_string(const Track &t);
bool track_from_string(Track &t, const std::string &s);

} // namespace gpx
