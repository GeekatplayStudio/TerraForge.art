// Geekatplay TerraForge - scatter stage 1: candidates on a lattice, and the
// hashing every later stage draws from.
//
// The lattice is fixed by the spacing, never by the density: a cell holds
// `per_cell` candidates whose ids are hash(seed, cell, index). Raising the
// density raises the accepted fraction of the same candidates; changing a
// mask changes which are accepted. Neither reshuffles the survivors, which
// is the stability Vue's manual promises (p1088) and Blender's Poisson mode
// gets the same way: generate first, filter second.
#include "gpx/scatter.hpp"
#include <algorithm>
#include <cmath>

namespace gpx::scatter {

uint64_t mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ull;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebull;
  x ^= x >> 31;
  return x;
}

uint64_t hash64(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  uint64_t h = 0x9E3779B97F4A7C15ull;
  h = mix64(h ^ a);
  h = mix64(h ^ b * 0xff51afd7ed558ccdull);
  h = mix64(h ^ c * 0xc4ceb9fe1a85ec53ull);
  h = mix64(h ^ d * 0x2545F4914F6CDD1Dull);
  return h;
}

float unit(uint64_t id, uint32_t channel) {
  uint64_t h = mix64(id ^ (uint64_t(channel) + 1) * 0x9E3779B97F4A7C15ull);
  return (h >> 40) / 16777216.f; // 24 bits, so it is never exactly 1
}

int lattice_n(const CandidateParams &p) {
  float cell = std::max(p.spacing, 1.f / 512.f);
  return std::clamp((int)std::floor(1.f / cell), 1, 512);
}

void candidates(const CandidateParams &p, PointCloud &out) {
  out.clear();
  const int n = lattice_n(p);
  const int k = std::clamp(p.per_cell, 1, 64);
  const float cell = 1.f / n;
  out.x.reserve((size_t)n * n * k);
  out.y.reserve((size_t)n * n * k);
  out.v.reserve((size_t)n * n * k);
  out.ensure_attrs();
  out.id.reserve((size_t)n * n * k);
  // sub-lattice for the jittered mode: k candidates fill a ceil(sqrt(k))^2
  // grid inside the cell, each jittered within its own slot
  const int sub = std::max(1, (int)std::ceil(std::sqrt((float)k)));
  const float clump_cell = std::max(p.clump_size, cell);
  const float pull = std::clamp(p.clump_amount, 0.f, 1.f);
  for (int cy = 0; cy < n; ++cy)
    for (int cx = 0; cx < n; ++cx)
      for (int j = 0; j < k; ++j) {
        const uint64_t id = hash64(p.seed, (uint64_t)cx, (uint64_t)cy, (uint64_t)j);
        float fx, fy; // 0..1 inside the cell
        if (p.mode == 2) {
          fx = (j % sub + 0.5f) / sub;
          fy = (j / sub + 0.5f) / sub;
        } else if (p.mode == 1) {
          fx = unit(id, 100);
          fy = unit(id, 101);
        } else {
          fx = (j % sub + unit(id, 100)) / sub;
          fy = (j / sub + unit(id, 101)) / sub;
        }
        float px = (cx + fx) * cell, py = (cy + fy) * cell;
        if (pull > 0.f) {
          // the clump centre of the clump cell this candidate falls in; a
          // hashed spot inside it so clumps do not sit on a visible grid
          int qx = (int)(px / clump_cell), qy = (int)(py / clump_cell);
          uint64_t cid = hash64(p.seed ^ 0xC1u, (uint64_t)qx, (uint64_t)qy, 7);
          float ccx = (qx + unit(cid, 0)) * clump_cell;
          float ccy = (qy + unit(cid, 1)) * clump_cell;
          // each candidate is pulled by its own share, so a clump has a
          // dense heart and a loose rim instead of a ring
          float w = pull * std::sqrt(unit(id, 102));
          px += (ccx - px) * w;
          py += (ccy - py) * w;
        }
        out.x.push_back(std::clamp(px, 0.f, 0.999999f));
        out.y.push_back(std::clamp(py, 0.f, 0.999999f));
        out.v.push_back(1.f);
        out.id.push_back(id);
      }
  // the other channels, at their defaults
  const size_t total = out.x.size();
  out.species.assign(total, 0);
  out.sx.assign(total, 1.f); out.sy.assign(total, 1.f); out.sz.assign(total, 1.f);
  out.yaw.assign(total, 0.f); out.tilt.assign(total, 0.f);
  out.tint.assign(total, 1.f); out.phase.assign(total, 0.f);
  out.radius.assign(total, 0.f); out.offset.assign(total, 0.f);
}

