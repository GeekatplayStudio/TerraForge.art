// Geekatplay TerraForge — offline rendering. Exports the exact viewport
// environment (sky + volumetric clouds as an HDR panorama), terrain mesh,
// albedo and PBR material, then drives an external engine through the Python
// layer so the render matches the preview.
#include "app.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/heightmap.hpp"
#include <imgui.h>
#include <json.hpp>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "stb_image.h"
#include "stb_image_write.h"
#include <glad/gl.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

// render_scene_export.cpp
bool export_scene(App &a, const std::string &out_png, int width, int height,
                  int spp, const char *engine, std::string &err,
                  int cam_index = -1, bool passes = false,
                  bool panorama = false);

std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested);
bool renderer_render_to_file(const std::string &path, int w, int h);
bool renderer_export_sky_hdr(const std::string &path, int w, int h);

static std::atomic<bool> render_running{false};
static std::atomic<bool> probe_running{false};
static std::mutex render_mtx;
static std::string render_status, render_output, engine_report;
// live render window state
static std::string preview_path, progress_path;
static unsigned preview_tex = 0;
static int preview_w = 0, preview_h = 0;
static long long preview_stamp = 0;
static bool render_window_open = false;
static std::string progress_line;
#ifdef _WIN32
static PROCESS_INFORMATION render_proc{};
static std::atomic<bool> render_proc_valid{false};
#endif

// export_scene (render_scene_export.cpp) points the live view at the files
// the backend will refine
void render_set_preview_paths(const std::string &preview,
                              const std::string &progress) {
  preview_path = preview;
  progress_path = progress;
  preview_stamp = 0;
}

// launches the render as a killable child process
static int launch_process(const std::string &cmdline, const std::string &cwd) {
#ifdef _WIN32
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::string cmd = cmdline;
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi))
    return -1;
  {
    std::lock_guard<std::mutex> lk(render_mtx);
    render_proc = pi;
    render_proc_valid.store(true);
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  {
    std::lock_guard<std::mutex> lk(render_mtx);
    render_proc_valid.store(false);
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (int)code;
#else
  return std::system(("cd \"" + cwd + "\" && " + cmdline).c_str());
#endif
}

void render_cancel() {
#ifdef _WIN32
  std::lock_guard<std::mutex> lk(render_mtx);
  if (render_proc_valid.load()) {
    TerminateProcess(render_proc.hProcess, 1);
    render_status = "render cancelled";
  }
#endif
}

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

fs::path render_workdir() {
  fs::path dir = fs::temp_directory_path() / "terraforge_render";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

static void run_render(std::string scene_json, std::string out_png,
                       std::string root) {
  {
    std::lock_guard<std::mutex> lk(render_mtx);
    render_status = "rendering (first run may compile kernels)...";
  }
  std::string cmd = "cmd /c \"python -m orchestrator.render_engines \"" +
                    scene_json + "\" > \"" + scene_json + ".log\" 2>&1\"";
  int rc = launch_process(cmd, root);
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

// ---------------------------------------------------------- live render view
// Watches the progressive preview the backend refines pass by pass and shows
// it as it converges, with the pass counter and a cancel button.
void draw_render_window(App &a) {
  (void)a;
  if (!render_window_open) return;
  if (!ImGui::Begin("Render output", &render_window_open)) {
    ImGui::End();
    return;
  }
  // reload the preview whenever the backend has written a newer pass
  if (!preview_path.empty()) {
    std::error_code ec;
    auto st = fs::last_write_time(preview_path, ec);
    if (!ec) {
      long long stamp = (long long)st.time_since_epoch().count();
      if (stamp != preview_stamp) {
        int w, h, c;
        unsigned char *data = stbi_load(preview_path.c_str(), &w, &h, &c, 4);
        if (data) {
          if (!preview_tex) glGenTextures(1, &preview_tex);
          glBindTexture(GL_TEXTURE_2D, preview_tex);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, data);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          stbi_image_free(data);
          preview_w = w;
          preview_h = h;
          preview_stamp = stamp;
        }
      }
    }
    std::ifstream pf(progress_path);
    if (pf) std::getline(pf, progress_line);
  }

  bool busy = render_running.load();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.47f, 0.19f, 1.f));
  ImGui::TextUnformatted(busy ? (progress_line.empty() ? "starting renderer..."
                                                       : progress_line.c_str())
                              : (progress_line.empty() ? "idle"
                                                       : progress_line.c_str()));
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (busy) {
    if (ImGui::SmallButton("Cancel")) render_cancel();
  } else if (!render_output.empty()) {
#ifdef _WIN32
    if (ImGui::SmallButton("Open file"))
      ShellExecuteA(nullptr, "open", render_output.c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
#endif
  }

  if (preview_tex && preview_w > 0) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale = std::min(avail.x / preview_w, avail.y / preview_h);
    if (scale > 0.f) {
      ImVec2 size(preview_w * scale, preview_h * scale);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - size.x) * 0.5f);
      ImGui::Image((ImTextureID)(intptr_t)preview_tex, size);
    }
  } else {
    ImGui::TextDisabled("waiting for the first pass...");
  }
  ImGui::End();
}

