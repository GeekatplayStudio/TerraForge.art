// Geekatplay Studio — tiny parallel-for over row ranges
#pragma once
#include <functional>

namespace gpx {

// Runs fn(y0, y1) on hardware threads, splitting [0, h) into row bands.
void parallel_rows(int h, const std::function<void(int, int)> &fn);

// Per-index variant: fn(i) for i in [0, n), chunked.
void parallel_index(size_t n, const std::function<void(size_t, size_t)> &fn);

} // namespace gpx
