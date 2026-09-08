// Geekatplay TerraForge — the frame service behind MaterialSource.
//
// The engine has no scene, so a node that wants "the terrain's material" can
// only name the object; which node that turns out to be is a question only
// the studio can answer. This asks it once a frame, exactly as the imprint
// service writes its footprints - the graph declares the intent, the studio
// supplies the fact, and the engine still knows nothing about objects.
//
// The deciding is in material_source.cpp, where it can be tested without a
// window. What is left here is the lock and the wake-up.
#include "app.hpp"
#include "console.hpp"
#include "material_source.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <mutex>

namespace studio {

void app_service_material_sources(App &a) {
  std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
  if (!lk.owns_lock()) return; // next frame, then
  const int changed = material_sources_resolve(
      a.graph, scene(), render_settings().terrain_material_node);
  if (!changed) return;
  a.request_eval();
  log_info("material", std::to_string(changed) +
                           " material source(s) pointed at their object");
}

} // namespace studio
