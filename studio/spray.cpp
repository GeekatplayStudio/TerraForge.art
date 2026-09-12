// Geekatplay TerraForge - the spray brush: paint a mixed population onto the
// ground (spray.hpp).
//
// A population node already places one kind of thing by rules - by slope, by
// altitude, in clumps. What it could not do is be a brush: there was no way
// to say "a scatter of boulders with gorse between them, thickest here,
// thinning to nothing over there" and then draw where "here" is.
//
// Two things were missing, and both already half existed. A MaskPaint node is
// a 0..1 field painted in the viewport with the terrain brushes, and an
// EcosystemLayer takes a mask - so wiring one into the other makes the layer
// paintable, which is the brush. And a layer already carries up to eight
// kinds with a share of the total each, which is the mix: what it lacked was
// anywhere to say what each kind IS, and for each to vary in its own way.
//
// So a spray is a MaskPaint feeding an EcosystemLayer, and a component is one
// of the layer's kinds: a scene object bound to it (studio/scene.hpp,
// SceneObject::scatter_species) with a share of the total, a size, how much
// that size varies, and how far it tips with the ground. A plant species, a
// rock, an imported model - anything that is a mesh in the scene.
#include "spray.hpp"
#include "app.hpp"
#include "plant_place.hpp"
#include "plant_species.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace studio {

namespace {

using Lock = std::unique_lock<App::GraphMutex>;

bool lock_graph(App &a, Lock &lk, std::string &err) {
  lk = Lock(a.graph_mtx, std::defer_lock);
  if (lk.try_lock_for(std::chrono::milliseconds(1200))) return true;
  err = "the graph is busy, try again";
  return false;
}

void setf(gpx::Node &n, const std::string &key, float v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Float) a->f = std::clamp(v, a->fmin, a->fmax);
}
void seti(gpx::Node &n, const std::string &key, int v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Int) a->i = std::clamp(v, a->imin, a->imax);
}
void setb(gpx::Node &n, const std::string &key, bool v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Bool) a->b = v;
}

// The heightmap the rules read, so slope and altitude mean the ground the
// viewport draws.
gpx::Node *terrain_feed(gpx::Graph &g, std::string &port) {
  gpx::Node *out = nullptr;
  for (auto &n : g.nodes)
    if (n->type == "TerrainOutput") out = n.get();
  if (out)
    for (const gpx::Link &l : g.links)
      if (l.to_node == out->id && l.to_port == "heightmap") {
        port = l.from_port;
        return g.find_node(l.from_node);
      }
  for (auto &n : g.nodes)
    if (n->type != "TerrainOutput")
      if (const gpx::Port *p = n->first_out(gpx::DataType::Heightmap)) {
        port = p->name;
        return n.get();
      }
  return nullptr;
}

int slot_of(const gpx::Graph &g, uint64_t layer) {
  // The first free kind: one past the highest already standing for something.
  //
  // Not the layer's "species" count - that cannot say nought, its own least
  // value being one, so an empty spray reads as having one kind already and
  // the first component lands in slot 1. Slot 0 is then a kind the points are
  // shared with and nothing draws, and a quarter of a three-way mix comes out
  // as bare ground.
  if (!g.find_node(layer)) return -1;
  int next = 0;
  for (const SceneObject &o : scene().objects)
    if (o.scatter_node == layer && o.scatter_species >= 0)
      next = std::max(next, o.scatter_species + 1);
  return next;
}

} // namespace

uint64_t spray_new(App &a, const SprayPlan &plan, std::string &err) {
  Lock lk;
  if (!lock_graph(a, lk, err)) return 0;
  undo_push_locked(a, "New spray");
  float x = 120.f, y = 120.f;
  for (auto &n : a.graph.nodes) x = std::max(x, n->pos_x + 240.f);

  gpx::Node *mask = a.graph.add_node("MaskPaint", x, y);
  gpx::Node *layer = a.graph.add_node("EcosystemLayer", x + 260.f, y);
  if (!mask || !layer) {
    err = "could not add the spray nodes";
    return 0;
  }
  // the brush feeds the population's presence: paint where things grow
  a.graph.add_link(mask->id, "mask", layer->id, "mask");
  {
    std::string port = "output";
    if (gpx::Node *feed = terrain_feed(a.graph, port))
      a.graph.add_link(feed->id, port, layer->id, "terrain");
  }
  setf(*mask, "soften", 0.01f);

  setb(*layer, "enabled", true);
  setb(*layer, "unbounded", plan.unbounded);
  setf(*layer, "population_m", plan.area_m);
  setf(*layer, "density", plan.density_ha);
  setf(*layer, "spacing_m", plan.spacing_m);
  setf(*layer, "clump_amount", plan.clumping);
  setf(*layer, "clump_size_m", std::max(plan.spacing_m * 8.f, 20.f));
  setb(*layer, "avoid_overlap", true);
  setf(*layer, "variation", plan.size_variation);
  setf(*layer, "keep_proportions", 0.85f);
  setf(*layer, "color_variation", 0.25f);
  seti(*layer, "species", 0); // nothing in it yet: components are added below
  // A painted spray answers the brush and nothing else by default - the point
  // of painting is to say where - so the slope and altitude bands start off.
  setb(*layer, "use_slope", false);
  setb(*layer, "use_altitude", false);

  a.graph.mark_dirty(layer->id);
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "a spray brush: paint the mask node to say where, then add what";
  return layer->id;
}

uint64_t spray_layer_for_group(const App &a, const std::string &group) {
  for (const auto &np : a.graph.nodes)
    if (np->type == "EcosystemLayer" && np->attrs.get_s("eco_group") == group) return np->id;
  return 0;
}

