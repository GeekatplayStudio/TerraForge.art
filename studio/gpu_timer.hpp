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

  void begin();
  void end();
  // Milliseconds of GPU time for this pass, smoothed over recent frames.
  // Zero until the first result comes back.
  double ms() const { return smoothed_ms; }
  const std::string &name() const { return pass_name; }

  struct Scope {
    GpuTimer &t;
    explicit Scope(GpuTimer &timer) : t(timer) { t.begin(); }
    ~Scope() { t.end(); }
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

// Timers are looked up by name and live for the process, so a caller does not
// have to own one. The name is what shows up in the readout.
GpuTimer &gpu_timer(const char *name);

// Every timer that has ever run, as "name: x.xx ms" lines. For the status
// readout and the API state.
std::string gpu_timer_report();
// One timer's last measurement, or 0 if it has never run.
double gpu_timer_ms(const char *name);

} // namespace studio
