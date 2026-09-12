// Geekatplay TerraForge - the sea's waves and the clipmap that draws them
// (gpx/water_waves.hpp, studio/water_surface.cpp), tested without GL.
//
// The promises, each asserted directly:
//   1. the waves are physical: deep-water speeds, sizes that grow with the
//      wind, calm water flat, the longest wave running with the wind;
//   2. the slopes are the surface's own - the analytic tangents match the
//      displacement differenced - and what a filter takes away is returned
//      as variance, never lost;
//   3. rebasing the phases changes nothing but the size of the numbers: the
//      sea evaluated near a far origin is the sea evaluated absolutely;
//   4. the choppiness never folds a single wave over, and round swells
//      (choppiness 0) never squeeze the surface;
//   5. the eye's point over the flat world inverts the placement on every
//      shape;
//   6. the clipmap's levels partition the sea - every point drawn by exactly
//      one level, from a mesh that covers it - and at every seam the two
//      sides are the same surface (fully morphed on the fine side, not at
//      all on the coarse one).
#include "water_surface.hpp"
#include "render_settings.hpp"
#include "gpx/planet_math.hpp"
#include "gpx/water_waves.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_fail = 0;
void check(bool ok, const std::string &msg) {
  if (!ok) {
    std::printf("  [FAIL] %s\n", msg.c_str());
    ++g_fail;
  }
}

using namespace gpx::water;

void build_waves(float wind, float chop, Wave w[MAX_WAVES]) {
  Params p;
  p.wind_speed = wind;
  p.choppiness = chop;
  build(p, w);
}

void test_physics() {
  std::printf("  water: the waves are physical...\n");
  Wave a[MAX_WAVES], b[MAX_WAVES];
  build_waves(4.f, 0.5f, a);
  build_waves(4.f, 0.5f, b);
  bool same = true;
  for (int i = 0; i < MAX_WAVES; ++i)
    same &= a[i].k == b[i].k && a[i].amp == b[i].amp && a[i].phase == b[i].phase &&
            a[i].dx == b[i].dx && a[i].q == b[i].q;
  check(same, "the same settings build the same sea");

  bool deep = true, longest_first = true;
  for (int i = 0; i < MAX_WAVES; ++i) {
    deep &= std::fabs(a[i].omega * a[i].omega - GRAVITY * a[i].k) <= 1e-3f * GRAVITY * a[i].k;
    if (i) longest_first &= a[i].lambda < a[i - 1].lambda;
  }
  check(deep, "every wave runs at the deep-water speed, omega^2 = g k");
  check(longest_first, "the waves are ordered longest first");

  // Pierson-Moskowitz: a fully developed sea under wind U stands about
  // 0.21 U^2/g high; the bands cover most of the spectrum's energy
  const float U = 10.f;
  Wave c[MAX_WAVES];
  build_waves(U, 0.5f, c);
  const float hs = significant_height(c, MAX_WAVES), pm = 0.21f * U * U / GRAVITY;
  check(hs > 0.6f * pm && hs < 1.2f * pm,
        "a 10 m/s wind raises the sea the spectrum says (" + std::to_string(hs) + " m against " +
            std::to_string(pm) + " m)");
  Wave d[MAX_WAVES];
  build_waves(2.f, 0.5f, d);
  check(significant_height(d, MAX_WAVES) < significant_height(a, MAX_WAVES) &&
            significant_height(a, MAX_WAVES) < hs,
        "a stronger wind raises higher waves");

  Wave calm[MAX_WAVES];
  build_waves(0.f, 1.f, calm);
  check(significant_height(calm, MAX_WAVES) < 1e-4f, "no wind is a mirror");
  float ph[MAX_WAVES];
  rebase(calm, MAX_WAVES, 0.0, 0.0, 3.0, ph);
  Sample s;
  evaluate(calm, ph, MAX_WAVES, 12.f, -7.f, 0.f, s);
  check(std::fabs(jacobian(s) - 1.f) < 1e-4f, "calm water is not squeezed");

  const float wind = 30.f * 0.017453293f;
  check(a[0].dx * std::cos(wind) + a[0].dz * std::sin(wind) > 0.9f,
        "the longest wave runs with the wind");

  // whitecaps follow the wind: none on a lake in a breeze, all by a gale
  bool rising = true;
  float prev = -1.f;
  for (float u = 0.f; u <= 20.f; u += 0.5f) {
    const float w = whitecap_share(u);
    rising &= w >= prev;
    prev = w;
  }
  check(rising, "whitecaps never lessen as the wind rises");
  check(whitecap_share(0.f) == 0.f && whitecap_share(4.f) < 0.03f &&
            whitecap_share(8.f) > 0.15f && whitecap_share(12.f) >= 0.999f,
        "no whitecaps in a breeze, a few at 8 m/s, all the breaking at 12 m/s");
}

void test_slopes_and_filter() {
  std::printf("  water: slopes are the surface's own...\n");
  Wave w[MAX_WAVES];
  build_waves(6.f, 0.8f, w);
  float ph[MAX_WAVES];
  rebase(w, MAX_WAVES, 0.0, 0.0, 17.25, ph);
  // waves shorter than 2 m are filtered out, so a 1 cm difference resolves
  // everything that is left
  const float minl = 1.f, h = 0.01f;
  float worst = 0.f;
  for (int j = 0; j < 7; ++j)
    for (int i = 0; i < 7; ++i) {
      const float x = -31.f + i * 9.7f, z = 12.f + j * 6.1f;
      Sample c, xp, xm, zp, zm;
      evaluate(w, ph, MAX_WAVES, x, z, minl, c);
      evaluate(w, ph, MAX_WAVES, x + h, z, minl, xp);
      evaluate(w, ph, MAX_WAVES, x - h, z, minl, xm);
      evaluate(w, ph, MAX_WAVES, x, z + h, minl, zp);
      evaluate(w, ph, MAX_WAVES, x, z - h, minl, zm);
      const float fdx[3] = {1.f + (xp.disp[0] - xm.disp[0]) / (2 * h),
                            (xp.disp[1] - xm.disp[1]) / (2 * h),
                            (xp.disp[2] - xm.disp[2]) / (2 * h)};
      const float fdz[3] = {(zp.disp[0] - zm.disp[0]) / (2 * h),
                            (zp.disp[1] - zm.disp[1]) / (2 * h),
                            1.f + (zp.disp[2] - zm.disp[2]) / (2 * h)};
      for (int k = 0; k < 3; ++k) {
        worst = std::max(worst, std::fabs(fdx[k] - c.dpdx[k]));
        worst = std::max(worst, std::fabs(fdz[k] - c.dpdz[k]));
      }
    }
  check(worst < 3e-3f, "the analytic tangents match the displacement differenced (worst " +
                           std::to_string(worst) + ")");

  float total = 0.f;
  for (int i = 0; i < MAX_WAVES; ++i) total += 0.5f * (w[i].amp * w[i].k) * (w[i].amp * w[i].k);
  Sample none;
  evaluate(w, ph, MAX_WAVES, 3.f, 4.f, 1.0e6f, none);
  check(std::fabs(none.disp[1]) < 1e-9f && std::fabs(none.lost - total) < 1e-5f * total + 1e-9f,
        "filtered to nothing, the surface is flat and its whole slope is returned as roughness");
  check(wave_weight(2.f, 1.f) == 1.f && wave_weight(1.f, 1.f) == 0.f &&
            std::fabs(wave_weight(1.5f, 1.f) - 0.5f) < 1e-6f && wave_weight(0.3f, 0.f) == 1.f,
        "a wave is whole at two samples a half-wave, gone at one");

  // the Gerstner limit: no wave folds over on its own
  bool fold = false;
  Wave chop[MAX_WAVES];
  build_waves(12.f, 1.f, chop);
  for (int i = 0; i < MAX_WAVES; ++i) fold |= chop[i].q * chop[i].amp * chop[i].k > 0.6f + 1e-4f;
  check(!fold, "a single wave never folds over");
  Wave round[MAX_WAVES];
  build_waves(12.f, 0.f, round);
  rebase(round, MAX_WAVES, 0.0, 0.0, 5.0, ph);
  float jmin = 2.f, jmax = 0.f;
  for (int i = 0; i < 40; ++i) {
    Sample s;
    evaluate(round, ph, MAX_WAVES, i * 3.3f, i * -1.7f, 0.f, s);
    jmin = std::min(jmin, jacobian(s));
    jmax = std::max(jmax, jacobian(s));
  }
  check(jmin == 1.f && jmax == 1.f, "round swells move the water up and down only");
  rebase(chop, MAX_WAVES, 0.0, 0.0, 5.0, ph);
  jmin = 2.f;
  for (int i = 0; i < 400; ++i) {
    Sample s;
    evaluate(chop, ph, MAX_WAVES, i * 0.37f, i * -0.21f, 0.f, s);
    jmin = std::min(jmin, jacobian(s));
  }
  check(jmin < 0.9f, "a choppy sea gathers in under its crests, where it foams");
}

