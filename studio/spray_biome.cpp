// Geekatplay TerraForge - a biome laid out as a stack of populations
// (spray.hpp, gpx/ecology.hpp).
//
// This is where the ecology stops being a table and becomes ground.
//
// A biome names groups and shares; the rules say how those groups sit against
// each other. Laying one out is therefore: sort the members into the order
// the ground assembles (what lies on bare earth, then what stands up out of
// it, then what lives among that, then the cover, then the litter), make one
// population layer per member in that order, and wire each layer's points
// into the next one's "below" input so the rules have something to be about.
//
// The order is the part that cannot be fudged. A layer can only be placed in
// relation to what is already on the ground, so moss placed before the
// boulders has nothing to gather on and ends up scattered in the open, which
// is exactly the thing that makes assembled scenery look assembled. Sorting
// by tier first is what makes every rule downstream possible.
//
// A population is placed in relation to ONE other population - the one wired
// into its "below" input - so which one that is decides which rule can be
// expressed. It need not be the layer immediately before it in the stack, and
// usually should not be: moss is about boulders, litter is about the tree
// that dropped it, fungi are about the dead wood they are eating. Each layer
// is therefore wired to whichever already-placed group its strongest rule
// names, and only falls back to the previous layer when it has no rule at all.
//
// The tier order still decides the order they are CREATED, because a layer
// can only be wired to one that already exists. Order of creation, and choice
// of what to be placed against, are two different questions, and conflating
// them was what put the moss on the ferns.
#include "spray.hpp"
#include "app.hpp"
#include "gpx/ecology.hpp"
#include "plant_place.hpp"
#include "plant_species.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace studio {

namespace {

using Lock = std::unique_lock<App::GraphMutex>;

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
void setrange(gpx::Node &n, const std::string &key, float lo, float hi) {
  if (gpx::Attribute *a = n.attrs.find(key))
    if (a->type == gpx::AttrType::Range) {
      a->v2[0] = std::clamp(lo, a->v2min, a->v2max);
      a->v2[1] = std::clamp(hi, a->v2min, a->v2max);
    }
}

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

// The already-placed group this one most wants to be judged against: the
// rule with the largest pull either way. Empty when it has no rule about
// anything on the ground yet, and the caller falls back to what came before.
std::string host_for(const std::string &group, const std::vector<std::string> &placed) {
  using namespace gpx::eco;
  std::string want;
  float best = 0.f;
  for (const Relation *rl : relations_of(group)) {
    const float pull = std::fabs(rl->affinity) + std::fabs(rl->repulsion);
    bool laid = false;
    for (const std::string &p : placed) laid = laid || p == rl->near;
    if (laid && pull > best) {
      best = pull;
      want = rl->near;
    }
  }
  return want;
}

// What this member's layer should say about the layer beneath it.
void apply_rules(gpx::Node &layer, const std::string &group, const std::string &below) {
  using namespace gpx::eco;
  if (below.empty()) return;
  const Relation *rl = relation_between(group, below);
  if (!rl) {
    // No rule between these two. That is not nothing: two groups with no
    // stated relationship should not be made to cluster, so both are cleared
    // rather than left at whatever the node happened to default to.
    setf(layer, "affinity", 0.f);
    setf(layer, "repulsion", 0.f);
    return;
  }
  setf(layer, "affinity", rl->affinity);
  setf(layer, "affinity_radius_m", rl->affinity_m);
  setf(layer, "repulsion", rl->repulsion);
  setf(layer, "repulsion_radius_m", rl->repulsion_m);
}

// What the group itself is, whatever it stands next to.
void apply_group(gpx::Node &layer, const gpx::eco::Group &gr, const gpx::eco::Biome &bi,
                 const gpx::eco::BiomeMember &mem) {
  using namespace gpx::eco;
  setf(layer, "density", mem.density_ha > 0.f ? mem.density_ha : 100.f);
  setf(layer, "spacing_m", std::max(gr.footprint_m * 1.2f, 0.05f));
  setf(layer, "footprint_m", gr.footprint_m);
  setb(layer, "avoid_overlap", true);
  setf(layer, "variation", gr.size_variation);
  setf(layer, "scale", mem.scale);
  // It stands up, or it lies on what it is on. A boulder follows the ground;
  // a tree does not care how steep the ground is, it grows toward the sky.
  setf(layer, "direction", gr.stance == Stance::Surface ? 1.f : gr.lean);
  // The slope it can hold on to. This is a property of the thing - a tree
  // cannot root on a cliff whatever biome it is in.
  setb(layer, "use_slope", true);
  setrange(layer, "slope", 0.f, gr.slope_max_deg);
  setf(layer, "slope_fuzz", 8.f);
  // Ground cover clumps; big things are spaced by their own root reach.
  setf(layer, "clump_amount", gr.tier >= 3 ? 0.55f : 0.3f);
  setf(layer, "clump_size_m", std::max(gr.footprint_m * 14.f, 8.f));
  // How it answers a dry or a wet biome: a thing wanting what this ground
  // has is thick on it, a thing wanting the opposite is sparse.
  const float want = 1.f - std::min(1.f, std::fabs(gr.moisture - bi.moisture) /
                                             std::max(gr.moisture_width, 0.05f));
  setf(layer, "threshold", 0.05f + 0.35f * (1.f - want));
  setf(layer, "color_variation", 0.25f);
}

} // namespace

