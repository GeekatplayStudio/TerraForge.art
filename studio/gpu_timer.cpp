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

// --------------------------------------------------------------- counter
namespace {
// A second in-flight query of the same target would end the first one's count
// in the wrong place, exactly as for the timer. The two targets are
// independent of each other, so a counter scope may sit inside a timer scope
// - which is the point, since the question is always "how long, and for how
// many triangles".
bool g_count_open = false;
} // namespace

GpuCounter::GpuCounter(const char *name) : pass_name(name ? name : "?") {}

bool GpuCounter::begin() {
  if (open || g_count_open) return false;
  if (!queries[slot]) glGenQueries(1, &queries[slot]);
  if (!queries[slot]) return false;
  if (issued[slot]) {
    // this slot has come round again; take its result if the driver has it,
    // and if not, leave the query alone rather than restarting it mid-flight
    GLuint ready = 0;
    glGetQueryObjectuiv(queries[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) return false;
    GLuint64 n = 0;
    glGetQueryObjectui64v(queries[slot], GL_QUERY_RESULT, &n);
    issued[slot] = false;
    last = (unsigned long long)n;
  }
  glBeginQuery(GL_PRIMITIVES_GENERATED, queries[slot]);
  open = true;
  g_count_open = true;
  return true;
}

void GpuCounter::end() {
  if (!open) return;
  glEndQuery(GL_PRIMITIVES_GENERATED);
  open = false;
  g_count_open = false;
  issued[slot] = true;
  slot = (slot + 1) % RING;
  if (!issued[slot] || !queries[slot]) return;
  GLuint ready = 0;
  glGetQueryObjectuiv(queries[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
  if (!ready) return;
  GLuint64 n = 0;
  glGetQueryObjectui64v(queries[slot], GL_QUERY_RESULT, &n);
  issued[slot] = false;
  last = (unsigned long long)n;
}

namespace {
std::map<std::string, GpuTimer *> &registry() {
  static std::map<std::string, GpuTimer *> m;
  return m;
}
std::map<std::string, GpuCounter *> &counters() {
  static std::map<std::string, GpuCounter *> m;
  return m;
}
} // namespace

GpuCounter &gpu_counter(const char *name) {
  auto &m = counters();
  auto it = m.find(name);
  if (it != m.end()) return *it->second;
  GpuCounter *c = new GpuCounter(name); // process lifetime, as the timers are
  m.emplace(name, c);
  return *c;
}

unsigned long long gpu_counter_primitives(const char *name) {
  auto &m = counters();
  auto it = m.find(name);
  return it == m.end() ? 0ull : it->second->primitives();
}

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
  for (auto &[name, c] : counters()) {
    if (!c->primitives()) continue;
    std::snprintf(buf, sizeof buf, "%s: %llu primitives\n", name.c_str(),
                  c->primitives());
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
