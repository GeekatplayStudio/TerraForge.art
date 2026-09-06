// Geekatplay TerraForge - see scatter_lod.hpp.
#include "scatter_lod.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace studio {

void scatter_bucket(std::vector<float> &inst, int stride, int key_at, int grid,
                    std::vector<InstanceCell> &cells) {
  cells.clear();
  if (stride <= 0 || inst.empty()) return;
  const int n = (int)(inst.size() / (size_t)stride);
  const int g = std::max(grid, 1);
  std::vector<int> cell_of((size_t)n);
  std::vector<int> count((size_t)g * g, 0);
  for (int i = 0; i < n; ++i) {
    const float *s = &inst[(size_t)i * stride];
    int cx = std::clamp((int)(s[0] * g), 0, g - 1);
    int cz = std::clamp((int)(s[2] * g), 0, g - 1);
    cell_of[(size_t)i] = cz * g + cx;
    ++count[(size_t)cell_of[(size_t)i]];
  }
  // order: by cell, then by key - one sort, stable so equal keys keep their
  // producer's order and the result is the same on every machine
  std::vector<int> order((size_t)n);
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    if (cell_of[(size_t)a] != cell_of[(size_t)b]) return cell_of[(size_t)a] < cell_of[(size_t)b];
    return inst[(size_t)a * stride + key_at] < inst[(size_t)b * stride + key_at];
  });
  std::vector<float> sorted;
  sorted.reserve(inst.size());
  int last_cell = -1;
  for (int k = 0; k < n; ++k) {
    const int i = order[(size_t)k];
    const float *s = &inst[(size_t)i * stride];
    const int c = cell_of[(size_t)i];
    if (c != last_cell) {
      InstanceCell cell;
      cell.first = k;
      cell.count = 0;
      for (int a = 0; a < 3; ++a) { cell.lo[a] = s[a]; cell.hi[a] = s[a]; }
      cells.push_back(cell);
      last_cell = c;
    }
    InstanceCell &cell = cells.back();
    ++cell.count;
    for (int a = 0; a < 3; ++a) {
      cell.lo[a] = std::min(cell.lo[a], s[a]);
      cell.hi[a] = std::max(cell.hi[a], s[a]);
    }
    sorted.insert(sorted.end(), s, s + stride);
  }
  inst.swap(sorted);
}

float scatter_keep(float dist_m, const LodParams &p) {
  const float full = std::max(p.full_m, 0.f);
  const float far = std::max(p.far_m * p.scale, full + 1e-3f);
  const float cull = std::max(p.cull_m * p.scale, far);
  if (dist_m <= full) return 1.f;
  if (dist_m >= cull) return 0.f;
  const float mk = std::clamp(p.min_keep, 0.f, 1.f);
  if (dist_m >= far) return mk;
  float t = (dist_m - full) / (far - full);
  t = t * t * (3.f - 2.f * t);
  return 1.f + (mk - 1.f) * t;
}

float scatter_grow(float keep) {
  if (keep >= 1.f || keep <= 0.f) return 1.f;
  return std::min(std::sqrt(1.f / keep), 2.f);
}

int scatter_lod_level(float dist_m, const LodParams &p) {
  const float far = std::max(p.far_m * p.scale, p.full_m);
  if (dist_m <= p.full_m) return 0;
  if (dist_m <= (p.full_m + far) * 0.5f) return 1;
  return 2;
}

float aabb_distance(const float q[3], const float lo[3], const float hi[3]) {
  float d2 = 0.f;
  for (int a = 0; a < 3; ++a) {
    float d = q[a] < lo[a] ? lo[a] - q[a] : q[a] > hi[a] ? q[a] - hi[a] : 0.f;
    d2 += d * d;
  }
  return std::sqrt(d2);
}

} // namespace studio
