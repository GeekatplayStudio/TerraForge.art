// Geekatplay Studio — entry point: window, GL, ImGui docking shell
#include "app.hpp"
#include "gpu_compute.hpp"
#include "prefs.hpp"
#include "i18n.hpp"
#include "console.hpp"
#include "crash_log.hpp"
#include "crash_ledger.hpp"
#include "hang_watch.hpp"
#include "config.hpp"
#include "paths.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <cstdio>
#include <string>

namespace studio {
App &app() {
  static App a;
  return a;
}
void run_main(); // app.cpp
} // namespace studio

int main(int argc, char **argv) {
  // First: the file log and the crash handlers, so whatever fails from here
  // on leaves a report in <project>/logs.
  studio::crash_log_init(argc, argv);
  // What earlier sessions left behind: crashes, hangs, and logs with no
  // clean exit. One warning line, so it is the first thing in the console.
  if (std::string s = studio::crash_reports_summary(
          studio::crash_reports(studio::log_dir(), studio::crash_log_stamp()));
      !s.empty())
    studio::log_warn("crashlog", s);
  // Hints must come *after* glfwInit(). An identical block used to sit above
  // this call doing nothing at all: glfwWindowHint opens with
  // _GLFW_REQUIRE_INIT() (external/glfw/src/window.c), so before init it
  // returns immediately and posts GLFW_NOT_INITIALIZED - the value never
  // reaches the hint state. glfwInit then calls glfwDefaultWindowHints()
  // (init.c:428) and would have cleared it regardless.
  //
  // Harmless while the two blocks agreed, and a silent trap the day someone
  // bumps the version in the wrong one.
  if (!glfwInit()) {
    std::fprintf(stderr, "GLFW init failed\n");
    return 1;
  }
  // macOS caps OpenGL at 4.1 core and will not ship anything newer, so ask
  // for what the platform can actually give. A core profile there also
  // *requires* the forward-compatible flag — without it glfwCreateWindow
  // fails outright. Nothing in the app needs 4.2/4.3: the shaders are
  // downgraded to `#version 410 core` by studio/glsl_version.hpp, which
  // explains why that is safe.
#ifdef __APPLE__
  constexpr int gl_major = 4, gl_minor = 1;
  constexpr const char *imgui_glsl_version = "#version 410";
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
  constexpr int gl_major = 4, gl_minor = 3;
  constexpr const char *imgui_glsl_version = "#version 430";
#endif
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, gl_major);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gl_minor);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  // No MSAA on the default framebuffer. Every 3D pass renders into its own
  // single-sampled FBO, so the swapchain only ever receives ImGui geometry,
  // which is already antialiased. Asking for 4x here bought a larger
  // swapchain and a resolve pass that cannot improve anything.
  glfwWindowHint(GLFW_SAMPLES, 0);
  // The window where it was last time: the preferences are read before
  // the window exists, so its size is known before it is made.
  studio::prefs_load();
  const studio::Prefs &wp = studio::prefs();
  const int win_w = wp.win_w > 200 ? wp.win_w : 1760, win_h = wp.win_h > 150 ? wp.win_h : 1000;
  GLFWwindow *win = glfwCreateWindow(win_w, win_h,
                                     "Geekatplay TerraForge \xC2\xB7 Vladimir Shopine",
                                     nullptr, nullptr);
  if (win && wp.win_x > -30000 && wp.win_y > -30000 && wp.win_w > 200)
    glfwSetWindowPos(win, wp.win_x, wp.win_y);
  if (win && wp.win_max) glfwMaximizeWindow(win);
  if (!win) {
    std::fprintf(stderr,
                 "window creation failed (OpenGL %d.%d core profile required)\n",
                 gl_major, gl_minor);
    return 1;
  }
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);
  if (!gladLoadGL(glfwGetProcAddress)) {
    std::fprintf(stderr, "OpenGL load failed\n");
    return 1;
  }

  // A second, invisible context sharing objects with this one, so the
  // evaluation worker can run compute shaders without borrowing the main
  // thread's. Both calls are allowed to decline - on macOS they always will,
  // since OpenGL stops at 4.1 there - and the engine then computes exactly
  // what it computes today. Nothing else in the application changes.
  studio::gpu_compute_init(win);
  studio::accel_gl_install();
  glfwMakeContextCurrent(win); // gpu_compute_init created a window; take it back

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // floatable/detachable panels
  io.ConfigDragClickToInputText = true; // click a number to type it in
  // The panel layout is per user, not per install: inside a macOS bundle the
  // working directory is "/", and an installed Windows copy may sit somewhere
  // read-only. settings_path() still prefers a file sitting in the current
  // directory, so a developer's layout stays with their checkout.
  static const std::string ini_path =
      studio::settings_path("geekatplay_studio_layout.ini").string();
  io.IniFilename = ini_path.c_str();
  studio::prefs_load();
  studio::i18n_init(); // the interface language, from the preferences
  // The platform's own interface font, falling back to the built-in one.
  ImFont *ui_font = nullptr;
  if (std::string font = studio::ui_font_path(); !font.empty())
    ui_font = io.Fonts->AddFontFromFileTTF(font.c_str());
  if (!ui_font) io.Fonts->AddFontDefault();
  ImGui::GetStyle().FontSizeBase = studio::prefs().font_size;
  ImGui::GetStyle().FontScaleMain = studio::prefs().ui_scale;
  studio::apply_theme();
  ImGui_ImplGlfw_InitForOpenGL(win, true);
  ImGui_ImplOpenGL3_Init(imgui_glsl_version);

  studio::app().window = win;
  studio::hang_watch_start(studio::config().perf.hang_seconds);
  studio::run_main();
  studio::hang_watch_stop();
  {
    // where the window is, for next time; a maximised window's restored
    // size is what glfw reports after un-maximising, so keep the last
    // normal size instead
    studio::Prefs &p = studio::prefs();
    p.win_max = glfwGetWindowAttrib(win, GLFW_MAXIMIZED) != 0;
    if (!p.win_max) {
      glfwGetWindowPos(win, &p.win_x, &p.win_y);
      glfwGetWindowSize(win, &p.win_w, &p.win_h);
    }
  }
  studio::log_info("app", "shutdown: main loop left, saving prefs");
  studio::prefs_save();

  studio::log_info("app", "shutdown: ImGui/GLFW teardown");
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(win);
  glfwTerminate();
  studio::log_info("app", "shutdown: static destructors next");
  studio::crash_log_shutdown();
  return 0;
}
