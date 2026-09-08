// Geekatplay TerraForge - see eco_dynamic.hpp.
#include "eco_dynamic.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "scatter_lod.hpp"
#include "scene.hpp"
#include "gpx/planet_math.hpp"
#include "gpx/scatter.hpp"
#include "nodes/scatter_attrs.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace studio {

namespace scatter = gpx::scatter; // the distribution engine
namespace eco = gpx::eco;         // the dials, declared once with the nodes

const gpx::Heightmap *app_placed_terrain(); // app_services.cpp

namespace {

// How many cells may be generated in one frame. A big camera move fills in
// over a few frames rather than dropping one, which is the whole reason the
// population is generated in pieces.
constexpr size_t kCellsPerFrame = 12;
// The most cells one layer keeps. Beyond this the farthest are retired even
// if they are still inside the radius - a runaway density cannot eat memory.
constexpr size_t kMaxCells = 4096;

// The ground the copies stand on, as a function of tile-space position.
// Inside the tile that is the heightmap the viewport draws; outside it, the
// infinite surround's own layers, at the amplitude and base the surround
// shader uses (studio/planet_renderer.cpp, `infinite_draw`), so a copy
// stands on the ground the camera can see rather than near it.
struct DynamicGround {
  const gpx::Heightmap *tile = nullptr;
  float hscale = 1.f;
  float amp = 0.f, base = 0.f;
  std::vector<gpx::planet::Layer> layers;

  float at(float u, float v) const {
    if (tile && !tile->empty() && u >= 0.f && u <= 1.f && v >= 0.f && v <= 1.f)
      return tile->sample(u, v) * hscale;
    if (layers.empty()) return base;
    // the surround's own relief. The octave budget is fixed here where the
    // shader's falls off with distance: the difference is the amplitude of
    // octaves too small to see at the distance a copy is placed.
    const float d[3] = {u, 0.37f, v};
    return gpx::planet::heightf(d, layers.data(), (int)layers.size(), 9.f) * amp + base;
  }
};

DynamicGround collect_ground(const App &a) {
  (void)a;
  DynamicGround g;
  const RenderSettings &rs = render_settings();
  g.tile = app_placed_terrain();
  g.hscale = rs.height_scale;
  g.amp = rs.height_scale * 1.2f;
  g.base = renderer_ground_base();
  SceneState &sc = scene();
  for (int idx : scene_surface_layers(-1)) {
    if (g.layers.size() >= 6) break; // the surround shader's own ceiling
    gpx::planet::Layer L = sc.objects[(size_t)idx].surf.layer;
    L.amplitude *= sc.objects[(size_t)idx].surf.height_scale;
    g.layers.push_back(L);
  }
  return g;
}

// One realised cell of one layer.
struct CellKey {
  uint64_t node;
  long long x, z;
  bool operator<(const CellKey &o) const {
    if (node != o.node) return node < o.node;
    if (z != o.z) return z < o.z;
    return x < o.x;
  }
};

struct Cache {
  std::map<CellKey, gpx::PointCloud> cells;
  std::map<uint64_t, uint64_t> rule;   // the settings each layer was built with
  long long anchor_x = 0, anchor_z = 0; // the camera cell the set was built for
  bool have_anchor = false;
  uint64_t serial = 0;                 // bumped whenever the set changes
};
Cache &cache() {
  static Cache c;
  return c;
}

// Everything about a layer that changes where its copies stand. A change
// clears that layer's cells; the camera moving does not.
uint64_t rule_hash(const gpx::Node &n) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t bytes) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < bytes; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  };
  for (const gpx::Attribute &at : n.attrs.items) {
    mix(at.key.data(), at.key.size());
    mix(&at.f, sizeof at.f);
    mix(&at.i, sizeof at.i);
    mix(&at.b, sizeof at.b);
    mix(&at.seed, sizeof at.seed);
    mix(at.v2, sizeof at.v2);
  }
  return h;
}

// The cell size a density wants: big enough that a cell is worth the visit,
// small enough that one holds a manageable crowd.
float cell_size_m(float per_hectare, float spacing_m) {
  const float per_m2 = std::max(per_hectare, 1e-4f) / 10000.f;
  const float want = std::sqrt(400.f / per_m2); // ~400 candidates a cell
  return std::clamp(want, std::max(spacing_m, 1.f), 400.f);
}

} // namespace

