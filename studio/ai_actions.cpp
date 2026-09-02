// Geekatplay TerraForge - applying the AI/scripting action documents:
// every op the assistant or the actions inbox may emit, one dispatcher.
// Split from ai_assist.cpp for the 500-line module rule; the assist UI
// stays there.
// Geekatplay TerraForge — natural-language assistant shared by every tab.
// The model returns a small JSON action document; ai_apply_actions executes
// it. The scripting API and the MCP server call the same function, so text,
// script and tool calls all take one code path.
#include "ai_assist.hpp"
#include "app.hpp"
#include "ollama.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/serialization.hpp"
#include <imgui.h>
#include <json.hpp>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace studio {

std::string field_gpu_verify_all(App &a);

// ---------------------------------------------------------------- helpers
static int format_index(const std::string &name) {
  int n = 0;
  const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&n);
  for (int i = 0; i < n; ++i)
    if (name == F[i].name) return i;
  // tolerant match: "35mm" -> full frame, "super35" -> cine
  std::string l = name;
  for (auto &c : l) c = (char)tolower(c);
  if (l.find("super") != std::string::npos) return 2;
  if (l.find("imax") != std::string::npos || l.find("65") != std::string::npos)
    return 5;
  if (l.find("aps") != std::string::npos) return 1;
  if (l.find("four thirds") != std::string::npos || l.find("m43") != std::string::npos)
    return 3;
  if (l.find("16") != std::string::npos) return 4;
  if (l.find("large") != std::string::npos || l.find("4x5") != std::string::npos)
    return 6;
  return 0;
}

static int film_index(const std::string &name) {
  int n = 0;
  const gpx::cam::FilmStock *F = gpx::cam::film_stocks(&n);
  for (int i = 0; i < n; ++i)
    if (name == F[i].name) return i;
  std::string l = name;
  for (auto &c : l) c = (char)tolower(c);
  if (l.find("portra") != std::string::npos) return 1;
  if (l.find("kodachrome") != std::string::npos) return 2;
  if (l.find("vision") != std::string::npos || l.find("500t") != std::string::npos)
    return 3;
  if (l.find("ekta") != std::string::npos || l.find("fuji") != std::string::npos)
    return 4;
  if (l.find("hp5") != std::string::npos || l.find("ilford") != std::string::npos ||
      l.find("black") != std::string::npos)
    return 5;
  if (l.find("kodak") != std::string::npos) return 1;
  return 0;
}

static bool read_vec3(const json &j, const char *key, float *out) {
  if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) return false;
  for (int i = 0; i < 3; ++i) out[i] = j[key][i].get<float>();
  return true;
}

static void resolve_look_at(const json &j, float *target) {
  target[0] = 0.5f;
  target[1] = render_settings().height_scale * 0.4f;
  target[2] = 0.5f;
  if (!j.contains("look_at")) return;
  const json &la = j["look_at"];
  if (la.is_array() && la.size() >= 3) {
    for (int i = 0; i < 3; ++i) target[i] = la[i].get<float>();
  } else if (la.is_string()) {
    std::string s = la.get<std::string>();
    for (auto &c : s) c = (char)tolower(c);
    if (s == "origin") {
      target[0] = target[1] = target[2] = 0.f;
    } else {
      // named scene object
      SceneState &sc = scene();
      for (const auto &o : sc.objects) {
        std::string n = o.name;
        for (auto &c : n) c = (char)tolower(c);
        if (n == s && o.type == SceneObject::Mesh) {
          target[0] = o.pos[0];
          target[1] = o.pos[1] * render_settings().height_scale;
          target[2] = o.pos[2];
          return;
        }
      }
    }
  }
}

