// Geekatplay Studio — erosion nodes.
// Hydraulic: particle droplets OR shallow-water pipe model (Mei et al. 2007).
// StreamPower: explicit D8 OR implicit Braun-Willett solver with tectonic
// uplift (reimplemented from the papers; Cordonnier 2016 lineage).
// Thermal talus (optional run-to-convergence), wind (aeolian), sediment fill.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"
#include "gpx/parallel.hpp"
#include <cmath>
#include <atomic>
#include <memory>
#include <cstdint>
#include <random>
#include <thread>

namespace gpx {

static const int DX8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int DY8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// ------------------------------------------------------ particle droplets
struct DropletParams {
  int num_particles = 120000;
  int max_lifetime = 48;
  float inertia = 0.06f;
  float capacity = 5.5f;
  float min_capacity = 0.01f;
  float erode_rate = 0.4f;
  float deposit_rate = 0.25f;
  float evaporate = 0.015f;
  float gravity = 4.f;
  int brush_radius = 3;
};

static void erode_droplets(Heightmap &map, const DropletParams &p, uint32_t seed,
                           Heightmap *erosion_out, Heightmap *deposit_out) {
  int w = map.w, h = map.h;
  std::vector<int> bx, by;
  std::vector<float> bw;
  float wsum = 0;
  for (int dy = -p.brush_radius; dy <= p.brush_radius; ++dy)
    for (int dx = -p.brush_radius; dx <= p.brush_radius; ++dx) {
      float d = std::sqrt(float(dx * dx + dy * dy));
      if (d > p.brush_radius) continue;
      bx.push_back(dx);
      by.push_back(dy);
      bw.push_back(1.f - d / (p.brush_radius + 1e-4f));
      wsum += bw.back();
    }
  for (auto &wgt : bw) wgt /= wsum;

  // Deterministic parallel droplets.
  //
  // "Deterministic" used to mean "on this machine". Particles were dealt out
  // as chunk = per_round / T, the RNG was seeded from the worker id, and each
  // worker sampled its own accumulator - so the partition decided which
  // particles existed, where they started, and what they saw. Measured at 256
  // with one seed: workers=1,2,3,4,8 gave five different terrains. AGENTS.md
  // engine rule 1 says "every thread count", and no test could see it because
  // the suite runs at whatever the machine has.
  //
  // Three changes make the result a function of the seed alone:
  //   - a particle's start comes from a counter hash of (seed, round, global
  //     particle index), so particle i lands in the same place under any
  //     partition. This also drops std::uniform_real_distribution, which the
  //     standard does not specify - libstdc++ and MSVC give different streams
  //     from the same engine.
  //   - a particle reads only the shared map, never a worker's accumulator,
  //     so its path cannot depend on who else that worker happened to run.
  //   - deltas still land in per-worker buffers and reduce in a fixed order.
  //
  // Self-limiting now happens at round granularity rather than continuously
  // within a worker, so ROUNDS rises to keep the erosion character: particles
  // in a round cannot see each other, and the per-round clamp below is what
  // stops several of them filling one sink.
  size_t N = (size_t)w * h;
  unsigned hw = gpx::worker_count();
  // three buffers per worker (height, erosion, deposition): cap total memory
  unsigned T = std::max(1u, hw);
  const int ROUNDS = 48;
  // Fixed point, not float.
  //
  // The reduction sums one partial buffer per worker, and float addition is
  // not associative: (a+b)+c differs from a+(b+c) in the last bits, so the
  // same particles split eight ways give a different total than split two
  // ways. Every other cause of core-count dependence was fixed and the hashes
  // still differed across 1/2/3/4/8 workers because of exactly this.
  //
  // Integer addition is associative and commutative, so any partition of the
  // same deltas reduces to the identical value. 2^40 over a map normalised to
  // 0..1 gives a resolution of 9.1e-13, six orders below float's 1.2e-7 at
  // 1.0, and leaves +/-8.4e6 of headroom in int64.
  const double FP = 1099511627776.0; // 2^40
  const double FP_INV = 1.0 / FP;
  // One shared accumulator, not one per worker.
  //
  // Per-worker buffers reduced in a fixed order are also partition-independent,
  // but they cost T times the memory traffic: at 512 with eight workers that
  // was 50 MB cleared and 50 MB read every round, and at 48 rounds it made the
  // solver 5.4x slower (109 -> 593 ms). A relaxed atomic fetch_add on int64
  // needs no reduction pass and one buffer instead of T, and is just as
  // partition-independent - integer addition is associative *and* commutative,
  // so the order the atomics happen to land in cannot change the total.
  //
  // Relaxed is the right ordering: the join after each round is the
  // synchronisation edge, and nothing reads these until then.
  auto make_acc = [N] {
    auto a = std::make_unique<std::atomic<int64_t>[]>(N);
    for (size_t i = 0; i < N; ++i) a[i].store(0, std::memory_order_relaxed);
    return a;
  };
  auto dmap = make_acc(), dero = make_acc(), ddep = make_acc();

  auto simulate = [&](unsigned tid, int round) {
    // one place that turns a delta into fixed point, so the rounding rule is
    // the same for every contribution
    auto fx = [FP](float v) { return (int64_t)std::llround((double)v * FP); };
    auto bump = [](std::atomic<int64_t> *a, size_t i, int64_t v) {
      a[i].fetch_add(v, std::memory_order_relaxed);
    };
    // Reads the shared map only. Adding this worker's accumulator here is
    // what made the solver's output a function of the core count.
    auto height_grad = [&](float px, float py, float &hgt, float &gx, float &gy) {
      int xi = std::clamp((int)px, 0, w - 2), yi = std::clamp((int)py, 0, h - 2);
      float fx = px - xi, fy = py - yi;
      auto at = [&](int x, int y) { return map.v[(size_t)y * w + x]; };
      float h00 = at(xi, yi), h10 = at(xi + 1, yi);
      float h01 = at(xi, yi + 1), h11 = at(xi + 1, yi + 1);
      gx = (h10 - h00) * (1 - fy) + (h11 - h01) * fy;
      gy = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
      hgt = h00 * (1 - fx) * (1 - fy) + h10 * fx * (1 - fy) +
            h01 * (1 - fx) * fy + h11 * fx * fy;
    };
    // deterministic particle range for this (round, worker)
    long long per_round = (long long)p.num_particles / ROUNDS;
    long long base = per_round * round;
    long long chunk = per_round / T;
    long long i0 = base + (long long)tid * chunk;
    long long i1 = (tid + 1 == T) ? base + per_round : i0 + chunk;
    // Counter-based, so the stream belongs to the particle rather than to the
    // worker that happened to draw it. splitmix64's finalizer: integer-only,
    // identical on every compiler, no distribution object involved.
    auto mix = [](uint64_t z) {
      z += 0x9e3779b97f4a7c15ull;
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      return z ^ (z >> 31);
    };
    auto unit = [](uint64_t r) { // top 24 bits -> [0,1)
      return (float)(r >> 40) * (1.0f / 16777216.0f);
    };
    for (long long i = i0; i < i1; ++i) {
    uint64_t k = mix(((uint64_t)seed << 32) ^ ((uint64_t)round << 48) ^
                     (uint64_t)i);
    float px = unit(k) * (w - 1), py = unit(mix(k)) * (h - 1);
    float dx = 0, dy = 0, speed = 1, water = 1, sediment = 0;
    for (int life = 0; life < p.max_lifetime; ++life) {
      float hgt, gx, gy;
      height_grad(px, py, hgt, gx, gy);
      dx = dx * p.inertia - gx * (1 - p.inertia);
      dy = dy * p.inertia - gy * (1 - p.inertia);
      float len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-8f) break;
      dx /= len;
      dy /= len;
      float nx = px + dx, ny = py + dy;
      if (nx < 1 || nx >= w - 2 || ny < 1 || ny >= h - 2) break;
      float nh, ngx, ngy;
      height_grad(nx, ny, nh, ngx, ngy);
      float dh = nh - hgt;
      float cap = std::max(-dh * speed * water * p.capacity, p.min_capacity);
      if (sediment > cap || dh > 0) {
        float amount = dh > 0 ? std::min(dh, sediment)
                              : (sediment - cap) * p.deposit_rate;
        sediment -= amount;
        int xi = (int)px, yi = (int)py;
        float fbx = px - xi, fby = py - yi;
        bump(dmap.get(), (size_t)yi * w + xi, fx(amount * (1 - fbx) * (1 - fby)));
        bump(dmap.get(), (size_t)yi * w + xi + 1, fx(amount * fbx * (1 - fby)));
        bump(dmap.get(), (size_t)(yi + 1) * w + xi, fx(amount * (1 - fbx) * fby));
        bump(dmap.get(), (size_t)(yi + 1) * w + xi + 1, fx(amount * fbx * fby));
        bump(ddep.get(), (size_t)yi * w + xi, fx(amount));
      } else {
        float amount = std::min((cap - sediment) * p.erode_rate, -dh);
        int cx = (int)px, cy = (int)py;
        for (size_t b = 0; b < bw.size(); ++b) {
          int ex = cx + bx[b], ey = cy + by[b];
          if (ex < 0 || ex >= w || ey < 0 || ey >= h) continue;
          float delta = amount * bw[b];
          bump(dmap.get(), (size_t)ey * w + ex, -fx(delta));
          sediment += delta;
          bump(dero.get(), (size_t)ey * w + ex, fx(delta));
        }
      }
      speed = std::sqrt(std::max(speed * speed + dh * -p.gravity, 0.f));
      water *= (1 - p.evaporate);
      px = nx;
      py = ny;
      if (water < 0.01f) break;
    }
    }
  };