void test_rebase() {
  std::printf("  water: rebasing keeps the sea where it is...\n");
  Wave w[MAX_WAVES];
  build_waves(5.f, 0.6f, w);
  const double ox = 40250.0, oz = -18750.0, t = 1234.5;
  float ph0[MAX_WAVES], ph1[MAX_WAVES];
  rebase(w, MAX_WAVES, 0.0, 0.0, t, ph0);
  rebase(w, MAX_WAVES, ox, oz, t, ph1);
  // the reference in double at the absolute position, every wave whole
  float worst = 0.f;
  for (int i = 0; i < 9; ++i) {
    const double x = ox + 3.25 * i, z = oz - 2.5 * i;
    double ref = 0.0;
    for (int k = 0; k < MAX_WAVES; ++k)
      ref += w[k].amp * std::sin(double(w[k].k) * (w[k].dx * x + w[k].dz * z) +
                                 double(w[k].phase) - double(w[k].omega) * t);
    Sample s;
    evaluate(w, ph1, MAX_WAVES, float(x - ox), float(z - oz), 0.f, s);
    worst = std::max(worst, float(std::fabs(ref - s.disp[1])));
  }
  check(worst < 2e-4f, "the sea near a far origin, rebased, is the sea itself (worst " +
                           std::to_string(worst) + " m)");
  bool wrapped = true;
  for (int k = 0; k < MAX_WAVES; ++k) wrapped &= ph1[k] >= 0.f && ph1[k] < 6.2832f;
  check(wrapped, "a rebased phase lies in 0..2pi");
}

void test_eye_param() {
  std::printf("  water: the eye's point over the flat world...\n");
  struct Case { const char *name; float R; gpx::planet::Shape S; };
  const Case cases[] = {{"globe", 1275.f, gpx::planet::shape_globe()},
                        {"small globe", 3.f, gpx::planet::shape_globe()},
                        {"dyson", 900.f, gpx::planet::shape_dyson()},
                        {"ring", 700.f, gpx::planet::shape_ring(true)},
                        {"flat", 1275.f, gpx::planet::shape_flat()}};
  for (const Case &c : cases) {
    double worst = 0.0;
    for (int j = 0; j < 5; ++j)
      for (int i = 0; i < 5; ++i) {
        const float u = -3.f + i * 1.9f, v = -2.f + j * 1.6f, h = 0.05f + 0.01f * i;
        float p[3];
        gpx::planet::sphere_place(u, v, h, c.R, c.S, p);
        double eu = 0.0, ev = 0.0;
        studio::water_eye_param(p, c.R, c.S, eu, ev);
        worst = std::max(worst, std::max(std::fabs(eu - u), std::fabs(ev - v)));
      }
    check(worst < 2e-3, std::string("the placement inverts on a ") + c.name + " (worst " +
                            std::to_string(worst) + ")");
  }
  float lx = 0.f, lz = 0.f;
  studio::water_param_limits(0.05f, gpx::planet::shape_globe(), lx, lz);
  check(std::fabs(lx - 0.5f) < 1e-5f && std::fabs(lz - 0.5f) < 1e-5f,
        "a globe smaller than the tile is wrapped by the tile's own square");
  studio::water_param_limits(1275.f, gpx::planet::shape_ring(true), lx, lz);
  check(std::fabs(lx - 3.14159265f * 1275.f) < 0.1f && lz > 1e8f,
        "a ring's flat world goes half way round along x and on for ever along z");
}

