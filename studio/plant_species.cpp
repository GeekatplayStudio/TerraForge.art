// Geekatplay TerraForge - plant species: finding them in the graph and
// making them (plant_species.hpp).
//
// A species is made in one undo step: its nodes laid out right of whatever
// the graph already holds, its plant standing in the scene, and the Plant
// Editor pointed at it. The plant's mesh arrives with the next evaluation
// (scene_plants_species.cpp), so the object is created here with no geometry
// and adopted by its root then - which is what lets Scatter bind to it now.
#include "plant_species.hpp"
#include "app.hpp"
#include "console.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>

namespace studio {

namespace {

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool digits_only(const std::string &s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; });
}

// Right of everything the graph holds, so a species never lands on a node.
void graph_free_spot(const gpx::Graph &g, float &x, float &y) {
  x = 600.f;
  y = 160.f;
  for (const auto &n : g.nodes) x = std::max(x, n->pos_x + 320.f);
}

uint32_t fresh_seed() {
  static uint32_t s = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
  s = s * 1664525u + 1013904223u;
  uint32_t x = s ^ (s >> 16);
  x *= 0x7feb352du;
  x ^= x >> 15;
  return x % 999983u + 1u;
}

// What every successful make ends with: the editor on the species, the
// graph told to evaluate, the status line saying what happened.
void finish(App &a, uint64_t root, const std::string &what) {
  species_current() = root;
  a.selected_node = root;
  a.show_plant_editor = true;
  a.graph_layout_serial++;
  a.request_eval();
  a.status = what;
  log_info("plants", what);
}

void place_and_multiply(App &a, gpx::Node &root, const SpeciesMake &mk, float height_m);

} // namespace

uint64_t &species_current() {
  static uint64_t id = 0;
  return id;
}

int species_object(uint64_t root) {
  const SceneState &sc = scene();
  for (int i = 0; i < (int)sc.objects.size(); ++i)
    if (root && sc.objects[(size_t)i].driver_node == root) return i;
  return -1;
}

std::vector<SpeciesRef> species_in_graph(const gpx::Graph &g) {
  std::vector<SpeciesRef> out;
  for (const auto &n : g.nodes) {
    if (n->type != "PlantSpecies") continue;
    SpeciesRef r;
    r.root = n->id;
    r.name = n->attrs.get_s("object");
    if (r.name.empty()) r.name = n->attrs.get_s("name");
    if (r.name.empty()) r.name = "Plant " + std::to_string(n->id);
    out.push_back(r);
  }
  return out;
}

gpx::Node *species_find(gpx::Graph &g, const std::string &which) {
  auto species = [](gpx::Node *n) { return n && n->type == "PlantSpecies" ? n : nullptr; };
  if (which.empty()) {
    if (gpx::Node *n = species(g.find_node(species_current()))) return n;
    for (auto &n : g.nodes)
      if (n->type == "PlantSpecies") return n.get();
    return nullptr;
  }
  if (digits_only(which))
    if (gpx::Node *n = species(g.find_node(std::stoull(which)))) return n;
  const std::string w = lower(which);
  for (auto &n : g.nodes)
    if (n->type == "PlantSpecies" && (lower(n->attrs.get_s("object")) == w || lower(n->attrs.get_s("name")) == w))
      return n.get();
  for (const SceneObject &o : scene().objects)
    if (o.driver_node && lower(o.name) == w)
      if (gpx::Node *n = species(g.find_node(o.driver_node))) return n;
  // a part's id: the species it belongs to
  if (digits_only(which))
    if (gpx::Node *part = g.find_node(std::stoull(which)))
      if (const gpx::Node *r = gpx::plant_root_of(g, *part)) return g.find_node(r->id);
  return nullptr;
}

uint64_t species_resolve(App &a, const std::string &which, std::string &err) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(800))) {
    err = "the graph is busy, try again";
    return 0;
  }
  if (gpx::Node *n = species_find(a.graph, which)) return n->id;
  err = which.empty() ? "there is no plant species in the graph (plant_species_new makes one)"
                      : "no plant species called '" + which + "'";
  return 0;
}

uint64_t species_create(App &a, const gpx::PlantDescription &d, const SpeciesMake &mk,
                        std::string &err) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(800))) {
    err = "the graph is busy, try again";
    return 0;
  }
  undo_push_locked(a, "New plant: " + d.name);
  float x = 0, y = 0;
  graph_free_spot(a.graph, x, y);
  const uint64_t root = gpx::plant_species_from_description(a.graph, d, x, y, err);
  gpx::Node *r = root ? a.graph.find_node(root) : nullptr;
  if (!r) {
    if (err.empty()) err = "the " + d.archetype + " archetype made no species";
    return 0;
  }
  if (gpx::Attribute *seed = r->attrs.find("seed"))
    if (seed->seed == 0) seed->seed = fresh_seed();
  place_and_multiply(a, *r, mk, d.height_m);
  finish(a, root, "grew a " + d.name + " (" + d.archetype + ", about " +
                      std::to_string((int)std::lround(d.height_m)) + " m)");
  return root;
}

