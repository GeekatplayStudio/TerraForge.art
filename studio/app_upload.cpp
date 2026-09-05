#include "app.hpp"
#include "console.hpp"
#include "perf.hpp"
#include "render_settings.hpp"
#include "planet_place.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/planet_math.hpp"
#include "terrain_upload.hpp"
#include "terrain_cull.hpp"
#include <future>
#include <optional>
#include <GLFW/glfw3.h>

namespace studio {

// ---------------------------------------------------------- placement
// The tile the graph last produced and the albedo that went with it, so a
// change to the planet's layers or the placement settings can re-place it
// without re-evaluating the graph.
static std::shared_ptr<gpx::Heightmap> g_last_tile;
static std::shared_ptr<const gpx::TextureRGBA> g_last_albedo;
static uint64_t g_place_key = 0;
static uint64_t g_prepared_serial = ~0ull;
struct PlacementRequest {
  std::shared_ptr<const gpx::Heightmap> tile;
  std::shared_ptr<const gpx::TextureRGBA> albedo;
  std::vector<gpx::planet::Layer> layers;
  PlaceSettings settings;
  uint64_t serial, key;
};
static std::optional<PlacementRequest> g_placement_next;
static std::future<TerrainUpload> g_placement_work;

static uint64_t placement_key() {
  const RenderSettings &rs = render_settings();
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  };
  // Field by field, never the struct's bytes: PlaceSettings has padding
  // after its bool, and hashing indeterminate padding made the key change
  // every frame - which re-placed the tile onto the planet and re-uploaded
  // the terrain every frame, 9 ms of work for a picture that had not moved.
  for (const gpx::planet::Layer &L : planet_home_layers()) {
    mix(&L.seed, sizeof L.seed);
    mix(&L.type, sizeof L.type);
    mix(&L.frequency, sizeof L.frequency);
    mix(&L.amplitude, sizeof L.amplitude);
    mix(&L.octaves, sizeof L.octaves);
    mix(&L.coverage, sizeof L.coverage);
    mix(&L.mask_scale, sizeof L.mask_scale);
  }
  const unsigned char on = rs.place_on_planet ? 1 : 0;
  mix(&on, 1);
  mix(&rs.place_edge, sizeof rs.place_edge);
  mix(&rs.place_flatten, sizeof rs.place_flatten);
  mix(&rs.place_presence, sizeof rs.place_presence);
  mix(&rs.place_ground, sizeof rs.place_ground);
  return h;
}

// Composite the tile onto the planet and hand the result to the renderer.
// Everything downstream - picking, shadows, culling bounds, overlays, the
// surround's base level - sees the placed map, so what you click is what you
// see. Caller holds graph_mtx.
static void upload_placed_terrain(App &a, const std::shared_ptr<gpx::Heightmap> &hm,
                                  std::shared_ptr<const gpx::TextureRGBA> albedo) {
  const auto &rs = render_settings();
  g_place_key = placement_key();
  g_placement_next = PlacementRequest{hm, std::move(albedo), planet_home_layers(),
      {rs.place_on_planet, rs.place_edge, rs.place_flatten, rs.place_presence, rs.place_ground},
      a.eval_serial, g_place_key};
}

static void service_placement(App &a) {
  if (g_placement_work.valid()) {
    if (g_placement_work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
      auto ready = g_placement_work.get();
      if (ready.serial == a.eval_serial && ready.key == placement_key() && !g_placement_next) {
        planet_place_set_last(ready.placement);
        renderer_set_terrain_base(ready.placement.ground);
        renderer_set_terrain_prepared(ready);
        app_set_overlay_terrain(ready.height);
        a.uploaded_serial = ready.serial;
      } else if (!g_placement_next && ready.serial != a.eval_serial &&
                 g_prepared_serial == a.eval_serial) {
        // The newer graph no longer has a terrain output; retain the image.
        a.uploaded_serial = g_prepared_serial;
      }
    } catch (const std::exception &e) {
      a.status = e.what();
      log_error("placement", e.what());
      // A failed job must not acknowledge a newer queued result as uploaded.
      // Keep the last successfully displayed serial until a job commits.
    }
  }
  if (!g_placement_next) return;
  auto request = std::move(*g_placement_next);
  g_placement_next.reset();
  g_placement_work = std::async(std::launch::async, [request = std::move(request)] {
    TerrainUpload ready;
    ready.serial = request.serial;
    ready.key = request.key;
    ready.albedo = request.albedo;
    ready.height = std::make_shared<gpx::Heightmap>(planet_place_tile(
        *request.tile, request.layers, request.settings, &ready.placement));
    const auto &h = *ready.height;
    ready.picking = h.w > 256 ? h.resampled(256, 256) : h;
    ready.bounds = patch_height_bounds(h, TERRAIN_PATCHES_PER_EDGE);
    double sum = 0.;
    for (float v : h.v) sum += v;
    ready.mean = h.v.empty() ? 0.f : (float)(sum / h.v.size());
    glfwPostEmptyEvent();
    return ready;
  });
}

