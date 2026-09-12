// Geekatplay TerraForge - a plant from the library into the scene. See the header.
#include "plant_place.hpp"
#include "plant_species.hpp"
#include "app.hpp"
#include "component_new.hpp"
#include "console.hpp"
#include "gpx/mesh_io.hpp"
#include "hang_watch.hpp"
#include "mesh_object.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "terrain_relief.hpp"
#include "undo.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace studio {

namespace {

float tile_m() { return std::max(render_settings().terrain_size_m, 1e-3f); }

// Where the plant stands, in tile units: under the pivot of the view last
// worked in - the point its scene camera looks at, when it looks through
// one - or where it was asked for; on the ground there.
void stand(const App &a, const PlantPlace &at, float out[3]) {
  if (at.at_view) {
    const RenderSettings &rs = render_settings();
    const RenderSettings::ViewConfig &vc = rs.views[(size_t)std::clamp(a.view_focus, 0, RenderSettings::MAX_VIEWS - 1)];
    const int cam = vc.scene_camera >= 0 ? vc.scene_camera : vc.scene_camera == -2 ? view_active_camera() : -1;
    const SceneState &sc = scene();
    float target[3], yaw, pitch, dist;
    renderer_orbit_get(target, yaw, pitch, dist);
    if (cam >= 0 && cam < (int)sc.objects.size() && sc.objects[(size_t)cam].type == SceneObject::Camera)
      for (int k = 0; k < 3; ++k) target[k] = sc.objects[(size_t)cam].cam.target[k];
    out[0] = target[0];
    out[2] = target[2];
  } else {
    out[0] = at.pos[0];
    out[2] = at.pos[2];
  }
  // The ground as the viewport draws it: the heightmap and the fractal
  // micro-relief over it, which on the default tile is metres either way -
  // enough to bury a fern or hang it in the air (terrain_relief.hpp).
  const RenderSettings &rs = render_settings();
  const float ground = renderer_ground_under(out[0], out[2], RELIEF_NEAR_OCTAVES);
  // An object's height is kept in the heightmap's units, the tile's height
  // scale applied when it is drawn (scene_object_matrix); the ground answers
  // in world units. Taken as it came, every plant stood a hundred metres
  // under a 130 m hill.
  out[1] = ground / std::max(rs.height_scale, 1e-6f);
}

// A name no other object has, so the node that drives this one can never
// adopt another plant of the same kind (scene_nodes_objects.cpp adopts by name).
std::string unique_name(const std::string &want) {
  const SceneState &sc = scene();
  auto taken = [&](const std::string &n) {
    for (const SceneObject &o : sc.objects)
      if (o.name == n) return true;
    return false;
  };
  if (!taken(want)) return want;
  for (int i = 2;; ++i) {
    const std::string n = want + " " + std::to_string(i);
    if (!taken(n)) return n;
  }
}

float heading_in_range(float deg) {
  deg = std::fmod(deg, 360.f);
  if (deg > 180.f) deg -= 360.f;
  if (deg < -180.f) deg += 360.f;
  return deg;
}

void put(gpx::Node *n, const char *key, float v) {
  if (gpx::Attribute *at = n->attrs.find(key)) at->f = v;
}

// The node's transform from the object's, in metres.
void write_transform(gpx::Node *n, const SceneObject &o) {
  const float m = tile_m();
  put(n, "x_m", o.pos[0] * m);
  put(n, "y_m", o.pos[1] * m);
  put(n, "z_m", o.pos[2] * m);
  put(n, "size_m", o.scale * m);
  put(n, "heading", o.yaw);
  if (gpx::Attribute *c = n->attrs.find("color"))
    for (int k = 0; k < 3; ++k) c->col[k] = 1.f; // its own colours, untinted
}

// Copies over the terrain: a Scatter points node, and the object bound to it.
// Caller holds the graph lock.
void bind_scatter(App &a, SceneObject &o, const PlantPlace &at, float height_m, float x, float y) {
  gpx::Node *s = a.graph.add_node("ScatterPoints", x, y + 160.f);
  if (!s) return;
  if (gpx::Attribute *c = s->attrs.find("count")) c->i = std::clamp(at.count, 1, 50000);
  if (gpx::Attribute *m = s->attrs.find("mode")) m->i = 1; // a jittered grid: even, not planted
  if (gpx::Attribute *sd = s->attrs.find("seed")) sd->seed = (uint32_t)a.graph.nodes.size() * 7919u;
  o.scatter_node = s->id;
  o.scatter_scale = 1.f;
  o.scatter_jitter = 0.35f;
  // a little wind at the top, as a share of the plant's height
  o.scatter_sway = 0.02f * std::max(height_m, 0.f) * at.size / tile_m();
}

// A free spot for new nodes, right of everything already there.
void free_spot(gpx::Graph &g, float &x, float &y) {
  x = 120.f;
  y = 120.f;
  for (auto &n : g.nodes) x = std::max(x, n->pos_x + 240.f);
}

int add_builtin(App &a, const PlantEntry &p, const PlantPlace &at, std::string &err) {
  const NewComponent nc = component_add_primitive(a, p.kind, at.name);
  if (nc.object < 0) {
    err = "add " + p.name + ": the graph is busy, try again";
    return -1;
  }
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(500))) {
    err = "add " + p.name + ": the graph is busy, try again";
    return -1;
  }
  SceneState &sc = scene();
  SceneObject &o = sc.objects[(size_t)nc.object];
  float where[3];
  stand(a, at, where);
  for (int k = 0; k < 3; ++k) o.pos[k] = where[k];
  o.scale = scene_plant_size_m(p.kind) * std::max(at.size, 1e-4f) / tile_m();
  o.yaw = heading_in_range(at.heading_deg);
  // The node drives the object from here on, so it is told the same place:
  // written to the object alone, the next evaluation put the plant back.
  if (gpx::Node *n = a.graph.find_node(nc.node)) {
    write_transform(n, o);
    if (at.scatter) bind_scatter(a, o, at, p.height_m, n->pos_x, n->pos_y);
    a.graph.mark_dirty(n->id);
  }
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "added " + o.name;
  return nc.object;
}