// Serviced from the frame loop, not from any panel: a camera (or the AI, or
// the scripting inbox) asking to render must work whether or not the Render
// tab happens to be on screen.
void render_service_requests(App &a) {
  SceneState &sc = scene();
  if (a.request_camera_render < 0) return;
  int c = a.request_camera_render;
  a.request_camera_render = -1;
  if (c < 0 || c >= (int)sc.objects.size() ||
      sc.objects[c].type != SceneObject::Camera)
    return;
  if (render_running.load()) {
    std::lock_guard<std::mutex> lk(render_mtx);
    render_status = "a render is already running";
    return;
  }
  const RenderAssign &r = sc.objects[c].cam.render;
  int engine = std::clamp(r.engine, 0, ENGINE_COUNT - 1);
  std::string err;
  if (export_scene(a, r.output, r.width, r.height, r.samples,
                   ENGINES[engine].key, err, c, r.passes, r.panorama)) {
    render_window_open = true;
    progress_line.clear();
    render_running.store(true);
    std::thread(run_render, (render_workdir() / "scene.json").string(),
                r.output, project_root().string()).detach();
  } else {
    std::lock_guard<std::mutex> lk(render_mtx);
    render_status = err;
  }
}

void render_properties_ui(App &a) {
  static int engine = 0;
  static int width = 1920, height = 1080, spp = 128;
  static char out_path[512] = "terraforge_render.png";

  // when a camera is active, the Render tab edits that camera's assignment
  SceneState &sc = scene();
  int cam = scene_active_camera();
  RenderAssign *assign = nullptr;
  if (cam >= 0 && cam < (int)sc.objects.size() &&
      sc.objects[cam].type == SceneObject::Camera) {
    assign = &sc.objects[cam].cam.render;
    engine = assign->engine;
    width = assign->width;
    height = assign->height;
    spp = assign->samples;
    snprintf(out_path, sizeof out_path, "%s", assign->output.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.55f, 0.24f, 1.f));
    ImGui::Text("Render settings of %s", sc.objects[cam].name.c_str());
    ImGui::PopStyleColor();
  }

  ImGui::SeparatorText("Engine");
  const char *items = "Mitsuba 3 (path tracer)\0Blender Cycles\0LuxCoreRender\0"
                      "appleseed\0OpenGL viewport (instant)\0";
  ImGui::SetNextItemWidth(-1);
  if (ImGui::Combo("##engine", &engine, items) && assign) assign->engine = engine;
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
  if (ImGui::InputInt("Width", &width) && assign) assign->width = width;
  ImGui::SetNextItemWidth(-90);
  if (ImGui::InputInt("Height", &height) && assign) assign->height = height;
  ImGui::SetNextItemWidth(-90);
  if (ImGui::SliderInt("Samples", &spp, 8, 1024) && assign) assign->samples = spp;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("More samples = less noise, longer render.\n"
                      "32 preview, 128 good, 512+ final.");
  ImGui::SetNextItemWidth(-90);
  if (ImGui::InputText("File", out_path, sizeof out_path) && assign)
    assign->output = out_path;
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
        render_window_open = true; // pop the live view up immediately
        ImGui::SetWindowFocus("Render output");
        progress_line.clear();
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
