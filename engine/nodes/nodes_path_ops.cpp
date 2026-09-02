// Geekatplay Studio - path point-operations: threading a scatter into a
// tour, even resampling, and fractal wander. Split from nodes_path.cpp
// for the 500-line module rule; the raster path nodes stay there.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

namespace {

struct P2 {
  float x, z;
};


// Chaikin corner cutting: each pass replaces every corner with two points a
// quarter of the way along its edges. A few passes turn a polyline into a
// smooth curve that still follows the drawn points.
std::vector<P2> chaikin(std::vector<P2> pts, int passes, bool closed) {
  for (int p = 0; p < passes && pts.size() >= 3; ++p) {
    std::vector<P2> out;
    out.reserve(pts.size() * 2);
    if (!closed) out.push_back(pts.front());
    const size_t n = pts.size();
    const size_t last = closed ? n : n - 1;
    for (size_t i = 0; i < last; ++i) {
      const P2 &a = pts[i], &b = pts[(i + 1) % n];
      out.push_back({a.x * 0.75f + b.x * 0.25f, a.z * 0.75f + b.z * 0.25f});
      out.push_back({a.x * 0.25f + b.x * 0.75f, a.z * 0.25f + b.z * 0.75f});
    }
    if (!closed) out.push_back(pts.back());
    pts = std::move(out);
  }
  return pts;
}

} // namespace

REGISTER_NODE(
    PointsToPath, "Path", "Order a point cloud into a path",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_out("path", DataType::Points);
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      PointCloud &out = n.out_points("path");
      if (!in || in->size() == 0) return;
      // greedy nearest-neighbor tour from the point nearest the origin -
      // fixed start and tie-breaking by index keep it deterministic
      size_t nn = in->size();
      std::vector<char> used(nn, 0);
      size_t cur = 0;
      float best = 1e30f;
      for (size_t i = 0; i < nn; ++i) {
        float d = in->x[i] * in->x[i] + in->y[i] * in->y[i];
        if (d < best) { best = d; cur = i; }
      }
      for (size_t step = 0; step < nn; ++step) {
        used[cur] = 1;
        out.add(in->x[cur], in->y[cur], in->v[cur]);
        size_t nxt = cur;
        float bd = 1e30f;
        for (size_t i = 0; i < nn; ++i) {
          if (used[i]) continue;
          float dx = in->x[i] - in->x[cur], dy = in->y[i] - in->y[cur];
          float d = dx * dx + dy * dy;
          if (d < bd) { bd = d; nxt = i; }
        }
        if (nxt == cur) break;
        cur = nxt;
      }
    })

REGISTER_NODE(
    PathResample, "Path", "Even spacing along a path",
    [](Node &n) {
      n.add_in("path", DataType::Points);
      n.add_out("path", DataType::Points);
      add_float(n.attrs, "spacing", "Spacing", 0.02f, 0.002f, 0.5f, "Path");
      add_int(n.attrs, "smooth", "Smoothing", 0, 0, 6, "Path");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("path");
      PointCloud &out = n.out_points("path");
      if (!in || in->size() < 2) return;
      std::vector<P2> pts;
      for (size_t i = 0; i < in->size(); ++i)
        pts.push_back({in->x[i], in->y[i]});
      pts = chaikin(std::move(pts), n.attrs.get_i("smooth", 0), false);
      float spacing = n.attrs.get_f("spacing", 0.02f);
      out.add(pts[0].x, pts[0].z, in->v[0]);
      float carry = 0;
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        float dx = pts[i + 1].x - pts[i].x, dz = pts[i + 1].z - pts[i].z;
        float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-12f) continue;
        float t = spacing - carry;
        while (t <= len) {
          out.add(pts[i].x + dx * (t / len), pts[i].z + dz * (t / len), 0.f);
          t += spacing;
        }
        carry = len - (t - spacing);
      }
      if (out.size() < 2) out.add(pts.back().x, pts.back().z, 0.f);
    })

