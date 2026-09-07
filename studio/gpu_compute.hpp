// Geekatplay TerraForge — compute shaders, on the thread that needs them.
//
// Node evaluation runs on a worker thread (studio/app_eval.cpp) so the UI
// never blocks, and a GL context belongs to one thread at a time. So the
// worker gets its own: a second, invisible window whose context *shares*
// objects with the main one, made current on the worker and left there.
// Programs, buffers and textures are shared between the two; container
// objects (VAOs, FBOs) are not, and nothing here uses one.
//
// Everything is allowed to fail and say so. macOS caps OpenGL at 4.1 and will
// never ship compute shaders, so on that platform this reports unavailable
// once and the CPU path runs exactly as it always has. The same is true of a
// driver that refuses the context or a shader that will not compile: the
// caller's fallback is cheap, and a wrong answer is not.
#pragma once
#include <string>

struct GLFWwindow;

namespace studio {

// Create the shared context. Main thread, after the main context exists and
// glad is loaded. Returns false when compute is unavailable — which is a
// normal outcome, not an error, and is logged once.
bool gpu_compute_init(GLFWwindow *main_window);

// True when init() succeeded and compute may be used.
bool gpu_compute_available();

// Why not, when it is not. For the log and the scene state: a fast path that
// silently is not taken is worse than no fast path.
const std::string &gpu_compute_status();

// Make the shared context current on *this* thread. Safe to call repeatedly;
// does the work once per thread. Must be called from the evaluation worker
// before any of the dispatch helpers.
bool gpu_compute_bind_thread();

// Compile and link a compute program, cached under `key` for the process.
// Returns 0 on failure, having logged the compiler's own words.
unsigned gpu_compute_program(const char *key, const std::string &source);

// The device's limits, for deciding a workgroup count.
struct GpuComputeLimits {
  int max_group_count[3] = {0, 0, 0};
  int max_group_size[3] = {0, 0, 0};
  int max_invocations = 0;
  int max_shared_bytes = 0;
};
const GpuComputeLimits &gpu_compute_limits();

// Install the GL accelerator with the engine, if compute is available.
// studio/accel_gl.cpp; declared here so main.cpp needs one include.
void accel_gl_install();
// How many jobs it has taken and declined, for the readout.
void accel_gl_stats(unsigned long long &taken, unsigned long long &declined);
// Run every fractal parameter combination through both paths and report the
// agreement. studio/accel_check.cpp, behind the "verify_accel" API op.
std::string accel_verify_all();

} // namespace studio
