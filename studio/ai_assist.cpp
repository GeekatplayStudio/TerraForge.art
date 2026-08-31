// Geekatplay TerraForge â€” natural-language assistant shared by every tab.
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

std::string dialog_open_file(const char *filter, const char *def_ext);
// CPU/GPU agreement check for field graphs (studio/field_gpu_check.cpp)
std::string field_gpu_verify_all(App &a);

// ---------------------------------------------------------------- schema
std::string ai_action_schema(AiDomain domain) {
  std::string s =
      "Reply with ONLY a JSON object: {\"actions\":[ ... ]}.\n"
      "Every action has \"op\". Supported operations:\n";
  switch (domain) {
    case AiDomain::Camera:
      s += R"(- {"op":"add_camera","name":"Hero","focal_mm":50,"format":"Full frame 35mm",
   "aperture":2.8,"shutter":0.008,"iso":200,"film":"Kodak Portra 400",
   "look_at":"terrain"|"origin"|[x,y,z], "distance":2.0, "height":0.6,
   "azimuth_deg":210, "activate":true}
- {"op":"set_camera","name":"Hero", ...same fields...}   (edits the selected
   or named camera instead of creating one)
Sensor formats: Full frame 35mm, APS-C, Super 35 (cine), Micro Four Thirds,
16mm film, 65mm / IMAX, Large format 4x5.
Film stocks: Digital (neutral), Kodak Portra 400, Kodak Kodachrome 64,
Kodak Vision3 500T, Fuji Ektachrome-style, Ilford HP5 (B&W).
"cinematic" implies a wide sensor (Super 35 or 65mm), a fast aperture
(f/2 - f/4) and a film stock rather than Digital.
shutter is in seconds (1/125 = 0.008).
EXPOSURE: the scene is lit like open daylight, which needs about EV100 13
(f/8, 1/125s, ISO 100). Keep aperture^2 / shutter / (iso/100) near 8000 or
the image blows out. So a shallow cinematic f/2.8 needs a fast shutter
(about 1/1000) at ISO 100 - do not combine a wide aperture with a high ISO
and a slow shutter unless the user asks for a night or interior shot.
The world is a unit tile: terrain spans x 0..1, z 0..1, height around 0..0.25.
"in front of the terrain" means a position outside the tile looking at its
centre, e.g. eye [0.5, 0.35, 1.9] with look_at "terrain".)";
      break;
    case AiDomain::World:
      s += R"(- {"op":"set_sun","azimuth_deg":220,"altitude_deg":12,"intensity":3.0,
   "color":[1,0.85,0.6]}
- {"op":"set_sky","density":1.2,"ambient":0.7,"zenith":[r,g,b],"horizon":[r,g,b]}
- {"op":"set_fog","type":"off"|"haze"|"fog"|"pollution","density":1.2,
   "level":0.3,"color":[r,g,b]}
- {"op":"set_clouds","enabled":true,"type":"stratus"|"cumulus"|"cumulonimbus",
   "coverage":0.6,"density":1.2,"altitude":1.4,"thickness":0.8,"wind_speed":0.03}
- {"op":"set_water","enabled":true,"level":0.1,"deep":[r,g,b],"shallow":[r,g,b],
   "foam":true})";
      break;
    case AiDomain::Render:
      s += R"(- {"op":"set_render","engine":"mitsuba"|"cycles"|"luxcore"|"viewport",
   "width":1920,"height":1080,"samples":256,"output":"shot.png"}
- {"op":"render"}   (starts the render immediately))";
      break;
    case AiDomain::Object:
      s += R"(- {"op":"place_object","name":"Rock","position":[x,y,z],"scale":0.1,
   "rotation_deg":30}
- {"op":"select","name":"Terrain"}
- {"op":"add_planet","name":"Mars","radius":3.5,"relief":0.03,"seed":42,
   "position":[x,y,z],"sea_level":0,"snow_line":0.9,"atmosphere":0.3,
   "rock_low":[0.45,0.25,0.15],"rock_high":[0.6,0.4,0.3],
   "atmo_color":[0.9,0.6,0.4]}
   (planets are procedural and free: any number is fine. sea_level 0 = dry
    world; the home terrain tile is at the origin, keep planets 8+ units away)
- {"op":"set_planet","name":"Mars", ...same fields...}
- {"op":"add_infinite_terrain","planet":"Mars","style":"mountains"|"hills"|"dunes",
   "scale":5,"amplitude":1.0,"coverage":0.5,"seed":7}
   (omit "planet" to extend the home ground plane to the horizon instead;
    layers stack, so add several with different styles and coverages)