static void apply_camera_fields(CameraData &cd, const json &j) {
  if (j.contains("focal_mm")) cd.focal_mm = std::clamp(j["focal_mm"].get<float>(), 4.f, 800.f);
  if (j.contains("aperture")) cd.aperture = std::clamp(j["aperture"].get<float>(), 0.7f, 45.f);
  if (j.contains("shutter")) cd.shutter = std::clamp(j["shutter"].get<float>(), 1.f / 8000, 30.f);
  if (j.contains("iso")) cd.iso = std::clamp(j["iso"].get<float>(), 12.f, 25600.f);
  if (j.contains("format") && j["format"].is_string())
    cd.format = format_index(j["format"].get<std::string>());
  if (j.contains("film") && j["film"].is_string())
    cd.film = film_index(j["film"].get<std::string>());

  float target[3];
  resolve_look_at(j, target);
  bool has_pos = read_vec3(j, "position", cd.eye) || read_vec3(j, "eye", cd.eye);
  for (int i = 0; i < 3; ++i) cd.target[i] = target[i];
  if (!has_pos) {
    // place the camera by distance / height / azimuth around the target
    float dist = j.value("distance", 1.9f);
    float height = j.value("height", render_settings().height_scale * 1.5f);
    float az = j.value("azimuth_deg", 0.f) * 0.017453293f;
    cd.eye[0] = target[0] + std::sin(az) * dist;
    cd.eye[1] = target[1] + height;
    cd.eye[2] = target[2] + std::cos(az) * dist;
  }
  if (j.contains("render") && j["render"].is_object()) {
    const json &r = j["render"];
    cd.render.width = r.value("width", cd.render.width);
    cd.render.height = r.value("height", cd.render.height);
    cd.render.samples = r.value("samples", cd.render.samples);
    if (r.contains("output")) cd.render.output = r["output"].get<std::string>();
  }
}

static int engine_index(const std::string &name) {
  std::string l = name;
  for (auto &c : l) c = (char)tolower(c);
  if (l.find("cycles") != std::string::npos) return 1;
  if (l.find("lux") != std::string::npos) return 2;
  if (l.find("apple") != std::string::npos) return 3;
  if (l.find("viewport") != std::string::npos || l.find("opengl") != std::string::npos)
    return 4;
  return 0;
}

