// Geekatplay TerraForge - the terrain horizon.
//
// The convex-hull sweep is exact, which is a strong claim and the reason this
// file exists: the same question is asked of a brute-force O(n^2) scan over
// the same cells, and the two have to agree to the float. An approximation
// that is nearly right on random terrain is easy to write by accident, and it
// fails on precisely the case the node is for - one thin ridge, far away,
// that a sampled ray steps over.
#include "gpx/horizon.hpp"
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static std::string g_case;

static void check(bool ok, const std::string &what) {
  if (ok) return;
  std::printf("  FAIL [%s] %s\n", g_case.c_str(), what.c_str());
  ++failures;
}

static uint32_t rng = 7u;
static float rnd() {
  rng = rng * 1664525u + 1013904223u;
  return (float)(rng >> 8) / 16777216.f;
}

// The same question, asked the slow and obvious way.
static void brute_force_line(const float *z, const float *t, int n,
                             float *out) {
  for (int i = 0; i < n; ++i) {
    float best = 0.f;
    for (int j = i + 1; j < n; ++j)
      best = std::max(best, (z[j] - z[i]) / (t[j] - t[i]));
    out[i] = best;
  }
}

int main() {
  std::printf("Geekatplay TerraForge - terrain horizon\n\n");

  // ---- the sweep agrees with brute force, on every shape ---------------
  {
    g_case = "sweep vs brute force";
    int worst_n = 0;
    double worst = 0.0;
    for (int trial = 0; trial < 400; ++trial) {
      const int n = 2 + (int)(rnd() * 60);
      std::vector<float> z(n), t(n), a(n), b(n);
      for (int i = 0; i < n; ++i) {
        t[i] = (float)i;
        // a mixture of shapes, because a hull bug can hide in any one of
        // them: noise, a single spike, a ramp, a bowl
        switch (trial % 4) {
          case 0: z[i] = rnd() * 10.f; break;
          case 1: z[i] = (i == n / 2) ? 20.f : 0.f; break;
          case 2: z[i] = (float)i * 0.3f; break;
          default: z[i] = (float)((i - n / 2) * (i - n / 2)) * 0.05f;
        }
      }
      std::vector<int> stack;
      gpx::sweep_line(z.data(), t.data(), n, a.data(), stack);
      brute_force_line(z.data(), t.data(), n, b.data());
      for (int i = 0; i < n; ++i) {
        double d = std::fabs((double)a[i] - b[i]);
        if (d > worst) {
          worst = d;
          worst_n = n;
        }
      }
    }
    check(worst < 1e-5, "exact over 400 random lines, worst error " +
                            std::to_string(worst) + " at length " +
                            std::to_string(worst_n));
  }

  // ---- a lone far ridge is not stepped over ----------------------------
  //
  // This is the case a marched ray gets wrong, and the reason for the hull.
  {
    g_case = "one thin far ridge";
    const int n = 500;
    std::vector<float> z(n, 0.f), t(n), out(n);
    for (int i = 0; i < n; ++i) t[i] = (float)i;
    z[400] = 50.f; // one cell, four hundred away
    std::vector<int> stack;
    gpx::sweep_line(z.data(), t.data(), n, out.data(), stack);
    check(std::fabs(out[0] - 50.f / 400.f) < 1e-6f,
          "seen from the near end, got " + std::to_string(out[0]));
    check(std::fabs(out[399] - 50.f) < 1e-6f, "and from right beside it");
    check(out[401] == 0.f, "but not from behind it");
  }

  // ---- flat ground has no horizon --------------------------------------
  {
    g_case = "flat";
    gpx::Heightmap flat(64, 64);
    std::fill(flat.v.begin(), flat.v.end(), 0.5f);
    gpx::Heightmap z = gpx::in_cell_units(flat, 0.25f);
    gpx::Heightmap hz(64, 64);
    gpx::horizon_pass(z, 1.f, 0.f, hz);
    float mx = 0.f;
    for (float v : hz.v) mx = std::max(mx, v);
    check(mx == 0.f, "nothing rises above it, max horizon " +
                         std::to_string(mx));
  }

  // ---- a wall shadows one side and not the other -----------------------
  {
    g_case = "a wall";
    const int W = 64;
    gpx::Heightmap t(W, W);
    for (int y = 0; y < W; ++y)
      for (int x = 0; x < W; ++x) t.at(x, y) = x >= W / 2 ? 1.f : 0.f;
    gpx::Heightmap z = gpx::in_cell_units(t, 0.5f);
    gpx::Heightmap hz(W, W);
    gpx::horizon_pass(z, 1.f, 0.f, hz); // looking east, towards the wall
    check(hz.at(W / 2 - 1, 10) > 1.f, "the cell against the wall's foot is "
                                      "shut in, got " +
                                          std::to_string(hz.at(W / 2 - 1, 10)));
    check(hz.at(W - 1, 10) == 0.f, "the far side of the wall sees out");
    gpx::horizon_pass(z, -1.f, 0.f, hz); // looking west, away from the wall
    check(hz.at(W / 2 - 1, 10) == 0.f,
          "and from the same cell, looking the other way, nothing");
  }

  // ---- a spike at a known distance subtends a known angle --------------
  //
  // The one place a direction's obliqueness can be dropped. Stepping one cell
  // along x when the ray points north-east is 1.41 cells along the ray, not
  // one, and getting that wrong overstates every diagonal horizon by 41% -
  // invisible on a random terrain and obvious here.
  {
    g_case = "a spike at a known distance";
    const int W = 65;
    gpx::Heightmap t(W, W);
    t.at(48, 48) = 1.f; // one cell, tall
    gpx::Heightmap z = gpx::in_cell_units(t, 0.25f); // 1.0 -> 0.25*65
    const float peak = 0.25f * W;
    gpx::Heightmap hz(W, W);

    gpx::horizon_pass(z, 1.f, 0.f, hz); // due east along row 48
    const float want_e = peak / (48 - 16);
    check(std::fabs(hz.at(16, 48) - want_e) < 1e-3f,
          "due east: expected " + std::to_string(want_e) + ", got " +
              std::to_string(hz.at(16, 48)));

    const float d = 0.70710678f;
    gpx::horizon_pass(z, d, d, hz); // north-east, through (16,16)
    const float want_d = peak / (32.f * 1.41421356f);
    check(std::fabs(hz.at(16, 16) - want_d) < 2e-3f,
          "diagonal: expected " + std::to_string(want_d) + ", got " +
              std::to_string(hz.at(16, 16)) +
              " - a diagonal step is 1.41 cells, not 1");
  }

  // ---- the direction has a sign ----------------------------------------
  //
  // Looking west is not looking east. An off-centre spike is what tells them
  // apart; a wall in the middle of the tile does not, because the answer is
  // the same on both sides of it.
  {
    g_case = "direction has a sign";
    const int W = 64;
    gpx::Heightmap t(W, W);
    t.at(5, 10) = 1.f;
    gpx::Heightmap z = gpx::in_cell_units(t, 0.25f);
    gpx::Heightmap hz(W, W);
    gpx::horizon_pass(z, -1.f, 0.f, hz); // looking west, towards the spike
    check(hz.at(20, 10) > 0.f, "a cell east of the spike sees it, got " +
                                   std::to_string(hz.at(20, 10)));
    check(hz.at(2, 10) == 0.f, "a cell west of it does not");
    gpx::horizon_pass(z, 1.f, 0.f, hz); // looking east, away from it
    check(hz.at(20, 10) == 0.f, "and looking the other way, neither does it");
    check(hz.at(2, 10) > 0.f, "while the cell behind it now does");
  }

  // ---- the bottom of a bowl sees the same rim whichever way it looks ----
  //
  // Nothing about a round bowl distinguishes north from north-east, so
  // nothing about the horizon from its lowest point should either. Measured
  // at the centre, not averaged over the tile: a square tile is genuinely
  // longer along its diagonal, so a whole-tile mean is anisotropic for
  // honest reasons and would prove nothing.
  //
  // This is what catches a line decomposition that drifts off its ray, or
  // that drifts one way going east and the other going west.
  {
    g_case = "isotropy";
    const int W = 129; // odd, so there is a single centre cell
    const int c = W / 2;
    gpx::Heightmap t(W, W);
    const float R = W * 0.35f;
    for (int y = 0; y < W; ++y)
      for (int x = 0; x < W; ++x) {
        const float dx = x - (float)c, dy = y - (float)c;
        t.at(x, y) = std::min(1.f, std::sqrt(dx * dx + dy * dy) / R);
      }
    gpx::Heightmap z = gpx::in_cell_units(t, 0.3f);
    gpx::Heightmap hz(W, W);
    float lo = 1e9f, hi = -1e9f;
    for (int d = 0; d < 32; ++d) {
      const float a = 2.f * 3.14159265f * d / 32.f;
      gpx::horizon_pass(z, std::cos(a), std::sin(a), hz);
      lo = std::min(lo, hz.at(c, c));
      hi = std::max(hi, hz.at(c, c));
    }
    check(hi > 0.f && (hi - lo) / hi < 0.03f,
          "32 directions agree within 3%, spread " + std::to_string(lo) +
              " to " + std::to_string(hi));
  }

  // ---- every cell is visited exactly once, in every direction ----------
  //
  // The line decomposition is the part most likely to go quietly wrong: a
  // missed cell keeps whatever was in the buffer, and on a smooth terrain
  // that looks plausible.
  {
    g_case = "coverage";
    const int W = 37; // deliberately not a power of two
    gpx::Heightmap probe(W, W);
    for (int i = 0; i < (int)probe.v.size(); ++i) probe.v[i] = rnd();
    gpx::Heightmap z = gpx::in_cell_units(probe, 0.25f);
    for (int d = 0; d < 16; ++d) {
      const float a = 2.f * 3.14159265f * d / 16.f;
      gpx::Heightmap out(W, W);
      const float sentinel = -12345.f;
      std::fill(out.v.begin(), out.v.end(), sentinel);
      gpx::horizon_pass(z, std::cos(a), std::sin(a), out);
      int untouched = 0;
      for (float v : out.v)
        if (v == sentinel) ++untouched;
      check(untouched == 0, "direction " + std::to_string(d) + " left " +
                                std::to_string(untouched) + " cells unvisited");
    }
  }

  // ---- the same terrain, twice, is the same answer ---------------------
  {
    g_case = "deterministic";
    const int W = 96;
    gpx::Heightmap t(W, W);
    for (int i = 0; i < (int)t.v.size(); ++i) t.v[i] = rnd();
    gpx::Heightmap z = gpx::in_cell_units(t, 0.3f);
    gpx::Heightmap a(W, W), b(W, W);
    gpx::horizon_pass(z, 0.6f, 0.8f, a);
    gpx::horizon_pass(z, 0.6f, 0.8f, b);
    check(a.v == b.v, "run twice, bit for bit");
  }

  // ---- a taller terrain has a higher horizon ---------------------------
  //
  // in_cell_units is what makes the angle mean something. If it ever stops
  // scaling, this is what notices.
  {
    g_case = "vertical scale";
    const int W = 64;
    gpx::Heightmap t(W, W);
    for (int y = 0; y < W; ++y)
      for (int x = 0; x < W; ++x) t.at(x, y) = x >= W / 2 ? 1.f : 0.f;
    gpx::Heightmap lo(W, W), hi(W, W);
    gpx::horizon_pass(gpx::in_cell_units(t, 0.1f), 1.f, 0.f, lo);
    gpx::horizon_pass(gpx::in_cell_units(t, 0.8f), 1.f, 0.f, hi);
    check(hi.at(0, 10) > lo.at(0, 10) * 3.f,
          "eight times the relief is a far higher horizon (" +
              std::to_string(lo.at(0, 10)) + " -> " +
              std::to_string(hi.at(0, 10)) + ")");
  }

  // ---- and the same terrain at two resolutions agrees ------------------
  //
  // A mask authored at 512 has to survive being rendered at 2048. Scaling by
  // the tile's width rather than by the cell is what buys that.
  {
    g_case = "resolution independence";
    auto cone = [](int W, int x, int y) {
      float dx = (x + 0.5f) / W - 0.5f, dy = (y + 0.5f) / W - 0.5f;
      return 1.f - std::sqrt(dx * dx + dy * dy) * 2.f;
    };
    float got[2];
    const int sizes[2] = {64, 256};
    for (int s = 0; s < 2; ++s) {
      const int W = sizes[s];
      gpx::Heightmap t(W, W);
      for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) t.at(x, y) = cone(W, x, y);
      gpx::Heightmap hz(W, W);
      gpx::horizon_pass(gpx::in_cell_units(t, 0.25f), 1.f, 0.f, hz);
      got[s] = hz.at(W / 8, W / 2); // well outside the cone, looking at it
    }
    const float rel = std::fabs(got[0] - got[1]) / std::max(got[0], 1e-6f);
    check(rel < 0.05f, "64 and 256 agree within 5%, got " +
                           std::to_string(got[0]) + " and " +
                           std::to_string(got[1]));
  }

  if (failures)
    std::printf("\n%d failure(s)\n", failures);
  else
    std::printf("\nall passed\n");
  return failures ? 1 : 0;
}
