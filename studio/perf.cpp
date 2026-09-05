// Geekatplay TerraForge - performance monitoring and the governor. See
// perf.hpp.
#include "perf.hpp"
#include "app.hpp"
#include "config.hpp"
#include "console.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <glad/gl.h>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace studio {

namespace {

PerfStats g_stats;
PerfQuality g_quality;

using clock_t_ = std::chrono::steady_clock;
clock_t_::time_point g_frame_start, g_phase_start, g_last_frame_end;
bool g_have_last = false;
float g_work_acc = 0.f;
struct PhaseAcc { const char *name; float ms; };
PhaseAcc g_phases[16];
int g_phase_count = 0;
std::vector<std::pair<std::string, float>> g_phase_smooth;

// GL timer queries: a ring of four, read one frame late
GLuint g_queries[32] = {};
GLuint g_query_ends[32] = {};
bool g_query_issued[32] = {};
int g_query_ix = 0;
bool g_query_open = false;
float g_gpu_frame_acc = 0.f;
float g_views_cpu_acc = 0.f;
int g_views_drawn = 0;
clock_t_::time_point g_view_cpu_start;

double g_sample_t = 0;
double g_slow_since = -1, g_fast_since = -1;
int g_level = 0;
double g_last_step = 0;
#ifdef _WIN32
ULONGLONG g_last_cpu = 0, g_last_wall = 0;
#endif

float smooth(float old, float now, float k = 0.1f) { return old <= 0.f ? now : old + (now - old) * k; }

void sample_system() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc)) {
    g_stats.process_mb = pmc.WorkingSetSize / 1048576.0;
    g_stats.process_peak_mb = pmc.PeakWorkingSetSize / 1048576.0;
  }
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof ms;
  if (GlobalMemoryStatusEx(&ms)) {
    g_stats.system_free_mb = ms.ullAvailPhys / 1048576.0;
    g_stats.system_total_mb = ms.ullTotalPhys / 1048576.0;
  }
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  g_stats.cpu_cores = (int)si.dwNumberOfProcessors;
  FILETIME c, e, k, u;
  if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
    ULONGLONG cpu = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) +
                    ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG wall = (ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime;
    if (g_last_wall && wall > g_last_wall)
      g_stats.cpu_pct = std::clamp((float)((cpu - g_last_cpu) * 100.0 / (double)(wall - g_last_wall) /
                                           std::max(g_stats.cpu_cores, 1)),
                                   0.f, 100.f);
    g_last_cpu = cpu;
    g_last_wall = wall;
  }
#endif
  // VRAM through the vendor extensions; the driver may say nothing
  GLint total_kb = 0, avail_kb = 0;
  glGetError();
  glGetIntegerv(0x9048 /* GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX */, &total_kb);
  glGetIntegerv(0x9049 /* GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX */, &avail_kb);
  if (glGetError() == GL_NO_ERROR && total_kb > 0) {
    g_stats.vram_total_mb = total_kb / 1024.0;
    g_stats.vram_used_mb = (total_kb - avail_kb) / 1024.0;
  } else {
    GLint ati[4] = {0, 0, 0, 0};
    glGetError();
    glGetIntegerv(0x87FC /* TEXTURE_FREE_MEMORY_ATI */, ati);
    if (glGetError() == GL_NO_ERROR && ati[0] > 0) {
      g_stats.vram_total_mb = -1;
      g_stats.vram_used_mb = -1;
      g_stats.system_free_mb = g_stats.system_free_mb; // free VRAM only; not enough to show used/total
    }
  }
}

void apply_level(int level) {
  PerfQuality q;
  switch (level) {
    case 1:
      q.scale_secondary = 0.5f;
      q.preview_quality_cap = 0;
      q.preview_fps_cap = 5;
      break;
    case 2:
      q.scale_secondary = 0.5f;
      q.preview_quality_cap = 0;
      q.preview_fps_cap = 3;
      q.shadows_secondary = false;
      q.cloud_quality_cap = 0;
      break;
    case 3:
      q.scale_secondary = 0.35f;
      q.scale_primary = 0.75f;
      q.preview_quality_cap = 0;
      q.preview_fps_cap = 2;
      q.shadows_secondary = false;
      q.cloud_quality_cap = 0;
      q.tess_scale = 0.5f;
      break;
    case 4:
      q.scale_secondary = 0.25f;
      q.scale_primary = 0.5f;
      q.preview_quality_cap = 0;
      q.preview_fps_cap = 1;
      q.shadows_secondary = false;
      q.shadows_primary = false;
      q.cloud_quality_cap = 0;
      q.tess_scale = 0.25f;
      break;
    default:
      break;
  }
  g_quality = q;
  g_stats.governor_level = level;
  static const char *notes[] = {"full quality", "lite: secondary views, preview",
                                "lite: + shadows and clouds in secondary views",
                                "lite: + primary view scale, subdivision",
                                "lite: everything lightened"};
  g_stats.governor_note = notes[std::clamp(level, 0, 4)];
}

} // namespace