uint64_t spray_biome(App &a, const std::string &biome_id, const SprayPlan &plan,
                     std::vector<BiomeLayer> &made, std::string &err) {
  using namespace gpx::eco;
  const Biome *bi = biome_of(biome_id);
  if (!bi) {
    err = "no biome '" + biome_id + "' - ask for the list with spray_biomes";
    return 0;
  }
  Lock lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(1500))) {
    err = "the graph is busy, try again";
    return 0;
  }
  undo_push_locked(a, "Spray a " + bi->name);

  float x = 120.f, y = 120.f;
  for (auto &n : a.graph.nodes) x = std::max(x, n->pos_x + 260.f);

  // One brush for the whole biome: everything in it is painted together, so
  // a stroke lays down a wood rather than a layer of a wood.
  gpx::Node *mask = a.graph.add_node("MaskPaint", x, y);
  if (!mask) {
    err = "could not add the brush";
    return 0;
  }
  setf(*mask, "soften", 0.015f);

  std::string tport = "output";
  gpx::Node *feed = terrain_feed(a.graph, tport);

  const std::vector<BiomeMember> order = in_order(*bi);
  std::map<std::string, uint64_t> layer_of; // group -> the layer placing it
  std::vector<std::string> placed;
  uint64_t below_node = 0;
  std::string below_group;
  uint64_t first = 0;
  float row = 0.f;
  for (const BiomeMember &mem : order) {
    const Group *gr = group_of(mem.group);
    if (!gr) continue;
    gpx::Node *layer = a.graph.add_node("EcosystemLayer", x + 300.f, y + row);
    if (!layer) break;
    row += 220.f;
    a.graph.add_link(mask->id, "mask", layer->id, "mask");
    if (feed) a.graph.add_link(feed->id, tport, layer->id, "terrain");
    // What this layer is judged against: whichever already-placed group its
    // strongest rule names - moss against the boulders, litter against the
    // tree that dropped it - and only the previous layer when it has no rule.
    const std::string host = host_for(mem.group, placed);
    const uint64_t host_node = host.empty() ? below_node : layer_of[host];
    const std::string host_group = host.empty() ? below_group : host;
    if (host_node) a.graph.add_link(host_node, "points", layer->id, "below");

    setb(*layer, "enabled", true);
    setb(*layer, "unbounded", plan.unbounded);
    setf(*layer, "population_m", plan.area_m);
    apply_group(*layer, *gr, *bi, mem);
    apply_rules(*layer, mem.group, host_group);
    seti(*layer, "species", 0);
    if (gpx::Attribute *nm = layer->attrs.find("name")) nm->s = gr->plural;
    // the durable link between this layer and the ecology's vocabulary, so
    // anything assigned to the group later lands here
    if (gpx::Attribute *eg = layer->attrs.find("eco_group")) eg->s = mem.group;

    made.push_back(BiomeLayer{layer->id, mem.group, gr->plural, host_group, mem.share});
    a.graph.mark_dirty(layer->id);
    if (!first) first = layer->id;
    layer_of[mem.group] = layer->id;
    placed.push_back(mem.group);
    below_node = layer->id;
    below_group = mem.group;
  }
  if (!first) {
    err = "the biome named no group this build knows";
    return 0;
  }
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "a " + bi->name + ": " + std::to_string(made.size()) +
             " layers, in the order the ground builds itself. Paint the brush, then say what "
             "stands for each group.";
  return first;
}

} // namespace studio
