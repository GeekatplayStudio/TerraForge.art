// Geekatplay Studio — grayscale morphology: dilate/erode and the operators
// composed from them, connected-blob pruning, and binary skeletonization.
// The separable min/max passes are exact for the square element; rows and
// columns are each independent, so parallelism cannot change the result.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

// sliding min/max over a 2r+1 window along rows, then columns (square SE)
static void morph_pass(std::vector<float> &v, int w, int h, int r, bool is_max) {
  auto line = [&](const float *src, float *dst, int n, int stride) {
    for (int i = 0; i < n; ++i) {
      int a = std::max(0, i - r), b = std::min(n - 1, i + r);
      float m = src[(size_t)a * stride];
      for (int j = a + 1; j <= b; ++j) {
        float s = src[(size_t)j * stride];
        m = is_max ? std::max(m, s) : std::min(m, s);
      }
      dst[(size_t)i * stride] = m;
    }
  };
  std::vector<float> tmp(v.size());
  parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      line(v.data() + (size_t)y * w, tmp.data() + (size_t)y * w, w, 1);
  });
  parallel_rows(w, [&](int x0, int x1) {
    for (int x = x0; x < x1; ++x) line(tmp.data() + x, v.data() + x, h, w);
  });
}

// one 3x3 min/max step; cross-shaped or full 8-neighborhood
static void morph_step3(std::vector<float> &v, int w, int h, bool is_max,
                        bool cross) {
  std::vector<float> src = v;
  parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        float m = src[(size_t)y * w + x];
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            if (cross && dx != 0 && dy != 0) continue;
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
            float s = src[(size_t)yy * w + xx];
            m = is_max ? std::max(m, s) : std::min(m, s);
          }
        v[(size_t)y * w + x] = m;
      }
  });
}

static void dilate(std::vector<float> &v, int w, int h, int r, int shape) {
  if (shape == 0) {
    morph_pass(v, w, h, r, true);
  } else {
    // alternating cross/square steps grow an octagon — the usual disc stand-in
    for (int i = 0; i < r; ++i) morph_step3(v, w, h, true, (i & 1) == 0);
  }
}

static void erode(std::vector<float> &v, int w, int h, int r, int shape) {
  if (shape == 0) {
    morph_pass(v, w, h, r, false);
  } else {
    for (int i = 0; i < r; ++i) morph_step3(v, w, h, false, (i & 1) == 0);
  }
}

REGISTER_NODE(
    Morphology, "Filter", "Dilate, erode and their compositions",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_choice(n.attrs, "op", "Operation",
                 {"Dilate", "Erode", "Open", "Close", "Gradient", "Top hat",
                  "Black hat"},
                 0, "Morphology");
      add_int(n.attrs, "radius", "Radius (px)", 3, 1, 64, "Morphology");
      add_choice(n.attrs, "shape", "Element", {"Square", "Octagon"}, 1,
                 "Morphology");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      int op = n.attrs.get_choice("op");
      int r = n.attrs.get_i("radius", 3);
      int shape = n.attrs.get_choice("shape");
      auto &v = out.v;
      int w = out.w, h = out.h;
      switch (op) {
      case 0: dilate(v, w, h, r, shape); break;
      case 1: erode(v, w, h, r, shape); break;
      case 2: // open: erode then dilate — shaves peaks thinner than the element
        erode(v, w, h, r, shape);
        dilate(v, w, h, r, shape);
        break;
      case 3: // close: dilate then erode — fills pits thinner than the element
        dilate(v, w, h, r, shape);
        erode(v, w, h, r, shape);
        break;
      case 4: { // gradient: dilation minus erosion — an edge band
        std::vector<float> lo = v;
        dilate(v, w, h, r, shape);
        erode(lo, w, h, r, shape);
        for (size_t i = 0; i < v.size(); ++i) v[i] -= lo[i];
        break;
      }
      case 5: { // top hat: input minus opening — just the shaved peaks
        std::vector<float> o = v;
        erode(o, w, h, r, shape);
        dilate(o, w, h, r, shape);
        for (size_t i = 0; i < v.size(); ++i) v[i] = in->v[i] - o[i];
        break;
      }
      case 6: { // black hat: closing minus input — just the filled pits
        std::vector<float> c = v;
        dilate(c, w, h, r, shape);
        erode(c, w, h, r, shape);
        for (size_t i = 0; i < v.size(); ++i) v[i] = c[i] - in->v[i];
        break;
      }
      }
    })

