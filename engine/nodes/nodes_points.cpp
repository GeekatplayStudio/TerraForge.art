// Geekatplay Studio — the point-cloud domain: scatter points over the tile,
// relax and filter them, and turn them back into rasters. The seed and the
// single-threaded generation make every cloud bit-identical across runs and
// thread counts; the stamp nodes are where the parallelism lives.
#include "gpx/node_graph.hpp"
#include "gpx/distance.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

// deterministic 0..1 stream: index + seed in, float out
static float pt_rand(uint32_t i, uint32_t k, uint32_t seed) {
  return (planet::pl_hash_bits((int)i, (int)k, 0, seed) & 0xffffffu) /
         16777215.f;
}

// nearest-sample read of an optional heightmap at tile coords
static float sample_hm(const Heightmap *hm, float x, float y) {
  if (!hm || hm->empty()) return 1.f;
  int ix = std::clamp((int)(x * hm->w), 0, hm->w - 1);
  int iy = std::clamp((int)(y * hm->h), 0, hm->h - 1);
  return hm->v[(size_t)iy * hm->w + ix];
}

REGISTER_NODE(
    ScatterPoints, "Points", "Scatter points over the tile",
    [](Node &n) {
      n.add_in("density", DataType::Heightmap, true);
      n.add_out("points", DataType::Points);
      add_int(n.attrs, "count", "Point count", 500, 1, 50000, "Scatter");
      add_choice(n.attrs, "mode", "Mode",
                 {"Random", "Jittered grid", "Spaced"}, 0, "Scatter");
      add_float(n.attrs, "min_dist", "Min spacing", 0.02f, 0.001f, 0.3f,
                "Scatter");
      add_seed(n.attrs, "seed", "Seed", 0, "Scatter");
    },
    [](Node &n) {
      PointCloud &out = n.out_points("points");
      const Heightmap *den = n.in_hmap("density");
      int count = n.attrs.get_i("count", 500);
      int mode = n.attrs.get_choice("mode");
      float md = n.attrs.get_f("min_dist", 0.02f);
      uint32_t seed = n.attrs.get_seed("seed");
      if (mode == 1) {
        // jittered grid: one candidate per cell, cells sized for the count
        int side = std::max(1, (int)std::ceil(std::sqrt((float)count)));
        float cell = 1.f / side;
        for (int gy = 0; gy < side && (int)out.size() < count; ++gy)
          for (int gx = 0; gx < side && (int)out.size() < count; ++gx) {
            uint32_t i = (uint32_t)(gy * side + gx);
            float px = (gx + pt_rand(i, 1, seed)) * cell;
            float py = (gy + pt_rand(i, 2, seed)) * cell;
            if (den && pt_rand(i, 3, seed) > sample_hm(den, px, py)) continue;
            out.add(px, py, pt_rand(i, 4, seed));
          }
      } else {
        // random / spaced: dart throwing; "Spaced" also rejects candidates
        // closer than min_dist to any accepted point (checked on a coarse
        // grid so the cost stays linear)
        const bool spaced = mode == 2;
        int gs = spaced ? std::max(1, (int)(1.f / std::max(md, 1e-3f))) : 1;
        std::vector<std::vector<int>> cells((size_t)gs * gs);
        uint32_t tries = 0, budget = (uint32_t)count * 30u;
        while ((int)out.size() < count && tries < budget) {
          uint32_t i = tries++;
          float px = pt_rand(i, 1, seed), py = pt_rand(i, 2, seed);
          if (den && pt_rand(i, 3, seed) > sample_hm(den, px, py)) continue;
          if (spaced) {
            int cx = std::min((int)(px * gs), gs - 1);
            int cy = std::min((int)(py * gs), gs - 1);
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; ++dy)
              for (int dx = -1; dx <= 1 && !near; ++dx) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= gs || ny >= gs) continue;
                for (int q : cells[(size_t)ny * gs + nx]) {
                  float ddx = out.x[q] - px, ddy = out.y[q] - py;
                  if (ddx * ddx + ddy * ddy < md * md) { near = true; break; }
                }
              }
            if (near) continue;
            cells[(size_t)cy * gs + cx].push_back((int)out.size());
          }
          out.add(px, py, pt_rand(i, 4, seed));
        }
      }
    })

