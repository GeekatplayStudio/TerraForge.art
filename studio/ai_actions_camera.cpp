// Geekatplay TerraForge — camera, render-engine and planet-surface helpers for
// the AI action dispatcher. Split from ai_actions.cpp for the 500-line module
// rule.
#include "ai_actions_internal.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <string>

using json = nlohmann::json;

namespace studio {

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

bool read_vec3(const json &j, const char *key, float *out) {
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

void apply_camera_fields(CameraData &cd, const json &j) {
  if (j.contains("focal_mm")) cd.focal_mm = std::clamp(j["focal_mm"].get<float>(), 4.f, 800.f);
  if (j.contains("aperture")) cd.aperture = std::clamp(j["aperture"].get<float>(), 0.7f, 45.f);
  if (j.contains("shutter")) cd.shutter = std::clamp(j["shutter"].get<float>(), 1.f / 8000, 30.f);
  if (j.contains("iso")) cd.iso = std::clamp(j["iso"].get<float>(), 12.f, 25600.f);
  if (j.contains("format") && j["format"].is_string())
    cd.format = format_index(j["format"].get<std::string>());
  if (j.contains("film") && j["film"].is_string())
    cd.film = film_index(j["film"].get<std::string>());

  // The optical simulation: one switch and the parts of a real lens under it.
  // Setting any part on its own turns the simulation on, because a script
  // asking for vignetting means it wants to see vignetting.
  bool touched = false;
  auto take = [&](const char *k, float &dst, float lo, float hi) {
    if (!j.contains(k) || !j[k].is_number()) return;
    dst = std::clamp(j[k].get<float>(), lo, hi);
    touched = true;
  };
  take("vignette", cd.vignette, 0.f, 4.f);
  take("chromatic", cd.chromatic, 0.f, 4.f);
  take("flare_strength", cd.flare_strength, 0.f, 4.f);
  take("motion_blur", cd.motion_blur, 0.f, 1.f);
  if (j.contains("distortion") && j["distortion"].is_number()) {
    cd.distortion = std::clamp(j["distortion"].get<float>(), -0.5f, 0.5f);
    cd.distortion_auto = false;
    touched = true;
  }
  if (j.contains("distortion_auto") && j["distortion_auto"].is_boolean()) {
    cd.distortion_auto = j["distortion_auto"].get<bool>();
    touched = true;
  }
  if (j.contains("flare") && j["flare"].is_boolean()) {
    cd.flare = j["flare"].get<bool>();
    touched = true;
  }
  if (j.contains("optics") && j["optics"].is_boolean())
    cd.optics = j["optics"].get<bool>();
  else if (touched)
    cd.optics = true;

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

int engine_index(const std::string &name) {
  std::string l = name;
  for (auto &c : l) c = (char)tolower(c);
  if (l.find("cycles") != std::string::npos) return 1;
  if (l.find("lux") != std::string::npos) return 2;
  if (l.find("apple") != std::string::npos) return 3;
  if (l.find("viewport") != std::string::npos || l.find("opengl") != std::string::npos)
    return 4;
  return 0;
}

// "surface_node" on a planet: a node id, an alias/type name resolved like
// every other node reference (the last SurfaceDisplacement wins), or 0 / ""
// for "the graph's first one".
unsigned long long planet_surface_node_of(App &a, const json &v) {
  if (v.is_number()) return v.get<unsigned long long>();
  if (!v.is_string()) return 0;
  std::string s = v.get<std::string>();
  if (s.empty()) return 0;
  unsigned long long last = 0;
  for (auto &n : a.graph.nodes)
    if (n->type == s || std::to_string(n->id) == s) last = n->id;
  if (!last && s == "new") {
    // a fresh graph for this planet, ready to edit
    float x = 0.f, y = 260.f;
    for (auto &n : a.graph.nodes) x = std::max(x, n->pos_x);
    gpx::Node *src = a.graph.add_node("FieldNoise", x, y);
    gpx::Node *sink = a.graph.add_node("SurfaceDisplacement", x + 260.f, y);
    if (src && sink) a.graph.add_link(src->id, "out", sink->id, "field");
    a.graph_layout_serial++;
    a.request_eval();
    last = sink ? sink->id : 0;
  }
  return last;
}

} // namespace studio
