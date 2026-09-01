// Geekatplay Studio — entry point: window, GL, ImGui docking shell
#include "app.hpp"
#include "prefs.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <cstdio>

namespace studio {
App &app() {
  static App a;
  return a;
}
void run_main(); // app.cpp
} // namespace studio

int main() {
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
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  // No MSAA on the default framebuffer. Every 3D pass renders into its own
  // single-sampled FBO, so the swapchain only ever receives ImGui geometry,
  // which is already antialiased. Asking for 4x here bought a larger
  // swapchain and a resolve pass that cannot improve anything.
  glfwWindowHint(GLFW_SAMPLES, 0);
  GLFWwindow *win = glfwCreateWindow(1760, 1000,
                                     "Geekatplay TerraForge \xC2\xB7 Vladimir Shopine",
                                     nullptr, nullptr);
  if (!win) {
    std::fprintf(stderr, "window creation failed (OpenGL 4.3 required)\n");
    return 1;
  }
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);
  if (!gladLoadGL(glfwGetProcAddress)) {
    std::fprintf(stderr, "OpenGL load failed\n");
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // floatable/detachable panels
  io.ConfigDragClickToInputText = true; // click a number to type it in
  io.IniFilename = "geekatplay_studio_layout.ini";
  studio::prefs_load();
  // real UI font: Segoe UI (falls back to built-in if unavailable)
  ImFont *ui_font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf");
  if (!ui_font) io.Fonts->AddFontDefault();
  ImGui::GetStyle().FontSizeBase = studio::prefs().font_size;
  ImGui::GetStyle().FontScaleMain = studio::prefs().ui_scale;
  studio::apply_theme();
  ImGui_ImplGlfw_InitForOpenGL(win, true);
  ImGui_ImplOpenGL3_Init("#version 430");

  studio::app().window = win;
  studio::run_main();
  studio::prefs_save();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(win);
  glfwTerminate();
  return 0;
}