const PerfStats &perf_stats() { return g_stats; }
const std::vector<std::pair<std::string, float>> &perf_phases() { return g_phase_smooth; }
const PerfQuality &perf_quality() { return g_quality; }

float perf_render_scale(int slot) { return slot == app().view_focus ? g_quality.scale_primary : g_quality.scale_secondary; }
bool perf_shadows_for(int slot) { return slot == app().view_focus ? g_quality.shadows_primary : g_quality.shadows_secondary; }

void perf_init_gpu() {
  const GLubyte *r = glGetString(GL_RENDERER);
  g_stats.gpu_name = r ? (const char *)r : "unknown GPU";
  glGenQueries(32, g_queries);
  glGenQueries(32, g_query_ends);
  g_last_step = 0;
}

// ---------------------------------------------------------------- phases
void perf_frame_begin() {
  auto now = clock_t_::now();
  if (g_have_last) {
    float frame = std::chrono::duration<float, std::milli>(now - g_frame_start).count();
    g_stats.frame_ms = smooth(g_stats.frame_ms, frame);
    g_stats.fps = g_stats.frame_ms > 0.01f ? 1000.f / g_stats.frame_ms : 0.f;
  }
  g_frame_start = g_phase_start = now;
  g_phase_count = 0;
  g_views_drawn = 0;
  g_gpu_frame_acc = 0.f;
  g_views_cpu_acc = 0.f;
}

void perf_mark(const char *phase) {
  auto now = clock_t_::now();
  float ms = std::chrono::duration<float, std::milli>(now - g_phase_start).count();
  g_phase_start = now;
  if (g_phase_count < 16) g_phases[g_phase_count++] = {phase, ms};
}

void perf_frame_end() {
  auto now = clock_t_::now();
  float work = std::chrono::duration<float, std::milli>(now - g_frame_start).count();
  g_stats.work_ms = smooth(g_stats.work_ms, work);
  static float recent[240] = {};
  static unsigned samples = 0;
  recent[samples++ % 240] = work;
  if (samples % 30 == 0) {
    unsigned n = std::min(samples, 240u);
    std::vector<float> sorted(recent, recent + n);
    std::sort(sorted.begin(), sorted.end());
    g_stats.work_p95_ms = sorted[(n - 1) * 95 / 100];
    g_stats.work_p99_ms = sorted[(n - 1) * 99 / 100];
  }
  auto phase = [&](const char *name) {
    for (int i = 0; i < g_phase_count; ++i)
      if (std::strcmp(g_phases[i].name, name) == 0) return g_phases[i].ms;
    return 0.f;
  };
  // a phase not marked this frame cost nothing this frame: it decays to
  // zero rather than showing its last value for ever
  for (auto &kv : g_phase_smooth) {
    bool seen = false;
    for (int i = 0; i < g_phase_count; ++i)
      if (kv.first == g_phases[i].name) { kv.second = smooth(kv.second, g_phases[i].ms); seen = true; }
    if (!seen) kv.second = kv.second * 0.9f;
  }
  for (int i = 0; i < g_phase_count; ++i) {
    bool found = false;
    for (auto &kv : g_phase_smooth)
      if (kv.first == g_phases[i].name) found = true;
    if (!found) g_phase_smooth.emplace_back(g_phases[i].name, g_phases[i].ms);
  }
  // interface = everything that is not the API, the upload path or the services
  g_stats.ui_ms = smooth(g_stats.ui_ms, std::max(0.f, work - phase("api") - phase("upload") - phase("previews") - phase("place") - phase("services")));
  g_stats.views_ms = smooth(g_stats.views_ms, g_views_cpu_acc);
  g_stats.views_drawn = g_views_drawn;
  g_stats.previews_ms = smooth(g_stats.previews_ms, phase("previews"));
  g_stats.api_ms = smooth(g_stats.api_ms, phase("api"));
  g_stats.upload_ms = smooth(g_stats.upload_ms, phase("upload"));
  g_stats.gpu_ms = smooth(g_stats.gpu_ms, g_gpu_frame_acc);
  float bound = std::max(g_stats.work_ms, g_stats.gpu_ms);
  g_stats.potential_fps = bound > 0.01f ? 1000.f / bound : 0.f;
  g_last_frame_end = now;
  g_have_last = true;
}

