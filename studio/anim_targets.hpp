// Geekatplay TerraForge - what can be animated, by name.
//
// Every animatable property of a scene object and of the world (the
// RenderSettings the sun, sky, fog, water and clouds live in) is declared
// once here with a path, a label, a group and a component count. The
// Properties panel draws the animation circle from this table, the Timeline
// lists tracks from it, the expression language resolves names through it,
// and anim_apply() writes sampled values back through it each frame. A
// property that is not in this table cannot be keyed, which is the point:
// there is exactly one list.
//
// Track keys in SceneObject::anim / SceneState::world_anim are the path,
// plus ".x" ".y" ".z" for three-component properties ("pos.x"). Nothing
// moves unless it has a key: an object with an empty map is static.
#pragma once
#include "scene.hpp"
#include <string>
#include <vector>

namespace studio {

struct RenderSettings;

struct AnimProp {
  const char *path;  // "pos", "light.intensity"
  const char *label; // "Position", "Intensity"
  const char *group; // "Transform", "Light"
  int comps;         // 1 or 3
  bool color;        // three components labelled R G B rather than X Y Z
  bool boolean;      // a switch: keyed as 0/1, step interpolation
};

// The properties that apply to this object (by its type), in panel order.
std::vector<const AnimProp *> anim_props_for(const SceneObject &o);
const std::vector<AnimProp> &anim_world_props();
const AnimProp *anim_find_prop(const SceneObject &o, const std::string &path);
const AnimProp *anim_find_world_prop(const std::string &path);

// The map key of one component of a property.
std::string anim_key(const AnimProp &p, int comp);
// The component's live value, or null when the property does not apply.
float *anim_ptr(SceneObject &o, const AnimProp &p, int comp);
bool *anim_bool_ptr(SceneObject &o, const AnimProp &p);
float *anim_world_ptr(RenderSettings &rs, const AnimProp &p, int comp);
bool *anim_world_bool_ptr(RenderSettings &rs, const AnimProp &p);

// Track access. find returns null when there is no track; get creates one
// (with the studio's default: Bezier keys, auto tangents).
gpx::Track *anim_find(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp);
gpx::Track &anim_get(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp);
bool anim_prop_animated(const std::map<std::string, gpx::Track> &m, const AnimProp &p);
bool anim_prop_keyed_at(const std::map<std::string, gpx::Track> &m, const AnimProp &p, float t);

// Record the property's current value as a key at t (comp -1 = every
// component). Returns true if any key was added.
bool anim_record(SceneObject &o, const AnimProp &p, int comp, float t);
bool anim_record_world(RenderSettings &rs, const AnimProp &p, int comp, float t);
// Remove the key at t; when a track becomes empty it is dropped, so the
// property is static again. comp -1 = every component.
bool anim_unkey(std::map<std::string, gpx::Track> &m, const AnimProp &p, int comp, float t);
void anim_remove_track(std::map<std::string, gpx::Track> &m, const AnimProp &p);

// Write every keyed property's value for time t. Returns whether anything
// in the scene, and anything in the world, changed.
void anim_apply(SceneState &sc, RenderSettings &rs, float t, bool &scene_changed,
                bool &world_changed);

// True when the object has at least one key or expression.
bool anim_object_animated(const SceneObject &o);

// Resolve a name for the expression language: "Rock.pos.x",
// "Camera 1.cam.focal_mm", "world.sun_azimuth", "Rock.pos" (-> .x).
bool anim_lookup(const std::string &name, float &out);

// The context expressions evaluate in for the current document time.
gpx::ExprContext anim_expr_context(float t);

} // namespace studio
