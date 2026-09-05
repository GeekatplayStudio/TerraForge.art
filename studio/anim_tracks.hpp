// Geekatplay TerraForge - every animation track in the document, addressed
// one way. Scene objects keep tracks in SceneObject::anim, the world in
// SceneState::world_anim, and graph nodes in each Attribute; the Timeline,
// the Curve editor and the API do not want three code paths, so this
// header names a track by a stable id string and resolves it to the live
// gpx::Track each frame.
//
//   "o:<object index>:<key>"      an object's property component
//   "w:<key>"                     a world property component
//   "n:<node id>:<attr key>[:c]"  a node attribute (component c for vectors)
#pragma once
#include "anim_targets.hpp"
#include "gpx/animation.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace studio {

struct App;

struct TrackRef {
  enum Kind : uint8_t { Object, World, Node } kind = Object;
  std::string id;      // the stable id above
  std::string owner;   // "Rock", "World", "Noise 3"
  std::string group;   // "Transform", "Sun", "Noise"
  std::string label;   // "Position", "Frequency"
  int comp = -1;       // -1 scalar, 0..2 X/Y/Z (or R/G/B when `color`)
  bool color = false;
  int object = -1;              // Object: scene index
  const AnimProp *prop = nullptr; // Object / World
  uint64_t node = 0;            // Node: id
  std::string attr;             // Node: attribute key
  // Resolved for this frame; may be null when the track was removed.
  gpx::Track *track = nullptr;
};

// Every track in the document. animated_only skips properties with no
// keys; otherwise every keyable property of `object` (or of the selection
// when object < 0) is listed so a key can be added from the timeline.
std::vector<TrackRef> anim_collect(App &a, bool animated_only, int object = -1);
// Resolve an id to its live track, or null. Creates nothing.
gpx::Track *anim_resolve(App &a, const std::string &id, TrackRef *out = nullptr);
// The live value the track drives (for "record a key here"), or false.
bool anim_current_value(App &a, const TrackRef &r, float &v);
// After a track of `r` changed: mark the node dirty / bump the scene.
void anim_touched(App &a, const TrackRef &r);
// Axis colours: X red, Y green, Z blue; scalars the accent.
unsigned anim_comp_color(int comp, bool color);

} // namespace studio