REGISTER_NODE(
    AreaRemove, "Mask", "Drop small connected blobs",
    [](Node &n) {
      n.add_in("input");
      n.add_out("mask");
      add_float(n.attrs, "threshold", "Threshold", 0.5f, 0.f, 1.f, "Blobs");
      add_float(n.attrs, "min_area", "Min area (fraction)", 0.001f, 0.f, 0.5f,
                "Blobs");
      add_bool(n.attrs, "invert", "Invert", false, "Blobs");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      m = *in;
      float thr = n.attrs.get_f("threshold", 0.5f);
      int w = m.w, h = m.h;
      size_t min_cells =
          (size_t)(n.attrs.get_f("min_area", 0.001f) * (float)w * (float)h);
      // 4-connected labelling with an explicit stack — single-threaded so the
      // label order (and thus the result) never depends on scheduling
      std::vector<int> label((size_t)w * h, -1);
      std::vector<size_t> area;
      std::vector<size_t> stack;
      for (size_t s = 0; s < label.size(); ++s) {
        if (label[s] >= 0 || in->v[s] < thr) continue;
        int id = (int)area.size();
        area.push_back(0);
        stack.push_back(s);
        label[s] = id;
        while (!stack.empty()) {
          size_t c = stack.back();
          stack.pop_back();
          ++area[id];
          int cx = (int)(c % w), cy = (int)(c / w);
          const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          for (auto &d : nb) {
            int nx = cx + d[0], ny = cy + d[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            size_t q = (size_t)ny * w + nx;
            if (label[q] >= 0 || in->v[q] < thr) continue;
            label[q] = id;
            stack.push_back(q);
          }
        }
      }
      bool invert = n.attrs.get_b("invert");
      for (size_t i = 0; i < m.v.size(); ++i) {
        bool keep = label[i] >= 0 && area[label[i]] >= min_cells;
        m.v[i] = keep != invert ? 1.f : 0.f;
      }
    })

REGISTER_NODE(
    Skeleton, "Mask", "Thin a mask to its centerlines",
    [](Node &n) {
      n.add_in("input");
      n.add_out("mask");
      add_float(n.attrs, "threshold", "Threshold", 0.5f, 0.f, 1.f, "Skeleton");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &m = n.out_hmap("mask");
      m = *in;
      float thr = n.attrs.get_f("threshold", 0.5f);
      int w = m.w, h = m.h;
      std::vector<unsigned char> a((size_t)w * h);
      for (size_t i = 0; i < a.size(); ++i) a[i] = in->v[i] >= thr ? 1 : 0;
      // Zhang-Suen thinning: two alternating sub-iterations until stable.
      // The pass structure is fixed, so the result is deterministic.
      auto at = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return a[(size_t)y * w + x];
      };
      bool changed = true;
      int guard = 0;
      while (changed && guard++ < 4096) {
        changed = false;
        for (int phase = 0; phase < 2; ++phase) {
          std::vector<size_t> kill;
          for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
              if (!at(x, y)) continue;
              int p2 = at(x, y - 1), p3 = at(x + 1, y - 1), p4 = at(x + 1, y);
              int p5 = at(x + 1, y + 1), p6 = at(x, y + 1), p7 = at(x - 1, y + 1);
              int p8 = at(x - 1, y), p9 = at(x - 1, y - 1);
              int bsum = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
              if (bsum < 2 || bsum > 6) continue;
              int seq[9] = {p2, p3, p4, p5, p6, p7, p8, p9, p2};
              int trans = 0;
              for (int k = 0; k < 8; ++k)
                if (seq[k] == 0 && seq[k + 1] == 1) ++trans;
              if (trans != 1) continue;
              if (phase == 0) {
                if (p2 * p4 * p6 != 0 || p4 * p6 * p8 != 0) continue;
              } else {
                if (p2 * p4 * p8 != 0 || p2 * p6 * p8 != 0) continue;
              }
              kill.push_back((size_t)y * w + x);
            }
          for (size_t i : kill) a[i] = 0;
          if (!kill.empty()) changed = true;
        }
      }
      for (size_t i = 0; i < m.v.size(); ++i) m.v[i] = a[i] ? 1.f : 0.f;
    })

} // namespace gpx
