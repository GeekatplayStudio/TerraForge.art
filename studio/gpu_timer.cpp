// Geekatplay TerraForge — GPU timing for one pass.
#include "gpu_timer.hpp"
#include <glad/gl.h>
#include <cstdio>
#include <map>
#include <string>

namespace studio {

namespace {
// GL permits exactly one GL_TIME_ELAPSED query in flight, so a second begin()
// while one is open would end the first one's measurement in the wrong place
// and report nonsense for both. Track it and refuse instead.
bool g_query_open = false;
} // namespace

GpuTimer::GpuTimer(const char *name) : pass_name(name ? name : "?") {}

GpuTimer::~GpuTimer() {
  // The GL context is usually already gone by the time process-lifetime timers
  // are destroyed; deleting queries then is not safe and not needed.
}

bool GpuTimer::begin() {
  if (open || g_query_open) return false;
  if (issued[slot]) {
    GLuint ready = 0;
    glGetQueryObjectuiv(queries[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) return false;
    GLuint64 ns = 0;
    glGetQueryObjectui64v(queries[slot], GL_QUERY_RESULT, &ns);
    issued[slot] = false;
    double ms = (double)ns / 1.0e6;
    smoothed_ms = smoothed_ms <= 0.0 ? ms : smoothed_ms * 0.85 + ms * 0.15;
  }
  if (!queries[slot]) glGenQueries(1, &queries[slot]);
  if (!queries[slot]) return false;
  glBeginQuery(GL_TIME_ELAPSED, queries[slot]);
  open = true;
  g_query_open = true;
  return true;
}

void GpuTimer::end() {
  if (!open) return;
  glEndQuery(GL_TIME_ELAPSED);
  open = false;
  g_query_open = false;
  issued[slot] = true;
  slot = (slot + 1) % RING;

  // Collect the oldest result, but only if the driver already has it. Asking
  // for a result that is not ready blocks until the GPU catches up, which
  // would make the instrument the bottleneck.
  if (!issued[slot] || !queries[slot]) return;
  GLuint ready = 0;
  glGetQueryObjectuiv(queries[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
  if (!ready) return;
  GLuint64 ns = 0;
  glGetQueryObjectui64v(queries[slot], GL_QUERY_RESULT, &ns);
  issued[slot] = false;
  double ms = (double)ns / 1.0e6;
  // Light smoothing: a single frame's GPU time jitters with clocks and other
  // work on the device, and a readout that flickers cannot be read.
  smoothed_ms = smoothed_ms <= 0.0 ? ms : smoothed_ms * 0.85 + ms * 0.15;
}

namespace {
std::map<std::string, GpuTimer *> &registry() {
  static std::map<std::string, GpuTimer *> m;
  return m;
}
} // namespace

GpuTimer &gpu_timer(const char *name) {
  auto &m = registry();
  auto it = m.find(name);
  if (it != m.end()) return *it->second;
  GpuTimer *t = new GpuTimer(name); // process lifetime, deliberately not freed
  m.emplace(name, t);
  return *t;
}

std::string gpu_timer_report() {
  std::string out;
  char buf[96];
  for (auto &[name, t] : registry()) {
    if (t->ms() <= 0.0) continue;
    std::snprintf(buf, sizeof buf, "%s: %.3f ms\n", name.c_str(), t->ms());
    out += buf;
  }
  if (out.empty()) out = "no GPU timings yet\n";
  return out;
}

double gpu_timer_ms(const char *name) {
  auto &m = registry();
  auto it = m.find(name);
  return it == m.end() ? 0.0 : it->second->ms();
}

} // namespace studio
