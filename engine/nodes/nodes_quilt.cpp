// Geekatplay Studio — texture quilting (Efros-Freeman): rebuild a larger or
// same-size surface from patches of an exemplar, each patch chosen for how
// well its overlap matches what is already laid down, joined along the
// minimum-error seam. Candidate offsets come from one hashed stream, so the
// same seed always quilts the same surface.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

REGISTER_NODE(
    Quilt, "Transform", "Resynthesize the surface from its own patches",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_seed(n.attrs);
      add_float(n.attrs, "patch", "Patch size", 0.12f, 0.03f, 0.4f, "Quilt");
      add_float(n.attrs, "overlap", "Overlap", 0.25f, 0.1f, 0.5f, "Quilt")
          .tooltip = "As a fraction of the patch. Wider overlaps hide seams\n"
                     "better and repeat more.";
      add_int(n.attrs, "candidates", "Candidates", 24, 4, 128, "Quilt")
          .tooltip = "Patches auditioned per cell; the best-matching overlap\n"
                     "wins. More candidates, better joins, slower quilt.";
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      const int w = out.w, h = out.h;
      const int ps = std::clamp((int)(n.attrs.get_f("patch", 0.12f) * w), 8,
                                std::min(w, h) / 2);
      const int ov = std::clamp((int)(ps * n.attrs.get_f("overlap", 0.25f)), 2,
                                ps / 2);
      const int cand = n.attrs.get_i("candidates", 24);
      const uint32_t seed = n.attrs.get_seed("seed");
      const int step = ps - ov;
      const int maxx = in->w - ps - 1, maxy = in->h - ps - 1;
      if (maxx < 1 || maxy < 1) return;
      std::vector<char> laid((size_t)w * h, 0);
      uint32_t ctr = 0;
      auto pick = [&](uint32_t k) {
        return planet::pl_hash_bits((int)(ctr++), (int)k, 0, seed);
      };
      // patches march row by row; each is serial on the previous ones, which
      // is the algorithm - candidate scoring inside a patch is where the
      // cycles go and stays cheap enough at these sizes
      for (int by = 0; by < h; by += step)
        for (int bx = 0; bx < w; bx += step) {
          int pw = std::min(ps, w - bx), ph = std::min(ps, h - by);
          // audition candidates: sum of squared differences over the cells
          // already laid inside this patch's footprint
          int best_sx = 0, best_sy = 0;
          float best = 1e30f;
          for (int c = 0; c < cand; ++c) {
            int sx = (int)(pick(1) % (uint32_t)maxx);
            int sy = (int)(pick(2) % (uint32_t)maxy);
            float err = 0;
            int counted = 0;
            for (int y = 0; y < ph; y += 2)
              for (int x = 0; x < pw; x += 2) {
                size_t di = (size_t)(by + y) * w + (bx + x);
                if (!laid[di]) continue;
                float d = out.v[di] - in->v[(size_t)(sy + y) * in->w + (sx + x)];
                err += d * d;
                ++counted;
              }
            if (counted) err /= counted;
            // an unconstrained first patch: any candidate is equally right,
            // the hash picks; ties elsewhere resolve to the earliest draw
            if (err < best) {
              best = err;
              best_sx = sx;
              best_sy = sy;
            }
          }
          // lay the patch, feathering across whatever it overlaps
          for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
              size_t di = (size_t)(by + y) * w + (bx + x);
              float v = in->v[(size_t)(best_sy + y) * in->w + (best_sx + x)];
              if (laid[di]) {
                float tx = x < ov ? x / (float)ov : 1.f;
                float ty = y < ov ? y / (float)ov : 1.f;
                float t = std::min(tx, ty);
                t = t * t * (3.f - 2.f * t);
                out.v[di] = out.v[di] * (1.f - t) + v * t;
              } else {
                out.v[di] = v;
                laid[di] = 1;
              }
            }
        }
    })

} // namespace gpx