The world is a unit tile: terrain spans x 0..1, z 0..1.)";
      break;
    default:
      s += R"(- {"op":"graph","spec":{ ...node graph in the standard node JSON... }}
Use this to build terrain or material node graphs.)";
      break;
  }
  s += R"(
Available in every domain:
- {"op":"undo","steps":1}   (revert the last change, including your own)
- {"op":"redo","steps":1}
)";
  s += "\nOmit any field you do not want to change. Return only JSON.";
  return s;
}

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
      // node-level graph editing lives in ai_ops_graph.cpp; -1 means it did
      // not recognise the op either, and it falls through to "unsupported"
      std::lock_guard<std::mutex> lk(a.graph_mtx);
      int r = ai_graph_op(a, op, act, err);
      if (r > 0) ++applied;
    }
  }
  if (!applied) {
    if (err.empty()) err = "no supported actions in the reply";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- UI bar
struct AiState {
  char prompt[512] = "";
  char image[512] = "";
  std::atomic<bool> running{false};
  std::mutex mtx;
  std::string result, status, error;
};

static AiState &state_for(AiDomain d) {
  static AiState s[6];
  return s[(int)d];
}

static const char *domain_name(AiDomain d) {
  switch (d) {
    case AiDomain::Camera: return "camera";
    case AiDomain::World: return "sky, sun, clouds, fog and water";
    case AiDomain::Material: return "material";
    case AiDomain::Terrain: return "terrain";
    case AiDomain::Object: return "scene object";
    default: return "render";
  }
}

static void run_assist(AiDomain domain, std::string prompt, std::string image) {
  AiState &st = state_for(domain);
  Prefs &p = prefs();
  std::string model = image.empty() ? p.text_model : p.vision_model;
  {
    std::lock_guard<std::mutex> lk(st.mtx);
    st.status = "asking " + model + "...";
    st.error.clear();
  }
  std::string sys = std::string("You control the ") + domain_name(domain) +
                    " settings of Geekatplay TerraForge, a 3D terrain studio.\n" +
                    ai_action_schema(domain);
  if (!image.empty())
    prompt += "\n(An image is attached: analyse it and match its lens, framing, "
              "light and mood.)";
  std::string out, err;
  bool ok = ollama_generate(p.ollama_url, model, sys, prompt, image, out, err);
  std::lock_guard<std::mutex> lk(st.mtx);
  if (ok) {
    st.result = out;
    st.status = "applying";
  } else {
    st.error = err;
    st.status.clear();
  }
  st.running.store(false);
}

void ai_assist_bar(App &a, AiDomain domain, const char *hint) {
  AiState &st = state_for(domain);
  ImGui::SeparatorText("Ask AI");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##aiprompt", hint, st.prompt, sizeof st.prompt);
  ImGui::SetNextItemWidth(-118);
  ImGui::InputTextWithHint("##aiimg", "reference image (optional)", st.image,
                           sizeof st.image);
  ImGui::SameLine();
  if (ImGui::Button("image...", ImVec2(56, 0))) {
    std::string p = dialog_open_file(
        "Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0", nullptr);
    if (!p.empty()) snprintf(st.image, sizeof st.image, "%s", p.c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button("clear", ImVec2(-1, 0))) st.image[0] = 0;

  bool busy = st.running.load();
  ImGui::BeginDisabled(busy || st.prompt[0] == 0);
  if (ImGui::Button(busy ? "thinking..." : "Apply", ImVec2(-1, 0))) {
    st.running.store(true);
    std::thread(run_assist, domain, std::string(st.prompt),
                std::string(st.image))
        .detach();
  }
  ImGui::EndDisabled();

  std::lock_guard<std::mutex> lk(st.mtx);
  if (!st.status.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
    ImGui::TextWrapped("%s", st.status.c_str());
    ImGui::PopStyleColor();
  }
  if (!st.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.3f, 0.2f, 1.f));
    ImGui::TextWrapped("%s", st.error.c_str());
    ImGui::PopStyleColor();
  }
  if (!st.result.empty()) {
    std::string doc = std::move(st.result);
    st.result.clear();
    std::string err;
    if (ai_apply_actions(a, doc, err)) {
      st.status = "applied";
      a.status = "AI applied changes";
    } else {
      st.error = err;
      st.status.clear();
    }
  }
}

} // namespace studio


