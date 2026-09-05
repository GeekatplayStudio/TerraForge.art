// Geekatplay TerraForge - the animation circle and the keyframe navigator:
// the two controls that let a property be animated from where it is
// edited, without opening the Timeline (Cinema 4D's Attribute Manager
// circle; Resolve's keyframe navigator).
//
//   anim_circle   - a small circle before a property label. Empty: static.
//                   Ring: animated, no key on this frame. Filled: a key
//                   here. Click adds/removes the key; Ctrl adds, Shift
//                   removes, Ctrl+Shift removes the whole track. Right-click
//                   opens the property's animation menu.
//   anim_key_nav  - < o > : previous key, add/remove, next key.
#pragma once
#include "anim_targets.hpp"
#include "gpx/node_graph.hpp"

namespace studio {

struct App;

// Draw the circle for an object property on the current line; returns true
// when a key was added or removed (the caller may want to mark the scene).
bool anim_circle(App &a, SceneObject &o, const AnimProp &p, int comp = -1);
bool anim_circle_world(App &a, const AnimProp &p, int comp = -1);
// The circle for a node attribute (comp -1 scalar, else the component).
bool anim_circle_node(App &a, gpx::Node &n, gpx::Attribute &at, int comp = -1);

// Called by a property widget after ImGui reports an edit: with Autokey on
// and the property animated, the new value is keyed at the current time.
void anim_autokey(App &a, SceneObject &o, const AnimProp &p, int comp = -1);
void anim_autokey_world(App &a, const AnimProp &p, int comp = -1);

// The navigator for the whole document (previous/next over every track).
void anim_key_nav(App &a);
// Key the selected object's transform at the current time (the K shortcut).
void anim_key_selection_transform(App &a);

// Move the clock: set, and re-evaluate what depends on it.
void anim_set_time(App &a, float t);
// Previous/next key time over every animated track (false when none).
bool anim_prev_key_time(App &a, float from, float &out);
bool anim_next_key_time(App &a, float from, float &out);

} // namespace studio
