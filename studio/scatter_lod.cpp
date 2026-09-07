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
  const float card = std::max(p.billboard_m * p.scale, far);
  if (dist_m <= p.full_m) return 0;
  if (dist_m <= (p.full_m + far) * 0.5f) return 1;
  if (dist_m <= card) return 2;
  return SCATTER_LOD_BILLBOARD;
}

void visible_cells(float eye_x, float eye_z, float radius_m, float cell_m,
                   size_t budget, std::vector<WorldCell> &out) {
  out.clear();
  if (radius_m <= 0.f || cell_m <= 0.f || budget == 0) return;
  const long long lo_x = (long long)std::floor((eye_x - radius_m) / cell_m);
  const long long hi_x = (long long)std::floor((eye_x + radius_m) / cell_m);
  const long long lo_z = (long long)std::floor((eye_z - radius_m) / cell_m);
  const long long hi_z = (long long)std::floor((eye_z + radius_m) / cell_m);
  // a radius far wider than the cell would ask for more cells than any
  // budget could hold; the clamp keeps the sweep itself bounded too
  const long long span = (long long)std::ceil(2.f * radius_m / cell_m) + 2;
  if (span > 4096) return;
  for (long long cz = lo_z; cz <= hi_z; ++cz)
    for (long long cx = lo_x; cx <= hi_x; ++cx) {
      const float x0 = (float)(cx * (double)cell_m), x1 = x0 + cell_m;
      const float z0 = (float)(cz * (double)cell_m), z1 = z0 + cell_m;
      const float dx = eye_x < x0 ? x0 - eye_x : eye_x > x1 ? eye_x - x1 : 0.f;
      const float dz = eye_z < z0 ? z0 - eye_z : eye_z > z1 ? eye_z - z1 : 0.f;
      const float d = std::sqrt(dx * dx + dz * dz);
      if (d > radius_m) continue;
      out.push_back({cx, cz, d});
    }
  std::stable_sort(out.begin(), out.end(), [](const WorldCell &a, const WorldCell &b) {
    if (a.dist != b.dist) return a.dist < b.dist;
    if (a.z != b.z) return a.z < b.z;
    return a.x < b.x;
  });
  if (out.size() > budget) out.resize(budget);
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
