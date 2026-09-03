// Geekatplay Studio - aeolian erosion and sediment fill.
// Wind: windward abrasion, leeward deposition (dunes). SedimentDeposit:
// valleys filled with smooth sediment.
// Split from nodes_erosion.cpp for the 500-line module rule; the goldens and
// the thread-count determinism suite pin that nothing moved but the text.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include "gpx/parallel.hpp"
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


// ------------------------------------------------------------------ wind
REGISTER_NODE(
    Wind, "Erosion", "Aeolian erosion — windward abrasion, leeward deposition (dunes)",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      // where the wind took material from and where it dropped it - the
      // masks for exposed rock on the windward faces and loose sand in the
      // lee; delta is the signed change, mid-grey = untouched
      n.add_out("abrasion_map");
      n.add_out("deposit_map");
      n.add_out("delta_map");
      add_float(n.attrs, "angle", "Wind direction °", 30.f, -180.f, 180.f);
      add_int(n.attrs, "iterations", "Iterations", 40, 1, 300);
      add_float(n.attrs, "strength", "Strength", 0.4f, 0.05f, 1.f);
      add_float(n.attrs, "carry_dist", "Carry distance", 0.03f, 0.005f, 0.15f);
      add_float(n.attrs, "shadow_angle", "Shadow angle", 1.f, 0.2f, 4.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      float mn, mx;
      out.minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      float a = n.attrs.get_f("angle", 30.f) * 0.017453293f;
      float wx = std::cos(a), wy = std::sin(a);
      int iters = n.attrs.get_i("iterations", 40);
      float strength = n.attrs.get_f("strength", 0.4f) * amp * 0.002f;
      int carry = std::max(2, (int)(n.attrs.get_f("carry_dist", 0.03f) * out.w));
      float shadow = n.attrs.get_f("shadow_angle", 1.f) * amp / out.w;
      // A cell lifts material from itself and drops it on a cell up to `carry`
      // steps downwind - which is very often in another worker's band. Writing
      // that through one shared delta buffer was an unsynchronised
      // read-modify-write: a race, so two workers could lose an update
      // outright, and the result depended on how the rows were split. The
      // thread-count determinism test caught it at 5 workers.
      //
      // Deltas go through one shared atomic int64 accumulator, for the same
      // reason as the droplet solver: integer addition is associative and
      // commutative, so however the atomics interleave the total is identical,
      // while float partial sums would still make it depend on the partition.
      const size_t N = out.v.size();
      const double FP = 1099511627776.0; // 2^40
      const double FP_INV = 1.0 / FP;
      unsigned T = std::max(1u, std::min<unsigned>(gpx::worker_count(),
                                                   (unsigned)std::max(1, out.h / 16)));
      auto delta = std::make_unique<std::atomic<int64_t>[]>(N);
      for (size_t i = 0; i < N; ++i)
        delta[i].store(0, std::memory_order_relaxed);
      Heightmap &abr = n.out_hmap("abrasion_map");
      Heightmap &dep = n.out_hmap("deposit_map");
      Heightmap &dlt = n.out_hmap("delta_map");
      std::fill(abr.v.begin(), abr.v.end(), 0.f);
      std::fill(dep.v.begin(), dep.v.end(), 0.f);
      int band = (out.h + (int)T - 1) / (int)T;
      for (int it = 0; it < iters; ++it) {
        auto sweep = [&](unsigned tid) {
          auto add = [&](int x, int y, float v) {
            delta[(size_t)y * out.w + x].fetch_add(
                (int64_t)std::llround((double)v * FP),
                std::memory_order_relaxed);
          };
          int y0 = (int)tid * band, y1 = std::min(out.h, y0 + band);
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < out.w; ++x) {
              // exposure: height above upwind neighbor => abrasion
              float here = out.at(x, y);
              float up = out.atc(x - (int)std::round(wx * 2), y - (int)std::round(wy * 2));
              float exposure = here - up;
              if (exposure <= 0) continue;
              float lift = std::min(exposure * 0.5f, strength);
              add(x, y, -lift);
              // deposit downwind at first shadowed cell
              for (int s = 2; s <= carry; ++s) {
                int dxp = x + (int)std::round(wx * s);
                int dyp = y + (int)std::round(wy * s);
                if (dxp < 0 || dxp >= out.w || dyp < 0 || dyp >= out.h) break;
                float drop = here - out.at(dxp, dyp);
                if (drop > shadow * s || s == carry) {
                  add(dxp, dyp, lift);
                  break;
                }
              }
            }
        };
        std::vector<std::thread> pool;
        for (unsigned t = 1; t < T; ++t) pool.emplace_back(sweep, t);
        sweep(0);
        for (auto &th : pool) th.join();
        // apply and clear in one pass; integer addition is commutative, so the
        // order the atomics landed in cannot change the total
        parallel_index(N, [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) {
            int64_t d = delta[i].exchange(0, std::memory_order_relaxed);
            if (!d) continue;
            float f = (float)(d * FP_INV);
            out.v[i] += f;
            if (f < 0.f) abr.v[i] -= f;
            else dep.v[i] += f;
          }
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
      abr.remap(0.f, 1.f);
      dep.remap(0.f, 1.f);
      for (size_t i = 0; i < N; ++i) dlt.v[i] = out.v[i] - in->v[i];
      float dm = 1e-9f;
      for (float v : dlt.v) dm = std::max(dm, std::fabs(v));
      for (float &v : dlt.v) v = 0.5f + 0.5f * v / dm;
    })

// ------------------------------------------------------ sediment blanket
REGISTER_NODE(
    SedimentDeposit, "Erosion", "Fill valleys with smooth sediment",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("sediment_map");
      n.add_out("exposed_map"); // what the blanket did not cover
      add_int(n.attrs, "iterations", "Iterations", 40, 1, 300);
      add_float(n.attrs, "amount", "Fill amount", 0.3f, 0.f, 1.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &sed = n.out_hmap("sediment_map");
      out = *in;
      int iters = n.attrs.get_i("iterations", 40);
      float amount = n.attrs.get_f("amount", 0.3f);
      Heightmap smoothed = out;
      for (int it = 0; it < iters; ++it) {
        Heightmap tmp = smoothed;
        parallel_rows(out.h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < out.w; ++x)
              smoothed.at(x, y) =
                  (tmp.atc(x - 1, y) + tmp.atc(x + 1, y) + tmp.atc(x, y - 1) +
                   tmp.atc(x, y + 1) + 4.f * tmp.at(x, y)) *
                  0.125f;
        });
      }
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float fill = std::max(smoothed.v[i] - out.v[i], 0.f) * amount;
          sed.v[i] = fill;
          out.v[i] += fill;
        }
      });
      sed.remap(0.f, 1.f);
      Heightmap &exp = n.out_hmap("exposed_map");
      for (size_t i = 0; i < exp.v.size(); ++i) exp.v[i] = 1.f - sed.v[i];
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx
