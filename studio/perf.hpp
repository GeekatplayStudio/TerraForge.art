// Geekatplay TerraForge - performance monitoring and the governor that keeps
// the viewport responsive.
//
// Every frame is timed in phases (UI, views, previews, API, upload) on the
// CPU and, with GL timer queries, on the GPU; the process and the system
// are sampled twice a second (memory, CPU, VRAM). The numbers feed the
// status bar (panel_statusbar.cpp), the API state (so a script can read
// them), and the governor: when the work per frame would drop the primary
// viewport below the threshold in Settings, it lowers what costs the most
// first - secondary views' render scale, the preview panel, shadows and
// cloud quality in secondary views, then the primary view's scale and the
// terrain's subdivision - and gives each step back once the frame has been
// comfortable for a while. Nothing it changes is written to the project.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace studio {

struct App;

struct PerfStats {
  // timing, exponentially smoothed
  float fps = 0.f;          // frames actually shown per second
  float work_ms = 0.f;      // CPU time spent per frame, sleep excluded
  float frame_ms = 0.f;     // wall time per frame, sleep included
  float ui_ms = 0.f, views_ms = 0.f, previews_ms = 0.f, api_ms = 0.f, upload_ms = 0.f;
  float gpu_ms = 0.f;       // GPU time inside the view draws
  float potential_fps = 0.f; // 1000 / max(work_ms, gpu_ms)
  int views_drawn = 0;
  // the last evaluation
  float eval_ms = 0.f;
  int nodes = 0;
  int patches = 0;
  // process and system
  double process_mb = 0, process_peak_mb = 0;
  double system_free_mb = 0, system_total_mb = 0;
  float cpu_pct = 0.f;      // this process, of all cores
  int cpu_cores = 0;
  double vram_used_mb = -1, vram_total_mb = -1; // -1 = the driver does not say
  std::string gpu_name;
  // governor
  int governor_level = 0;   // 0 full quality .. 4 everything lightened
  std::string governor_note;
};

const PerfStats &perf_stats();
// Every marked phase, smoothed, in mark order: the breakdown a script reads.
const std::vector<std::pair<std::string, float>> &perf_phases();

// Frame phases. begin at the top of the loop, mark after each phase by name
// ("ui", "views", "previews", "api", "upload"), end before the swap.
void perf_frame_begin();
void perf_mark(const char *phase);
void perf_frame_end();

// GPU timing around a view draw. Results are read a frame later, so the
// queries never stall the pipeline.
void perf_gpu_begin();
void perf_gpu_end();

// What the governor has decided for this frame.
struct PerfQuality {
  float scale_primary = 1.f;   // render scale of View 1
  float scale_secondary = 1.f; // render scale of every other view
  bool shadows_secondary = true;
  bool shadows_primary = true;
  int cloud_quality_cap = 2;   // 0 draft .. 2 high
  float tess_scale = 1.f;      // multiplies the subdivision limit
  int preview_quality_cap = 2; // the Preview panel's render scale index
  int preview_fps_cap = 60;
};
const PerfQuality &perf_quality();
float perf_render_scale(int slot);
bool perf_shadows_for(int slot);

// Once a frame, after the phases are marked: sample the system and move the
// governor a step if the frame has been slow or comfortable long enough.
void perf_governor_tick(App &a);

// The GPU name, read once from the context.
void perf_init_gpu();

} // namespace studio