// A model read into an object that is not in the scene yet: the part that
// can run on any thread.
bool load_model(const std::string &file, SceneObject &o, std::string &err) {
  gpx::TriMesh m;
  if (!gpx::mesh_load(file, m, err)) return false;
  o.type = SceneObject::Mesh;
  o.path = file;
  mesh_to_object(o, m);
  return true;
}

// The loaded object into the scene with the node that drives it. Main thread.
int finish_model(App &a, const PlantEntry &p, const PlantPlace &at, SceneObject &&loaded,
                 const std::string &file, float height_m, std::string &err) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::defer_lock);
  if (!lk.try_lock_for(std::chrono::milliseconds(500))) {
    err = "add " + p.name + ": the graph is busy, try again";
    return -1;
  }
  undo_push_locked(a, "Add " + p.name);
  SceneObject o = std::move(loaded);
  const PlantVariant *v = plant_variant(p, at.variant);
  o.name = unique_name(!at.name.empty() ? at.name : v ? p.name + " " + v->name : p.name);
  o.color[0] = o.color[1] = o.color[2] = 1.f;
  float where[3];
  stand(a, at, where);
  for (int k = 0; k < 3; ++k) o.pos[k] = where[k];
  o.scale = p.unit_m * std::max(at.size, 1e-4f) / tile_m();
  o.yaw = heading_in_range(at.heading_deg);
  float x = 0, y = 0;
  free_spot(a.graph, x, y);
  if (gpx::Node *n = a.graph.add_node("ImportObject", x, y)) {
    if (gpx::Attribute *f = n->attrs.find("file")) f->s = file;
    if (gpx::Attribute *on = n->attrs.find("object")) on->s = o.name;
    write_transform(n, o);
    o.driver_node = n->id;
    a.selected_node = n->id;
    if (at.scatter) bind_scatter(a, o, at, height_m, x, y);
  }
  SceneState &sc = scene();
  sc.objects.push_back(std::move(o));
  const int idx = (int)sc.objects.size() - 1;
  sc.selected = idx;
  a.scene_selection_serial++;
  a.graph_layout_serial++;
  a.request_eval();
  a.status = "added " + sc.objects[(size_t)idx].name;
  log_info("plants", a.status + " (" + file + ")");
  return idx;
}

// The model file and height a request means: the variant's, or the plant's.
void model_of(const PlantEntry &p, const PlantPlace &at, std::string &file, float &height_m) {
  const PlantVariant *v = plant_variant(p, at.variant);
  file = v ? v->model : p.model;
  height_m = v && v->height_m > 0.f ? v->height_m : p.height_m;
}

// ---- the worker's queue -----------------------------------------------------
struct Pending {
  PlantEntry plant;
  PlantPlace at;
  std::string file;
  float height_m = 0;
  SceneObject object;
  std::string err;
  std::atomic<bool> done{false};
};
std::mutex g_queue_mtx;
std::vector<std::shared_ptr<Pending>> g_queue; // guarded by g_queue_mtx

} // namespace


