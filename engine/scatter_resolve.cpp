// Geekatplay TerraForge - scatter stage 3: how a layer's candidates react
// to the instances of the layer below, and to each other.
//
// Vue (p1097-1098): affinity is gentle - a positive value keeps the
// primroses close to the trees and thins them elsewhere, a negative one
// puts them everywhere except near the trees. Repulsion is sudden - a
// positive value opens a void around each tree, a negative one allows
// instances only inside that void. Both at once: grass near the trees but
// not under them. Overlap avoidance then keeps two instances from sharing
// the ground their radii claim, in id order so the outcome never depends
// on the order the candidates arrived in.
#include "gpx/scatter.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace gpx::scatter {

void interact(PointCloud &pc, const Interaction &it) {
  const size_t m = pc.size();
  if (m == 0) return;
  pc.ensure_attrs();

  // ---- affinity and repulsion against the layer below --------------------
  if (it.below && it.below->size() && (it.affinity != 0.f || it.repulsion != 0.f)) {
    const float ra = std::max(it.affinity_radius, 1e-4f);
    const float rr = std::max(it.repulsion_radius, 1e-4f);
    const float reach = std::max(ra, rr);
    std::vector<float> dist;
    nearest_distance(pc, *it.below, reach, dist);
    std::vector<uint8_t> keep(m, 1);
    for (size_t i = 0; i < m; ++i) {
      const float d = dist[i];
      float f = 1.f;
      if (it.affinity != 0.f) {
        // 1 right at the neighbour, 0 at the radius and beyond
        float w = 1.f - std::clamp(d / ra, 0.f, 1.f);
        w = w * w * (3.f - 2.f * w);
        const float a = std::clamp(it.affinity, -1.f, 1.f);
        f *= a > 0.f ? (1.f - a) + a * w : 1.f + a * w;
      }
      if (it.repulsion != 0.f) {
        const float r = std::clamp(it.repulsion, -1.f, 1.f);
        const float zone = rr * std::fabs(r);
        const float edge = std::max(zone * 0.2f, 1e-5f);
        if (r > 0.f) f *= std::clamp((d - zone) / edge, 0.f, 1.f);
        else f *= std::clamp((zone - d) / edge, 0.f, 1.f);
      }
      if (f <= 0.f || unit(pc.id[i], 10) >= f) keep[i] = 0;
    }
    pc.compact(keep);
  }

  // ---- overlap avoidance ---------------------------------------------------
  if (it.avoid_overlap && pc.size() > 1) {
    const size_t n = pc.size();
    float rmax = 0.f;
    for (size_t i = 0; i < n; ++i) rmax = std::max(rmax, pc.radius[i] * it.overlap_scale);
    if (rmax <= 0.f) return;
    // walk by id: the same set of candidates resolves the same way whatever
    // order a producer emitted them in
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(),
                     [&](uint32_t a, uint32_t b) { return pc.id[a] < pc.id[b]; });
    const float cell = std::max(2.f * rmax, 1.f / 1024.f);
    const int g = std::clamp((int)(1.f / cell), 1, 1024);
    std::vector<std::vector<uint32_t>> cells((size_t)g * g);
    std::vector<uint8_t> keep(n, 0);
    for (uint32_t i : order) {
      const float ri = pc.radius[i] * it.overlap_scale;
      const int cx = std::clamp((int)(pc.x[i] * g), 0, g - 1);
      const int cy = std::clamp((int)(pc.y[i] * g), 0, g - 1);
      bool clash = false;
      for (int dy = -1; dy <= 1 && !clash; ++dy)
        for (int dx = -1; dx <= 1 && !clash; ++dx) {
          int gx = cx + dx, gy = cy + dy;
          if (gx < 0 || gy < 0 || gx >= g || gy >= g) continue;
          for (uint32_t j : cells[(size_t)gy * g + gx]) {
            float ddx = pc.x[j] - pc.x[i], ddy = pc.y[j] - pc.y[i];
            float lim = ri + pc.radius[j] * it.overlap_scale;
            if (ddx * ddx + ddy * ddy < lim * lim) { clash = true; break; }
          }
        }
      if (clash) continue;
      keep[i] = 1;
      cells[(size_t)cy * g + cx].push_back(i);
    }
    pc.compact(keep);
  }
}

} // namespace gpx::scatter
