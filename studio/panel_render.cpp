// Geekatplay TerraForge — offline rendering. Exports the exact viewport
// environment (sky + volumetric clouds as an HDR panorama), terrain mesh,
// albedo and PBR material, then drives an external engine through the Python
// layer so the render matches the preview.
#include "app.hpp"
#include "render_settings.hpp"
#include "gpx/heightmap.hpp"
#include <imgui.h>
#include <json.hpp>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "stb_image_write.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested);
bool renderer_render_to_file(const std::string &path, int w, int h);
bool renderer_export_sky_hdr(const std::string &path, int w, int h);

static std::atomic<bool> render_running{false};
static std::atomic<bool> probe_running{false};
static std::mutex render_mtx;
static std::string render_status, render_output, engine_report;

struct EngineInfo {
  const char *key;
  const char *label;
  const char *install;
};
static const EngineInfo ENGINES[] = {
    {"mitsuba", "Mitsuba 3 (path tracer)", "pip install mitsuba"},
    {"cycles", "Blender Cycles", "install Blender (blender.org), then set it on PATH"},
    {"luxcore", "LuxCoreRender", "pip install pyluxcore"},
    {"appleseed", "appleseed", "unmaintained since 2019 - not recommended"},
    {"viewport", "OpenGL viewport (instant)", "built in"},
};
static const int ENGINE_COUNT = 5;

static fs::path project_root() {
  // the studio runs from build/, the python layer sits next to it
  fs::path exe_dir = fs::current_path();
  if (fs::exists(exe_dir / "orchestrator")) return exe_dir;
  if (fs::exists(exe_dir.parent_path() / "orchestrator")) return exe_dir.parent_path();
  return exe_dir;
}

