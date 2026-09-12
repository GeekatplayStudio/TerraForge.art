// Geekatplay TerraForge - a wood of one species: many trees, no two the same
// (plant_species.hpp).
//
// A forest is not one plant repeated. Scattering a single grown tree gives a
// plantation of clones - every trunk leaning the same way, every crown the
// same silhouette - and the eye picks that out immediately, however many
// copies there are. Nor is it one tree per individual: a mature oak is half
// a million triangles, and a hundred of them would be fifty million.
//
// So a wood is a handful of individuals and a great many copies of them. The
// species is grown a few times, each with its own seed, which is enough to
// make each one its own tree - its own lean, its own branching, its own
// height. The population then splits its points among them: the layer already
// carries a species index per point (engine/scatter_rules.cpp) so that a
// meadow can mix grasses, and the same channel serves just as well for the
// varieties of one kind. Every copy is still instanced, so the whole wood
// costs what the handful of trees costs, plus a transform each.
//
// The rules come with it, because a wood that ignores them is not a wood: an
// altitude band keeps the trees out of the water, a slope band keeps them off
// cliffs, and clumping gathers them into stands with clearings between rather
// than spreading them evenly like an orchard.
#include "app.hpp"
#include "plant_place.hpp"
#include "plant_species.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace studio {

namespace {

using Lock = std::unique_lock<App::GraphMutex>;

bool lock_graph(App &a, Lock &lk, std::string &err) {
  lk = Lock(a.graph_mtx, std::defer_lock);
  if (lk.try_lock_for(std::chrono::milliseconds(1200))) return true;
  err = "the graph is busy, try again";
  return false;
}

// The heightmap the population should read: what feeds TerrainOutput, so the
// altitude and slope rules see the ground the viewport draws.
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

void setf(gpx::Node &n, const char *key, float v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Float) a->f = std::clamp(v, a->fmin, a->fmax);
}
void seti(gpx::Node &n, const char *key, int v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Int) a->i = std::clamp(v, a->imin, a->imax);
}
void setb(gpx::Node &n, const char *key, bool v) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Bool) a->b = v;
}
void setrange(gpx::Node &n, const char *key, float lo, float hi) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Range) {
      a->v2[0] = std::clamp(lo, a->v2min, a->v2max);
      a->v2[1] = std::clamp(hi, a->v2min, a->v2max);
    }
}

// A seed no other individual of this species is using.
uint32_t forest_seed() {
  static uint32_t n = 0x9E3779B9u;
  n = n * 1664525u + 1013904223u;
  return n ? n : 1u;
}

// The layer's rules: where trees will and will not stand.
void forest_rules(gpx::Node &layer, const ForestPlan &p) {
  setb(layer, "enabled", true);
  setb(layer, "unbounded", p.unbounded);
  setf(layer, "population_m", p.area_m);
  setf(layer, "density", p.density_ha);
  setf(layer, "spacing_m", p.spacing_m);
  // A wood grows in stands with clearings between, not in an orchard's rows.
  setf(layer, "clump_amount", p.clumping);
  setf(layer, "clump_size_m", p.clump_size_m);
  setb(layer, "avoid_overlap", true);
  setf(layer, "footprint_m", std::max(p.spacing_m * 0.35f, 0.5f));
  // Out of the water and off the cliffs. Altitude is a share of the terrain's
  // own height range, so the band means the same thing on any terrain.
  setb(layer, "use_altitude", true);
  setrange(layer, "altitude", p.altitude_lo, p.altitude_hi);
  setf(layer, "altitude_fuzz", 0.06f);
  setb(layer, "use_slope", true);
  setrange(layer, "slope", 0.f, p.max_slope_deg);
  setf(layer, "slope_fuzz", 8.f);
  // every tree its own size and heading, on top of being its own individual
  setf(layer, "variation", p.size_variation);
  setf(layer, "keep_proportions", 0.8f);
  setf(layer, "color_variation", 0.25f);
  seti(layer, "rotation", 0);
  setf(layer, "rotation_max", 1.f);
  setf(layer, "direction", 0.15f); // a little lean with the ground
  setf(layer, "phase_range", 3.f); // they do not all sway on the same beat
}

} // namespace

