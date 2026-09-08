// Geekatplay TerraForge — the performance watcher: where the time went, and
// which of it was for nothing.
#include "perf_watch.hpp"
#include "app.hpp"
#include "config.hpp"
#include "console.hpp"
#include "perf.hpp"
#include <json.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace studio {

namespace {

constexpr double WATCH_PERIOD_S = 10.0;

std::mutex g_mtx;
std::map<std::string, long long> g_counts;        // since the last report
std::map<std::string, long long> g_counts_total;  // since launch

// Per-period accumulation of the frame picture.
struct Period {
  double started = 0;
  int frames = 0;
  int busy_idle_frames = 0;   // did real work while nothing changed
  double work_sum = 0, work_max = 0;
  double gpu_sum = 0;
  std::map<std::string, double> phase_sum;
  std::map<std::string, double> phase_max;
};
Period g_period;
std::string g_last_json, g_last_findings;
double g_last_report_time = 0;

// A frame that spent more than this working, with no input, no evaluation,
// no upload and no animation, is a frame that ran for nothing.
constexpr float IDLE_WORK_MS = 4.f;

} // namespace

void perf_count(const char *name, int n) {
  std::lock_guard<std::mutex> lk(g_mtx);
  g_counts[name] += n;
  g_counts_total[name] += n;
}

static void report(App &a, double now) {
  Period p;
  std::map<std::string, long long> counts;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    p = g_period;
    counts = g_counts;
    g_counts.clear();
    g_period = Period{};
    g_period.started = now;
  }
  const double secs = std::max(now - p.started, 1e-3);
  const PerfStats &st = perf_stats();
  auto rate = [&](const char *k) { return (double)counts[k] / secs; };

  std::vector<std::string> findings;
  char buf[320];
  // 1. work done while nothing changed
  if (p.frames > 0 && p.busy_idle_frames > p.frames / 4) {
    std::snprintf(buf, sizeof buf,
                  "%d of %d frames did more than %.0f ms of work while nothing changed "
                  "(no input, no evaluation, no upload, not animating) - something redraws "
                  "or recomputes every frame regardless",
                  p.busy_idle_frames, p.frames, IDLE_WORK_MS);
    findings.push_back(buf);
  }
  // 2. evaluations nobody asked for
  if (rate("eval") > 2.0 && rate("input.frames") < 0.5) {
    std::snprintf(buf, sizeof buf,
                  "the graph was evaluated %.1f times a second with no input - a node or a "
                  "service is marking the graph dirty on its own",
                  rate("eval"));
    findings.push_back(buf);
  }
  // 3. secondary views redrawn while idle
  if (rate("view.draw.secondary") > 5.0 && rate("input.frames") < 0.5 && rate("eval") < 0.2) {
    std::snprintf(buf, sizeof buf,
                  "secondary viewports were redrawn %.0f times a second while idle - their "
                  "cache is being invalidated without a reason to",
                  rate("view.draw.secondary"));
    findings.push_back(buf);
  }
  // 4. panels losing the graph lock
  if (rate("lease.miss") > 2.0) {
    std::snprintf(buf, sizeof buf,
                  "panels failed to take the graph lock %.1f times a second - each miss is a "
                  "blanked panel for a frame; the evaluation is holding it too long or too often",
                  rate("lease.miss"));
    findings.push_back(buf);
  }
  // 5. uploads faster than evaluations
  if (rate("upload") > rate("eval") * 2.0 + 1.0) {
    std::snprintf(buf, sizeof buf,
                  "the terrain was uploaded %.1f times a second against %.1f evaluations - "
                  "uploads are being forced without new data",
                  rate("upload"), rate("eval"));
    findings.push_back(buf);
  }
  // 6. the heaviest phase, when the frame is over budget
  if (p.frames > 0) {
    std::string worst;
    double worst_ms = 0;
    for (auto &kv : p.phase_sum) {
      const double avg = kv.second / p.frames;
      if (avg > worst_ms) { worst_ms = avg; worst = kv.first; }
    }
    const double work_avg = p.work_sum / p.frames;
    if (work_avg > 1000.0 / std::max(config().perf.fps_primary, 1) && !worst.empty()) {
      std::snprintf(buf, sizeof buf,
                    "frames averaged %.1f ms of work, over the %d fps budget; the heaviest "
                    "phase is '%s' at %.1f ms (peak %.1f ms)",
                    work_avg, config().perf.fps_primary, worst.c_str(), worst_ms,
                    p.phase_max[worst]);
      findings.push_back(buf);
    }
  }
  // 7. GPU-bound
  if (st.gpu_ms > st.work_ms * 1.5f && st.gpu_ms > 12.f) {
    std::snprintf(buf, sizeof buf,
                  "the GPU takes %.1f ms a frame against %.1f ms of CPU work - the frame is "
                  "fragment-bound; resolution scale and shadows are the levers",
                  st.gpu_ms, st.work_ms);
    findings.push_back(buf);
  }

  json j;
  j["at"] = now;
  j["period_s"] = secs;
  j["frames"] = p.frames;
  j["fps"] = st.fps;
  j["work_ms"] = st.work_ms;
  j["work_p95_ms"] = st.work_p95_ms;
  j["gpu_ms"] = st.gpu_ms;
  j["governor_level"] = st.governor_level;
  j["busy_idle_frames"] = p.busy_idle_frames;
  json phases = json::object();
  for (auto &kv : p.phase_sum)
    phases[kv.first] = {{"avg_ms", p.frames ? kv.second / p.frames : 0.0},
                        {"max_ms", p.phase_max[kv.first]}};
  j["phases"] = phases;
  json rates = json::object();
  for (auto &kv : counts) rates[kv.first] = (double)kv.second / secs;
  j["events_per_s"] = rates;
  json totals = json::object();
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto &kv : g_counts_total) totals[kv.first] = kv.second;
  }
  j["events_total"] = totals;
  j["findings"] = findings;
  j["memory_mb"] = st.process_mb;
  j["vram_mb"] = st.vram_used_mb;

  std::string text;
  for (const std::string &f : findings) text += (text.empty() ? "" : "\n") + f;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_last_json = j.dump(2);
    g_last_findings = text;
  }
  for (const std::string &f : findings) log_warn("perfwatch", f);
  std::error_code ec;
  fs::create_directories("logs", ec);
  std::ofstream out("logs/perf_watch.json");
  if (out) out << g_last_json;
  (void)a;
}

