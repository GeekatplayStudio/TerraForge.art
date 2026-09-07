// Geekatplay TerraForge — an accelerator a node may borrow, or not.
//
// Node evaluation is the slowest thing in the application by three orders of
// magnitude: an erosion node at 2k costs 16 seconds where the whole render
// path costs one millisecond (docs/GPU_USE.md). The fix is the GPU, and
// three constraints decide the shape it has to take.
//
//   The engine is GL-free on purpose. libgeekatplay_nodeterrain is linked by
//   nodeterrain_cli and by every headless test; a node compute function that
//   called GL would drag a window and a context into all of them.
//
//   Evaluation runs on a worker thread so the UI never blocks, and a GL
//   context belongs to one thread at a time.
//
//   Results must be deterministic and bit-identical. GPU floating point is
//   neither, across vendors or drivers, so a golden hash computed on the GPU
//   would be a golden hash for one machine.
//
// So: the engine declares what it would like done and nothing about how. The
// studio installs an implementation; the CLI, the tests and the regression
// goldens install none and run exactly the CPU code they run today. A node
// asks, and carries on by itself when the answer is no.
//
// The contract for an implementation is narrow and strict:
//
//   * return false rather than a wrong answer - a fallback is cheap, a
//     silently different terrain is not;
//   * agree with the CPU path within the tolerance a test asserts, so what
//     somebody sees while dragging is what they get when they bake;
//   * be safe to call from the evaluation worker.
#pragma once
#include "fractal_core.hpp"
#include <cstdint>

namespace gpx {

class Heightmap;

// One job an accelerator may be asked to do. Deliberately coarse: the unit is
// a whole node's output, because that is the granularity at which falling
// back to the CPU is simple and correct.
struct Accelerator {
  virtual ~Accelerator() = default;

  // Is this accelerator usable right now? An implementation that failed to
  // compile its shaders, or is running on a driver without compute, says no
  // once and is never asked again this session.
  virtual bool available() const = 0;

  // A name for the log and the scene state, so a person can tell whether the
  // fast path is actually being taken.
  virtual const char *name() const = 0;

  // Fill `out` and `rough` (both already sized) with an fBm-family fractal
  // over the unit tile, exactly as gpx::fractal::eval would.
  //
  // It takes fractal::Params itself rather than a copy of the fields it
  // happens to need. A parallel struct would be a second place for a
  // parameter's *meaning* to live, and the two would drift the first time one
  // gained a control. What may differ between the paths is arithmetic, within
  // a tolerance a test asserts - never intent.
  //
  // Returning false is the normal way to handle a case an implementation does
  // not cover, and implementations are expected to cover a subset: the common
  // parameter combinations are worth accelerating and the exotic ones are not
  // worth the risk of a second, subtly different terrain.
  virtual bool fractal(const fractal::Params &, uint32_t seed, Heightmap &out,
                       Heightmap &rough) {
    (void)out;
    (void)rough;
    (void)seed;
    return false;
  }

  // How many jobs this accelerator has taken and how many it declined, for
  // the readout. A fast path nobody can see the use of is a fast path nobody
  // trusts.
  virtual void stats(uint64_t &taken, uint64_t &declined) const {
    taken = 0;
    declined = 0;
  }
};

// The installed accelerator, or null. Null is the normal case: the engine's
// own tests, the CLI and every golden run with none.
Accelerator *accelerator();

// Install one, or clear it with null. Not thread-safe against concurrent
// evaluation - call it at start-up, before any graph runs.
void set_accelerator(Accelerator *a);

// Sugar for the call site, which should read as "if something faster is
// available, use it; otherwise carry on".
inline Accelerator *accel_if_available() {
  Accelerator *a = accelerator();
  return (a && a->available()) ? a : nullptr;
}

} // namespace gpx