REGISTER_NODE(
    PointsRelax, "Points", "Even out point spacing",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_out("points", DataType::Points);
      add_int(n.attrs, "iterations", "Iterations", 8, 1, 50, "Relax");
      add_float(n.attrs, "strength", "Strength", 0.5f, 0.01f, 1.f, "Relax");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      PointCloud &out = n.out_points("points");
      if (!in || in->size() == 0) return;
      out = *in;
      int iters = n.attrs.get_i("iterations", 8);
      float k = n.attrs.get_f("strength", 0.5f);
      // repulsion radius from mean density; neighbor search on a uniform grid
      float r = 1.2f / std::sqrt((float)out.size());
      int gs = std::max(1, (int)(1.f / r));
      for (int it = 0; it < iters; ++it) {
        std::vector<std::vector<int>> cells((size_t)gs * gs);
        for (size_t p = 0; p < out.size(); ++p) {
          int cx = std::clamp((int)(out.x[p] * gs), 0, gs - 1);
          int cy = std::clamp((int)(out.y[p] * gs), 0, gs - 1);
          cells[(size_t)cy * gs + cx].push_back((int)p);
        }
        std::vector<float> nx = out.x, ny = out.y;
        for (size_t p = 0; p < out.size(); ++p) {
          int cx = std::clamp((int)(out.x[p] * gs), 0, gs - 1);
          int cy = std::clamp((int)(out.y[p] * gs), 0, gs - 1);
          float fx = 0, fy = 0;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              int qx = cx + dx, qy = cy + dy;
              if (qx < 0 || qy < 0 || qx >= gs || qy >= gs) continue;
              for (int q : cells[(size_t)qy * gs + qx]) {
                if ((size_t)q == p) continue;
                float ddx = out.x[p] - out.x[q], ddy = out.y[p] - out.y[q];
                float d2 = ddx * ddx + ddy * ddy;
                if (d2 < 1e-12f || d2 > r * r) continue;
                float d = std::sqrt(d2);
                float push = (r - d) / r;
                fx += ddx / d * push;
                fy += ddy / d * push;
              }
            }
          nx[p] = std::clamp(out.x[p] + fx * k * r * 0.5f, 0.f, 1.f);
          ny[p] = std::clamp(out.y[p] + fy * k * r * 0.5f, 0.f, 1.f);
        }
        out.x.swap(nx);
        out.y.swap(ny);
      }
    })

REGISTER_NODE(
    PointsFilter, "Points", "Keep points by mask and chance",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("points", DataType::Points);
      add_range(n.attrs, "band", "Mask band", 0.5f, 1.f, 0.f, 1.f, "Filter");
      add_float(n.attrs, "keep", "Keep fraction", 1.f, 0.f, 1.f, "Filter");
      add_seed(n.attrs, "seed", "Seed", 0, "Filter");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      PointCloud &out = n.out_points("points");
      if (!in) return;
      const Heightmap *mask = n.in_hmap("mask");
      float lo, hi;
      n.attrs.get_range("band", lo, hi);
      float keep = n.attrs.get_f("keep", 1.f);
      uint32_t seed = n.attrs.get_seed("seed");
      for (size_t i = 0; i < in->size(); ++i) {
        if (mask) {
          float m = sample_hm(mask, in->x[i], in->y[i]);
          if (m < lo || m > hi) continue;
        }
        if (keep < 1.f && pt_rand((uint32_t)i, 7, seed) > keep) continue;
        out.add(in->x[i], in->y[i], in->v[i]);
      }
    })

REGISTER_NODE(
    PointsToMask, "Points", "Stamp points into a raster",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_out("mask", DataType::Heightmap);
      add_choice(n.attrs, "kernel", "Kernel", {"Gaussian", "Cone", "Disc"}, 0,
                 "Stamp");
      add_float(n.attrs, "radius", "Radius", 0.03f, 0.001f, 0.5f, "Stamp");
      add_float(n.attrs, "amplitude", "Amplitude", 1.f, 0.f, 4.f, "Stamp");
      add_bool(n.attrs, "scale_by_value", "Scale by point value", false, "Stamp");
      add_choice(n.attrs, "blend", "Blend", {"Max", "Add"}, 0, "Stamp");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      Heightmap &m = n.out_hmap("mask");
      if (!in) return;
      int kernel = n.attrs.get_choice("kernel");
      float radius = n.attrs.get_f("radius", 0.03f);
      float amp = n.attrs.get_f("amplitude", 1.f);
      bool by_val = n.attrs.get_b("scale_by_value");
      bool add_mode = n.attrs.get_choice("blend") == 1;
      int w = m.w, h = m.h;
      int rp = std::max(1, (int)(radius * w) + 1);
      // per-point stamps are serial per point (overlaps race in Add mode),
      // but each stamp's rows are independent — cheap enough serial
      for (size_t p = 0; p < in->size(); ++p) {
        float a = amp * (by_val ? in->v[p] : 1.f);
        int cx = (int)(in->x[p] * w), cy = (int)(in->y[p] * h);
        for (int dy = -rp; dy <= rp; ++dy) {
          int yy = cy + dy;
          if (yy < 0 || yy >= h) continue;
          for (int dx = -rp; dx <= rp; ++dx) {
            int xx = cx + dx;
            if (xx < 0 || xx >= w) continue;
            float d = std::sqrt((float)(dx * dx + dy * dy)) / (radius * w);
            if (d > 1.f) continue;
            float v;
            if (kernel == 0) v = std::exp(-d * d * 4.f);
            else if (kernel == 1) v = 1.f - d;
            else v = 1.f;
            v *= a;
            size_t idx = (size_t)yy * w + xx;
            m.v[idx] = add_mode ? m.v[idx] + v : std::max(m.v[idx], v);
          }
        }
      }
    })