uint64_t species_create_from_words(App &a, const std::string &words, const SpeciesMake &mk,
                                   bool &matched, std::string &err) {
  gpx::PlantDescription d;
  matched = gpx::plant_describe_from_words(words, d);
  if (!mk.at.name.empty()) d.name = mk.at.name;
  if (d.name.empty() || d.name == "Plant") d.name = words;
  const uint64_t root = species_create(a, d, mk, err);
  if (root && !matched)
    a.status += " - '" + words + "' is not a plant I know, so this is my best guess; describe it "
                "(tall, weeping, needles...) or ask the AI model";
  return root;
}

uint64_t species_load(App &a, const std::string &path, const SpeciesMake &mk, std::string &err) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(800))) {
    err = "the graph is busy, try again";
    return 0;
  }
  undo_push_locked(a, "Load plant species");
  float x = 0, y = 0;
  graph_free_spot(a.graph, x, y);
  gpx::PlantSpeciesInfo info;
  const uint64_t root = gpx::plant_species_load(a.graph, path, x, y, info, err);
  gpx::Node *r = root ? a.graph.find_node(root) : nullptr;
  if (!r) return 0;
  if (gpx::Attribute *nm = r->attrs.find("name"))
    if (nm->s.empty() || nm->s == "Plant") nm->s = info.name;
  place_and_multiply(a, *r, mk, info.height_m > 0.f ? info.height_m : 5.f);
  finish(a, root, "loaded the " + (info.name.empty() ? std::string("plant") : info.name) + " species");
  return root;
}

int species_add_individuals(App &a, uint64_t root_id, int count, const SpeciesMake &mk,
                            std::string &err) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(800))) {
    err = "the graph is busy, try again";
    return 0;
  }
  gpx::Node *root = a.graph.find_node(root_id);
  if (!root || root->type != "PlantSpecies") {
    err = "no plant species " + std::to_string(root_id);
    return 0;
  }
  undo_push_locked(a, "More individuals");
  SpeciesMake more = mk;
  more.individuals = std::clamp(count, 0, 32) + 1;
  const auto mesh = gpx::plant_mesh_for(root_id);
  const size_t before = a.graph.nodes.size();
  // the first individual exists: only the new ones are placed
  place_and_multiply(a, *root, more, mesh ? mesh->height_m : 5.f);
  a.graph_layout_serial++;
  a.request_eval();
  const int made = (int)(a.graph.nodes.size() - before);
  a.status = std::to_string(made) + " more individual(s) of the species";
  return made;
}

namespace {

// Stand the plant (when asked) and make the extra individuals: roots sharing
// the trunk and the biases, a seed of their own, their objects in a row
// beside the first. Caller holds the lock.
void place_and_multiply(App &a, gpx::Node &root, const SpeciesMake &mk, float height_m) {
  const bool first_exists = species_object(root.id) >= 0;
  if (mk.place && !first_exists) plant_place_species(a, root, mk.at, height_m);
  const int extra = std::max(mk.individuals - 1, 0);
  if (extra == 0) return;
  gpx::Node *trunk = a.graph.upstream_node(root, "trunk");
  const float tile = std::max(render_settings().terrain_size_m, 1e-3f);
  const int base_obj = species_object(root.id);
  // count the individuals already on this trunk, so names keep counting
  int existing = 0;
  if (trunk)
    for (const gpx::Link &l : a.graph.links)
      if (l.from_node == trunk->id && l.to_port == "trunk") ++existing;
  for (int i = 0; i < extra; ++i) {
    gpx::Node *n = a.graph.add_node("PlantSpecies", root.pos_x, root.pos_y + 260.f * (float)(existing + i));
    if (!n) break;
    n->attrs = root.attrs;
    if (gpx::Attribute *s = n->attrs.find("seed")) s->seed = fresh_seed();
    const std::string base = root.attrs.get_s("object").empty() ? root.attrs.get_s("name") : root.attrs.get_s("object");
    if (gpx::Attribute *o = n->attrs.find("object")) o->s = base + " " + std::to_string(existing + i + 1);
    if (trunk) a.graph.add_link(trunk->id, "plant", n->id, "trunk");
    for (int b = 1; b <= 4; ++b)
      if (gpx::Node *bias = a.graph.upstream_node(root, "bias " + std::to_string(b)))
        a.graph.add_link(bias->id, "plant", n->id, "bias " + std::to_string(b));
    if (!mk.place) continue;
    PlantPlace at = mk.at;
    if (base_obj >= 0) {
      const SceneObject &o = scene().objects[(size_t)base_obj];
      at.at_view = false;
      at.pos[0] = o.pos[0] + (float)(i + 1) * std::max(height_m, 1.f) * 0.9f / tile;
      at.pos[2] = o.pos[2] + (float)((i % 2) ? 1 : -1) * std::max(height_m, 1.f) * 0.35f / tile;
    }
    at.name.clear();
    at.count = std::max(1, mk.at.count / std::max(mk.individuals, 1));
    at.heading_deg = mk.at.heading_deg + 73.f * (float)(i + 1);
    plant_place_species(a, *n, at, height_m);
  }
}

} // namespace

} // namespace studio