// ---------------------------------------------------------------- apply
bool ai_apply_actions(App &a, const std::string &text, std::string &err) {
  json j;
  try {
    size_t b = text.find('{'), e = text.rfind('}');
    if (b == std::string::npos || e == std::string::npos || e <= b) {
      err = "no JSON object in the reply";
      return false;
    }
    j = json::parse(text.substr(b, e - b + 1));
  } catch (const std::exception &ex) {
    err = std::string("JSON parse: ") + ex.what();
    return false;
  }
  json actions = j.contains("actions") ? j["actions"] : json::array({j});
  if (!actions.is_array() || actions.empty()) {
    err = "no actions";
    return false;
  }
  // Anything the assistant, a script or an MCP tool does is a single undo
  // step, so an AI change can be reverted like any other edit.
  {
    std::string what = "AI change";
    if (actions.size() == 1 && actions[0].contains("op"))
      what = "AI: " + actions[0]["op"].get<std::string>();
    else
      what = "AI: " + std::to_string(actions.size()) + " changes";
    bool only_history = true;
    for (const json &act : actions) {
      std::string op = act.value("op", "");
      if (op != "undo" && op != "redo") only_history = false;
    }
    if (!only_history) undo_push(a, what);
  }
  RenderSettings &rs = render_settings();
  SceneState &sc = scene();
  int applied = 0;
  for (const json &act : actions) {
    if (!act.is_object() || !act.contains("op")) continue;
    std::string op = act["op"].get<std::string>();

    if (op == "add_camera" || op == "set_camera") {
      int idx = -1;
      if (op == "set_camera") {
        if (act.contains("name")) {
          std::string want = act["name"].get<std::string>();
          for (int i = 0; i < (int)sc.objects.size(); ++i)
            if (sc.objects[i].type == SceneObject::Camera &&
                sc.objects[i].name == want)
              idx = i;
        }
        if (idx < 0 && sc.selected >= 0 && sc.selected < (int)sc.objects.size() &&
            sc.objects[sc.selected].type == SceneObject::Camera)
          idx = sc.selected;
        if (idx < 0) idx = scene_active_camera();
      }
      if (idx < 0) {
        idx = scene_add_camera(act.value("name", std::string()));
      }
      apply_camera_fields(sc.objects[idx].cam, act);
      if (act.value("activate", op == "add_camera")) {
        scene_active_camera() = idx;
        scene_last_used_camera() = idx;
      }
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "set_sun") {
      rs.sun_mode = 0;
      if (act.contains("azimuth_deg")) rs.sun_azimuth = act["azimuth_deg"].get<float>();
      if (act.contains("altitude_deg")) rs.sun_altitude = std::clamp(act["altitude_deg"].get<float>(), 1.f, 89.f);
      if (act.contains("intensity")) rs.sun_intensity = act["intensity"].get<float>();
      read_vec3(act, "color", rs.sun_color);
      ++applied;
    } else if (op == "set_sky") {
      if (act.contains("density")) rs.atmosphere_density = act["density"].get<float>();
      if (act.contains("ambient")) rs.ambient_intensity = act["ambient"].get<float>();
      read_vec3(act, "zenith", rs.sky_zenith);
      read_vec3(act, "horizon", rs.sky_horizon);
      ++applied;
    } else if (op == "set_fog") {
      if (act.contains("type") && act["type"].is_string()) {
        std::string t = act["type"].get<std::string>();
        rs.fog_type = t == "off" ? 0 : t == "haze" ? 1 : t == "pollution" ? 3 : 2;
      }
      if (act.contains("density")) rs.fog_density = act["density"].get<float>();
      if (act.contains("level")) rs.fog_level = act["level"].get<float>();
      read_vec3(act, "color", rs.fog_color);
      ++applied;
    } else if (op == "set_clouds") {
      if (act.contains("enabled")) rs.clouds_on = act["enabled"].get<bool>();
      if (act.contains("type") && act["type"].is_string()) {
        std::string t = act["type"].get<std::string>();
        rs.cloud_type = t == "stratus" ? 0 : t == "cumulonimbus" ? 2 : 1;
      }
      if (act.contains("coverage")) rs.cloud_coverage = std::clamp(act["coverage"].get<float>(), 0.f, 1.f);
      if (act.contains("density")) rs.cloud_density = act["density"].get<float>();
      if (act.contains("altitude")) rs.cloud_altitude = act["altitude"].get<float>();
      if (act.contains("thickness")) rs.cloud_thickness = act["thickness"].get<float>();
      if (act.contains("wind_speed")) rs.cloud_wind_speed = act["wind_speed"].get<float>();
      ++applied;
    } else if (op == "set_water") {
      if (act.contains("enabled")) rs.show_water = act["enabled"].get<bool>();
      if (act.contains("level")) rs.water_level = act["level"].get<float>();
      read_vec3(act, "deep", rs.water_deep_color);
      read_vec3(act, "shallow", rs.water_shallow_color);
      if (act.contains("foam")) rs.water_foam = act["foam"].get<bool>();
      ++applied;
    } else if (op == "set_render") {
      int cam = scene_active_camera();
      RenderAssign fallback;
      RenderAssign &r = (cam >= 0 && cam < (int)sc.objects.size() &&
                         sc.objects[cam].type == SceneObject::Camera)
                            ? sc.objects[cam].cam.render
                            : fallback;
      if (act.contains("engine") && act["engine"].is_string())
        r.engine = engine_index(act["engine"].get<std::string>());
      r.width = act.value("width", r.width);
      r.height = act.value("height", r.height);
      r.samples = act.value("samples", r.samples);
      if (act.contains("output")) r.output = act["output"].get<std::string>();
      ++applied;
    } else if (op == "render") {
      a.request_camera_render = scene_active_camera();
      ++applied;
    } else if (op == "undo") {
      int n = act.value("steps", 1);
      for (int i = 0; i < n && undo_perform(a); ++i) ++applied;
      if (!applied) err = "nothing to undo";
    } else if (op == "redo") {
      int n = act.value("steps", 1);
      for (int i = 0; i < n && redo_perform(a); ++i) ++applied;
      if (!applied) err = "nothing to redo";
    } else if (op == "select") {
      if (act.contains("name")) {
        std::string want = act["name"].get<std::string>();
        for (int i = 0; i < (int)sc.objects.size(); ++i)
          if (sc.objects[i].name == want) {
            sc.selected = i;
            a.scene_selection_serial++;
            ++applied;
          }
      }
    } else if (op == "place_object") {
      std::string want = act.value("name", std::string());
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Mesh) continue;
        if (!want.empty() && o.name != want) continue;
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        if (act.contains("rotation_deg")) o.yaw = act["rotation_deg"].get<float>();
        ++applied;
        break;
      }
    } else if (op == "save_project") {
      std::string path = act.value("path", std::string());
      if (path.empty()) {
        err = "save_project needs a path";
      } else if (project_save(a, path)) {
        ++applied;
      } else {
        err = "could not write " + path;
      }
    } else if (op == "open_project") {
      std::string path = act.value("path", std::string());
      if (path.empty()) {
        err = "open_project needs a path";
      } else if (project_load(a, path)) {
        ++applied;
      } else {
        err = "could not open " + path;
      }
    } else if (op == "add_light" || op == "set_light") {
      int idx = -1;
      if (op == "set_light") {
        std::string want = act.value("name", std::string());
        for (int i = 0; i < (int)sc.objects.size(); ++i)
          if (sc.objects[i].type == SceneObject::Light &&
              (want.empty() || sc.objects[i].name == want))
            idx = i;
        if (idx < 0) {
          err = "no light named '" + want + "'";
          continue;
        }
      } else {
        idx = scene_add_light(act.value("name", std::string()));
      }
      SceneObject &o = sc.objects[idx];
      read_vec3(act, "position", o.pos);
      read_vec3(act, "color", o.color);
      if (act.contains("intensity"))
        o.light_intensity = act["intensity"].get<float>();
      if (act.contains("reach")) o.light_radius = act["reach"].get<float>();
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "add_primitive") {
      std::string kind = act.value("kind", std::string("cube"));
      int idx = scene_add_primitive(kind, act.value("name", std::string()));
      if (idx < 0) {
        err = "unknown primitive '" + kind +
              "' (cube, sphere, plane, cylinder, cone)";
      } else {
        SceneObject &o = sc.objects[idx];
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        read_vec3(act, "color", o.color);
        a.scene_selection_serial++;
        ++applied;
      }
    } else if (op == "import_object") {
      std::string path = act.value("path", std::string());
      std::string ierr;
      int idx = scene_import_obj(path, ierr);
      if (idx < 0) {
        err = "import failed: " + ierr;
      } else {
        SceneObject &o = sc.objects[idx];
        if (act.contains("name")) o.name = act["name"].get<std::string>();
        read_vec3(act, "position", o.pos);
        if (act.contains("scale")) o.scale = act["scale"].get<float>();
        sc.selected = idx;
        a.scene_selection_serial++;
        ++applied;
      }
    } else if (op == "set_scatter") {
      // bind a mesh object to a Points node: copies of the mesh appear at
      // every point, standing on the terrain
      std::string want = act.value("object", std::string());
      uint64_t node_id = 0;
      if (act.contains("node")) {
        if (act["node"].is_number()) node_id = act["node"].get<uint64_t>();
        else {
          std::string t = act["node"].get<std::string>();
          for (auto &cand : a.graph.nodes)
            if (cand->type == t || std::to_string(cand->id) == t)
              node_id = cand->id;
        }
      }
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Mesh) continue;
        if (!want.empty() && o.name != want) continue;
        o.scatter_node = node_id;
        if (act.contains("size")) o.scatter_scale = act["size"].get<float>();
        if (act.contains("jitter")) o.scatter_jitter = act["jitter"].get<float>();
        if (act.contains("sway")) o.scatter_sway = act["sway"].get<float>();
        if (act.contains("size_from_value"))
          o.scatter_value_size = act["size_from_value"].get<float>();
        if (act.contains("seed")) o.scatter_seed = act["seed"].get<uint32_t>();
        if (!node_id) o.inst.clear();
        ++applied;
        if (!want.empty()) break;
      }
      if (!applied) err = "no mesh object named '" + want + "'";
      else a.request_eval();
    } else if (op == "add_planet") {
      int idx = scene_add_planet(act.value("name", std::string()));
      SceneObject &o = sc.objects[idx];
      PlanetData &P = o.planet;
      read_vec3(act, "position", o.pos);
      if (act.contains("radius")) P.radius = act["radius"].get<float>();
      if (act.contains("relief")) P.relief = act["relief"].get<float>();
      if (act.contains("seed")) P.seed = act["seed"].get<uint32_t>();
      if (act.contains("sea_level")) P.sea_level = act["sea_level"].get<float>();
      if (act.contains("snow_line")) P.snow_line = act["snow_line"].get<float>();
      if (act.contains("atmosphere")) P.atmo_density = act["atmosphere"].get<float>();
      read_vec3(act, "water_color", P.water_color);
      read_vec3(act, "rock_low", P.rock_low);
      read_vec3(act, "rock_high", P.rock_high);
      read_vec3(act, "atmo_color", P.atmo_color);
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "set_planet") {
      std::string want = act.value("name", std::string());
      for (auto &o : sc.objects) {
        if (o.type != SceneObject::Planet) continue;
        if (!want.empty() && o.name != want) continue;
        PlanetData &P = o.planet;
        read_vec3(act, "position", o.pos);
        if (act.contains("radius")) P.radius = act["radius"].get<float>();
        if (act.contains("relief")) P.relief = act["relief"].get<float>();
        if (act.contains("seed")) P.seed = act["seed"].get<uint32_t>();
        if (act.contains("sea_level")) P.sea_level = act["sea_level"].get<float>();
        if (act.contains("snow_line")) P.snow_line = act["snow_line"].get<float>();
        if (act.contains("atmosphere")) P.atmo_density = act["atmosphere"].get<float>();
        read_vec3(act, "water_color", P.water_color);
        read_vec3(act, "rock_low", P.rock_low);
        read_vec3(act, "rock_high", P.rock_high);
        read_vec3(act, "atmo_color", P.atmo_color);
        ++applied;
        if (!want.empty()) break;
      }
      if (!applied) err = "no planet named '" + want + "'";
    } else if (op == "add_infinite_terrain") {
      // "planet":"name" attaches to that planet; omitted = home ground plane
      int parent = -1;
      std::string pn = act.value("planet", std::string());
      if (!pn.empty()) {
        for (int i = 0; i < (int)sc.objects.size(); ++i)
          if (sc.objects[i].type == SceneObject::Planet &&
              sc.objects[i].name == pn)
            parent = i;
        if (parent < 0) {
          err = "no planet named '" + pn + "'";
          continue;
        }
      }
      int idx = scene_add_infinite_surface(parent, act.value("name", std::string()));
      gpx::planet::Layer &L = sc.objects[idx].surf.layer;
      std::string style = act.value("style", std::string());
      if (style == "hills") L.type = 0;
      else if (style == "mountains" || style == "ridged") L.type = 1;
      else if (style == "dunes" || style == "billow") L.type = 2;
      if (act.contains("scale")) L.frequency = act["scale"].get<float>();
      if (act.contains("amplitude")) L.amplitude = act["amplitude"].get<float>();
      if (act.contains("coverage")) L.coverage = act["coverage"].get<float>();
      if (act.contains("seed")) L.seed = act["seed"].get<uint32_t>();
      sc.selected = idx;
      a.scene_selection_serial++;
      ++applied;
    } else if (op == "verify_field_gpu") {

      // the load-bearing check of the dual-domain design: does the generated

      // shader compute the same numbers as the CPU evaluator?

      a.status = "field GPU check:\n" + field_gpu_verify_all(a);

      ++applied;

    } else if (op == "graph") {
      if (act.contains("spec")) {
        std::string spec = act["spec"].dump();
        std::string gerr;
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        // replace:true clears first, so a script can author a whole scene
        // rather than only ever bolting more onto what is already there
        bool merge = !act.value("replace", false);
        if (gpx::graph_from_ai_spec(a.graph, spec, gerr, nullptr, merge)) {
          if (!merge) a.view_node = a.selected_node = 0;
          a.graph_layout_serial++;
          a.request_eval();
          ++applied;
        } else {
          err = gerr;
        }
      }
    } else {
      // Viewport settings touch no graph state, so they are tried first and
      // without the lock — waiting on a running evaluation to change the
      // exposure would be a stall for nothing.
      int r = ai_view_op(a, op, act, err);
      if (r < 0) {
        // node-level graph editing lives in ai_ops_graph.cpp; -1 means it did
        // not recognise the op either, and it falls through to "unsupported"
        std::lock_guard<std::mutex> lk(a.graph_mtx);
        r = ai_graph_op(a, op, act, err);
      }
      if (r > 0) ++applied;
    }
  }
  if (!applied) {
    if (err.empty()) err = "no supported actions in the reply";
    return false;
  }
  return true;
}

} // namespace studio