void app_service_population(App &a) {
  SceneState &sc = scene();
  Cache &C = cache();

  // which layers are unbounded, and which meshes stand for them
  struct Job {
    gpx::Node *node = nullptr;
    float population_m = 0.f, cell_m = 0.f;
    std::vector<SceneObject *> meshes;
  };
  std::vector<Job> jobs;
  {
    std::unique_lock<App::GraphMutex> lock(a.graph_mtx, std::try_to_lock);
    if (!lock.owns_lock()) return; // evaluation has it; next frame will do
    for (auto &np : a.graph.nodes) {
      gpx::Node &n = *np;
      if (n.type != "EcosystemLayer" || !n.enabled) continue;
      if (!n.attrs.get_b("unbounded", false) || !n.attrs.get_b("enabled", true)) continue;
      Job j;
      j.node = &n;
      j.population_m = n.attrs.get_f("population_m", 2000.f);
      j.cell_m = cell_size_m(n.attrs.get_f("density", 20.f), n.attrs.get_f("spacing_m", 5.f));
      for (SceneObject &o : sc.objects)
        if (o.type == SceneObject::Mesh && o.scatter_node == n.id) j.meshes.push_back(&o);
      if (!j.meshes.empty()) jobs.push_back(std::move(j));
    }
    if (jobs.empty()) {
      if (!C.cells.empty()) { C.cells.clear(); C.rule.clear(); C.have_anchor = false; }
      return;
    }

    const RenderSettings &rs = render_settings();
    const float size_m = std::max(rs.terrain_size_m, 1.f);
    // the eye the viewport is actually drawing from: an activated scene
    // camera when there is one, the orbit camera otherwise. Populating
    // around the wrong one puts the whole crowd behind the viewer.
    float eye[3], target[3], fovy = 45.f;
    renderer_get_camera(eye, target, &fovy);
    const int cam = scene_active_camera();
    if (cam >= 0 && cam < (int)sc.objects.size() &&
        sc.objects[(size_t)cam].type == SceneObject::Camera)
      for (int k = 0; k < 3; ++k) eye[k] = sc.objects[(size_t)cam].cam.eye[k];
    const float eye_x = eye[0] * size_m, eye_z = eye[2] * size_m;
    const DynamicGround ground = collect_ground(a);
    // the tile's own height range, in metres: the band a user set against
    // the terrain they can see means the same heights out on the surround
    float tile_lo = 0.f, tile_hi = 1.f;
    if (ground.tile && !ground.tile->empty()) ground.tile->minmax(tile_lo, tile_hi);
    const float band_lo = tile_lo * rs.height_scale * size_m;
    const float band_span = std::max((tile_hi - tile_lo) * rs.height_scale * size_m, 1e-3f);

    bool changed = false;
    size_t budget = kCellsPerFrame;
    for (Job &j : jobs) {
      const uint64_t id = j.node->id;
      const uint64_t rh = rule_hash(*j.node);
      auto it = C.rule.find(id);
      if (it == C.rule.end() || it->second != rh) {
        // the rule moved: everything this layer had is stale
        for (auto c = C.cells.begin(); c != C.cells.end();)
          c = c->first.node == id ? C.cells.erase(c) : std::next(c);
        C.rule[id] = rh;
        changed = true;
      }

      std::vector<WorldCell> want;
      visible_cells(eye_x, eye_z, j.population_m, j.cell_m, kMaxCells, want);

      // retire what is no longer wanted
      {
        std::vector<CellKey> keep;
        keep.reserve(want.size());
        for (const WorldCell &w : want) keep.push_back({id, w.x, w.z});
        std::sort(keep.begin(), keep.end());
        for (auto c = C.cells.begin(); c != C.cells.end();) {
          if (c->first.node != id) { ++c; continue; }
          if (std::binary_search(keep.begin(), keep.end(), c->first)) { ++c; continue; }
          c = C.cells.erase(c);
          changed = true;
        }
      }

      // realise what is missing, nearest first, a few per frame
      scatter::Presence pr = eco::read_presence(*j.node, nullptr, nullptr, nullptr);
      pr.ground = [&ground, size_m](float x_m, float z_m) {
        return ground.at(x_m / size_m, z_m / size_m) * size_m;
      };
      pr.step = std::max(j.cell_m * 0.05f, 0.5f);
      // the altitude band, read against the terrain the user authored on
      pr.alt_lo = band_lo + pr.alt_lo * band_span;
      pr.alt_hi = band_lo + pr.alt_hi * band_span;
      scatter::Transform tr = eco::read_transform(*j.node, nullptr, size_m);
      // the transform's lengths are tile fractions; out here they are metres
      tr.offset *= size_m;
      tr.radius *= size_m;
      tr.shrink_radius *= size_m;
      scatter::Interaction it_own;
      it_own.avoid_overlap = j.node->attrs.get_b("avoid_overlap", true);

      scatter::CandidateParams cp;
      cp.seed = j.node->attrs.get_seed("seed");
      cp.spacing = j.cell_m;
      cp.mode = j.node->attrs.get_choice("placement");
      cp.clump_amount = j.node->attrs.get_f("clump_amount", 0.f);
      cp.clump_size = j.node->attrs.get_f("clump_size_m", 60.f);
      const double per_m2 = std::max(j.node->attrs.get_f("density", 20.f), 1e-4f) / 10000.0;
      const double target = per_m2 * (double)j.cell_m * (double)j.cell_m;
      cp.per_cell = (int)std::clamp(std::ceil(target * 2.0), 1.0, 4096.0);
      const float rate = (float)std::min(target / std::max(cp.per_cell, 1), 1.0);

      for (const WorldCell &w : want) {
        if (budget == 0) break;
        const CellKey key{id, w.x, w.z};
        if (C.cells.count(key)) continue;
        gpx::PointCloud pc;
        scatter::candidates_cell(cp, w.x, w.z, pc);
        scatter::filter(pc, pr, rate);
        scatter::transform(pc, tr);
        scatter::interact(pc, it_own);
        C.cells.emplace(key, std::move(pc));
        --budget;
        changed = true;
      }
    }
    if (!changed) return;
    ++C.serial;

    // ---- write the streams -------------------------------------------------
    // one cell of the world is one cell of the LOD lattice, so the distance
    // thinning downstream needs no idea any of this happened
    for (Job &j : jobs) {
      const uint64_t id = j.node->id;
      for (SceneObject *op : j.meshes) {
        SceneObject &o = *op;
        o.inst.clear();
        o.inst_cells.clear();
        for (const auto &kv : C.cells) {
          if (kv.first.node != id) continue;
          const gpx::PointCloud &pc = kv.second;
          if (pc.size() == 0) continue;
          InstanceCell cell;
          cell.first = o.inst_count();
          bool first = true;
          for (size_t i = 0; i < pc.size(); ++i) {
            if (pc.has_attrs() && o.scatter_species >= 0 &&
                pc.species[i] != o.scatter_species)
              continue;
            const float px = pc.x[i] / size_m, pz = pc.y[i] / size_m;
            const float gy = ground.at(px, pz) + (pc.has_attrs() ? pc.offset[i] / size_m : 0.f);
            // the ground's normal, for copies that grow from the surface
            const float s = std::max(j.cell_m * 0.05f, 0.5f) / size_m;
            float nx = -(ground.at(px + s, pz) - ground.at(px - s, pz)) / (2.f * s);
            float nz = -(ground.at(px, pz + s) - ground.at(px, pz - s)) / (2.f * s);
            const float nl = std::sqrt(nx * nx + 1.f + nz * nz);
            nx /= nl; nz /= nl;
            const float sc_i = o.scatter_scale;
            const float row[SceneObject::INST_FLOATS] = {
                px, gy, pz, sc_i,
                std::cos(pc.yaw[i]), std::sin(pc.yaw[i]), pc.tint[i], pc.phase[i],
                pc.sx[i], pc.sy[i], pc.sz[i], pc.tilt[i],
                nx, 1.f / nl, nz, scatter::unit(pc.id[i], 77)};
            o.inst.insert(o.inst.end(), row, row + SceneObject::INST_FLOATS);
            if (first) {
              for (int k = 0; k < 3; ++k) { cell.lo[k] = row[k]; cell.hi[k] = row[k]; }
              first = false;
            } else {
              for (int k = 0; k < 3; ++k) {
                cell.lo[k] = std::min(cell.lo[k], row[k]);
                cell.hi[k] = std::max(cell.hi[k], row[k]);
              }
            }
            ++cell.count;
          }
          if (cell.count > 0) o.inst_cells.push_back(cell);
        }
        o.inst_revision = C.serial * 1000003ull + (uint64_t)(op - sc.objects.data());
      }
    }
  }
}

} // namespace studio