REGISTER_NODE(
    PathFractalize, "Path", "Midpoint-displace a path into a wander",
    [](Node &n) {
      n.add_in("path", DataType::Points);
      n.add_out("path", DataType::Points);
      add_int(n.attrs, "iterations", "Iterations", 4, 1, 8, "Fractal");
      add_float(n.attrs, "amplitude", "Amplitude", 0.4f, 0.f, 1.f, "Fractal");
      add_seed(n.attrs, "seed", "Seed", 0, "Fractal");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("path");
      PointCloud &out = n.out_points("path");
      if (!in || in->size() < 2) return;
      std::vector<P2> pts;
      for (size_t i = 0; i < in->size(); ++i)
        pts.push_back({in->x[i], in->y[i]});
      int iters = n.attrs.get_i("iterations", 4);
      float amp = n.attrs.get_f("amplitude", 0.4f);
      uint32_t seed = n.attrs.get_seed("seed");
      uint32_t idx = 0;
      for (int it = 0; it < iters; ++it) {
        std::vector<P2> next;
        next.reserve(pts.size() * 2);
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
          const P2 &a = pts[i], &b = pts[i + 1];
          next.push_back(a);
          float dx = b.x - a.x, dz = b.z - a.z;
          float len = std::sqrt(dx * dx + dz * dz);
          // perpendicular offset scaled by the segment length, so detail
          // shrinks with each subdivision the way coastlines do
          float r = (planet::pl_hash_bits((int)idx++, it, 0, seed) & 0xffffu) /
                        65535.f -
                    0.5f;
          float off = r * amp * len;
          next.push_back({(a.x + b.x) * 0.5f - dz / std::max(len, 1e-9f) * off,
                          (a.z + b.z) * 0.5f + dx / std::max(len, 1e-9f) * off});
        }
        next.push_back(pts.back());
        pts = std::move(next);
      }
      for (const P2 &p : pts)
        out.add(std::clamp(p.x, 0.f, 1.f), std::clamp(p.z, 0.f, 1.f), 0.f);
    })

REGISTER_NODE(
    PathSpline, "Path", "A smooth curve through the points",
    [](Node &n) {
      n.add_in("path", DataType::Points);
      n.add_out("path", DataType::Points);
      add_int(n.attrs, "samples", "Samples per segment", 8, 2, 64, "Spline");
      add_float(n.attrs, "tension", "Tension", 0.5f, 0.f, 1.f, "Spline");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("path");
      PointCloud &out = n.out_points("path");
      if (!in || in->size() < 2) return;
      // Catmull-Rom: unlike Chaikin's corner cutting, the curve passes
      // exactly through every control point; tension 0.5 is the classic
      // centripetal-feeling default, 0 goes straight, 1 overshoots
      int samples = n.attrs.get_i("samples", 8);
      float tens = n.attrs.get_f("tension", 0.5f);
      size_t nn = in->size();
      auto px = [&](int i) {
        return in->x[(size_t)std::clamp(i, 0, (int)nn - 1)];
      };
      auto py = [&](int i) {
        return in->y[(size_t)std::clamp(i, 0, (int)nn - 1)];
      };
      for (int seg = 0; seg + 1 < (int)nn; ++seg) {
        for (int s = 0; s < samples; ++s) {
          float t = (float)s / samples;
          float t2 = t * t, t3 = t2 * t;
          // tangents from the neighbors, scaled by the tension
          float m1x = tens * (px(seg + 1) - px(seg - 1));
          float m1y = tens * (py(seg + 1) - py(seg - 1));
          float m2x = tens * (px(seg + 2) - px(seg));
          float m2y = tens * (py(seg + 2) - py(seg));
          float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
          float h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
          out.add(std::clamp(h00 * px(seg) + h10 * m1x + h01 * px(seg + 1) +
                                 h11 * m2x,
                             0.f, 1.f),
                  std::clamp(h00 * py(seg) + h10 * m1y + h01 * py(seg + 1) +
                                 h11 * m2y,
                             0.f, 1.f),
                  0.f);
        }
      }
      out.add(in->x[nn - 1], in->y[nn - 1], in->v[nn - 1]);
    })

} // namespace gpx
