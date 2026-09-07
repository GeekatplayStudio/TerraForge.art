// Geekatplay TerraForge — does the accelerated fractal agree with the CPU?
//
// The load-bearing check of the accelerator design, and the same one the
// dual-domain field graph already answers for its transpiled shaders
// (studio/field_gpu_check.cpp). A fast path that draws a different terrain is
// not a fast path; it is a bug with a stopwatch on it.
//
// Driven from the API rather than from a test binary for the reason the field
// check is: the CPU side is a linked library and the GPU side needs a
// context, and only the running studio has both.
//
//     {"op":"verify_accel"}
//
// The tolerance is 2e-4, matching the field check. It is not zero and cannot
// be: the two paths differ in the order of their floating-point arithmetic
// and in the precision of cos/sin, which GLSL bounds but does not pin. What
// *is* exact is the lattice - the hashes are 32-bit integer arithmetic on
// both sides - so a disagreement here means a real difference in the maths,
// not accumulated noise.
#include "gpu_compute.hpp"
#include "gpx/accel.hpp"
#include "gpx/fractal_core.hpp"
#include "gpx/heightmap.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace studio {

namespace {

struct Case {
  const char *name;
  gpx::fractal::Params p;
};

std::vector<Case> cases() {
  using namespace gpx::fractal;
  std::vector<Case> v;
  auto add = [&](const char *n, Params p) { v.push_back({n, p}); };

  Params base; // the defaults, which is what most graphs actually use
  add("defaults", base);

  Params ridged = base;
  ridged.landscape = RIDGES;
  add("ridges", ridged);

  Params billow = base;
  billow.landscape = BILLOWS;
  billow.ridge_smooth = 0.6f;
  add("billows, smoothed", billow);

  Params mix = base;
  mix.landscape = RIDGE_MIX;
  mix.blend = 0.35f;
  add("ridge mix", mix);

  Params val = base;
  val.base = VALUE;
  add("value noise", val);

  Params grainy = base;
  grainy.base = GRAINY;
  add("grainy", grainy);

  Params stretched = base;
  stretched.stretch_x = 4.f;
  stretched.stretch_y = 0.4f;
  stretched.stretch_damping = 0.3f;
  add("stretched", stretched);

  Params warped = base;
  warped.distortion = 0.4f;
  warped.distortion_scale = 2.f;
  add("distorted", warped);

  Params rough = base;
  rough.influence = 0.7f;
  rough.local_influence = 0.5f;
  rough.smooth_level = -0.2f;
  add("variable roughness", rough);

  Params grain = base;
  grain.variation_strength = 0.8f;
  grain.variation_roughness = 1.3f;
  add("grainy variation", grain);

  for (int c = 0; c <= 8; ++c) {
    Params combo = base;
    combo.combine = c;
    static const char *names[] = {"add",     "blend",   "var rough",
                                  "var abs", "max",     "max abs",
                                  "min",     "min abs", "multiply"};
    add(names[c], combo);
  }
  for (int pr = 1; pr <= 5; ++pr) {
    Params prof = base;
    prof.profile = pr;
    prof.profile_steps = 7.f;
    prof.creep_in = 0.25f;
    prof.filter_min = 0.1f;
    prof.filter_max = 0.85f;
    static const char *names[] = {"", "terraces", "soft clip",
                                  "s-curve", "plateau", "valleys"};
    add(names[pr], prof);
  }

  // One at a time as well as all at once. A combined case that differs says
  // only that something in it does, and the answer to "which" decides
  // whether the accelerator should decline it or the shader is wrong.
  Params o16 = base; o16.octaves = 16;                 add("16 octaves", o16);
  Params gn = base;  gn.gain = 3.f;                    add("gain 3", gn);
  Params bs = base;  bs.bump_surge = 0.6f;             add("bump surge", bs);
  Params dn = base;  dn.double_noise = true;           add("double noise", dn);
  Params fs = base;  fs.filter_steepness = 2.2f;       add("filter steepness", fs);
  Params rr = base;  rr.rough_ref = 0.08f;             add("rough ref", rr);

  Params extreme = base;
  extreme.octaves = 16;
  extreme.gain = 3.f;
  extreme.bump_surge = 0.6f;
  extreme.double_noise = true;
  extreme.filter_steepness = 2.2f;
  extreme.rough_ref = 0.08f;
  add("everything at once", extreme);
  return v;
}

} // namespace

std::string accel_verify_all() {
  gpx::Accelerator *acc = gpx::accel_if_available();
  if (!acc)
    return "no accelerator installed (" + gpu_compute_status() + ")\n";

  const int N = 256;
  std::string out;
  char line[320];
  int checked = 0, failed = 0, declined = 0;
  double worst_all = 0.0;
  double cpu_total = 0.0, gpu_total = 0.0;

  for (const Case &c : cases()) {
    gpx::Heightmap g_h(N, N), g_r(N, N), c_h(N, N), c_r(N, N);
    auto t0 = std::chrono::steady_clock::now();
    const bool took = acc->fractal(c.p, 12345u, g_h, g_r);
    auto t1 = std::chrono::steady_clock::now();
    if (!took) {
      ++declined;
      std::snprintf(line, sizeof line, "  %-22s declined (CPU runs)\n", c.name);
      out += line;
      continue;
    }
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
        float r = 0.f;
        c_h.at(x, y) = gpx::fractal::eval(x / (float)N, y / (float)N, 12345u,
                                          c.p, &r);
        c_r.at(x, y) = r;
      }
    auto t2 = std::chrono::steady_clock::now();
    cpu_total += std::chrono::duration<double, std::milli>(t2 - t1).count();
    gpu_total += std::chrono::duration<double, std::milli>(t1 - t0).count();

    double worst = 0.0, sum = 0.0, worst_r = 0.0;
    for (size_t i = 0; i < c_h.v.size(); ++i) {
      const double d = std::fabs((double)c_h.v[i] - g_h.v[i]);
      worst = std::max(worst, d);
      sum += d;
      worst_r = std::max(worst_r, std::fabs((double)c_r.v[i] - g_r.v[i]));
    }
    const double mean = sum / (double)c_h.v.size();
    const bool ok = worst < 2e-4 && worst_r < 2e-4;
    if (!ok) ++failed;
    ++checked;
    worst_all = std::max(worst_all, worst);
    std::snprintf(line, sizeof line,
                  "  %-22s max %.3e (rough %.3e) mean %.3e -> %s\n", c.name,
                  worst, worst_r, mean, ok ? "AGREE" : "DIFFER");
    out += line;
  }
  std::snprintf(line, sizeof line,
                "%d cases agree, %d differ, %d declined; worst %.3e "
                "(tolerance 2e-4)\n"
                "at %dx%d: GPU %.1f ms total, CPU %.1f ms total (%.1fx)\n"
                "  - the CPU side here is single-threaded, so that ratio "
                "flatters the GPU;\n    the honest comparison is "
                "build/node_bench against the studio's eval_ms.\n",
                checked - failed, failed, declined, worst_all, N, N, gpu_total,
                cpu_total, cpu_total / (gpu_total > 0.01 ? gpu_total : 1.0));
  return out + line;
}

} // namespace studio
