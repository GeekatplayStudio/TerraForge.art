// Geekatplay TerraForge — a picture of a point cloud.
//
// A cloud is the one thing in the graph that cannot be judged from its
// parameters. Whether a scatter clumps, leaves bald patches or covers the
// ground evenly is a picture, not a number, and the nodes that make them
// (ScatterPoints, PointsRelax, the path operators) used to show nothing at
// all: the card carried the name and the sliders, and the result had to be
// imagined.
//
// This lives in the engine rather than the studio because it is a pure
// function of the cloud — no GL, no App — which is what makes it testable
// headlessly (tests/cpp/test_points_thumb.cpp) and reusable by anything that
// wants to draw a cloud small.
#pragma once
#include "points.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gpx {

// Points accumulate rather than overwrite, so a cluster reads brighter than a
// lone point: that is the whole question these nodes are asked. `ordered`
// draws the cloud as the polyline it is, for a path.
inline std::vector<uint8_t> points_thumbnail(const PointCloud &pc, int w,
                                             bool ordered) {
  std::vector<float> acc((size_t)w * w, 0.f);
  auto splat = [&](float fx, float fy, float weight) {
    if (!std::isfinite(fx) || !std::isfinite(fy)) return;
    // Bilinear, so a point moving half a pixel changes the picture by half a
    // pixel instead of jumping. At 112 px a scatter is mostly sub-pixel
    // motion, and nearest-neighbour would make relaxation look like noise.
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    for (int dy = 0; dy < 2; ++dy)
      for (int dx = 0; dx < 2; ++dx) {
        int x = x0 + dx, y = y0 + dy;
        if (x < 0 || y < 0 || x >= w || y >= w) continue;
        acc[(size_t)y * w + x] +=
            weight * (dx ? tx : 1.f - tx) * (dy ? ty : 1.f - ty);
      }
  };
  // Deliberately not clamped. A cloud arrives from PointsTransform or a CSV
  // and neither promises 0..1, and a point outside the tile is outside the
  // picture — clamping would pile it onto the edge and paint a bright rim
  // that is not there, which is exactly the artefact that makes someone
  // believe a transform worked when it pushed the whole cloud off the tile.
  auto to_px = [&](float t) { return (double)t * (w - 1); };

  if (ordered) {
    // A path is the line between its points, not the points. Each segment is
    // walked at one sample per pixel so a long leg is no brighter than a
    // short one — the picture is the route, not how finely it was sampled.
    for (size_t i = 1; i < pc.size(); ++i) {
      // Clipped in double, and this is not fastidiousness. A leg running to
      // 1e6 in tile units is 1e8 pixels long, and the parameter where it
      // crosses the canvas is 0.5 plus a few billionths - a difference float
      // cannot hold, so the clipped segment collapses to a single point and
      // the line vanishes. It is a hundred points of double arithmetic per
      // path, against a preview that silently draws nothing.
      double ax = to_px(pc.x[i - 1]), ay = to_px(pc.y[i - 1]);
      double bx = to_px(pc.x[i]), by = to_px(pc.y[i]);
      if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) ||
          !std::isfinite(by))
        continue;
      // Clip before walking, not while walking: stepping 1e8 pixels one at a
      // time would hang the preview, and a bad CSV is all it takes.
      double t0 = 0.0, t1 = 1.0, dx = bx - ax, dy = by - ay;
      const double lo = -1.0, hi = (double)w;
      const double p[4] = {-dx, dx, -dy, dy};
      const double q[4] = {ax - lo, hi - ax, ay - lo, hi - ay};
      bool visible = true;
      for (int k = 0; k < 4 && visible; ++k) {
        if (std::fabs(p[k]) < 1e-12) {
          if (q[k] < 0.0) visible = false; // parallel to this edge and outside
        } else {
          double r = q[k] / p[k];
          if (p[k] < 0.0) t0 = std::max(t0, r);
          else t1 = std::min(t1, r);
          if (t0 > t1) visible = false;
        }
      }
      if (!visible) continue;
      double cax = ax + dx * t0, cay = ay + dy * t0;
      double cbx = ax + dx * t1, cby = ay + dy * t1;
      int steps = (int)std::max(std::fabs(cbx - cax), std::fabs(cby - cay)) + 1;
      for (int s = 0; s <= steps; ++s) {
        double t = (double)s / (double)steps;
        splat((float)(cax + (cbx - cax) * t), (float)(cay + (cby - cay) * t),
              1.f);
      }
    }
  } else {
    for (size_t i = 0; i < pc.size(); ++i)
      splat((float)to_px(pc.x[i]), (float)to_px(pc.y[i]), 1.f);
  }

  float mx = 0.f;
  for (float v : acc) mx = std::max(mx, v);
  if (mx <= 0.f) mx = 1.f;
  // A square root rather than a straight scale: one point in a pixel has to
  // be plainly visible beside a pixel holding twenty, and a linear ramp makes
  // the sparse half of a clustered scatter disappear into the background —
  // which is the half you were looking at it to see.
  std::vector<uint8_t> rgba((size_t)w * w * 4);
  for (size_t i = 0; i < acc.size(); ++i) {
    float t = std::sqrt(std::min(acc[i] / mx, 1.f));
    rgba[i * 4 + 0] = (uint8_t)(26.f + t * 229.f);
    rgba[i * 4 + 1] = (uint8_t)(28.f + t * 148.f);
    rgba[i * 4 + 2] = (uint8_t)(32.f + t * 51.f);
    rgba[i * 4 + 3] = 255;
  }
  return rgba;
}

} // namespace gpx
