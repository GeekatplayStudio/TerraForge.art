// Geekatplay TerraForge - a new component, complete. See the header.
#include "component_new.hpp"
#include "app.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>

namespace studio {

namespace {

// Somewhere clear to put new nodes: to the right of everything already there,
// so a component added to a busy graph does not land on top of it.
void free_spot(gpx::Graph &g, float &x, float &y) {
  x = 120.f;
  y = 120.f;
  for (auto &n : g.nodes) x = std::max(x, n->pos_x + 240.f);
}

} // namespace

uint64_t component_material(App &a) {
  // The last one used, if it is still a material.
  if (gpx::Node *m = a.graph.find_node(a.last_material))
    if (m->type == "MaterialOutput") return m->id;

  // Otherwise a plain grey one. Grey rather than coloured because it is a
  // starting point, not a decision: it reads as "not yet painted" the way a
  // clay render does, and every value in it is easy to judge against.
  float x = 0, y = 0;
  free_spot(a.graph, x, y);
  gpx::Node *flat = a.graph.add_node("FlatColor", x, y + 220.f);
  gpx::Node *mat = a.graph.add_node("MaterialOutput", x + 240.f, y + 220.f);
  if (!flat || !mat) return 0;
  for (const char *k : {"r", "g", "b"})
    if (gpx::Attribute *at = flat->attrs.find(k)) at->f = 0.55f;
  if (gpx::Attribute *nm = mat->attrs.find("name")) {
    int n = 0;
    for (auto &c : a.graph.nodes)
      if (c->type == "MaterialOutput") ++n;
    nm->s = n > 1 ? "Material " + std::to_string(n) : std::string("Material");
  }
  a.graph.add_link(flat->id, "texture", mat->id, "base color");
  a.last_material = mat->id;
  return mat->id;
}

NewComponent component_add_primitive(App &a, const std::string &kind,
                                     const std::string &name) {
  NewComponent out;
  // A bounded wait rather than GraphLease: this is a one-shot action from a
  // button, not a panel drawing itself, so it has no menu to keep alive and
  // no business depending on ImGui - which is what lets the same code link
  // into the GL-free test binary. Waiting rather than giving up, because a
  // button press that silently does nothing because an evaluation happened to
  // be running is the worst of the three possible behaviours.
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(500))) return out;
  undo_push_locked(a, "Add " + kind);

  // The object first, so it has geometry and a name straight away rather than
  // appearing a frame later when the graph next evaluates.
  out.object = scene_add_primitive(kind, name);
  if (out.object < 0) return out;
  SceneObject &o = scene().objects[(size_t)out.object];

  // The node that drives it. scene_nodes_objects.cpp binds the two by
  // driver_node, so from here the object is the node's, and editing the node
  // moves the object rather than the two drifting apart.
  float x = 0, y = 0;
  free_spot(a.graph, x, y);
  if (gpx::Node *n = a.graph.add_node("Primitive", x, y)) {
    static const char *KINDS[] = {"cube", "sphere", "plane", "cylinder", "cone", "pine",
                                  "juniper", "palm", "fern", "grass", "bush", "boulder"};
    if (gpx::Attribute *k = n->attrs.find("kind"))
      for (int i = 0; i < 12; ++i)
        if (kind == KINDS[i]) k->i = i;
    if (gpx::Attribute *on = n->attrs.find("object")) on->s = o.name;
    if (scene_is_plant_kind(kind)) {
      // the node says the plant's own size and white, so its parts keep
      // their colours - the node is what drives the object from here on
      if (gpx::Attribute *s = n->attrs.find("size_m")) s->f = scene_plant_size_m(kind);
      if (gpx::Attribute *c = n->attrs.find("color"))
        for (int ch = 0; ch < 3; ++ch) c->col[ch] = 1.f;
    }
    // the node's transform starts where the object was put, so the first
    // thing the node says about the object is not a jump
    if (gpx::Attribute *p = n->attrs.find("position")) {
      // add_transform stores metres; the scene stores tile units
      (void)p;
    }
    o.driver_node = n->id;
    out.node = n->id;
    a.selected_node = n->id;
  }

  // a plant's colours are its bark and foliage: no grey material over them
  if (!scene_is_plant_kind(kind)) {
    out.material = component_material(a);
    o.material_node = out.material;
  }

  a.graph_layout_serial++;
  a.request_eval();
  return out;
}

} // namespace studio