  for (int round = 0; round < ROUNDS; ++round) {
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < T; ++t) pool.emplace_back(simulate, t, round);
    simulate(0, round);
    for (auto &th : pool) th.join();
    // Fixed-order reduction => reproducible floating-point accumulation.
    // No particle in a round can see any other's deposits, so several may
    // fill the same sink; clamp each cell's per-round change (the map is
    // normalised to 0..1 here) to keep that from building spikes.
    const float MAX_STEP = 0.05f;
    // per-cell and independent, so parallelising keeps it deterministic
    // Apply and clear in one pass over each cell: a separate clearing sweep
    // doubled the memory traffic for no reason.
    parallel_index(N, [&](size_t i0, size_t i1) {
      for (size_t i = i0; i < i1; ++i) {
        int64_t d = dmap[i].exchange(0, std::memory_order_relaxed);
        int64_t de = dero[i].exchange(0, std::memory_order_relaxed);
        int64_t dd = ddep[i].exchange(0, std::memory_order_relaxed);
        if (!d && !de && !dd) continue; // most cells are untouched in a round
        map.v[i] += std::clamp((float)(d * FP_INV), -MAX_STEP, MAX_STEP);
        if (erosion_out) erosion_out->v[i] += (float)(de * FP_INV);
        if (deposit_out) deposit_out->v[i] += (float)(dd * FP_INV);
      }
    });
  }
}