// A grown species into the scene: the object its root drives, standing where
// `at` says, bound to a Scatter points node when asked. The object is made
// now with no geometry and adopted by the root at the next evaluation
// (scene_plants_species.cpp), which is what lets Scatter bind to it at once.
int plant_place_species(App &a, gpx::Node &root, const PlantPlace &at, float height_m) {
  SceneObject o;
  o.type = SceneObject::Mesh;
  std::string want = !at.name.empty() ? at.name : root.attrs.get_s("object");
  if (want.empty()) want = root.attrs.get_s("name");
  if (want.empty()) want = "Plant";
  o.name = unique_name(want);
  o.path = "species:" + std::to_string(root.id);
  o.color[0] = o.color[1] = o.color[2] = 1.f;
  o.plant = true;
  o.driver_node = root.id;
  float where[3];
  stand(a, at, where);
  for (int k = 0; k < 3; ++k) o.pos[k] = where[k];
  o.scale = std::max(height_m, 0.01f) * std::max(at.size, 1e-4f) / tile_m();
  o.yaw = heading_in_range(at.heading_deg);
  if (gpx::Attribute *on = root.attrs.find("object")) on->s = o.name;
  // The placement's size belongs to the object standing in the scene, and it
  // is already in o.scale above. Writing it onto the species' own scale threw
  // away what the archetype had worked out - how big this plant grows - so a
  // described plant came out the wrong size the moment it was placed.
  write_transform(&root, o);
  if (at.scatter) bind_scatter(a, o, at, height_m, root.pos_x, root.pos_y);
  SceneState &scn = scene();
  scn.objects.push_back(std::move(o));
  const int idx = (int)scn.objects.size() - 1;
  scn.selected = idx;
  a.scene_selection_serial++;
  return idx;
}

// A species from the library: its file loaded into the graph, its plant
// standing where the request says. Returns the object's index or -1.
int add_species_entry(App &a, const PlantEntry &p, const PlantPlace &at, std::string &err) {
  SpeciesMake mk;
  mk.at = at;
  mk.place = true;
  const uint64_t root = species_load(a, p.model, mk, err);
  return root ? species_object(root) : -1;
}

int plant_add(App &a, const PlantEntry &p, const PlantPlace &at, std::string &err) {
  if (p.source == "species") return add_species_entry(a, p, at, err);
  if (!p.kind.empty()) return add_builtin(a, p, at, err);
  std::string file;
  float height_m = 0;
  model_of(p, at, file, height_m);
  SceneObject o;
  // a script asked for it and waits for it: a big scan takes seconds, which
  // is the load and not a hang, so the watchdog is told
  hang_watch_pause(true);
  const bool loaded = load_model(file, o, err);
  hang_watch_pause(false);
  if (!loaded) return -1;
  return finish_model(a, p, at, std::move(o), file, height_m, err);
}

bool plant_add_async(App &a, const PlantEntry &p, const PlantPlace &at, std::string &err) {
  if (p.source == "species") return add_species_entry(a, p, at, err) >= 0;
  if (!p.kind.empty()) return add_builtin(a, p, at, err) >= 0;
  auto job = std::make_shared<Pending>();
  job->plant = p;
  job->at = at;
  model_of(p, at, job->file, job->height_m);
  {
    std::lock_guard<std::mutex> lk(g_queue_mtx);
    g_queue.push_back(job);
  }
  a.status = "loading " + p.name + "...";
  std::thread([job]() {
    if (!load_model(job->file, job->object, job->err) && job->err.empty())
      job->err = "could not read " + job->file;
    job->done.store(true);
  }).detach();
  return true;
}

void plant_place_service(App &a) {
  std::vector<std::shared_ptr<Pending>> ready;
  {
    std::lock_guard<std::mutex> lk(g_queue_mtx);
    for (const auto &j : g_queue)
      if (j->done.load()) ready.push_back(j);
  }
  for (const auto &j : ready) {
    std::string err = j->err;
    if (err.empty() &&
        finish_model(a, j->plant, j->at, std::move(j->object), j->file, j->height_m, err) < 0 &&
        err.find("busy") != std::string::npos)
      continue; // the graph was busy: next frame
    if (!err.empty()) {
      a.status = err;
      log_error("plants", err);
    }
    std::lock_guard<std::mutex> lk(g_queue_mtx);
    g_queue.erase(std::remove(g_queue.begin(), g_queue.end(), j), g_queue.end());
  }
}

bool plant_place_busy(std::string *what) {
  std::lock_guard<std::mutex> lk(g_queue_mtx);
  if (g_queue.empty()) return false;
  if (what) *what = g_queue.front()->plant.name;
  return true;
}

} // namespace studio
