// Geekatplay Studio - slope and channel erosion.
// Thermal: talus relaxation to the angle of repose, optionally run to
// convergence. StreamPower: fluvial incision E = K*A^m*S^n, explicit D8 or
// the implicit Braun-Willett solver with tectonic uplift.
// Split from nodes_erosion.cpp for the 500-line module rule; the goldens and
// the thread-count determinism suite pin that nothing moved but the text.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include "gpx/parallel.hpp"
#include "gpx/erosion_kernels.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace gpx {

// D8 neighbourhood, same order as the analysis nodes
static const int DX8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int DY8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};


// --------------------------------------------------------------- thermal
REGISTER_NODE(
    Thermal, "Erosion", "Thermal weathering — talus slopes to angle of repose",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      add_float(n.attrs, "talus", "Talus angle", 1.2f, 0.05f, 4.f);
      add_int(n.attrs, "iterations", "Iterations", 60, 1, 500);
      add_float(n.attrs, "rate", "Transport rate", 0.5f, 0.05f, 1.f);
      add_bool(n.attrs, "converge", "Run to convergence", false);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      out.minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      float talus = n.attrs.get_f("talus", 1.2f) * amp / out.w;
      thermal_relax(out, talus, n.attrs.get_i("iterations", 60),
                    n.attrs.get_f("rate", 0.5f), n.attrs.get_b("converge"));
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// Deterministic two-pass talus transport: pass 1 computes each cell's
// outflow (writes only its own cell), pass 2 gathers inflow from the
// neighbours. No cross-thread writes, so the result is reproducible.
// Shared with ErosionLayers through gpx/erosion_kernels.hpp.
void thermal_relax(Heightmap &out, float talus, int iters, float rate,
                   bool converge, Heightmap *deposit_out) {
  if (converge) iters = 2000;
  Heightmap move_amt(out.w, out.h), move_total(out.w, out.h);
  Heightmap delta(out.w, out.h);
  if (deposit_out) *deposit_out = Heightmap(out.w, out.h);
  auto excess = [&](int x, int y, int k) {
    float dist = (DX8[k] && DY8[k]) ? 1.41421356f : 1.f;
    float d = (out.atc(x, y) - out.atc(x + DX8[k], y + DY8[k])) / dist - talus;
    return d > 0 ? d : 0.f;
  };
  for (int it = 0; it < iters; ++it) {
    std::atomic<bool> moved{false};
    parallel_rows(out.h, [&](int y0, int y1) {
      bool local_moved = false;
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < out.w; ++x) {
          float dmax = 0, dtotal = 0;
          for (int k = 0; k < 8; ++k) {
            float d = excess(x, y, k);
            if (d > 0) {
              dtotal += d;
              dmax = std::max(dmax, d);
            }
          }
          move_total.at(x, y) = dtotal;
          move_amt.at(x, y) = dtotal > 0 ? rate * dmax * 0.5f : 0.f;
          if (dtotal > 0) local_moved = true;
        }
      if (local_moved) moved.store(true);
    });
    parallel_rows(out.h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < out.w; ++x) {
          float in = 0;
          for (int k = 0; k < 8; ++k) {
            int sx = x + DX8[k], sy = y + DY8[k]; // neighbour giving to us
            if (sx < 0 || sx >= out.w || sy < 0 || sy >= out.h) continue;
            float tot = move_total.at(sx, sy);
            if (tot <= 0) continue;
            // the neighbour's excess toward this cell is the opposite dir
            int opp = 7 - k;
            float share = excess(sx, sy, opp);
            if (share > 0) in += move_amt.at(sx, sy) * share / tot;
          }
          delta.at(x, y) = in - move_amt.at(x, y);
          if (deposit_out) deposit_out->at(x, y) += in;
        }
    });
    parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
      for (size_t i = i0; i < i1; ++i) out.v[i] += delta.v[i];
    });
    if (converge && !moved.load()) break;
  }
}