// -------------------------------------- shallow-water pipe model (Mei 2007)
struct PipeParams {
  int iterations = 120;
  float dt = 0.02f;
  float rain = 0.012f;
  float evaporation = 0.015f;
  float pipe_area = 20.f;    // A*g/l lumped
  float capacity_k = 1.f;    // Kc
  float erode_k = 0.5f;      // Ks
  float deposit_k = 0.5f;    // Kd
  float min_tilt = 0.005f;
};

static void erode_pipes(Heightmap &hmap, const PipeParams &pp,
                        Heightmap *erosion_out, Heightmap *deposit_out) {
  int w = hmap.w, h = hmap.h;
  size_t N = (size_t)w * h;
  std::vector<float> water(N, 0.f), sed(N, 0.f), sed2(N, 0.f);
  std::vector<float> fL(N, 0.f), fR(N, 0.f), fT(N, 0.f), fB(N, 0.f);
  std::vector<float> vx(N, 0.f), vy(N, 0.f);
  auto idx = [&](int x, int y) { return (size_t)y * w + x; };
  auto Hs = [&](int x, int y) {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return hmap.v[idx(x, y)] + water[idx(x, y)];
  };

  for (int it = 0; it < pp.iterations; ++it) {
    // 1. rain
    for (size_t i = 0; i < N; ++i) water[i] += pp.rain * pp.dt;
    // 2. flux update (reads heights+water, writes own fluxes: race-free)
    parallel_rows(h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
          size_t i = idx(x, y);
          float Hc = Hs(x, y);
          float nfL = std::max(0.f, fL[i] + pp.dt * pp.pipe_area * (Hc - Hs(x - 1, y)));
          float nfR = std::max(0.f, fR[i] + pp.dt * pp.pipe_area * (Hc - Hs(x + 1, y)));
          float nfT = std::max(0.f, fT[i] + pp.dt * pp.pipe_area * (Hc - Hs(x, y - 1)));
          float nfB = std::max(0.f, fB[i] + pp.dt * pp.pipe_area * (Hc - Hs(x, y + 1)));
          float total = (nfL + nfR + nfT + nfB) * pp.dt;
          float K = total > 1e-9f ? std::min(1.f, water[i] / total) : 0.f;
          fL[i] = nfL * K;
          fR[i] = nfR * K;
          fT[i] = nfT * K;
          fB[i] = nfB * K;
        }
    });
    // 3. water volume + velocity
    parallel_rows(h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
          size_t i = idx(x, y);
          float in_flux = 0;
          if (x > 0) in_flux += fR[idx(x - 1, y)];
          if (x < w - 1) in_flux += fL[idx(x + 1, y)];
          if (y > 0) in_flux += fB[idx(x, y - 1)];
          if (y < h - 1) in_flux += fT[idx(x, y + 1)];
          float out_flux = fL[i] + fR[i] + fT[i] + fB[i];
          float dv = pp.dt * (in_flux - out_flux);
          float w_old = water[i];
          water[i] = std::max(water[i] + dv, 0.f);
          float w_avg = std::max(0.5f * (w_old + water[i]), 1e-6f);
          float flow_x = 0.5f * ((x > 0 ? fR[idx(x - 1, y)] : 0) - fL[i] + fR[i] -
                                 (x < w - 1 ? fL[idx(x + 1, y)] : 0));
          float flow_y = 0.5f * ((y > 0 ? fB[idx(x, y - 1)] : 0) - fT[i] + fB[i] -
                                 (y < h - 1 ? fT[idx(x, y + 1)] : 0));
          vx[i] = flow_x / w_avg;
          vy[i] = flow_y / w_avg;
        }
    });
    // 4. erosion / deposition by capacity
    parallel_rows(h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
          size_t i = idx(x, y);
          float dhx = (hmap.atc(x + 1, y) - hmap.atc(x - 1, y)) * 0.5f * w;
          float dhy = (hmap.atc(x, y + 1) - hmap.atc(x, y - 1)) * 0.5f * h;
          float tilt = std::sqrt(dhx * dhx + dhy * dhy);
          float sin_tilt = tilt / std::sqrt(1.f + tilt * tilt);
          sin_tilt = std::max(sin_tilt, pp.min_tilt);
          float speed = std::sqrt(vx[i] * vx[i] + vy[i] * vy[i]);
          float C = pp.capacity_k * sin_tilt * speed;
          if (C > sed[i]) {
            float e = pp.erode_k * (C - sed[i]) * pp.dt;
            hmap.v[i] -= e;
            sed[i] += e;
            if (erosion_out) erosion_out->v[i] += e;
          } else {
            float d = pp.deposit_k * (sed[i] - C) * pp.dt;
            hmap.v[i] += d;
            sed[i] -= d;
            if (deposit_out) deposit_out->v[i] += d;
          }
        }
    });
    // 5. semi-Lagrangian sediment advection
    parallel_rows(h, [&](int y0, int y1) {
      for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
          size_t i = idx(x, y);
          float sx = x - vx[i] * pp.dt * w;
          float sy = y - vy[i] * pp.dt * h;
          int x0 = std::clamp((int)sx, 0, w - 2), y0c = std::clamp((int)sy, 0, h - 2);
          float fx = std::clamp(sx - x0, 0.f, 1.f), fy = std::clamp(sy - y0c, 0.f, 1.f);
          sed2[i] = sed[idx(x0, y0c)] * (1 - fx) * (1 - fy) +
                    sed[idx(x0 + 1, y0c)] * fx * (1 - fy) +
                    sed[idx(x0, y0c + 1)] * (1 - fx) * fy +
                    sed[idx(x0 + 1, y0c + 1)] * fx * fy;
        }
    });
    std::swap(sed, sed2);
    // 6. evaporation
    for (size_t i = 0; i < N; ++i) water[i] *= (1.f - pp.evaporation * pp.dt);
  }
  // settle remaining sediment
  for (size_t i = 0; i < N; ++i) hmap.v[i] += sed[i];
}

REGISTER_NODE(
    Hydraulic, "Erosion", "Hydraulic erosion: particle droplets or shallow-water pipe model",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("erosion_map");
      n.add_out("deposition_map");
      add_choice(n.attrs, "method", "Method",
                 {"Particle droplets", "Shallow water (pipe model)"}, 0);
      add_seed(n.attrs);
      // droplets
      add_int(n.attrs, "particles", "Particles (x1000)", 120, 1, 2000, "Droplets");
      add_int(n.attrs, "lifetime", "Particle lifetime", 48, 8, 256, "Droplets");
      add_float(n.attrs, "inertia", "Inertia", 0.06f, 0.f, 0.6f, "Droplets");
      add_float(n.attrs, "capacity", "Carry capacity", 5.5f, 0.5f, 20.f, "Droplets");
      add_float(n.attrs, "erode_rate", "Erosion rate", 0.4f, 0.01f, 1.f, "Droplets");
      add_float(n.attrs, "deposit_rate", "Deposition rate", 0.25f, 0.01f, 1.f, "Droplets");
      add_float(n.attrs, "evaporation", "Evaporation", 0.015f, 0.f, 0.1f, "Droplets");
      add_float(n.attrs, "gravity", "Gravity", 4.f, 0.5f, 12.f, "Droplets");
      add_int(n.attrs, "brush", "Brush radius", 3, 1, 8, "Droplets");
      // pipe model
      add_int(n.attrs, "iterations", "Iterations", 120, 10, 600, "Shallow water");
      add_float(n.attrs, "rain", "Rainfall", 0.012f, 0.001f, 0.1f, "Shallow water");
      add_float(n.attrs, "capacity_k", "Capacity Kc", 1.f, 0.1f, 4.f, "Shallow water");
      add_float(n.attrs, "erode_k", "Erosion Ks", 0.5f, 0.05f, 2.f, "Shallow water");
      add_float(n.attrs, "deposit_k", "Deposition Kd", 0.5f, 0.05f, 2.f, "Shallow water");
      add_float(n.attrs, "sw_evap", "Evaporation", 0.015f, 0.f, 0.2f, "Shallow water");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &ero = n.out_hmap("erosion_map");
      Heightmap &dep = n.out_hmap("deposition_map");
      out = *in;
      float mn, mx;
      out.minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? (mx - mn) : 1.f;
      for (auto &v : out.v) v = (v - mn) / amp;

      if (n.attrs.get_choice("method") == 0) {
        DropletParams p;
        p.num_particles = n.attrs.get_i("particles", 120) * 1000;
        p.num_particles = (int)(p.num_particles * (out.w / 512.f) * (out.h / 512.f));
        p.max_lifetime = n.attrs.get_i("lifetime", 48);
        p.inertia = n.attrs.get_f("inertia", 0.06f);
        p.capacity = n.attrs.get_f("capacity", 5.5f);
        p.erode_rate = n.attrs.get_f("erode_rate", 0.4f);
        p.deposit_rate = n.attrs.get_f("deposit_rate", 0.25f);
        p.evaporate = n.attrs.get_f("evaporation", 0.015f);
        p.gravity = n.attrs.get_f("gravity", 4.f);
        p.brush_radius = std::max(1, (int)(n.attrs.get_i("brush", 3) * out.w / 512.f));
        erode_droplets(out, p, n.attrs.get_seed("seed"), &ero, &dep);
      } else {
        PipeParams p;
        p.iterations = n.attrs.get_i("iterations", 120);
        p.rain = n.attrs.get_f("rain", 0.012f);
        p.capacity_k = n.attrs.get_f("capacity_k", 1.f) * 0.01f;
        p.erode_k = n.attrs.get_f("erode_k", 0.5f);
        p.deposit_k = n.attrs.get_f("deposit_k", 0.5f);
        p.evaporation = n.attrs.get_f("sw_evap", 0.015f) * 10.f;
        erode_pipes(out, p, &ero, &dep);
      }
      for (auto &v : out.v) v = mn + v * amp;
      ero.remap(0.f, 1.f);
      dep.remap(0.f, 1.f);
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

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
      int iters = n.attrs.get_i("iterations", 60);
      bool converge = n.attrs.get_b("converge");
      if (converge) iters = 2000;
      float rate = n.attrs.get_f("rate", 0.5f);
      // Deterministic two-pass talus transport: pass 1 computes each cell's
      // outflow (writes only its own cell), pass 2 gathers inflow from the
      // neighbours. No cross-thread writes, so the result is reproducible.
      Heightmap move_amt(out.w, out.h), move_total(out.w, out.h);
      Heightmap delta(out.w, out.h);
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
            }
        });
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i) out.v[i] += delta.v[i];
        });
        if (converge && !moved.load()) break;
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

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

// ------------------------------------------------------------------ wind
REGISTER_NODE(
    Wind, "Erosion", "Aeolian erosion — windward abrasion, leeward deposition (dunes)",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
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
            if (d) out.v[i] += (float)(d * FP_INV);
          }
        });
      }
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

// ------------------------------------------------------ sediment blanket
REGISTER_NODE(
    SedimentDeposit, "Erosion", "Fill valleys with smooth sediment",
    [](Node &n) {
      n.add_in("input");
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("output");
      n.add_out("sediment_map");
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
      apply_mask_blend(n.in_hmap("mask"), *in, out);
    })

} // namespace gpx