void candidates_cell(const CandidateParams &p, long long cell_x, long long cell_z,
                     PointCloud &out) {
  out.clear();
  const int k = std::clamp(p.per_cell, 1, 64);
  const float cell = std::max(p.spacing, 1e-9f);
  const int sub = std::max(1, (int)std::ceil(std::sqrt((float)k)));
  const float clump_cell = std::max(p.clump_size, cell);
  const float pull = std::clamp(p.clump_amount, 0.f, 1.f);
  out.ensure_attrs();
  for (int j = 0; j < k; ++j) {
    // the cell index is the hash's coordinate, so the same cell is the same
    // set of candidates whichever visit computed it
    const uint64_t id = hash64(p.seed, (uint64_t)cell_x, (uint64_t)cell_z, (uint64_t)j);
    float fx, fy;
    if (p.mode == 2) {
      fx = (j % sub + 0.5f) / sub;
      fy = (j / sub + 0.5f) / sub;
    } else if (p.mode == 1) {
      fx = unit(id, 100);
      fy = unit(id, 101);
    } else {
      fx = (j % sub + unit(id, 100)) / sub;
      fy = (j / sub + unit(id, 101)) / sub;
    }
    float px = (cell_x + fx) * cell, py = (cell_z + fy) * cell;
    if (pull > 0.f) {
      const long long qx = (long long)std::floor(px / clump_cell);
      const long long qz = (long long)std::floor(py / clump_cell);
      const uint64_t cid = hash64(p.seed ^ 0xC1u, (uint64_t)qx, (uint64_t)qz, 7);
      const float ccx = (qx + unit(cid, 0)) * clump_cell;
      const float ccy = (qz + unit(cid, 1)) * clump_cell;
      const float w = pull * std::sqrt(unit(id, 102));
      px += (ccx - px) * w;
      py += (ccy - py) * w;
    }
    out.x.push_back(px);
    out.y.push_back(py);
    out.v.push_back(1.f);
    out.id.push_back(id);
  }
  const size_t total = out.x.size();
  out.species.assign(total, 0);
  out.sx.assign(total, 1.f); out.sy.assign(total, 1.f); out.sz.assign(total, 1.f);
  out.yaw.assign(total, 0.f); out.tilt.assign(total, 0.f);
  out.tint.assign(total, 1.f); out.phase.assign(total, 0.f);
  out.radius.assign(total, 0.f); out.offset.assign(total, 0.f);
}

// ---------------------------------------------------------- neighbourhood
namespace {

// A uniform grid over the unit tile for radius queries. Cell = radius, so a
// query touches nine cells.
struct Grid {
  int n = 1;
  float cell = 1.f;
  std::vector<int> start, items; // CSR: items[start[c]..start[c+1])
  void build(const PointCloud &pc, float radius) {
    cell = std::max(radius, 1.f / 1024.f);
    n = std::clamp((int)(1.f / cell), 1, 1024);
    cell = 1.f / n;
    const size_t m = pc.size();
    std::vector<int> cnt((size_t)n * n + 1, 0);
    std::vector<int> cidx(m);
    for (size_t i = 0; i < m; ++i) {
      int cx = std::clamp((int)(pc.x[i] * n), 0, n - 1);
      int cy = std::clamp((int)(pc.y[i] * n), 0, n - 1);
      cidx[i] = cy * n + cx;
      ++cnt[(size_t)cidx[i] + 1];
    }
    for (size_t c = 0; c < (size_t)n * n; ++c) cnt[c + 1] += cnt[c];
    start = cnt;
    items.assign(m, 0);
    std::vector<int> fill(start.begin(), start.end() - 1);
    for (size_t i = 0; i < m; ++i) items[(size_t)fill[(size_t)cidx[i]]++] = (int)i;
  }
  template <class F> void each_near(float x, float y, F &&f) const {
    int cx = std::clamp((int)(x * n), 0, n - 1);
    int cy = std::clamp((int)(y * n), 0, n - 1);
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        int gx = cx + dx, gy = cy + dy;
        if (gx < 0 || gy < 0 || gx >= n || gy >= n) continue;
        int c = gy * n + gx;
        for (int s = start[(size_t)c]; s < start[(size_t)c + 1]; ++s) f(items[(size_t)s]);
      }
  }
};

} // namespace

void neighbour_counts(const PointCloud &pc, float radius, std::vector<int> &out) {
  const size_t m = pc.size();
  out.assign(m, 0);
  if (m == 0 || radius <= 0.f) return;
  Grid g;
  g.build(pc, radius);
  const float r2 = radius * radius;
  for (size_t i = 0; i < m; ++i) {
    int c = 0;
    g.each_near(pc.x[i], pc.y[i], [&](int j) {
      if ((size_t)j == i) return;
      float dx = pc.x[(size_t)j] - pc.x[i], dy = pc.y[(size_t)j] - pc.y[i];
      if (dx * dx + dy * dy <= r2) ++c;
    });
    out[i] = c;
  }
}

void nearest_distance(const PointCloud &pc, const PointCloud &other, float reach,
                      std::vector<float> &out) {
  const size_t m = pc.size();
  out.assign(m, reach);
  if (m == 0 || other.size() == 0 || reach <= 0.f) return;
  const bool rad = other.radius.size() == other.size();
  // the window must reach past the widest footprint, or a broad canopy
  // just outside it counts as farther than it is
  float rmax = 0.f;
  if (rad) for (float r : other.radius) rmax = std::max(rmax, r);
  Grid g;
  g.build(other, reach + rmax);
  for (size_t i = 0; i < m; ++i) {
    float best = reach;
    g.each_near(pc.x[i], pc.y[i], [&](int j) {
      float dx = other.x[(size_t)j] - pc.x[i], dy = other.y[(size_t)j] - pc.y[i];
      float d = std::sqrt(dx * dx + dy * dy);
      if (rad) d = std::max(d - other.radius[(size_t)j], 0.f);
      best = std::min(best, d);
    });
    out[i] = best;
  }
}

} // namespace gpx::scatter
