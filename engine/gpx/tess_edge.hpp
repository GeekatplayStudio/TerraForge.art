// Geekatplay TerraForge — where the tessellator puts vertices along an edge.
//
// A crack is a hole, not a performance trade. The terrain's whole subdivision
// design rests on one invariant: two patches sharing an edge place their
// vertices in exactly the same places, so no gap can open between them.
// Today that holds for a reason that is easy to state and easy to lose — the
// level of an edge is computed from that edge's two endpoints and nothing
// else, so both patches compute the identical float.
//
// A quadtree breaks the premise. A coarse patch beside two finer ones does
// not share an edge with either of them; it shares half an edge with each.
// Whether any level assignment can still make the vertices line up is not a
// question to answer by writing the shader and looking at the screen, so it
// is answered here instead, against the spacing rules the hardware actually
// uses (tests/cpp/test_terrain_cracks.cpp).
//
// Header-only and free of GL: this is arithmetic, and it is the arithmetic
// the tessellation control shader in studio/shaders_terrain.cpp mirrors.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx::tess {

enum class Spacing {
  Equal,         // GL equal_spacing
  FractionalOdd, // GL fractional_odd_spacing — what the terrain uses
};

// How many segments the hardware will actually cut an edge into.
//
// equal_spacing rounds up. fractional_odd rounds up to the next *odd*
// integer, which is why a requested level of 8 becomes 9 segments — and why
// a patch at the floor of 8 emits 2*9*9 = 162 triangles, the number the GPU
// counter reports.
inline int segments(float level, Spacing sp, int max_level = 64) {
  const float t = std::clamp(level, 1.f, (float)max_level);
  if (sp == Spacing::Equal) return std::max(1, (int)std::ceil(t - 1e-6f));
  if (t <= 1.f) return 1;
  int n = 2 * (int)std::ceil((t - 1.f) * 0.5f - 1e-6f) + 1;
  return std::clamp(n, 1, max_level | 1);
}

// The parameter values, 0..1, at which vertices land along one edge.
//
// fractional_odd keeps `n-2` segments of equal length 1/t in the middle and
// splits what is left between two segments at the ends. That is why a level
// rising from 7 towards 9 grows two new vertices out of the corners rather
// than out of the middle, and why an exactly-odd level is uniform.
inline std::vector<float> edge_vertices(float level, Spacing sp,
                                        int max_level = 64) {
  const float t = std::clamp(level, 1.f, (float)max_level);
  const int n = segments(t, sp, max_level);
  std::vector<float> v;
  v.reserve((size_t)n + 1);
  if (sp == Spacing::Equal || n <= 1) {
    for (int i = 0; i <= n; ++i) v.push_back((float)i / (float)n);
    return v;
  }
  const float L = 1.f / t;              // the length of a full middle segment
  const float ends = (1.f - (n - 2) * L) * 0.5f; // what is left, split in two
  v.push_back(0.f);
  v.push_back(ends);
  for (int i = 1; i <= n - 2; ++i) v.push_back(ends + (float)i * L);
  v.push_back(1.f);
  // Floating point can leave the last full segment a hair past 1; the corner
  // is a corner and must land exactly on it or the test measures noise.
  for (float &x : v) x = std::clamp(x, 0.f, 1.f);
  std::sort(v.begin(), v.end());
  return v;
}

// The largest distance, in edge-length units, from a vertex one side places
// to the nearest vertex the other side placed. Zero means the two agree and
// no crack can open; anything else is the width of the hole.
//
// `b_offset` and `b_scale` map the second edge onto the first, so a coarse
// edge can be compared against one half of itself: a fine neighbour covering
// [0, 0.5] is (0.0, 0.5).
inline float boundary_gap(const std::vector<float> &a,
                          const std::vector<float> &b, float b_offset = 0.f,
                          float b_scale = 1.f) {
  float worst = 0.f;
  auto nearest = [](const std::vector<float> &set, float x) {
    float d = 1e30f;
    for (float y : set) d = std::min(d, std::fabs(y - x));
    return d;
  };
  std::vector<float> mapped;
  mapped.reserve(b.size());
  for (float y : b) mapped.push_back(b_offset + y * b_scale);
  // Only the span the two edges actually share is compared. A coarse edge
  // spans more than one fine neighbour, and its vertices outside that
  // neighbour's half are that neighbour's business, not this comparison's.
  const float lo = b_offset, hi = b_offset + b_scale;
  for (float x : a)
    if (x >= lo - 1e-6f && x <= hi + 1e-6f)
      worst = std::max(worst, nearest(mapped, x));
  for (float y : mapped) worst = std::max(worst, nearest(a, y));
  return worst;
}

// The screen-space rule the control shader uses, in one place so a test can
// ask the same question the GPU will. `px` is the edge's length in pixels.
//
// The floor tapers rather than being flat: a flat floor charges for relief on
// edges too small to show any, which was costing 81x the triangles the metric
// asked for at orbital range (docs/TERRAIN_PERFORMANCE.md).
inline float edge_level(float px, float target_px, float min_level,
                        float max_level) {
  const float floor_here = std::min(min_level, std::max(px * 0.5f, 1.f));
  return std::clamp(px / std::max(target_px, 1.f), floor_here, max_level);
}

} // namespace gpx::tess
