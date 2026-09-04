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

// Renders the active camera's view straight to a PNG (renderer.cpp).
bool renderer_render_to_file(const std::string &path, int w, int h);

// Returns 1 when handled and something changed, 0 when handled but nothing
// did, -1 when the op is not ours.
int ai_view_op(App &a, const std::string &op, const json &act,
               std::string &err) {
  if (op == "capture") {
    // The viewport as a PNG. The UI could already do this from the File menu
    // and the render panel; scripting could not, so no automated check could
    // ever look at the picture — which is the only way to verify most of what
    // the renderer does.
    //
    // Safe to call here: actions are applied from the UI loop, which is the
    // thread holding the GL context.
    std::string path = act.value("path", std::string());
    if (path.empty()) {
      err = "capture needs a 'path'";
      return 0;
    }
    int w = std::clamp(act.value("width", 1280), 16, 8192);
    int h = std::clamp(act.value("height", 720), 16, 8192);
    if (!renderer_render_to_file(path, w, h)) {
      err = "capture: could not write " + path;
      return 0;
    }
    a.status = "captured " + path;
    return 1;
  }

  // Open or close one of the dockable panels by name. The Window menu does
  // the same thing; having it as an op means a script, the assistant or a
  // test can put a panel in front of the user without asking them to go
  // hunting through a menu.
  if (op == "show_panel") {
    std::string want = act.value("panel", std::string());
    for (auto &c : want) c = (char)tolower(c);
    const bool on = act.value("visible", true);
    struct Row { const char *key; bool *flag; };
    const Row rows[] = {
        {"library", &a.show_library},   {"nodes", &a.show_nodelist},
        {"properties", &a.show_properties}, {"viewport", &a.show_viewport},
        {"toolbar", &a.show_toolbar},   {"console", &a.show_console},
        {"timeline", &a.show_timeline}, {"preview", &a.show_preview},
        {"material editor", &a.show_material_editor},
    };
    for (const Row &r : rows)
      if (want == r.key) {
        *r.flag = on;
        a.status = std::string(on ? "opened " : "closed ") + r.key;
        return 1;
      }
    err = "show_panel: no panel called '" + want + "'";
    return 0;
  }

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
  n += take_f(act, "planet_radius", rs.planet_radius, 0.f, 1e12f);
  // placing the tile on the planet (studio/planet_place.cpp)
  n += take_b(act, "place_on_planet", rs.place_on_planet);
  n += take_f(act, "place_edge", rs.place_edge, 0.f, 0.5f);
  n += take_f(act, "place_flatten", rs.place_flatten, 0.f, 1.f);
  n += take_f(act, "place_presence", rs.place_presence, 0.001f, 1.f);
  n += take_f(act, "place_ground", rs.place_ground, -1.f, 2.f);
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

  if (act.contains("cloud_scatter_octaves") &&
      act["cloud_scatter_octaves"].is_number()) {
    rs.cloud_scatter_octaves =
        std::clamp(act["cloud_scatter_octaves"].get<int>(), 1, 4);
    ++n;
  }

  n += take_f(act, "cloud_scatter_depth", rs.cloud_scatter_depth, 0.05f, 0.99f);

  // Shading mode, per view or for every view at once. Scripting could set
  // every other display option but not this one, so no automated check could
  // ever confirm that "solid" actually stops shading with the material - and
  // that is precisely where the bug was.
  if (act.contains("shading")) {
    const json &v = act["shading"];
    int mode = -1;
    if (v.is_number()) mode = v.get<int>();
    else if (v.is_string()) {
      std::string t = v.get<std::string>();
      for (auto &c : t) c = (char)tolower(c);
      if (t.rfind("wire", 0) == 0) mode = 0;
      else if (t.rfind("solid", 0) == 0 || t.rfind("shade", 0) == 0) mode = 1;
      else if (t.rfind("text", 0) == 0) mode = 2;
    }
    if (mode < 0 || mode > 2) {
      err = "set_viewport: shading is 0..2, or wireframe/solid/textured";
      return 0;
    }
    int view = act.value("view", -1); // -1 = every view
    for (int i = 0; i < 6; ++i)
      if (view < 0 || view == i) rs.views[i].display = mode;
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