void perf_watch_tick(App &a) {
  if (!config().perf.watch) return;
  const double now = ImGui::GetTime();
  const PerfStats &st = perf_stats();
  ImGuiIO &io = ImGui::GetIO();
  const bool input = io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f || io.MouseWheel != 0.f ||
                     ImGui::IsAnyMouseDown() || io.InputQueueCharacters.Size > 0 ||
                     ImGui::IsAnyItemActive();
  const bool changing = a.eval.running.load() || a.eval_serial != a.uploaded_serial ||
                        a.anim_playing || a.seq_active;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_period.started == 0) g_period.started = now;
    Period &p = g_period;
    ++p.frames;
    if (input) ++g_counts["input.frames"];
    // the instantaneous frame, not the smoothed one: the last mark's sum
    double work = 0;
    for (const auto &ph : perf_phases()) {
      p.phase_sum[ph.first] += ph.second;
      p.phase_max[ph.first] = std::max(p.phase_max[ph.first], (double)ph.second);
      work += ph.second;
    }
    p.work_sum += work;
    p.work_max = std::max(p.work_max, work);
    p.gpu_sum += st.gpu_ms;
    if (!input && !changing && work > IDLE_WORK_MS) ++p.busy_idle_frames;
  }
  if (now - g_last_report_time >= WATCH_PERIOD_S) {
    g_last_report_time = now;
    report(a, now);
  }
}

void perf_watch_report_now(App &a) {
  g_last_report_time = ImGui::GetTime();
  report(a, g_last_report_time);
}

std::string perf_watch_report_json() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_last_json;
}
std::string perf_watch_findings_text() {
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_last_findings.empty() ? "no findings: nothing is doing work for nothing" : g_last_findings;
}

} // namespace studio