REGISTER_NODE(
    PointsSDF, "Points", "Distance to the nearest point",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_out("distance", DataType::Heightmap);
      add_float(n.attrs, "reach", "Reach", 0.2f, 0.005f, 1.f, "Distance");
      add_bool(n.attrs, "invert", "Invert", false, "Distance");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      Heightmap &m = n.out_hmap("distance");
      if (!in) return;
      int w = m.w, h = m.h;
      // seed image: edt_squared treats cells > 0.5 as the shape, so mark the
      // point cells with 1 and it returns squared distance to the nearest
      std::vector<float> d2((size_t)w * h, 0.f);
      for (size_t p = 0; p < in->size(); ++p) {
        int ix = std::clamp((int)(in->x[p] * w), 0, w - 1);
        int iy = std::clamp((int)(in->y[p] * h), 0, h - 1);
        d2[(size_t)iy * w + ix] = 1.f;
      }
      edt_squared(d2, w, h);
      float reach = n.attrs.get_f("reach", 0.2f) * w;
      bool invert = n.attrs.get_b("invert");
      parallel_index(m.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = std::min(std::sqrt(d2[i]) / std::max(reach, 1.f), 1.f);
          m.v[i] = invert ? 1.f - t : t;
        }
      });
    })

REGISTER_NODE(
    PointsMerge, "Points", "Combine two point clouds",
    [](Node &n) {
      n.add_in("points A", DataType::Points);
      n.add_in("points B", DataType::Points, true);
      n.add_out("points", DataType::Points);
      add_float(n.attrs, "min_dist", "Drop B closer than", 0.f, 0.f, 0.2f,
                "Merge")
          .tooltip = "0 keeps everything. Above 0, a B point this close to\n"
                     "any A point is dropped - A has right of way.";
    },
    [](Node &n) {
      const PointCloud *a = n.in_points("points A");
      const PointCloud *b = n.in_points("points B");
      PointCloud &out = n.out_points("points");
      if (a) out = *a;
      if (!b) return;
      float md = n.attrs.get_f("min_dist", 0.f);
      for (size_t i = 0; i < b->size(); ++i) {
        bool keep = true;
        if (md > 0.f && a)
          for (size_t j = 0; j < a->size() && keep; ++j) {
            float dx = a->x[j] - b->x[i], dy = a->y[j] - b->y[i];
            keep = dx * dx + dy * dy >= md * md;
          }
        if (keep) out.add(b->x[i], b->y[i], b->v[i]);
      }
    })

REGISTER_NODE(
    PointsShuffle, "Points", "Reorder a cloud deterministically",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_out("points", DataType::Points);
      add_seed(n.attrs, "seed", "Seed", 0, "Shuffle");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      PointCloud &out = n.out_points("points");
      if (!in) return;
      out = *in;
      // Fisher-Yates over the hash stream: same seed, same order, always
      uint32_t seed = n.attrs.get_seed("seed");
      for (size_t i = out.size(); i > 1; --i) {
        size_t j = planet::pl_hash_bits((int)i, 3, 0, seed) % (uint32_t)i;
        std::swap(out.x[i - 1], out.x[j]);
        std::swap(out.y[i - 1], out.y[j]);
        std::swap(out.v[i - 1], out.v[j]);
      }
    })

REGISTER_NODE(
    PointsSetValues, "Points", "Point values from the terrain",
    [](Node &n) {
      n.add_in("points", DataType::Points);
      n.add_in("source", DataType::Heightmap);
      n.add_out("points", DataType::Points);
      add_bool(n.attrs, "normalize", "Normalize 0..1", true, "Values");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("points");
      const Heightmap *src = n.in_hmap("source");
      PointCloud &out = n.out_points("points");
      if (!in) return;
      out = *in;
      if (!src || src->empty()) return;
      for (size_t i = 0; i < out.size(); ++i)
        out.v[i] = sample_hm(src, out.x[i], out.y[i]);
      if (n.attrs.get_b("normalize", true) && out.size()) {
        float mn = out.v[0], mx = out.v[0];
        for (float v : out.v) { mn = std::min(mn, v); mx = std::max(mx, v); }
        float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
        for (float &v : out.v) v = (v - mn) / d;
      }
    })

} // namespace gpx
