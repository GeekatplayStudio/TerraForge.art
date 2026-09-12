// Geekatplay TerraForge — Vue's Terrain Editor as ops, so the assistant, the
// Python API and MCP drive it the way the Properties tab does
// (terrain_editor.hpp).
//
//   {"op":"terrain_clip","low_m":120,"high_m":900,"low_mode":"hole","high_mode":"flatten","softness":0.02}
//   {"op":"terrain_clip","clear":true}
//   {"op":"terrain_effect","effect":"dissolve","hardness":0.7,"iterations":2}
//   {"op":"terrain_style","name":"Canyon"}
//   {"op":"terrain_global","action":"invert"|"zero_edges"|"smooth_all"|"halve"|"double"|"reset_sculpt"|"remove_effects"}
//   {"op":"terrain_import_picture","path":"C:/dem.png","mode":"blend","proportion":0.6}
//   {"op":"set_sculpt","active":true,"tool":"plateau","radius":0.08,"flow":0.6,"falloff":2,
//    "invert":false,"altitude_m":300,"constrain_clip":true}
#include "ai_assist.hpp"
#include "app.hpp"
#include "component_add.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include "render_settings.hpp"
#include "sculpt.hpp"
#include "terrain_editor.hpp"
#include <algorithm>
#include <cctype>
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

namespace {
std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
} // namespace

int ai_terrain_op(App &a, const std::string &op, const json &act, std::string &err) {
  if (op == "terrain_clip") {
    if (act.value("clear", false)) return terrain_editor_clip_clear(a, err) ? 1 : 0;
    const RenderSettings &rs = render_settings();
    const float top_m = std::max(rs.height_scale * rs.terrain_size_m, 1.f);
    TerrainClipState s = terrain_editor_clip_get(a);
    if (!s.present) { s.low = 0.f; s.high = 1.f; }
    if (act.contains("low")) s.low = act["low"].get<float>();
    if (act.contains("high")) s.high = act["high"].get<float>();
    if (act.contains("low_m")) s.low = act["low_m"].get<float>() / top_m;
    if (act.contains("high_m")) s.high = act["high_m"].get<float>() / top_m;
    s.low = std::clamp(s.low, 0.f, 1.f);
    s.high = std::clamp(s.high, 0.f, 1.f);
    if (act.contains("low_mode")) {
      const json &v = act["low_mode"];
      if (v.is_number()) s.low_mode = std::clamp(v.get<int>(), 0, 1);
      else s.low_mode = lower(v.get<std::string>()) == "flatten" ? 1 : 0;
    }
    if (act.contains("high_mode")) {
      const json &v = act["high_mode"];
      if (v.is_number()) s.high_mode = std::clamp(v.get<int>(), 0, 1);
      else s.high_mode = lower(v.get<std::string>()) == "hole" ? 1 : 0;
    }
    if (act.contains("softness")) s.softness = std::clamp(act["softness"].get<float>(), 0.f, 0.2f);
    return terrain_editor_clip_set(a, s, err) ? 1 : 0;
  }
  if (op == "terrain_effect") {
    const std::string effect = act.value("effect", act.value("name", std::string()));
    if (effect.empty()) {
      err = "terrain_effect needs 'effect' (grit, gravel, pebbles, stones, peaks, fir trees, "
            "plateaus, terraces, stairs, craters, sharpen, cracks; diffusive, thermal, glaciation, "
            "wind, dissolve, alluvium, fluvial, river valley)";
      return 0;
    }
    const float hardness = std::clamp(act.value("hardness", 0.5f), 0.f, 1.f);
    const int iterations = std::clamp(act.value("iterations", 1), 1, 16);
    for (int i = 0; i < iterations; ++i)
      if (!terrain_editor_effect(a, effect, hardness, err)) return 0;
    return 1;
  }
  if (op == "terrain_style") {
    const std::string name = act.value("name", act.value("style", std::string()));
    return terrain_style_apply(a, name, err) ? 1 : 0;
  }
  if (op == "terrain_global") {
    return terrain_editor_global(a, act.value("action", std::string()), err) ? 1 : 0;
  }
  if (op == "terrain_import_picture") {
    const std::string path = act.value("path", std::string());
    if (path.empty()) {
      err = "terrain_import_picture needs 'path'";
      return 0;
    }
    return terrain_editor_import_picture(a, path, act.value("mode", std::string("blend")),
                                         act.value("proportion", 1.f), err)
               ? 1
               : 0;
  }
  if (op == "delete_object") {
    // any asset, with everything under it: a tile, a light, the home planet
    const std::string name = act.value("name", std::string());
    SceneState &sc = scene();
    int idx = -1;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
      if (sc.objects[(size_t)i].name == name) idx = i;
    if (idx < 0) {
      err = "delete_object: no object called '" + name + "'";
      return 0;
    }
    // with the node that would rebuild it, as the tree and the viewport delete
    if (!scene_delete_objects(a, {idx}, false, err)) {
      err = "delete_object: " + err;
      return 0;
    }
    return 1;
  }
  if (op == "add_component") {
    const std::string kind = act.value("kind", act.value("type", std::string()));
    if (kind.empty()) {
      err = "add_component needs 'kind': terrain, infinite_terrain, planet, atmosphere, cloud_layer, "
            "sun, water, light, camera, cube, sphere, plane, cylinder, cone, import_mesh (with path), "
            "scatter, ecosystem, material";
      return 0;
    }
    NewComponent nc;
    if (!component_add(a, kind, act.value("name", std::string()), act.value("path", std::string()), nc, err))
      return 0;
    a.status = "added " + kind;
    return 1;
  }
  if (op == "set_sculpt") {
    SculptState &s = sculpt_state();
    if (act.contains("active")) sculpt_set_active(a, act["active"].get<bool>());
    if (act.contains("tool")) {
      static const char *names[] = {"raise", "flatten", "smooth", "terrace", "noise", "erase",
                                    "shade", "plateau", "altitude"};
      const std::string t = lower(act["tool"].get<std::string>());
      bool found = false;
      for (int i = 0; i < 9; ++i)
        if (t == names[i] || (t == "plateaus" && i == 7) || (t == "unislope" && i == 1)) {
          s.tool = (SculptTool)i;
          found = true;
        }
      if (!found) {
        err = "set_sculpt: tool is raise, plateau, flatten, altitude, smooth, terrace, noise, erase or shade";
        return 0;
      }
    }
    if (act.contains("radius")) s.radius = std::clamp(act["radius"].get<float>(), 0.005f, 0.4f);
    if (act.contains("flow")) s.flow = std::clamp(act["flow"].get<float>(), 0.f, 2.f);
    if (act.contains("falloff")) s.falloff = std::clamp(act["falloff"].get<float>(), 0.2f, 8.f);
    if (act.contains("invert")) s.invert = act["invert"].get<bool>();
    if (act.contains("constrain_clip")) s.constrain_clip = act["constrain_clip"].get<bool>();
    if (act.contains("shade")) s.shade = std::clamp(act["shade"].get<float>(), 0.f, 1.f);
    if (act.contains("altitude")) s.altitude = std::clamp(act["altitude"].get<float>(), 0.f, 1.f);
    if (act.contains("altitude_m")) {
      const RenderSettings &rs = render_settings();
      s.altitude = std::clamp(act["altitude_m"].get<float>() /
                                  std::max(rs.height_scale * rs.terrain_size_m, 1.f),
                              0.f, 1.f);
    }
    a.status = "sculpt settings applied";
    return 1;
  }
  return -1;
}

} // namespace studio
