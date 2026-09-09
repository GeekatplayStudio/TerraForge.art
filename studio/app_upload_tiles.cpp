// Geekatplay TerraForge — the placement pipeline for every terrain tile
// after the first.
//
// app_upload.cpp prepares tile 0 the way it always did: copy the chain's
// heightmap, place it on the planet on a worker, upload the result. Each
// further Terrain object (terrain_tiles.hpp) gets the same three steps
// here, one job per tile, the same placement settings, its own material.
// Grounding and imprint stay with tile 0; an extra tile carries no
// object imprint (surface_features_apply with nothing to apply).
#include "app.hpp"
#include "console.hpp"
#include "planet_place.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "surface_features.hpp"
#include "terrain_cull.hpp"
#include "terrain_tiles.hpp"
#include "terrain_upload.hpp"
#include "world_shape.hpp"
#include <GLFW/glfw3.h>
#include <future>
#include <optional>
#include <vector>

namespace studio {

namespace {

struct TileRequest {
  std::shared_ptr<const gpx::Heightmap> tile;
  std::shared_ptr<const gpx::TextureRGBA> albedo;
  std::vector<gpx::planet::Layer> layers;
  PlaceSettings settings;
  uint64_t serial = 0, key = 0;
};

struct TileJob {
  uint64_t output_node = 0;
  int object = -1;
  std::shared_ptr<const gpx::Heightmap> last_tile; // for a re-place when the settings change
  std::shared_ptr<const gpx::TextureRGBA> last_albedo;
  uint64_t last_key = 0;
  std::optional<TileRequest> next;
  std::future<TerrainUpload> work;
};
std::vector<TileJob> g_jobs;

void launch(TileJob &job) {
  TileRequest req = std::move(*job.next);
  job.next.reset();
  job.work = std::async(std::launch::async, [req = std::move(req)] {
    TerrainUpload ready;
    ready.serial = req.serial;
    ready.key = req.key;
    ready.albedo = req.albedo;
    ready.height = std::make_shared<gpx::Heightmap>(
        planet_place_tile(*req.tile, req.layers, req.settings, &ready.placement));
    ready.natural = std::make_shared<gpx::Heightmap>();
    surface_features_apply(*ready.height, SurfaceFeatures(), ready.natural.get());
    const gpx::Heightmap &h = *ready.height;
    ready.picking = h.w > 256 ? h.resampled(256, 256) : h;
    ready.bounds = patch_height_bounds(h, TERRAIN_PATCHES_PER_EDGE);
    double sum = 0.;
    for (float v : h.v) sum += v;
    ready.mean = h.v.empty() ? 0.f : (float)(sum / h.v.size());
    glfwPostEmptyEvent();
    return ready;
  });
}

} // namespace

// Caller holds the graph lock; terrain_tiles_bind(a) has run.
void extra_tiles_prepare(App &a) {
  std::vector<TerrainTileGpu> &tiles = terrain_tiles_extra();
  g_jobs.resize(tiles.size());
  const RenderSettings &rs = render_settings();
  for (size_t k = 0; k < tiles.size(); ++k) {
    TerrainTileGpu &t = tiles[k];
    TileJob &job = g_jobs[k];
    if (job.output_node != t.output_node) job = TileJob();
    job.output_node = t.output_node;
    job.object = t.object;
    gpx::Node *out = a.graph.find_node(t.output_node);
    if (!out) continue;
    gpx::Port *ph = out->first_out(gpx::DataType::Heightmap);
    if (!ph || !ph->hmap || ph->hmap->empty()) continue;
    // the tile's own material: its base colour as the albedo, its numbers
    // as the pass's scalars (the same reading app_upload.cpp does for tile 0)
    const gpx::TextureRGBA *albedo = nullptr;
    const SceneObject &o = scene().objects[(size_t)t.object];
    if (gpx::Node *m = o.material_node ? a.graph.find_node(o.material_node) : nullptr)
      if (m->type == "MaterialOutput") {
        const gpx::TextureRGBA *base = m->in_tex("base color");
        albedo = (base && !base->empty()) ? base : nullptr;
        const gpx::AttrSet &at = m->attrs;
        t.matp = gpx::material_params_from(at);
        t.mat[0] = at.get_f("roughness", 0.85f);
        t.mat[1] = at.get_f("metallic", 0.f);
        t.mat[2] = at.get_f("specular", 0.35f);
        t.mat[3] = at.get_f("reflection", 0.25f);
        t.mat[4] = at.get_f("translucency", 0.f);
        t.mat[5] = at.get_f("transparency", 0.f);
        t.mat[6] = at.get_f("normal_strength", 1.f);
        t.mat[7] = at.get_f("displacement", 0.f);
      }
    TileRequest req;
    req.tile = std::make_shared<gpx::Heightmap>(*ph->hmap);
    req.albedo = albedo ? std::make_shared<gpx::TextureRGBA>(*albedo) : nullptr;
    req.layers = planet_home_layers(object_side(render_settings(), o)); // its own face's ground
    req.settings = app_place_settings(a, out, t.object);
    req.serial = a.eval_serial;
    req.key = app_placement_key_for(t.object);
    job.last_tile = req.tile;
    job.last_albedo = req.albedo;
    job.last_key = req.key;
    job.next = std::move(req);
  }
  (void)rs;
}

// Main thread, every frame: collect what the workers finished, start what
// is queued, and re-place a tile whose settings changed under it.
void extra_tiles_service(App &a) {
  for (size_t k = 0; k < g_jobs.size(); ++k) {
    TileJob &job = g_jobs[k];
    const uint64_t key = app_placement_key_for(job.object);
    if (job.work.valid() &&
        job.work.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      try {
        TerrainUpload ready = job.work.get();
        terrain_tile_set_prepared((int)k + 1, ready);
      } catch (const std::exception &e) {
        log_error("placement", std::string("tile ") + std::to_string(k + 1) + ": " + e.what());
      }
    }
    if (!job.next && job.last_tile && job.last_key != key && !job.work.valid()) {
      // the placement settings changed: place the last copy again
      TileRequest req;
      req.tile = job.last_tile;
      req.albedo = job.last_albedo;
      req.layers = planet_home_layers(
          job.object >= 0 && job.object < (int)scene().objects.size()
              ? object_side(render_settings(), scene().objects[(size_t)job.object]) : 0);
      {
        std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
        if (!lk.owns_lock()) continue;
        req.settings = app_place_settings(a, a.graph.find_node(job.output_node), job.object);
      }
      req.serial = a.eval_serial;
      req.key = key;
      job.last_key = key;
      job.next = std::move(req);
    }
    if (job.next && !job.work.valid()) launch(job);
  }
}

void extra_tiles_shutdown() {
  for (TileJob &job : g_jobs) {
    job.next.reset();
    if (job.work.valid()) {
      try { job.work.get(); } catch (const std::exception &) {}
    }
  }
  g_jobs.clear();
}

} // namespace studio
