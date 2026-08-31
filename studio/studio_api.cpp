// Geekatplay TerraForge — file-based scripting bridge.
// Publishes a scene snapshot and consumes action documents, so the Python
// API, the MCP server and the in-app AI assistant all drive the identical
// code path (ai_apply_actions).
#include "ai_assist.hpp"
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include <imgui.h>
#include <json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

static fs::path api_dir() {
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path d = base ? fs::path(base) : fs::temp_directory_path();
  d = d / "GeekatplayTerraForge" / "api";
  std::error_code ec;
  fs::create_directories(d, ec);
  return d;
}

static void publish_state(App &a) {
  RenderSettings &rs = render_settings();
  SceneState &sc = scene();
  json j;
  json cams = json::array();
  int nf = 0, nfilm = 0;
  const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&nf);
  const gpx::cam::FilmStock *S = gpx::cam::film_stocks(&nfilm);
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (o.type != SceneObject::Camera) continue;
    const CameraData &c = o.cam;
    const gpx::cam::SensorFormat &fmt = F[std::clamp(c.format, 0, nf - 1)];
    cams.push_back({
        {"index", i},
        {"name", o.name},
        {"active", scene_active_camera() == i},
        {"position", {c.eye[0], c.eye[1], c.eye[2]}},
        {"look_at", {c.target[0], c.target[1], c.target[2]}},
        {"focal_mm", c.focal_mm},
        {"format", fmt.name},
        {"fov_y_deg", gpx::cam::fov_y_deg(c.focal_mm, fmt.height_mm)},
        {"aperture", c.aperture},
        {"shutter", c.shutter},
        {"iso", c.iso},
        {"film", S[std::clamp(c.film, 0, nfilm - 1)].name},
        {"ev100", gpx::cam::ev100(c.aperture, c.shutter, c.iso)},
        {"render", {{"engine", c.render.engine},
                    {"width", c.render.width},
                    {"height", c.render.height},
                    {"samples", c.render.samples},
                    {"output", c.render.output}}},
    });
  }
  j["cameras"] = cams;
  json objs = json::array();
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    objs.push_back({{"index", i},
                    {"name", o.name},
                    {"type", (int)o.type},
                    {"parent", o.parent},
                    {"visible", o.visible}});
  }
  j["objects"] = objs;
  j["selected"] = sc.selected;
  json planets = json::array();
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (o.type != SceneObject::Planet) continue;
    int layer_count = 0;
    for (const auto &c : sc.objects)
      if (c.type == SceneObject::InfiniteSurface && c.parent == i) ++layer_count;
    planets.push_back({{"index", i},
                       {"name", o.name},
                       {"position", {o.pos[0], o.pos[1], o.pos[2]}},
                       {"radius", o.planet.radius},
                       {"relief", o.planet.relief},
                       {"seed", o.planet.seed},
                       {"sea_level", o.planet.sea_level},
                       {"atmosphere", o.planet.atmo_density},
                       {"surface_layers", layer_count},
                       {"visible", sc.object_visible(o)}});
  }
  j["planets"] = planets;
  j["sun"] = {{"azimuth_deg", rs.sun_azimuth},
              {"altitude_deg", rs.sun_altitude},
              {"intensity", rs.sun_intensity},
              {"color", {rs.sun_color[0], rs.sun_color[1], rs.sun_color[2]}}};
  j["sky"] = {{"density", rs.atmosphere_density},
              {"ambient", rs.ambient_intensity}};
  j["fog"] = {{"type", rs.fog_type}, {"density", rs.fog_density},
              {"level", rs.fog_level}};
  j["clouds"] = {{"enabled", rs.clouds_on},
                 {"type", rs.cloud_type},
                 {"coverage", rs.cloud_coverage},
                 {"altitude", rs.cloud_altitude}};
  j["water"] = {{"enabled", rs.show_water}, {"level", rs.water_level}};
  j["terrain"] = {{"resolution", a.graph.resolution},
                  {"height_scale", rs.height_scale},
                  {"size_m", rs.terrain_size_m}};
  fs::path tmp = api_dir() / "scene_state.json.tmp";
  {
    std::ofstream f(tmp);
    if (!f) return;
    f << j.dump(2);
  }
  std::error_code ec;
  fs::rename(tmp, api_dir() / "scene_state.json", ec);
}

// Polls the API folder: applies any queued action document and republishes
// the scene snapshot. Cheap enough to call once per frame.
void studio_api_tick(App &a) {
  static double last_check = 0;
  double now = ImGui::GetTime();
  if (now - last_check < 0.25) return; // 4 Hz is plenty for scripting
  last_check = now;

  fs::path inbox = api_dir() / "actions_inbox.json";
  std::error_code ec;
  if (fs::exists(inbox, ec)) {
    std::ifstream f(inbox);
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    f.close();
    fs::remove(inbox, ec);
    if (!text.empty()) {
      std::string err;
      if (ai_apply_actions(a, text, err))
        a.status = "API: actions applied";
      else
        a.status = "API error: " + err;
    }
  }
  publish_state(a);
}

} // namespace studio
