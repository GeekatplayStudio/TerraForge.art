#include "gpx/parallel.hpp"
#include "parallel_pool.hpp"
#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <thread>
#include <vector>

namespace gpx {

static unsigned g_forced_workers = 0;

unsigned worker_count() {
  if (g_forced_workers) return g_forced_workers;
  // Read once: the tests set this through set_worker_count(), and a solver
  // asking the environment on every call would be both slow and racy.
  static const unsigned from_env = [] {
    const char *e = std::getenv("GPX_WORKERS");
    if (!e) return 0u;
    long v = std::strtol(e, nullptr, 10);
    return (v > 0 && v < 4096) ? (unsigned)v : 0u;
  }();
  if (from_env) return from_env;
  unsigned n = std::thread::hardware_concurrency();
  return n ? n : 4;
}

void set_worker_count(unsigned n) { g_forced_workers = n; }

void parallel_rows(int h, const std::function<void(int, int)> &fn) {
  unsigned nt = std::min<unsigned>(worker_count(), std::max(1, h / 16));
  if (nt <= 1) {
    fn(0, h);
    return;
  }
  int band = (h + (int)nt - 1) / (int)nt;
  detail::parallel_pool().run(nt, [&](unsigned t) {
    int y0 = (int)t * band, y1 = std::min(h, y0 + band);
    if (y0 < y1) fn(y0, y1);
  });
}

void parallel_index(size_t n, const std::function<void(size_t, size_t)> &fn) {
  unsigned nt = worker_count();
  if (n < 4096 || nt <= 1) {
    fn(0, n);
    return;
  }
  size_t band = (n + nt - 1) / nt;
  detail::parallel_pool().run(nt, [&](unsigned t) {
    size_t i0 = t * band, i1 = std::min(n, i0 + band);
    if (i0 < i1) fn(i0, i1);
  });
}

} // namespace gpx