static fs::path render_workdir() {
  fs::path dir = fs::temp_directory_path() / "terraforge_render";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

static bool export_scene(App &a, const std::string &out_png, int width, int height,
                         int spp, const char *engine, std::string &err) {
  RenderSettings &rs = render_settings();
  fs::path dir = render_workdir();
  gpx::Heightmap hm;
  std::vector<uint8_t> albedo_u8;
  int alb_w = 0;
  {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    gpx::Node *best = nullptr;
    for (auto &n : a.graph.nodes) {
      if (n->type == "TerrainOutput") best = n.get();
      else if (!best && n->first_out(gpx::DataType::Heightmap)) best = n.get();
    }
    if (!best) {
      err = "no terrain in the graph";
      return false;
    }
    gpx::Port *ph = best->first_out(gpx::DataType::Heightmap);
    if (!ph || !ph->hmap || ph->hmap->empty()) {
      err = "terrain not computed yet";
      return false;
    }
    hm = *ph->hmap;
    if (rs.terrain_material_mode == 2) {
      gpx::Node *mn = a.graph.find_node(rs.terrain_material_node);
      gpx::Port *mp = mn ? mn->first_out(gpx::DataType::Texture) : nullptr;
      if (mp && mp->tex && !mp->tex->empty()) {
        albedo_u8 = mp->tex->to_u8();
        alb_w = mp->tex->w;
      }
    } else if (rs.terrain_material_mode == 0) {
      for (auto &n : a.graph.nodes) {
        if (n->type == "Splatmap" || n->type == "NormalMap" ||
            n->type == "AlbedoToPBR")
          continue;
        gpx::Port *pt = n->first_out(gpx::DataType::Texture);
        if (pt && pt->tex && !pt->tex->empty()) {
          albedo_u8 = pt->tex->to_u8();
          alb_w = pt->tex->w;
        }
      }
    }
  }

  // terrain mesh with UVs (denser than the preview grid for close-ups)
  int side = std::min(hm.w, 768);
  gpx::Heightmap m = hm.resampled(side, side);
  m.remap(0.f, rs.height_scale);
  {
    std::ofstream f(dir / "terrain.obj");
    f << "# Geekatplay TerraForge render mesh\n";
    for (int y = 0; y < side; ++y)
      for (int x = 0; x < side; ++x)
        f << "v " << x / float(side - 1) << ' ' << m.at(x, y) << ' '
          << y / float(side - 1) << '\n';
    for (int y = 0; y < side; ++y)
      for (int x = 0; x < side; ++x)
        f << "vt " << x / float(side - 1) << ' ' << 1.f - y / float(side - 1) << '\n';
    for (int y = 0; y < side - 1; ++y)
      for (int x = 0; x < side - 1; ++x) {
        int i = y * side + x + 1;
        f << "f " << i << '/' << i << ' ' << i + side << '/' << i + side << ' '
          << i + 1 << '/' << i + 1 << '\n';
        f << "f " << i + 1 << '/' << i + 1 << ' ' << i + side << '/' << i + side
          << ' ' << i + side + 1 << '/' << i + side + 1 << '\n';
      }
  }
  if (!albedo_u8.empty())
    stbi_write_png((dir / "albedo.png").string().c_str(), alb_w, alb_w, 4,
                   albedo_u8.data(), alb_w * 4);

  // the viewport's own sky + clouds, as an HDR environment map
  std::string sky_path = (dir / "sky.hdr").string();
  bool sky_ok = renderer_export_sky_hdr(sky_path, 2048, 1024);

  float eye[3], target[3], fov;
  renderer_get_camera(eye, target, &fov);
  float sun[3];
  compute_sun_dir(rs, sun);
  json j;
  j["engine"] = engine;
  j["width"] = width;
  j["height"] = height;
  j["spp"] = spp;
  j["output"] = out_png;
  j["terrain_obj"] = (dir / "terrain.obj").string();
  j["albedo"] = albedo_u8.empty() ? "" : (dir / "albedo.png").string();
  j["sky_hdr"] = sky_ok ? sky_path : "";
  j["exposure"] = rs.exposure;
  j["camera"] = {{"eye", {eye[0], eye[1], eye[2]}},
                 {"target", {target[0], target[1], target[2]}},
                 {"fov", fov}};
  j["sun"] = {{"dir", {sun[0], sun[1], sun[2]}},
              {"color", {rs.sun_color[0], rs.sun_color[1], rs.sun_color[2]}},
              {"intensity", rs.sun_intensity}};
  j["sky"] = {{"zenith", {rs.sky_zenith[0], rs.sky_zenith[1], rs.sky_zenith[2]}},
              {"horizon", {rs.sky_horizon[0], rs.sky_horizon[1], rs.sky_horizon[2]}},
              {"ambient", rs.ambient_intensity}};
  j["material"] = {{"roughness", rs.mat_roughness},
                   {"metallic", rs.mat_metallic},
                   {"specular", rs.mat_specular},
                   {"transparency", rs.mat_transparency}};
  j["fog"] = {{"type", rs.fog_type},
              {"density", rs.fog_density},
              {"level", rs.fog_level * rs.height_scale * 4.f},
              {"falloff", rs.fog_falloff / std::max(rs.height_scale, 1e-3f)},
              {"color", {rs.fog_color[0], rs.fog_color[1], rs.fog_color[2]}},
              {"absorb", {rs.absorption_color[0], rs.absorption_color[1],
                          rs.absorption_color[2]}},
              {"scatter", rs.fog_sun_scatter}};
  j["water"] = {{"enabled", rs.show_water},
                {"level", rs.water_level * rs.height_scale},
                {"roughness", std::max(rs.mat_roughness * 0.05f, 0.01f)},
                {"deep", {rs.water_deep_color[0], rs.water_deep_color[1],
                          rs.water_deep_color[2]}}};
  std::ofstream sj(dir / "scene.json");
  sj << j.dump(2);
  return true;
}

static void run_render(std::string scene_json, std::string out_png,
                       std::string root) {
  {
    std::lock_guard<std::mutex> lk(render_mtx);
    render_status = "rendering (first run may compile kernels)...";
  }
  std::string cmd = "cmd /c \"cd /d \"" + root + "\" && python -m orchestrator.render_engines \"" +
                    scene_json + "\" > \"" + scene_json + ".log\" 2>&1\"";
  int rc = std::system(cmd.c_str());
  std::string tail;
  {
    std::ifstream lf(scene_json + ".log");
    std::string line;
    while (std::getline(lf, line))
      if (!line.empty()) tail = line;
  }
  std::lock_guard<std::mutex> lk(render_mtx);
  if (rc == 0 && fs::exists(out_png)) {
    render_status = "done: " + out_png;
    render_output = out_png;
  } else {
    render_status = tail.empty() ? ("render failed (exit " + std::to_string(rc) + ")")
                                 : tail;
  }
  render_running.store(false);
}

static void run_probe(std::string root) {
  fs::path out = render_workdir() / "engines.txt";
  std::string cmd = "cmd /c \"cd /d \"" + root +
                    "\" && python -m orchestrator.render_engines --probe > \"" +
                    out.string() + "\" 2>&1\"";
  std::system(cmd.c_str());
  std::ifstream f(out);
  std::string all((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
  std::lock_guard<std::mutex> lk(render_mtx);
  engine_report = all.empty() ? "probe produced no output (is python on PATH?)" : all;
  probe_running.store(false);
}

void render_properties_ui(App &a) {
  static int engine = 0;
  static int width = 1920, height = 1080, spp = 128;
  static char out_path[512] = "terraforge_render.png";

  ImGui::SeparatorText("Engine");
  const char *items = "Mitsuba 3 (path tracer)\0Blender Cycles\0LuxCoreRender\0"
                      "appleseed\0OpenGL viewport (instant)\0";
  ImGui::SetNextItemWidth(-1);
  ImGui::Combo("##engine", &engine, items);
  ImGui::TextDisabled("install: %s", ENGINES[engine].install);
  if (engine == 3)
    ImGui::TextDisabled("appleseed has had no release since 2019; the adapter\n"
                        "only detects an existing install.");
  ImGui::BeginDisabled(probe_running.load());
  if (ImGui::Button(probe_running.load() ? "detecting..." : "Detect installed engines")) {
    probe_running.store(true);
    std::thread(run_probe, project_root().string()).detach();
  }
  ImGui::EndDisabled();
  {
    std::lock_guard<std::mutex> lk(render_mtx);
    if (!engine_report.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.70f, 0.66f, 1.f));
      ImGui::TextUnformatted(engine_report.c_str());
      ImGui::PopStyleColor();
    }
  }

  ImGui::SeparatorText("Output");
  ImGui::SetNextItemWidth(-90);
  ImGui::InputInt("Width", &width);
  ImGui::SetNextItemWidth(-90);
  ImGui::InputInt("Height", &height);
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderInt("Samples", &spp, 8, 1024);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("More samples = less noise, longer render.\n"
                      "32 preview, 128 good, 512+ final.");
  ImGui::SetNextItemWidth(-90);
  ImGui::InputText("File", out_path, sizeof out_path);
  if (ImGui::Button("choose file...")) {
    std::string p = dialog_save_file("PNG image\0*.png\0", "png", out_path);
    if (!p.empty()) snprintf(out_path, sizeof out_path, "%s", p.c_str());
  }

  ImGui::SeparatorText("Match with viewport");
  ImGui::TextDisabled("The render uses the viewport's sky and volumetric\n"
                      "clouds as an HDR environment, the same PBR material,\n"
                      "sun, water and height fog, and the same ACES exposure.");

  bool busy = render_running.load();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button(busy ? "rendering..." : "Render", ImVec2(-1, 0))) {
    width = std::clamp(width, 64, 8192);
    height = std::clamp(height, 64, 8192);
    if (engine == 4) {
      a.status = renderer_render_to_file(out_path, width, height)
                     ? std::string("rendered: ") + out_path
                     : "RENDER FAILED";
      std::lock_guard<std::mutex> lk(render_mtx);
      render_status = a.status;
      render_output = out_path;
    } else {
      std::string err;
      if (export_scene(a, out_path, width, height, spp, ENGINES[engine].key, err)) {
        render_running.store(true);
        std::string sj = (render_workdir() / "scene.json").string();
        std::thread(run_render, sj, std::string(out_path),
                    project_root().string()).detach();
      } else {
        std::lock_guard<std::mutex> lk(render_mtx);
        render_status = err;
      }
    }
  }
  ImGui::EndDisabled();
  {
    std::lock_guard<std::mutex> lk(render_mtx);
    if (!render_status.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
      ImGui::TextWrapped("%s", render_status.c_str());
      ImGui::PopStyleColor();
    }
#ifdef _WIN32
    if (!render_output.empty() && ImGui::Button("open image"))
      ShellExecuteA(nullptr, "open", render_output.c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
#endif
  }
}

} // namespace studio
