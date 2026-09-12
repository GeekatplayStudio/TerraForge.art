// Geekatplay TerraForge — file-based scripting bridge.
// Publishes a scene snapshot and consumes action documents, so the Python
// API, the MCP server and the in-app AI assistant all drive the identical
// code path (ai_apply_actions).
#include "ai_assist.hpp"
#include "app.hpp"
#include "perf.hpp"
#include "console.hpp"
#include "gpu_timer.hpp"
#include "gpu_compute.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include "renderer_instances.hpp"
#include "terrain_cull.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include "world_shape.hpp"
#include <imgui.h>
#include <json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

static fs::path api_dir() {
  static const fs::path cached = [] {
    const char *base = std::getenv("LOCALAPPDATA");
    fs::path d = base ? fs::path(base) : fs::temp_directory_path();
    d = d / "GeekatplayTerraForge" / "api";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
  }();
  return cached;
}

static void publish_state(App &a) {
  // A single writer owns each complete document. If disk is slow, skip a
  // telemetry sample instead of queuing stale snapshots or blocking a frame.
  static std::future<void> writer;
  if (writer.valid()) {
    if (writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { writer.get(); }
    catch (const std::exception &e) { log_warn("api", e.what()); }
  }
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
                    {"visible", o.visible},
                    {"material_node", o.material_node},
                    {"pos", {o.pos[0], o.pos[1], o.pos[2]}},
                    {"rot", {o.yaw, o.pitch, o.roll}},
                    {"scale", o.scale},
                    {"animated", !o.anim.empty()}});
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
                       {"clouds", o.planet.clouds},
                       {"surface_layers", layer_count},
                       {"visible", sc.object_visible(o)}});
  }
  j["planets"] = planets;
  json nebulas = json::array();
  for (int i = 0; i < (int)sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (o.type != SceneObject::Nebula) continue;
    const NebulaData &N = o.nebula;
    nebulas.push_back({{"index", i}, {"name", o.name}, {"type", N.type},
                       {"azimuth", N.azimuth}, {"elevation", N.elevation},
                       {"size_deg", N.size_deg}, {"brightness", N.brightness},
                       {"seed", N.seed}, {"visible", sc.object_visible(o)}});
  }
  j["nebulas"] = nebulas;
  j["space"] = {{"on", rs.space.on},
                {"brightness", rs.space.brightness},
                {"realism", rs.space.realism},
                {"glow", rs.space.glow},
                {"quality", rs.space.quality},
                {"star_spikes", rs.space.star_spikes},
                {"star_halo", rs.space.star_halo},
                {"star_clump", rs.space.star_clump},
                {"star_spike_points", rs.space.star_spike_points},
                {"star_spike_angle", rs.space.star_spike_angle},
                {"star_spike_chroma", rs.space.star_spike_chroma},
                {"star_saturation", rs.space.star_saturation},
                {"star_glow", rs.space.star_glow},
                {"star_bright_share", rs.space.star_bright_share},
                {"star_clusters", rs.space.star_clusters},
                {"star_cluster_size", rs.space.star_cluster_size},
                {"galaxy_grain", rs.space.galaxy_grain},
                {"stars", rs.space.stars}, {"star_density", rs.space.star_density},
                {"star_brightness", rs.space.star_brightness}, {"star_size", rs.space.star_size},
                {"star_temperature", rs.space.star_temperature}, {"star_seed", rs.space.star_seed},
                {"galaxy", rs.space.galaxy_on}, {"galaxy_intensity", rs.space.galaxy_intensity},
                {"galaxy_width", rs.space.galaxy_width}, {"galaxy_yaw", rs.space.galaxy_yaw},
                {"galaxy_pitch", rs.space.galaxy_pitch}, {"galaxy_core", rs.space.galaxy_core},
                {"galaxy_dust", rs.space.galaxy_dust}, {"galaxy_seed", rs.space.galaxy_seed},
                {"atmosphere_height", rs.atmosphere_height}};
  j["world"] = {{"shape", world_shape_name(rs.world_shape)}, {"inside", rs.world_inside},
                {"width", rs.world_width}, {"sun_inside", rs.world_sun_inside},
                {"thickness", rs.world_thickness}, {"outline", world_outline_name(rs.world_outline)},
                {"planet_radius", rs.planet_radius}, {"atmosphere_height", rs.atmosphere_height}};
  {
    json presets = json::array();
    for (const RenderPreset &p : sc.render_presets)
      presets.push_back({{"name", p.name}, {"engine", p.assign.engine}, {"width", p.assign.width},
                         {"height", p.assign.height}, {"samples", p.assign.samples}});
    j["render_presets"] = presets;
    j["render_queue"] = (int)a.render_queue.size();
  }
  j["sun"] = {{"azimuth_deg", rs.sun_azimuth},
              {"altitude_deg", rs.sun_altitude},
              {"intensity", rs.sun_intensity},
              {"color", {rs.sun_color[0], rs.sun_color[1], rs.sun_color[2]}},
              {"size_deg", rs.sun_angle_deg},
              {"glow", rs.sun_glow},
              {"glow_size", rs.sun_glow_size}};
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
  // What the graph is holding, against what it is allowed to. Published so a
  // memory claim can be checked rather than asserted: released_bytes staying
  // at zero is how you tell a ceiling that never engaged from one that is not
  // wired up.
  j["memory"] = {{"buffers_mb", a.snapshot_bytes / (1024.0 * 1024.0)},
                 {"budget_mb", prefs().graph_memory_mb},
                 {"released_mb", a.graph.released_bytes / (1024.0 * 1024.0)}};
  // How the surface is being drawn, and what that is costing. Published so a
  // performance claim can be checked from a script instead of asserted: the
  // frame time is the renderer's own, and patches_visible is the count the
  // culling shader arrived at.
  int inst_drawn = 0, inst_total = 0, inst_cards = 0;
  renderer_instance_stats(inst_drawn, inst_total, &inst_cards);
  int view_w = 0, view_h = 0;
  renderer_view_size(view_w, view_h);
  unsigned long long accel_taken = 0, accel_declined = 0;
  accel_gl_stats(accel_taken, accel_declined);
  j["viewport"] = {{"tessellation", rs.tessellation},
                   {"tess_pixels", rs.tess_pixels},
                   {"tess_min", rs.tess_min},
                   {"tess_max", rs.tess_max},
                   {"frustum_cull", rs.frustum_cull},
                   {"patches_visible", renderer_patches_visible()},
                   {"patches_total",
                    TERRAIN_PATCHES_PER_EDGE * TERRAIN_PATCHES_PER_EDGE},
                   {"frame_ms", ImGui::GetIO().DeltaTime * 1000.0f},
                   {"terrain_gpu_ms", gpu_timer_ms("terrain")},
                   // What the tessellator actually emitted. The patch count
                   // above is what survived culling; this is what those
                   // patches cost, and the two together are the only way to
                   // tell a geometry-bound terrain pass from a
                   // fragment-bound one.
                   {"terrain_primitives", gpu_counter_primitives("terrain")},
                   // Whether node evaluation is taking the GPU path, and how
                   // often it declines. A fast path nobody can see the use of
                   // is a fast path nobody trusts.
                   {"accel", std::string(gpu_compute_available() ? "gl-compute"
                                                                : "cpu")},
                   {"accel_taken", accel_taken},
                   {"accel_declined", accel_declined},
                   // the pixels the terrain time was spent on
                   {"view_w", view_w},
                   {"view_h", view_h},
                   {"sky_gpu_ms", gpu_timer_ms("sky+clouds")},
                   {"water_gpu_ms", gpu_timer_ms("water")},
                   // the clouds over the ground and the sky the sea reflects
                   // (renderer_clouds.cpp)
                   {"clouds_over_gpu_ms", gpu_timer_ms("clouds over ground")},
                   {"sky_reflection_gpu_ms", gpu_timer_ms("sky reflection")},
                   {"cloud_scatter_octaves", rs.cloud_scatter_octaves},
                   {"scatter_lod_full_m", rs.scatter_lod_full_m},
                   {"scatter_lod_far_m", rs.scatter_lod_far_m},
                   {"scatter_lod_cull_m", rs.scatter_lod_cull_m},
                   {"scatter_lod_billboard_m", rs.scatter_lod_billboard_m},
                   {"scatter_lod_min_keep", rs.scatter_lod_min_keep},
                   {"terrain_lod", rs.terrain_lod},
                   {"instances_drawn", inst_drawn},
                   {"instances_total", inst_total},
                   {"instances_cards", inst_cards},
                   {"planet_radius", rs.planet_radius},
                   {"world_shape", world_shape_name(rs.world_shape)},
                   {"world_inside", rs.world_inside},
                   {"world_width", rs.world_width},
                   {"world_sun_inside", rs.world_sun_inside},
                   {"world_thickness", rs.world_thickness},
                   {"world_outline", world_outline_name(rs.world_outline)},
                   {"place_mode", rs.place_mode},
                   {"fractal_detail", rs.fractal_detail},
                   {"field_displacement", rs.field_displacement},
                   {"wireframe", rs.wireframe},
                   {"shadows", rs.shadows},
                   {"exposure", rs.exposure},
                   {"layout", rs.viewport_layout},
                   {"engine", rs.viewport_engine}};

  // The graph itself. Without this an agent can write to the graph and never
  // read it: it cannot see what nodes exist, so it can only ever bolt more on,
  // never inspect, tune or repair. Published from the lock-free UI snapshot,
  // so this costs nothing and cannot block evaluation.
  {
    json nodes = json::array();
    for (const App::NodeView &n : a.node_views) {
      json jn = {{"id", n.id},
                 {"type", n.type},
                 {"category", n.category},
                 {"pos", {n.pos_x, n.pos_y}},
                 {"enabled", n.enabled},
                 {"ms", n.ms}};
      if (!n.error.empty()) jn["error"] = n.error;
      json ports = json::array();
      for (const App::PortView &p : n.ports)
        ports.push_back({{"name", p.name},
                         {"in", p.is_input},
                         {"texture", p.is_texture},
                         {"optional", p.optional}});
      jn["ports"] = ports;
      nodes.push_back(jn);
    }
    j["nodes"] = nodes;
    json links = json::array();
    for (const App::LinkView &l : a.link_views)
      links.push_back({{"id", l.id},
                       {"from", l.from_node},
                       {"from_port", l.from_port},
                       {"to", l.to_node},
                       {"to_port", l.to_port}});
    j["links"] = links;
    j["selected_node"] = a.selected_node;
    j["view_node"] = a.view_node;
    j["project_path"] = a.project_path;
    // so a caller can wait for an actual event rather than for the file's
    // modification time to move
    // the last op's one-line result, so a script can read what it did
  j["status"] = a.status;
  if (!a.api_reply.empty()) {
    // Kept until the next action document rather than cleared here: the state
    // is republished every tick, so a reply cleared on publish lived for one
    // file write and a script reading a moment later found nothing.
    try { j["reply"] = json::parse(a.api_reply); } catch (...) { j["reply"] = a.api_reply; }
  }
  {
    const gpx::Timeline &tl = scene().timeline;
    j["timeline"] = {{"fps", tl.fps}, {"start", tl.start}, {"end", tl.end}, {"time", a.graph.time},
                     {"frame", tl.frame_of(a.graph.time)}, {"playing", a.anim_playing},
                     {"preview", tl.preview}, {"preview_start", tl.preview_start}, {"preview_end", tl.preview_end},
                     {"autokey", tl.autokey}, {"loop", (int)tl.loop}, {"markers", tl.markers.size()}};
  }
  {
    const PerfStats &ps = perf_stats();
    j["frame_latency"] = {{"cpu_work_p95_ms", ps.work_p95_ms},
                           {"cpu_work_p99_ms", ps.work_p99_ms}};
    j["perf"] = {{"fps", ps.fps}, {"potential_fps", ps.potential_fps}, {"work_ms", ps.work_ms},
                 {"ui_ms", ps.ui_ms}, {"views_ms", ps.views_ms}, {"gpu_ms", ps.gpu_ms},
                 {"previews_ms", ps.previews_ms}, {"api_ms", ps.api_ms}, {"upload_ms", ps.upload_ms},
                 {"eval_ms", ps.eval_ms}, {"process_mb", ps.process_mb}, {"cpu_pct", ps.cpu_pct},
                 {"vram_used_mb", ps.vram_used_mb}, {"governor_level", ps.governor_level},
                 {"views_drawn", ps.views_drawn}, {"patches", ps.patches}};
    json ph = json::object();
    for (const auto &kv : perf_phases()) ph[kv.first] = kv.second;
    j["perf"]["phases"] = ph;
  }
  j["eval"] = {{"running", a.eval.running.load()},
                 {"uploaded_serial", a.uploaded_serial},
                 {"serial", a.eval_serial.load()},
                 {"done", a.eval.progress_done.load()},
                 {"total", a.eval.progress_total.load()}};
  }

  writer = std::async(std::launch::async, [j = std::move(j), dir = api_dir()] {
    fs::path tmp = dir / "scene_state.json.tmp";
    std::ofstream f(tmp);
    if (!f) return;
    f << j.dump(2);
    f.close();
    if (!f) return;
    std::error_code ec;
    fs::rename(tmp, dir / "scene_state.json", ec);
  });
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
      a.api_reply.clear(); // the previous document's answer is spent
      // an op that reported something keeps its line; the generic one is
      // for batches that said nothing
      const std::string before = a.status;
      if (ai_apply_actions(a, text, err)) {
        if (a.status == before) a.status = "API: actions applied";
        log_info("api", "actions applied (" + std::to_string(text.size()) + " bytes)");
      } else {
        a.status = "API error: " + err;
        log_error("api", err);
      }
    }
  }
  publish_state(a);
}

} // namespace studio
