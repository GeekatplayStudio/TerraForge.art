// Geekatplay TerraForge — GPU timing for one pass.
//
// The standing rule is that no performance claim ships without a number beside
// it, and wall-clock frame time cannot supply one: with vsync on, every frame
// is 16.7 ms whatever the GPU is doing, so a change that halves the terrain
// pass measures as zero. Timing the pass on the GPU itself measures the thing
// that actually changed.
//
// GL_TIME_ELAPSED queries are asynchronous. Reading one back in the frame that
// issued it would stall the pipeline — turning a measuring instrument into a
// performance problem — so results are collected from a query issued several
// frames earlier, and only when the driver says it is ready.
#pragma once
#include <string>

namespace studio {

// One timer per named pass. Scope it around the draw calls to measure.
//
//   { GpuTimer::Scope s(gpu_timer("terrain")); ...draws... }
//
// Nesting two scopes is not supported: GL allows only one GL_TIME_ELAPSED
// query at a time, so an inner scope would silently break the outer one. The
// implementation refuses rather than corrupting both.
class GpuTimer {
public:
  explicit GpuTimer(const char *name);
  ~GpuTimer();
  GpuTimer(const GpuTimer &) = delete;
  GpuTimer &operator=(const GpuTimer &) = delete;

  bool begin();
  void end();
  // Milliseconds of GPU time for this pass, smoothed over recent frames.
  // Zero until the first result comes back.
  double ms() const { return smoothed_ms; }
  const std::string &name() const { return pass_name; }

  struct Scope {
    GpuTimer &t;
    bool started;
    explicit Scope(GpuTimer &timer) : t(timer), started(t.begin()) {}
    ~Scope() { if (started) t.end(); }
  };

private:
  static const int RING = 4;
  unsigned queries[RING] = {0, 0, 0, 0};
  bool issued[RING] = {false, false, false, false};
  int slot = 0;
  bool open = false;
  double smoothed_ms = 0.0;
  std::string pass_name;
};

// How many primitives a pass actually put through the pipeline.
//
// Time alone cannot tell you what to fix. A terrain pass at 4 ms is a
// different problem if it drew 60 000 triangles than if it drew 12 million:
// the first is fragment-bound and a finer geometry LOD would buy nothing, the
// second is geometry-bound and is exactly what a quadtree is for. With a
// tessellation shader the count is not knowable on the CPU at all — the
// tessellator decides it per patch, per frame — so it has to be asked of the
// GPU.
//
// GL_PRIMITIVES_GENERATED counts what the geometry stage emitted, before
// clipping and before rasterisation. That is the right number here: it is the
// work the tessellator was asked to do.
class GpuCounter {
public:
  explicit GpuCounter(const char *name);
  GpuCounter(const GpuCounter &) = delete;
  GpuCounter &operator=(const GpuCounter &) = delete;

  bool begin();
  void end();
  // Primitives from the most recent completed frame. Not smoothed: this is a
  // count, and an average of two camera positions is a number that describes
  // neither.
  unsigned long long primitives() const { return last; }

  struct Scope {
    GpuCounter &c;
    bool started;
    explicit Scope(GpuCounter &counter) : c(counter), started(c.begin()) {}
    ~Scope() { if (started) c.end(); }
  };

private:
  static const int RING = 4;
  unsigned queries[RING] = {0, 0, 0, 0};
  bool issued[RING] = {false, false, false, false};
  int slot = 0;
  bool open = false;
  unsigned long long last = 0;
  std::string pass_name;
};

// Timers are looked up by name and live for the process, so a caller does not
// have to own one. The name is what shows up in the readout.
GpuTimer &gpu_timer(const char *name);
GpuCounter &gpu_counter(const char *name);

// Every timer that has ever run, as "name: x.xx ms" lines. For the status
// readout and the API state.
std::string gpu_timer_report();
// One timer's last measurement, or 0 if it has never run.
double gpu_timer_ms(const char *name);
// One counter's last reading, or 0 if it has never run.
unsigned long long gpu_counter_primitives(const char *name);

} // namespace studio