// ------------------------------------- stream power: explicit or implicit
REGISTER_NODE(
    StreamPower, "Erosion", "Fluvial erosion E=K·A^m·S^n — explicit incision or implicit solver with tectonic uplift",
    [](Node &n) {
      n.add_in("input");
      n.add_in("uplift", DataType::Heightmap, true);
      n.add_in("hardness", DataType::Heightmap, true);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("flow_map");
      add_choice(n.attrs, "method", "Method",
                 {"Explicit incision", "Implicit + uplift (Braun-Willett)"}, 1);
      add_int(n.attrs, "iterations", "Iterations", 40, 1, 400, "Simulation");
      add_float(n.attrs, "k_erode", "Erodibility K", 0.03f, 0.001f, 0.3f, "Simulation");
      add_float(n.attrs, "m_exp", "Area exponent m", 0.5f, 0.2f, 1.f, "Simulation");
      add_float(n.attrs, "n_exp", "Slope exponent n (explicit)", 1.f, 0.5f, 2.f, "Simulation");
      add_float(n.attrs, "dt", "Timestep (implicit)", 1.f, 0.05f, 10.f, "Simulation");
      add_float(n.attrs, "uplift_rate", "Uplift rate", 0.004f, 0.f, 0.05f, "Simulation");
      add_float(n.attrs, "smooth", "Diffusion", 0.08f, 0.f, 0.5f, "Simulation");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &flow = n.out_hmap("flow_map");
      out = *in;
      int w = out.w, h = out.h;
      int method = n.attrs.get_choice("method");
      int iters = n.attrs.get_i("iterations", 40);
      float K = n.attrs.get_f("k_erode", 0.03f);
      float me = n.attrs.get_f("m_exp", 0.5f);
      float ne = n.attrs.get_f("n_exp", 1.f);
      float dt = n.attrs.get_f("dt", 1.f);
      float uplift_rate = n.attrs.get_f("uplift_rate", 0.004f);
      float diff = n.attrs.get_f("smooth", 0.08f);
      const Heightmap *uplift = n.in_hmap("uplift");
      const Heightmap *hard = n.in_hmap("hardness");
      float mn0, mx0;
      out.minmax(mn0, mx0);
      float amp = (mx0 - mn0) > 1e-9f ? mx0 - mn0 : 1.f;

      std::vector<int> order((size_t)w * h);
      std::vector<int> receiver((size_t)w * h);
      std::vector<float> area((size_t)w * h);
      std::vector<float> rdist((size_t)w * h);

      for (int it = 0; it < iters; ++it) {
        // receivers + height-descending order
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(),
                  [&](int a2, int b2) { return out.v[a2] > out.v[b2]; });
        parallel_rows(h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
              int i = y * w + x;
              float hgt = out.v[i];
              int best = -1;
              float bestdrop = 0, bestdist = 1;
              for (int k = 0; k < 8; ++k) {
                int nx2 = x + DX8[k], ny2 = y + DY8[k];
                if (nx2 < 0 || nx2 >= w || ny2 < 0 || ny2 >= h) continue;
                float dist = (DX8[k] && DY8[k]) ? 1.41421356f : 1.f;
                float drop = (hgt - out.at(nx2, ny2)) / dist;
                if (drop > bestdrop) {
                  bestdrop = drop;
                  best = ny2 * w + nx2;
                  bestdist = dist;
                }
              }
              receiver[i] = best;
              rdist[i] = bestdist;
            }
        });
        // flow accumulation, high to low
        std::fill(area.begin(), area.end(), 1.f);
        for (int i : order)
          if (receiver[i] >= 0) area[receiver[i]] += area[i];

        if (method == 0) {
          // explicit incision
          parallel_rows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
              for (int x = 0; x < w; ++x) {
                int i = y * w + x;
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) continue;
                if (receiver[i] < 0) continue;
                float smax = (out.v[i] - out.v[receiver[i]]) / rdist[i];
                float kk = K * (hard ? std::clamp(1.f - hard->v[i], 0.05f, 1.f) : 1.f);
                float a_norm = area[i] / float(w);
                float e = kk * std::pow(a_norm, me) *
                          std::pow(smax / amp * w, ne) * amp / w;
                out.v[i] -= std::min(e, smax * rdist[i] * 0.9f);
              }
          });
        } else {
          // implicit Braun-Willett: update low -> high so receiver is current
          float U = uplift_rate * amp;
          for (auto ri = order.rbegin(); ri != order.rend(); ++ri) {
            int i = *ri;
            int x = i % w, y = i / w;
            float u = U * (uplift ? std::clamp(uplift->v[i], 0.f, 1.f) : 1.f);
            bool border = (x == 0 || y == 0 || x == w - 1 || y == h - 1);
            if (receiver[i] < 0 || border) {
              if (!border) out.v[i] += u * dt; // lakes/pits only uplift
              continue;
            }
            float kk = K * (hard ? std::clamp(1.f - hard->v[i], 0.05f, 1.f) : 1.f);
            // c = K·A^m / Δx, scaled so slope is measured in amp-per-tile units
            float c = kk * std::pow(area[i] / float(w), me) * float(w) / (rdist[i] * amp);
            out.v[i] = (out.v[i] + u * dt + dt * c * amp * out.v[receiver[i]]) /
                       (1.f + dt * c * amp);
          }
        }
        // hillslope diffusion
        if (diff > 0) {
          Heightmap tmp = out;
          parallel_rows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
              for (int x = 0; x < w; ++x) {
                float lap = tmp.atc(x - 1, y) + tmp.atc(x + 1, y) +
                            tmp.atc(x, y - 1) + tmp.atc(x, y + 1) -
                            4.f * tmp.at(x, y);
                out.at(x, y) = tmp.at(x, y) + diff * 0.25f * lap;
              }
          });
        }
      }
      for (size_t i = 0; i < area.size(); ++i) flow.v[i] = std::log1p(area[i]);
      flow.remap(0.f, 1.f);
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx
