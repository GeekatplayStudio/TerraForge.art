// Geekatplay TerraForge — the terrain horizon, exactly and in linear time.
//
// At every cell, in a given compass direction, how high does the ground rise
// before the sky begins? Everything long-range about a landscape follows from
// that one number: which valley floors never see the sun, where a ridge keeps
// its snow, how open a place feels. SelectCavities answers a version of it
// over a few pixels; this answers it over the whole tile, which is where the
// ridge that actually shadows you usually is.
//
// Ray marching is the obvious way and the wrong one. At 1024 square with 16
// directions and 32 samples it is half a billion samples, it is still an
// approximation, and it misses precisely the thin ridge that casts the
// shadow. The convex-hull sweep (Stewart 1998) is exact and linear: sixteen
// passes over the grid, one comparison per cell amortised.
//
// Header-only and free of Node, so it can be tested against brute force
// without a graph (tests/cpp/test_horizon.cpp).
#pragma once
#include "heightmap.hpp"
#include "parallel.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

// ---------------------------------------------------------------- the sweep
//
// Along one line of cells, the horizon at cell i is the largest
// (z[j] - z[i]) / (t[j] - t[i]) over every j further along. Walking backwards
// and keeping the upper convex hull of the points ahead makes that a single
// comparison per cell, amortised:
//
//   the hull's tangent from i is the answer, and as i moves back the tangent
//   only ever moves further away — so a hull point popped for one i is never
//   wanted again.
//
// The push needs no separate convexity pass: after the tangent pops,
// slope(i, top) > slope(i, second), and slope(i, second) is a weighted mean
// of slope(i, top) and slope(top, second), so slope(i, top) > slope(top,
// second) already holds. That identity is why this is five lines and not
// twenty.
inline void sweep_line(const float *z, const float *t, int n, float *out,
                std::vector<int> &stack) {
  stack.clear();
  for (int i = n - 1; i >= 0; --i) {
    auto slope_to = [&](int j) { return (z[j] - z[i]) / (t[j] - t[i]); };
    while (stack.size() >= 2 &&
           slope_to(stack[stack.size() - 1]) <=
               slope_to(stack[stack.size() - 2]))
      stack.pop_back();
    out[i] = stack.empty() ? 0.f : std::max(0.f, slope_to(stack.back()));
    stack.push_back(i);
  }
}

// Every cell of the grid, walked once, in lines one cell apart along `dir`.
//
// With the dominant axis stepping by one and the cross axis by the rounded
// gradient, a cell's line index is exactly `cross - lround(m * along)`: each
// cell belongs to one line and only one, so the pass is a permutation of the
// grid rather than a resampling of it, and no cell is either missed or
// counted twice. That is also what makes it safe to run the lines in
// parallel — each writes a disjoint set of cells, so the result cannot depend
// on the thread count.
inline void horizon_pass(const Heightmap &z, float ux, float uy, Heightmap &out) {
  const int w = z.w, hgt = z.h;
  const bool x_major = std::fabs(ux) >= std::fabs(uy);
  // along: the dominant axis, walked in the ray's direction so index order is
  // distance order. cross: the other one.
  const int n_along = x_major ? w : hgt;
  const int n_cross = x_major ? hgt : w;
  const float major = x_major ? ux : uy;
  const float minor = x_major ? uy : ux;
  const float m = minor / major;          // |m| <= 1
  const int step = major >= 0.f ? 1 : -1; // walk in the ray's direction
  // one step along the dominant axis is 1/|major| along the ray
  const float dt = 1.f / std::fabs(major);

  std::vector<int> off(n_along);
  for (int a = 0; a < n_along; ++a) off[a] = (int)std::lround(m * a);
  const int off_lo = *std::min_element(off.begin(), off.end());
  const int off_hi = *std::max_element(off.begin(), off.end());
  const int b_lo = -off_hi, b_hi = n_cross - 1 - off_lo;

  parallel_rows(b_hi - b_lo + 1, [&](int i0, int i1) {
    std::vector<int> stack;
    std::vector<float> zs, ts, hs;
    for (int i = i0; i < i1; ++i) {
      const int b = b_lo + i;
      // the contiguous run of this line that lands on the grid
      int a_first = -1, a_last = -1;
      for (int a = 0; a < n_along; ++a) {
        const int c = b + off[a];
        if (c < 0 || c >= n_cross) continue;
        if (a_first < 0) a_first = a;
        a_last = a;
      }
      if (a_first < 0) continue;
      const int len = a_last - a_first + 1;
      zs.resize(len);
      ts.resize(len);
      hs.resize(len);
      for (int k = 0; k < len; ++k) {
        // index order must be distance order, so a backward ray reads the
        // run backwards
        const int a = step > 0 ? a_first + k : a_last - k;
        // Read where the ray actually is, not where the nearest cell is.
        // The two differ by up to half a cell, which is nothing at distance
        // and everything close up: the neighbour one step away is timed at
        // 1.12 cells and sampled at 1.41, and the horizon it reports comes
        // out 30% too steep. Bilinear here costs one lerp and makes the
        // measured angle isotropic - a round bowl reads the same from its
        // floor whichever way it looks, which it plainly should.
        const float cf = b + m * a;
        const int c0 = std::clamp((int)std::floor(cf), 0, n_cross - 1);
        const int c1 = std::clamp(c0 + 1, 0, n_cross - 1);
        const float f = std::clamp(cf - std::floor(cf), 0.f, 1.f);
        const float z0 = x_major ? z.at(a, c0) : z.at(c0, a);
        const float z1 = x_major ? z.at(a, c1) : z.at(c1, a);
        zs[k] = z0 + (z1 - z0) * f;
        ts[k] = k * dt;
      }
      sweep_line(zs.data(), ts.data(), len, hs.data(), stack);
      for (int k = 0; k < len; ++k) {
        const int a = step > 0 ? a_first + k : a_last - k;
        const int c = b + off[a];
        (x_major ? out.at(a, c) : out.at(c, a)) = hs[k];
      }
    }
  });
}

// The heightmap in cell units: heights rescaled so that the whole relief is
// `relief` times the tile's width. Without this the horizon angle would
// depend on the resolution, and a mask authored at 512 would be wrong at
// 2048 — which is exactly the bug that makes people distrust analysis nodes.
inline Heightmap in_cell_units(const Heightmap &in, float relief) {
  float mn, mx;
  in.minmax(mn, mx);
  const float span = (mx - mn) > 1e-12f ? mx - mn : 1.f;
  const float k = relief * (float)in.w / span;
  Heightmap z(in.w, in.h);
  parallel_index(z.v.size(), [&](size_t i0, size_t i1) {
    for (size_t i = i0; i < i1; ++i) z.v[i] = (in.v[i] - mn) * k;
  });
  return z;
}

} // namespace gpx
