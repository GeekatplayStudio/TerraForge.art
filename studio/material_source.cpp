// Geekatplay TerraForge - which material node an object is wearing.
#include "material_source.hpp"
#include "scene.hpp"
#include <string>

namespace studio {

unsigned long long material_of_object(const SceneState &sc,
                                      const std::string &name,
                                      unsigned long long terrain_fallback) {
  if (name.empty()) return 0;
  for (const SceneObject &o : sc.objects) {
    if (o.name != name) continue;
    if (o.material_node) return o.material_node;
    if (o.type == SceneObject::Terrain) return terrain_fallback;
    return 0;
  }
  return 0;
}

int material_sources_resolve(gpx::Graph &g, const SceneState &sc,
                             unsigned long long terrain_fallback) {
  if (sc.objects.empty()) return 0;
  int changed = 0;
  for (auto &n : g.nodes) {
    if (n->type != "MaterialSource") continue;
    gpx::Attribute *obj = n->attrs.find("object");
    gpx::Attribute *src = n->attrs.find("source_node");
    if (!obj || !src) continue;
    const unsigned long long id =
        material_of_object(sc, obj->s, terrain_fallback);
    const std::string want = id ? std::to_string(id) : std::string();
    if (src->s == want) continue;
    src->s = want;
    // The node and everything downstream of it: what it hands out has just
    // changed, and noticing that is the graph's job, not the reader's.
    g.mark_dirty(n->id);
    ++changed;
  }
  return changed;
}

} // namespace studio
