// Geekatplay TerraForge — the installed accelerator, or none.
#include "gpx/accel.hpp"
#include <atomic>

namespace gpx {

namespace {
// Set once at start-up and read from the evaluation worker. Atomic because
// those are two threads, even though the write happens long before the reads:
// a plain pointer read racing a write is undefined however unlikely, and this
// costs a relaxed load on a path that then does millions of floating-point
// operations.
std::atomic<Accelerator *> g_accel{nullptr};
} // namespace

Accelerator *accelerator() { return g_accel.load(std::memory_order_acquire); }

void set_accelerator(Accelerator *a) {
  g_accel.store(a, std::memory_order_release);
}

} // namespace gpx
