// Geekatplay Studio — tiny parallel-for over row ranges
#pragma once
#include <functional>

namespace gpx {

// Runs fn(y0, y1) on hardware threads, splitting [0, h) into row bands.
void parallel_rows(int h, const std::function<void(int, int)> &fn);

// Per-index variant: fn(i) for i in [0, n), chunked.
void parallel_index(size_t n, const std::function<void(size_t, size_t)> &fn);

// The number of workers the solvers split across.
//
// This is exposed, and overridable, because AGENTS.md engine rule 1 promises
// bit-identical output "on every thread count" and nothing could test that
// while the count came straight from hardware_concurrency(). A test that runs
// at whatever the machine happens to have cannot see a solver whose result
// depends on how the work was divided.
//
// Order of precedence: an explicit set_worker_count(), then the GPX_WORKERS
// environment variable, then hardware_concurrency(). 0 restores the default.
unsigned worker_count();
void set_worker_count(unsigned n);


} // namespace gpx