// Every individual shares the species' parts; only the seed differs, and one
// seed is one plant, so each is its own tree.
int plant_forest(App &a, const ForestPlan &plan, ForestResult &out, std::string &err) {
  const int kinds = std::clamp(plan.individuals, 1, 8); // the layer carries 8
  uint64_t root = plan.species;
  if (!root) {
    // grow the species first, without standing a lone specimen in the scene
    SpeciesMake mk;
    mk.place = false;
    bool matched = false;
    root = species_create_from_words(a, plan.words, mk, matched, err);
    if (!root) return 0;
    out.known = matched;
  }

  Lock lk;
  if (!lock_graph(a, lk, err)) return 0;
  gpx::Node *first = a.graph.find_node(root);
  if (!first || first->type != "PlantSpecies") {
    err = "no plant species to make a wood of";
    return 0;
  }
  undo_push_locked(a, "Plant a wood");

  const auto mesh = gpx::plant_mesh_for(root);
  const float height_m = mesh && mesh->height_m > 0.1f ? mesh->height_m : 8.f;
  const std::string base = first->attrs.get_s("name").empty() ? std::string("Wood")
                                                              : first->attrs.get_s("name");

  // ---- the population: one layer, `kinds` species, the forest's own rules
  float x = first->pos_x, y = first->pos_y + 320.f;
  gpx::Node *layer = a.graph.add_node("EcosystemLayer", x, y);
  if (!layer) {
    err = "could not add the population layer";
    return 0;
  }
  {
    std::string port = "output";
    if (gpx::Node *feed = terrain_feed(a.graph, port)) a.graph.add_link(feed->id, port, layer->id, "terrain");
  }
  ForestPlan p = plan;
  if (p.spacing_m <= 0.f) p.spacing_m = std::max(height_m * 0.55f, 1.f);
  if (p.density_ha <= 0.f) p.density_ha = std::max(4.f, 10000.f / (p.spacing_m * p.spacing_m));
  if (p.clump_size_m <= 0.f) p.clump_size_m = std::max(p.spacing_m * 8.f, 20.f);
  forest_rules(*layer, p);
  seti(*layer, "species", kinds);
  for (int s = 1; s <= kinds; ++s) {
    const std::string k = "species" + std::to_string(s);
    setf(*layer, (k + "_presence").c_str(), 1.f);
    setf(*layer, (k + "_scale").c_str(), 1.f);
  }
  out.layer = layer->id;

  // ---- the individuals, and a mesh of the wood standing for each
  gpx::Node *trunk = a.graph.upstream_node(*first, "trunk");
  for (int s = 0; s < kinds; ++s) {
    gpx::Node *r = first;
    if (s > 0) {
      r = a.graph.add_node("PlantSpecies", first->pos_x, first->pos_y + 260.f * (float)s);
      if (!r) break;
      r->attrs = first->attrs;
      if (gpx::Attribute *sd = r->attrs.find("seed")) sd->seed = forest_seed();
      if (trunk) a.graph.add_link(trunk->id, "plant", r->id, "trunk");
      for (int b = 1; b <= 4; ++b)
        if (gpx::Node *bias = a.graph.upstream_node(*first, "bias " + std::to_string(b)))
          a.graph.add_link(bias->id, "plant", r->id, "bias " + std::to_string(b));
    }
    if (gpx::Attribute *o = r->attrs.find("object"))
      o->s = base + " " + std::to_string(s + 1);
    // the object the copies are stamped from: one per individual
    PlantPlace at;
    at.at_view = false;
    at.name = base + " " + std::to_string(s + 1);
    at.size = 1.f;
    at.scatter = false;
    const int obj = plant_place_species(a, *r, at, height_m);
    if (obj < 0) continue;
    SceneObject &o = scene().objects[(size_t)obj];
    o.scatter_node = layer->id;
    o.scatter_species = s; // this individual takes its own share of the points
    o.scatter_scale = 1.f;
    o.scatter_sway = 0.02f * height_m / std::max(render_settings().terrain_size_m, 1e-3f);
    out.individuals.push_back(r->id);
    a.graph.mark_dirty(r->id);
  }

  a.graph.mark_dirty(layer->id);
  a.graph_layout_serial++;
  a.scene_selection_serial++;
  a.request_eval();
  out.kinds = (int)out.individuals.size();
  a.status = "planted a wood of " + base + ": " + std::to_string(out.kinds) +
             " different trees, about " + std::to_string((int)std::lround(p.density_ha)) +
             " per hectare";
  return out.kinds;
}

} // namespace studio
