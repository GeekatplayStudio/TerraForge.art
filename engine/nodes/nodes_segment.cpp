// Geekatplay Studio - segmentation: k-means zoning and mean-shift mode
// seeking. Split from nodes_filters_local.cpp for the 500-line module
// rule.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

REGISTER_NODE(
    MeanShift, "Filter", "Mode-seeking smoothing (flattens toward plateaus)",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_int(n.attrs, "radius", "Radius (px)", 6, 2, 24, "MeanShift");
      add_float(n.attrs, "tolerance", "Value tolerance", 0.08f, 0.005f, 0.5f,
                "MeanShift");
      add_int(n.attrs, "iterations", "Iterations", 3, 1, 8, "MeanShift");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int r = n.attrs.get_i("radius", 6);
      float tol = n.attrs.get_f("tolerance", 0.08f);
      int iters = n.attrs.get_i("iterations", 3);
      int w = in->w, h = in->h;
      float mn, mx;
      in->minmax(mn, mx);
      float span = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      float bw = tol * span;
      // each pass shifts every cell toward the mean of the neighbors whose
      // value sits within the bandwidth - values drift to their local mode,
      // so gentle gradients collapse into plateaus while real jumps survive
      Heightmap cur = out;
      for (int it = 0; it < iters; ++it) {
        parallel_rows(h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
              float c = cur.at(x, y);
              float s = 0, wt = 0;
              for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                  int xx = x + dx, yy = y + dy;
                  if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                  float v = cur.at(xx, yy);
                  float dv = (v - c) / bw;
                  if (dv * dv > 1.f) continue;
                  float k = 1.f - dv * dv;
                  s += v * k;
                  wt += k;
                }
              out.at(x, y) = wt > 1e-12f ? s / wt : c;
            }
        });
        cur = out;
      }
    })

REGISTER_NODE(
    KMeans, "Mask", "Cluster the terrain into zones",
    [](Node &n) {
      n.add_in("input");
      n.add_in("feature B", DataType::Heightmap, true);
      n.add_out("clusters");
      n.add_out("mask A");
      n.add_out("mask B");
      n.add_out("mask C");
      add_int(n.attrs, "k", "Clusters", 4, 2, 8, "Clustering");
      add_float(n.attrs, "slope_weight", "Slope weight", 1.f, 0.f, 4.f,
                "Clustering");
      add_seed(n.attrs, "seed", "Seed", 0, "Clustering");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &cl = n.out_hmap("clusters");
      Heightmap &ma = n.out_hmap("mask A");
      Heightmap &mb = n.out_hmap("mask B");
      Heightmap &mc = n.out_hmap("mask C");
      cl = *in;
      ma = *in;
      mb = *in;
      mc = *in;
      int w = in->w, h = in->h;
      int k = n.attrs.get_i("k", 4);
      float sw = n.attrs.get_f("slope_weight", 1.f);
      const Heightmap *fb = n.in_hmap("feature B");
      // features per cell: height, slope magnitude (or the second input)
      std::vector<float> f1(in->v.size()), f2(in->v.size());
      float mn, mx;
      in->minmax(mn, mx);
      float span = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            size_t i = (size_t)y * w + x;
            f1[i] = (in->v[i] - mn) / span;
            if (fb && i < fb->v.size()) {
              f2[i] = fb->v[i] * sw;
            } else {
              float gx = in->atc(x + 1, y) - in->atc(x - 1, y);
              float gy = in->atc(x, y + 1) - in->atc(x, y - 1);
              f2[i] = std::sqrt(gx * gx + gy * gy) / span * (float)w * 0.05f *
                      sw;
            }
          }
      });
      // seeded init: centers drawn from hashed cell indices, then a fixed 12
      // Lloyd iterations - the same inputs always land the same clustering
      uint32_t seed = n.attrs.get_seed("seed");
      std::vector<float> c1(k), c2(k);
      for (int c = 0; c < k; ++c) {
        size_t i = (size_t)(planet::pl_hash_bits(c, 17, 0, seed) %
                            (uint32_t)in->v.size());
        c1[c] = f1[i];
        c2[c] = f2[i];
      }
      std::vector<int> assign(in->v.size(), 0);
      for (int it = 0; it < 12; ++it) {
        parallel_index(in->v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            float bd = 1e30f;
            int bc = 0;
            for (int c = 0; c < k; ++c) {
              float dx = f1[i] - c1[c], dy = f2[i] - c2[c];
              float d = dx * dx + dy * dy;
              if (d < bd) { bd = d; bc = c; }
            }
            assign[i] = bc;
          }
        });
        std::vector<double> s1(k, 0), s2(k, 0);
        std::vector<size_t> cnt(k, 0);
        for (size_t i = 0; i < in->v.size(); ++i) {
          s1[assign[i]] += f1[i];
          s2[assign[i]] += f2[i];
          ++cnt[assign[i]];
        }
        for (int c = 0; c < k; ++c)
          if (cnt[c]) {
            c1[c] = (float)(s1[c] / cnt[c]);
            c2[c] = (float)(s2[c] / cnt[c]);
          }
      }
      // stable output: relabel clusters by ascending mean height, so "mask A"
      // is always the lowest zone whatever the seed drew
      std::vector<int> order(k);
      for (int c = 0; c < k; ++c) order[c] = c;
      std::sort(order.begin(), order.end(),
                [&](int a, int b) { return c1[a] < c1[b]; });
      std::vector<int> rank(k);
      for (int r = 0; r < k; ++r) rank[order[r]] = r;
      for (size_t i = 0; i < in->v.size(); ++i) {
        int r = rank[assign[i]];
        cl.v[i] = k > 1 ? (float)r / (k - 1) : 0.f;
        ma.v[i] = r == 0 ? 1.f : 0.f;
        mb.v[i] = r == 1 ? 1.f : 0.f;
        mc.v[i] = r == 2 ? 1.f : 0.f;
      }
    })

} // namespace gpx
