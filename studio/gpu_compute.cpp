// Geekatplay TerraForge — compute shaders, on the thread that needs them.
#include "gpu_compute.hpp"
#include "console.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <map>
#include <mutex>
#include <vector>

namespace studio {

namespace {

GLFWwindow *g_shared = nullptr;   // invisible, shares objects with the main one
bool g_available = false;
std::string g_status = "not initialised";
GpuComputeLimits g_limits;

// The program cache is written from the evaluation worker and read from it,
// but a second worker one day would race. A mutex on a path taken once per
// shader per process costs nothing worth measuring.
std::mutex g_prog_mtx;
std::map<std::string, unsigned> g_programs;

bool compile_ok(GLuint obj, bool program, const char *what) {
  GLint ok = 0;
  if (program) glGetProgramiv(obj, GL_LINK_STATUS, &ok);
  else glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
  if (ok) return true;
  GLint len = 0;
  if (program) glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
  else glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
  std::vector<char> msg((size_t)(len > 1 ? len : 1), 0);
  if (program) glGetProgramInfoLog(obj, len, nullptr, msg.data());
  else glGetShaderInfoLog(obj, len, nullptr, msg.data());
  // The compiler's own words, not a summary of them. A driver's message is
  // the only thing that ever says which line and why.
  log_error("gpu", std::string(what) + " failed: " + msg.data());
  return false;
}

} // namespace

bool gpu_compute_init(GLFWwindow *main_window) {
  if (g_shared) return g_available;
  if (!main_window) {
    g_status = "no main window";
    return false;
  }
  // Compute shaders are core in 4.3. macOS stops at 4.1 and always will, so
  // this is the ordinary outcome there rather than a fault.
  GLint major = 0, minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  if (major * 10 + minor < 43) {
    g_status = "OpenGL " + std::to_string(major) + "." + std::to_string(minor) +
               ", compute shaders need 4.3";
    log_info("gpu", "compute unavailable: " + g_status);
    return false;
  }

  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  g_shared = glfwCreateWindow(1, 1, "gpx compute", nullptr, main_window);
  glfwDefaultWindowHints(); // leave the hints as they were found
  if (!g_shared) {
    const char *err = nullptr;
    glfwGetError(&err);
    g_status = std::string("shared context creation failed: ") +
               (err ? err : "no reason given");
    log_error("gpu", g_status);
    return false;
  }

  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &g_limits.max_group_count[0]);
  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &g_limits.max_group_count[1]);
  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &g_limits.max_group_count[2]);
  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &g_limits.max_group_size[0]);
  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &g_limits.max_group_size[1]);
  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &g_limits.max_group_size[2]);
  glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &g_limits.max_invocations);
  glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &g_limits.max_shared_bytes);

  g_available = true;
  g_status = "OpenGL " + std::to_string(major) + "." + std::to_string(minor) +
             ", up to " + std::to_string(g_limits.max_invocations) +
             " invocations per group";
  log_info("gpu", "compute ready: " + g_status);
  return true;
}

bool gpu_compute_available() { return g_available; }
const std::string &gpu_compute_status() { return g_status; }

bool gpu_compute_bind_thread() {
  if (!g_available) return false;
  // Once per thread. GLFW documents glfwMakeContextCurrent as one of the few
  // calls that may be made from any thread, and a context may be current on
  // only one thread at a time - which is exactly why the worker has its own
  // rather than borrowing the main one.
  static thread_local bool bound = false;
  if (bound) return true;
  glfwMakeContextCurrent(g_shared);
  bound = true;
  return true;
}

unsigned gpu_compute_program(const char *key, const std::string &source) {
  std::lock_guard<std::mutex> lk(g_prog_mtx);
  auto it = g_programs.find(key);
  if (it != g_programs.end()) return it->second;
  // A failure is cached too, as zero. A driver that rejected this shader once
  // will reject it every frame, and recompiling to be told so again is the
  // sort of thing that turns a fast path into a slow one.
  unsigned prog = 0;
  GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  const char *src = source.c_str();
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  if (compile_ok(sh, false, (std::string("compute shader '") + key + "'").c_str())) {
    prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    if (!compile_ok(prog, true, (std::string("compute program '") + key + "'").c_str())) {
      glDeleteProgram(prog);
      prog = 0;
    }
  }
  glDeleteShader(sh);
  g_programs.emplace(key, prog);
  return prog;
}

const GpuComputeLimits &gpu_compute_limits() { return g_limits; }

} // namespace studio