// Commit completed evaluations on the context owner before views draw.
void app_service_upload(App &a) {
    service_placement(a);
    std::unique_lock<std::mutex> upload_lock(a.graph_mtx, std::try_to_lock);
    if (!upload_lock.owns_lock()) return;
    // upload fresh eval results to GPU (main thread only)
    if (g_prepared_serial != a.eval_serial && !a.eval.running.load()) {
      // skip thumbnail regeneration during interactive drags — keeps the
      // slider->viewport loop as tight as possible
      if (!a.eval_interactive.load()) previews_update(a);
      perf_mark("previews");
      // Terragen-style: atmosphere/render nodes drive the renderer
      apply_scene_nodes(a);

      // A TerrainDisplacement node compiles its field graph to GLSL and the
      // viewport evaluates it per vertex. Transpiling is pure string work on
      // the CPU; the relink happens at draw time where the context is current.
      {
        // Compile whichever sink is present into the shader it drives. Both
        // follow the same shape: find the node, transpile what feeds it, hand
        // the source to the renderer, and say so on the node when we cannot.
        // Buffers the generated programs sample, collected across both sinks.
        std::vector<std::pair<std::string, const gpx::Heightmap *>> field_tex;
        // A sampler is named after the Sample node that declared it, so the
        // buffer to bind is that node's own input.
        auto collect_samplers = [&](const gpx::GlslProgram &prog) {
          for (const std::string &s : prog.samplers) {
            size_t us = s.rfind('_');
            if (us == std::string::npos) continue;
            uint64_t id = 0;
            try {
              id = std::stoull(s.substr(us + 1));
            } catch (const std::exception &) {
              continue;
            }
            gpx::Node *sn = a.graph.find_node(id);
            const gpx::Heightmap *hm = sn ? sn->in_hmap("input") : nullptr;
            if (hm && !hm->empty()) field_tex.emplace_back(s, hm);
          }
        };

        auto compile_node = [&](gpx::Node *sink, const char *type,
                                const char *port, const char *fn) -> std::string {
          if (!sink || !sink->attrs.get_b("live", true) ||
              !sink->field_connected(port))
            return {};
          gpx::Node *src = a.graph.upstream_node(*sink, port);
          const gpx::Port *sp = a.graph.upstream(*sink, port);
          if (!src) return {};
          gpx::GlslProgram prog =
              gpx::field_to_glsl(*src, sp ? sp->name : "", fn);
          if (!prog.ok) {
            sink->error = prog.error;
            return {};
          }
          // A graph reading a buffer needs its textures bound. There are only
          // so many units to spare, so say plainly when a graph asks for more
          // rather than binding some and sampling black for the rest.
          if (prog.samplers.size() > 4) {
            sink->error = "too many sampled buffers in one graph (max 4)";
            return {};
          }
          // The procedural surfaces are unbounded and have no heightmap, so
          // there is nothing a Sample node could read there. Refusing is
          // honest; binding an empty texture and shading black is not.
          if (std::string(type) == "SurfaceDisplacement" &&
              !prog.samplers.empty()) {
            sink->error = "planets and the infinite ground plane cannot read\n"
                          "buffers - they have no heightmap. Use field nodes\n"
                          "only, or Rasterize into the terrain tile instead.";
            return {};
          }
          collect_samplers(prog);
          sink->error.clear();
          if (std::string(type) == "TerrainDisplacement")
            render_settings().field_displacement =
                sink->attrs.get_f("strength", 0.05f);
          return prog.code;
        };
        // the last enabled sink of a type is the one that counts
        auto compile_sink = [&](const char *type, const char *port,
                                const char *fn) -> std::string {
          gpx::Node *sink = nullptr;
          for (auto &cand : a.graph.nodes)
            if (cand->type == type && cand->enabled) sink = cand.get();
          return compile_node(sink, type, port, fn);
        };
        renderer_set_field_program(
            compile_sink("TerrainDisplacement", "field", "gpx_terrain_field"),
            a.eval_serial);
        renderer_set_surface_program(
            compile_sink("TerrainSurface", "color", "gpx_terrain_surface"),
            compile_sink("TerrainSurface", "roughness", "gpx_terrain_rough"),
            compile_sink("TerrainSurface", "bump", "gpx_terrain_bump"),
            a.eval_serial);
        // The same bridge, one domain out: this one shapes every surface that
        // has no heightmap to bake into - planets and the endless ground
        // plane. See engine/nodes/nodes_displace.cpp, SurfaceDisplacement.
        // Every SurfaceDisplacement node is its own program: a planet names
        // the one that shapes it, the way a Terragen planet owns its terrain
        // network, and several worlds can each carry a different graph.
        {
          std::vector<unsigned long long> live;
          for (auto &cand : a.graph.nodes) {
            if (cand->type != "SurfaceDisplacement") continue;
            live.push_back(cand->id);
            std::string surf = cand->enabled
                                   ? compile_node(cand.get(), "SurfaceDisplacement",
                                                  "field", "gpx_surface_field")
                                   : std::string();
            planet_set_field_program(cand->id, surf,
                                     cand->attrs.get_f("strength", 1.f));
          }
          planet_field_programs_keep(live);
        }
        for (auto &cand : a.graph.nodes)
          if (cand->type == "TerrainSurface" && cand->enabled)
            renderer_set_surface_bump(cand->attrs.get_f("bump_strength", 1.f),
                                      cand->attrs.get_f("bump_scale", 0.004f));
        renderer_set_field_textures(field_tex, a.eval_serial);
      }
      uint64_t view = a.view_node ? a.view_node : a.selected_node;
      gpx::Node *n = a.graph.find_node(view);
      // a TerrainOutput node is the canonical final terrain — prefer it
      // unless the user explicitly pinned another node
      if (!a.view_node) {
        for (auto &cand : a.graph.nodes)
          if (cand->type == "TerrainOutput") n = cand.get();
      }
      if (!n) {
        // fall back to last node with a heightmap output
        for (auto &cand : a.graph.nodes)
          if (cand->first_out(gpx::DataType::Heightmap)) n = cand.get();
      }
      if (n) {
        gpx::Port *ph = n->first_out(gpx::DataType::Heightmap);
        gpx::Port *pt = n->first_out(gpx::DataType::Texture);
        // if the node has no texture out, look downstream? keep simple:
        // search graph for a TerrainTexture/Colorize whose input chain includes n
        const gpx::TextureRGBA *albedo = pt && pt->tex ? pt->tex.get() : nullptr;
        // material assignment: a MaterialOutput node assigned to the terrain
        // object wins over the older albedo-source modes
        RenderSettings &rs = render_settings();
        gpx::Node *mat_out = nullptr;
        for (const SceneObject &o : scene().objects)
          if (o.type == SceneObject::Terrain && o.material_node)
            mat_out = a.graph.find_node(o.material_node);
        if (mat_out && mat_out->type == "MaterialOutput") {
          const gpx::TextureRGBA *base = mat_out->in_tex("base color");
          albedo = (base && !base->empty()) ? base : nullptr;
          const gpx::AttrSet &at = mat_out->attrs;
          rs.mat_roughness = at.get_f("roughness", rs.mat_roughness);
          rs.mat_metallic = at.get_f("metallic", rs.mat_metallic);
          rs.mat_specular = at.get_f("specular", rs.mat_specular);
          rs.mat_reflection = at.get_f("reflection", rs.mat_reflection);
          rs.mat_translucency = at.get_f("translucency", rs.mat_translucency);
          rs.mat_transparency = at.get_f("transparency", rs.mat_transparency);
          rs.mat_normal_strength = at.get_f("normal_strength", rs.mat_normal_strength);
          rs.mat_displacement = at.get_f("displacement", rs.mat_displacement);
          rs.matp = gpx::material_params_from(at);
        } else if (rs.terrain_material_mode == 1) {
          albedo = nullptr; // procedural in-shader material
        } else if (rs.terrain_material_mode == 2) {
          albedo = nullptr;
          if (gpx::Node *mn = a.graph.find_node(rs.terrain_material_node)) {
            gpx::Port *mp = mn->first_out(gpx::DataType::Texture);
            if (mp && mp->tex && !mp->tex->empty()) albedo = mp->tex.get();
          }
        } else if (!albedo) {
          // auto: the last composite albedo in the graph; skip data textures
          for (auto &cand : a.graph.nodes) {
            if (cand->type == "Splatmap" || cand->type == "NormalMap" ||
                cand->type == "AlbedoToPBR")
              continue;
            gpx::Port *cpt = cand->first_out(gpx::DataType::Texture);
            if (cpt && cpt->tex && !cpt->tex->empty()) albedo = cpt->tex.get();
          }
        }
        if (ph && ph->hmap && !ph->hmap->empty()) {
          // Detach from mutable graph buffers before the worker reads them.
          g_last_tile = std::make_shared<gpx::Heightmap>(*ph->hmap);
          g_last_albedo = albedo ? std::make_shared<gpx::TextureRGBA>(*albedo) : nullptr;
          upload_placed_terrain(a, g_last_tile, g_last_albedo);
        }
        else if (pt && pt->tex) {
          // texture-only node: keep last heightmap, update albedo
        }
      }
      g_prepared_serial = a.eval_serial;
      if (!g_placement_next && !g_placement_work.valid()) a.uploaded_serial = a.eval_serial;
    }
    // The placement also changes when the planet's layers or the placement
    // settings do, with no evaluation in between; re-place the last tile.
    {
      uint64_t key = placement_key();
      if (key != g_place_key && g_last_tile && g_prepared_serial == a.eval_serial &&
          !a.eval.running.load()) {
        g_place_key = key;
        upload_placed_terrain(a, g_last_tile, g_last_albedo);
      }
    }

    perf_mark("place");
    service_placement(a);
}

void app_service_upload_shutdown() {
  g_placement_next.reset();
  if (g_placement_work.valid()) {
    try { g_placement_work.get(); } catch (const std::exception &) {}
  }
}

} // namespace studio