void test_clipmap() {
  std::printf("  water: the clipmap partitions the sea...\n");
  using namespace studio;
  int overlap = 0, uncovered = 0, hole = 0, seam = 0, samples = 0;
  const double eyes[][2] = {{0.5, 0.5}, {0.513, 0.4871}, {3.2177, -1.093}, {-12.4, 27.77}};
  for (const auto &e : eyes) {
    const double c0 = std::ldexp(1.0, -14);
    const double need = 31.5;
    WaterLevel lv[28];
    const int n = water_levels(e[0], e[1], c0, need, lv, 28);
    check(WATER_OWN * lv[n - 1].cell >= need, "enough levels to reach the ground's rim");
    // points spread over every scale, from a centimetre to the rim
    for (int k = 0; k < 4000; ++k) {
      const double r = c0 * std::pow(need / c0, (k % 97) / 96.0);
      const double a = k * 2.399963;
      const double x = e[0] + r * std::cos(a), z = e[1] + r * std::sin(a) * 0.93;
      ++samples;
      // FS_WATER's rule, level by level: inside its own (the last: its mesh)
      // and outside the finer level's own
      int drawn = 0, who = -1;
      for (int l = 0; l < n; ++l) {
        const double d = std::max(std::fabs(x - lv[l].ox), std::fabs(z - lv[l].oz));
        const double r_own = l + 1 < n ? WATER_OWN * lv[l].cell : WATER_GRID * 0.5 * lv[l].cell;
        bool draws = d < r_own;
        if (l > 0) {
          const double dp = std::max(std::fabs(x - lv[l - 1].ox), std::fabs(z - lv[l - 1].oz));
          draws = draws && dp >= WATER_OWN * lv[l - 1].cell;
        }
        if (draws) { ++drawn; who = l; }
      }
      if (std::max(std::fabs(x - e[0]), std::fabs(z - e[1])) > need) continue;
      if (drawn > 1) ++overlap;
      if (drawn == 0) ++uncovered;
      if (who < 0) continue;
      check(who == water_level_owner(lv, n, x, z) || drawn != 1,
            "the owner function agrees with the fragment rule");
      // the owner's mesh must be there: a ring leaves its middle open
      const double d = std::max(std::fabs(x - lv[who].ox), std::fabs(z - lv[who].oz));
      if (who > 0 && d <= WATER_HOLE * lv[who].cell) ++hole;
    }
    // the seams: on the rim a level owns it is wholly morphed, and the next
    // level, just outside the finer one's rim, not at all; every triangle
    // that straddles a seam has its vertices within a cell of it
    for (int l = 0; l + 1 < n; ++l) {
      const WaterLevel &L = lv[l], &C = lv[l + 1];
      for (int k = 0; k < 64; ++k) {
        const double t = -1.0 + 2.0 * k / 63.0;
        const double rim = WATER_OWN * L.cell;
        const double pts[4][2] = {{L.ox + rim, L.oz + t * rim}, {L.ox - rim, L.oz + t * rim},
                                  {L.ox + t * rim, L.oz + rim}, {L.ox + t * rim, L.oz - rim}};
        for (const auto &p : pts)
          for (double off : {-1.0, 0.0, 1.0}) {
            const double px = p[0] + off * L.cell, pz = p[1];
            if (water_morph(e[0], e[1], L, px, pz) < 1.0) ++seam;
            if (water_morph(e[0], e[1], C, px, pz) > 0.0 &&
                std::max(std::fabs(px - e[0]), std::fabs(pz - e[1])) <
                    (WATER_OWN / 2 + 2) * C.cell)
              ++seam;
          }
      }
    }
  }
  check(overlap == 0, "no point of the sea is drawn twice (" + std::to_string(overlap) + " of " +
                          std::to_string(samples) + ")");
  check(uncovered == 0, "no point within reach is left undrawn (" + std::to_string(uncovered) + ")");
  check(hole == 0, "no level owns a point its ring leaves open (" + std::to_string(hole) + ")");
  check(seam == 0, "both sides of every seam are the same lattice (" + std::to_string(seam) + ")");
}

void test_cache() {
  std::printf("  water: the settings' waves...\n");
  studio::RenderSettings rs;
  const studio::WaterWaves &a = studio::water_waves(rs);
  const float first = a.w[3].amp;
  float var = 0.f;
  for (int i = 0; i < a.n; ++i) var += 0.5f * (a.w[i].amp * a.w[i].k) * (a.w[i].amp * a.w[i].k);
  check(a.n == MAX_WAVES && std::fabs(a.slope_var - var) < 1e-6f,
        "the cache holds every wave and their whole slope variance");
  rs.water_wind_speed *= 2.f;
  check(studio::water_waves(rs).w[3].amp != first, "a changed wind rebuilds the waves");
  rs.water_wind_speed /= 2.f;
  check(studio::water_waves(rs).w[3].amp == first, "and changing it back gives the same sea");
}

} // namespace

int test_water_run() {
  g_fail = 0;
  test_physics();
  test_slopes_and_filter();
  test_rebase();
  test_eye_param();
  test_clipmap();
  test_cache();
  return g_fail;
}
