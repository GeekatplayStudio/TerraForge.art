// Geekatplay TerraForge - the bands of air, as things in the scene
// (scene.hpp: SceneObject::AirLayer).
//
// A sky is not one cloud deck and one fog. It is low stratus under cumulus
// under a cirrus veil, with haze to the horizon, a fog lying in the valley
// and a brown layer over the town. Every one of those is a band with its own
// height, thickness, colour and drift, and they are all in the sky at once.
//
// The rendering has taken as many as eight of each for a while - the sky pass
// marches them far to near and the surfaces sum their optical depths - but
// the only way to make one was to add a node in the graph, which is not where
// anybody looks for the sky. So a band is a thing in the scene now: a child
// of an Atmosphere, listed under it in the Objects tree, added and removed
// there like any other object.
//
// An Atmosphere may itself be a child of a Planet, and a band under it
// belongs to that planet's sky rather than to the world the camera stands on.
// That is the whole reason for making them children rather than a list on a
// panel: the tree already says what belongs to what.
//
// The numbers live on the graph node that drives the object, as every driven
// object's do - the object is the handle, the node is the truth, and editing
// either moves the other. Deleting the object leaves the node, which is the
// rule for every driven object in the scene.
#include "app.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <string>

namespace studio {

namespace {

// The Atmosphere a new band should hang under: the one selected, the one
// whose child is selected, or the scene's own.
int atmosphere_for(const SceneState &s, int selected) {
  if (selected >= 0 && selected < (int)s.objects.size()) {
    const SceneObject &o = s.objects[(size_t)selected];
    if (o.type == SceneObject::Atmosphere) return selected;
    if (o.type == SceneObject::AirLayer && o.parent >= 0) return o.parent;
    // a planet: its own atmosphere, if it has one
    if (o.type == SceneObject::Planet)
      for (int i = 0; i < (int)s.objects.size(); ++i)
        if (s.objects[(size_t)i].type == SceneObject::Atmosphere &&
            s.objects[(size_t)i].parent == selected)
          return i;
  }
  for (int i = 0; i < (int)s.objects.size(); ++i)
    if (s.objects[(size_t)i].type == SceneObject::Atmosphere && s.objects[(size_t)i].parent < 0)
      return i;
  return -1;
}

} // namespace

std::vector<int> scene_air_layers(int atmosphere_idx) {
  std::vector<int> out;
  const SceneState &s = scene();
  for (int i = 0; i < (int)s.objects.size(); ++i) {
    const SceneObject &o = s.objects[(size_t)i];
    if (o.type != SceneObject::AirLayer) continue;
    if (atmosphere_idx >= 0 && o.parent != atmosphere_idx) continue;
    out.push_back(i);
  }
  return out;
}

// The graph lock is the caller's to hold, not this function's.
//
// An operation coming in over the API already holds it - the whole dispatch
// runs under it - and a mutex that is not recursive cannot be taken twice on
// one thread. Taking it here made every scripted add fail with "is there an
// atmosphere to hang it under?", which is a true statement about a lock and a
// misleading one about the scene. So there are two: the panel takes the lock
// and calls through, the operation calls straight in.
int scene_add_air_layer_locked(App &a, int kind, int atmosphere_idx, const std::string &name) {
  SceneState &s = scene();
  const int parent = atmosphere_idx >= 0 ? atmosphere_idx : atmosphere_for(s, s.selected);
  const bool fog = kind == (int)SceneObject::AirLayerData::Fog;

  // the node that holds what the band actually is
  gpx::Node *n = nullptr;
  {
    float x = 120.f, y = 120.f;
    for (auto &np : a.graph.nodes) x = std::max(x, np->pos_x + 240.f);
    n = a.graph.add_node(fog ? "FogLayer" : "CloudLayer", x, y);
    if (!n) return -1;
    // Somewhere of its own to sit. A new band on top of the ones already
    // there, rather than inside them: two decks at one height is a mistake
    // nobody makes on purpose, and it is invisible until the march produces
    // mush.
    const int above = (int)scene_air_layers(parent).size();
    if (fog) {
      if (gpx::Attribute *at = n->attrs.find("level")) at->f = 0.08f + 0.22f * (float)above;
      if (gpx::Attribute *at = n->attrs.find("density")) at->f = above ? 0.35f : 0.7f;
      if (gpx::Attribute *at = n->attrs.find("type")) at->i = above ? 1 : 2; // fog low, haze above
    } else {
      if (gpx::Attribute *at = n->attrs.find("altitude")) at->f = 1.4f + 0.9f * (float)above;
      if (gpx::Attribute *at = n->attrs.find("thickness")) at->f = above ? 0.3f : 0.8f;
      if (gpx::Attribute *at = n->attrs.find("coverage")) at->f = above ? 0.35f : 0.55f;
      if (gpx::Attribute *at = n->attrs.find("type")) at->i = above ? 0 : 1; // cumulus, then a veil
    }
    a.graph.mark_dirty(n->id);
  }

  SceneObject o;
  o.type = SceneObject::AirLayer;
  o.air.kind = fog ? SceneObject::AirLayerData::Fog : SceneObject::AirLayerData::Cloud;
  o.parent = parent;
  o.driver_node = n->id;
  const int nth = (int)scene_air_layers(parent).size() + 1;
  o.name = !name.empty() ? name
                         : (fog ? "Fog " : "Clouds ") + std::to_string(nth);
  s.objects.push_back(o);
  const int idx = (int)s.objects.size() - 1;
  a.graph_layout_serial++;
  a.scene_selection_serial++;
  a.request_eval();
  return idx;
}

int scene_add_air_layer(App &a, int kind, int atmosphere_idx, const std::string &name) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(1200))) return -1;
  return scene_add_air_layer_locked(a, kind, atmosphere_idx, name);
}

} // namespace studio
