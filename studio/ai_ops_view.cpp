// Geekatplay TerraForge — viewport and surface-quality operations for the
// API/MCP surface.
//
// The scripting bridge could set the sun, the fog, the water and the clouds,
// but nothing about how the terrain surface itself is drawn: subdivision,
// culling, height scale, planetary radius, fractal relief, displacement
// strength, shadows. Those are all things the UI can change and scripting
// could not, which is the gap the standing rule "anything the UI can do,
// scripting can do" exists to close.
//
// They also happen to be the settings a performance measurement needs to turn
// on and off, which is how the omission was noticed.
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

namespace {

// Read one value if it is present, clamped, and say whether it was.
bool take_f(const json &j, const char *key, float &dst, float lo, float hi) {
  if (!j.contains(key) || !j[key].is_number()) return false;
  dst = std::clamp(j[key].get<float>(), lo, hi);
  return true;
}
bool take_b(const json &j, const char *key, bool &dst) {
  if (!j.contains(key) || !j[key].is_boolean()) return false;
  dst = j[key].get<bool>();
  return true;
}

} // namespace

// Returns 1 when handled and something changed, 0 when handled but nothing
// did, -1 when the op is not ours.
int ai_view_op(App &a, const std::string &op, const json &act,
               std::string &err) {
  if (op != "set_viewport") return -1;
  RenderSettings &rs = render_settings();
  int n = 0;

  // adaptive subdivision
  n += take_b(act, "tessellation", rs.tessellation);
  n += take_f(act, "tess_pixels", rs.tess_pixels, 1.f, 64.f);
  n += take_f(act, "tess_min", rs.tess_min, 1.f, 64.f);
  n += take_f(act, "tess_max", rs.tess_max, 1.f, 64.f);
  n += take_b(act, "frustum_cull", rs.frustum_cull);
  // surface shape
  n += take_f(act, "height_scale", rs.height_scale, 0.f, 8.f);
  n += take_f(act, "planet_radius", rs.planet_radius, 0.f, 1e7f);
  n += take_f(act, "fractal_detail", rs.fractal_detail, 0.f, 1.f);
  n += take_f(act, "fractal_scale", rs.fractal_scale, 0.1f, 4096.f);
  n += take_f(act, "field_displacement", rs.field_displacement, -8.f, 8.f);
  // shading and exposure
  n += take_b(act, "wireframe", rs.wireframe);
  n += take_b(act, "shadows", rs.shadows);
  n += take_f(act, "shadow_softness", rs.shadow_softness, 0.f, 8.f);
  n += take_f(act, "exposure", rs.exposure, 0.01f, 16.f);
  n += take_b(act, "use_albedo", rs.use_albedo);

  // The graph's memory ceiling, in megabytes. 0 lifts it. Lives here rather
  // than with the graph ops because it is a machine setting, not a document
  // one: it never changes what the graph computes, only how much of it is
  // kept in memory at once.
  if (act.contains("graph_memory_mb") && act["graph_memory_mb"].is_number()) {
    prefs().graph_memory_mb =
        std::clamp(act["graph_memory_mb"].get<int>(), 0, 1024 * 1024);
    ++n;
  }

  if (act.contains("layout") && act["layout"].is_number())
    rs.viewport_layout = std::clamp(act["layout"].get<int>(), 0, 5), ++n;
  if (act.contains("engine") && act["engine"].is_number())
    rs.viewport_engine = std::clamp(act["engine"].get<int>(), 0, 1), ++n;

  if (!n) {
    err = "set_viewport: no recognised setting in the action";
    return 0;
  }
  a.status = "viewport settings applied";
  return 1;
}

} // namespace studio
