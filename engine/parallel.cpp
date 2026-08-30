#include "gpx/parallel.hpp"
#include <algorithm>
#include <thread>
#include <vector>

namespace gpx {

static unsigned worker_count() {
  unsigned n = std::thread::hardware_concurrency();
  return n ? n : 4;
}

void parallel_rows(int h, const std::function<void(int, int)> &fn) {
  unsigned nt = std::min<unsigned>(worker_count(), std::max(1, h / 16));
  if (nt <= 1) {
    fn(0, h);
    return;
  }
  std::vector<std::thread> pool;
  int band = (h + (int)nt - 1) / (int)nt;
  for (unsigned t = 0; t < nt; ++t) {
    int y0 = (int)t * band, y1 = std::min(h, y0 + band);
    if (y0 >= y1) break;
    pool.emplace_back(fn, y0, y1);
  }
  for (auto &th : pool) th.join();
}

void parallel_index(size_t n, const std::function<void(size_t, size_t)> &fn) {
  unsigned nt = worker_count();
  if (n < 4096 || nt <= 1) {
    fn(0, n);
    return;
  }
  std::vector<std::thread> pool;
  size_t band = (n + nt - 1) / nt;
  for (unsigned t = 0; t < nt; ++t) {
    size_t i0 = t * band, i1 = std::min(n, i0 + band);
    if (i0 >= i1) break;
    pool.emplace_back(fn, i0, i1);
  }
  for (auto &th : pool) th.join();
}

} // namespace gpx