int spray_add(App &a, uint64_t layer_id, const SprayComponent &c, std::string &err) {
  Lock lk;
  if (!lock_graph(a, lk, err)) return -1;
  gpx::Node *layer = a.graph.find_node(layer_id);
  if (!layer || layer->type != "EcosystemLayer") {
    err = "no spray " + std::to_string(layer_id);
    return -1;
  }
  const int slot = slot_of(a.graph, layer_id);
  if (slot < 0 || slot >= 8) {
    err = "a spray carries eight kinds at most";
    return -1;
  }
  undo_push_locked(a, "Add to the spray");

  // the thing itself: a plant grown from its name, or an object already in
  // the scene (a rock, an imported model - anything that is a mesh)
  int obj = -1;
  if (!c.plant_words.empty()) {
    lk.unlock(); // species_create takes the lock itself
    SpeciesMake mk;
    mk.place = false;
    bool matched = false;
    const uint64_t root = species_create_from_words(a, c.plant_words, mk, matched, err);
    if (!root) return -1;
    if (!lock_graph(a, lk, err)) return -1;
    gpx::Node *r = a.graph.find_node(root);
    if (!r) {
      err = "the species did not grow";
      return -1;
    }
    const auto mesh = gpx::plant_mesh_for(root);
    PlantPlace at;
    at.at_view = false;
    at.name = c.name.empty() ? c.plant_words : c.name;
    at.scatter = false;
    obj = plant_place_species(a, *r, at, mesh && mesh->height_m > 0.1f ? mesh->height_m : 5.f);
  } else if (!c.object.empty()) {
    SceneState &sc = scene();
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
      std::string n = sc.objects[(size_t)i].name, w = c.object;
      for (char &ch : n) ch = (char)std::tolower((unsigned char)ch);
      for (char &ch : w) ch = (char)std::tolower((unsigned char)ch);
      if (n == w && sc.objects[(size_t)i].type == SceneObject::Mesh) {
        obj = i;
        break;
      }
    }
    if (obj < 0) {
      err = "no mesh called '" + c.object + "' in the scene";
      return -1;
    }
  } else {
    err = "a spray component needs 'plant' (a plant's name) or 'object'";
    return -1;
  }
  if (obj < 0) {
    err = "the component could not be placed";
    return -1;
  }

  SceneObject &o = scene().objects[(size_t)obj];
  o.scatter_node = layer_id;
  o.scatter_species = slot; // this kind takes its own share of the points
  o.scatter_scale = 1.f;

  const std::string k = "sp" + std::to_string(slot + 1);
  seti(*layer, "species", slot + 1);
  setf(*layer, k + "_presence", std::clamp(c.share, 0.f, 1.f));
  setf(*layer, k + "_scale", std::max(c.scale, 0.01f));
  setf(*layer, k + "_variation", std::clamp(c.size_variation, 0.f, 1.f));
  setf(*layer, k + "_lean", c.lean);

  a.graph.mark_dirty(layer_id);
  a.scene_selection_serial++;
  a.request_eval();
  a.status = "added " + o.name + " to the spray";
  return slot;
}

std::vector<SprayComponent> spray_components(const App &a, uint64_t layer_id) {
  std::vector<SprayComponent> out;
  const gpx::Node *layer = a.graph.find_node(layer_id);
  if (!layer) return out;
  const int n = layer->attrs.get_i("species", 0);
  // every kind's share, and what stands for it
  float total = 0.f;
  for (int s = 0; s < n; ++s)
    total += std::max(0.f, layer->attrs.get_f("sp" + std::to_string(s + 1) + "_presence", 1.f));
  for (int s = 0; s < n; ++s) {
    const std::string k = "sp" + std::to_string(s + 1);
    SprayComponent c;
    c.slot = s;
    c.share = layer->attrs.get_f(k + "_presence", 1.f);
    c.percent = total > 1e-5f ? 100.f * c.share / total : 0.f;
    c.scale = layer->attrs.get_f(k + "_scale", 1.f);
    c.size_variation = layer->attrs.get_f(k + "_variation", 0.f);
    c.lean = layer->attrs.get_f(k + "_lean", -1.f);
    for (const SceneObject &o : scene().objects)
      if (o.scatter_node == layer_id && o.scatter_species == s) {
        c.object = o.name;
        break;
      }
    out.push_back(c);
  }
  return out;
}

bool spray_set(App &a, uint64_t layer_id, int slot, const SprayComponent &c, std::string &err) {
  Lock lk;
  if (!lock_graph(a, lk, err)) return false;
  gpx::Node *layer = a.graph.find_node(layer_id);
  if (!layer || layer->type != "EcosystemLayer") {
    err = "no spray " + std::to_string(layer_id);
    return false;
  }
  if (slot < 0 || slot >= layer->attrs.get_i("species", 0)) {
    err = "the spray has no kind " + std::to_string(slot + 1);
    return false;
  }
  undo_push_locked(a, "Spray settings");
  const std::string k = "sp" + std::to_string(slot + 1);
  if (c.share >= 0.f) setf(*layer, k + "_presence", std::clamp(c.share, 0.f, 1.f));
  if (c.scale > 0.f) setf(*layer, k + "_scale", c.scale);
  if (c.size_variation >= 0.f) setf(*layer, k + "_variation", std::clamp(c.size_variation, 0.f, 1.f));
  setf(*layer, k + "_lean", c.lean);
  a.graph.mark_dirty(layer_id);
  a.request_eval();
  return true;
}

} // namespace studio