// ------------------------------------------------------------------- gpu
void perf_gpu_begin() {
  if (!g_queries[0] || g_query_open) return;
  // Timestamp pairs may enclose the pass timers' elapsed queries. Never
  // overwrite an outstanding pair or wait for the GPU to finish it.
  g_view_cpu_start = clock_t_::now();
  ++g_views_drawn;
  GLuint q = g_queries[g_query_ix];
  if (g_query_issued[g_query_ix]) {
    GLint avail = 0;
    glGetQueryObjectiv(g_query_ends[g_query_ix], GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) return;
    GLuint64 start = 0, end = 0;
    glGetQueryObjectui64v(q, GL_QUERY_RESULT, &start);
    glGetQueryObjectui64v(g_query_ends[g_query_ix], GL_QUERY_RESULT, &end);
    g_gpu_frame_acc += (float)((end - start) / 1.0e6);
  }
  glQueryCounter(q, GL_TIMESTAMP);
  g_query_open = true;
}

void perf_gpu_end() {
  g_views_cpu_acc += std::chrono::duration<float, std::milli>(clock_t_::now() - g_view_cpu_start).count();
  if (!g_query_open) return;
  glQueryCounter(g_query_ends[g_query_ix], GL_TIMESTAMP);
  g_query_issued[g_query_ix] = true;
  g_query_open = false;
  g_query_ix = (g_query_ix + 1) % 32;
}

// -------------------------------------------------------------- governor
void perf_governor_tick(App &a) {
  double now = std::chrono::duration<double>(clock_t_::now().time_since_epoch()).count();
  if (now - g_sample_t > 0.5) {
    g_sample_t = now;
    sample_system();
    g_stats.eval_ms = (float)a.snapshot_total_ms;
    g_stats.nodes = (int)a.node_views.size();
    g_stats.patches = renderer_patches_visible();
  }
  const PerfConfig &pc = config().perf;
  if (!pc.governor) {
    if (g_level != 0) apply_level(g_level = 0);
    return;
  }
  // the governor judges the work, not the capped frame rate: a frame that
  // sleeps to the viewport rate is not slow
  if (g_stats.potential_fps <= 0.f) return;
  // the first seconds are shader compiles and first uploads, not a verdict
  static double t_first = now;
  if (now - t_first < 4.0) return;
  const float target = (float)std::max(pc.fps_primary, 5);
  const bool slow = g_stats.potential_fps < target;
  const bool fast = g_stats.potential_fps > target * 1.6f;
  if (slow) { g_fast_since = -1; if (g_slow_since < 0) g_slow_since = now; }
  else { g_slow_since = -1; }
  if (fast) { if (g_fast_since < 0) g_fast_since = now; }
  else { g_fast_since = -1; }
  // a step down after 0.7 s slow, a step up after 4 s comfortable, and never
  // two steps within a second so the picture does not flicker between them
  if (slow && g_level < 4 && now - g_slow_since > 0.7 && now - g_last_step > 1.0) {
    apply_level(++g_level);
    g_last_step = now;
    g_slow_since = now;
    log_info("perf", "governor down to level " + std::to_string(g_level) + ": " + g_stats.governor_note +
                         " (" + std::to_string((int)g_stats.potential_fps) + " fps possible, target " +
                         std::to_string((int)target) + ")");
  } else if (fast && g_level > 0 && now - g_fast_since > 4.0 && now - g_last_step > 1.0) {
    apply_level(--g_level);
    g_last_step = now;
    g_fast_since = now;
    log_info("perf", "governor up to level " + std::to_string(g_level) + ": " + g_stats.governor_note);
  }
}

} // namespace studio
