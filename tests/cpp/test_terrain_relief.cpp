// Geekatplay TerraForge - the micro-relief's CPU twin (studio/terrain_relief.hpp).
//
// What placement and the render bake rely on: the relief stays inside half its
// amount either way, it is the same number every time it is asked, a fading
// octave arrives without a jump, a small planet's cap applies, and the grid
// limit follows the shaders' "no wave shorter than two edges". What the
// pickers and the orbit pivot rely on: the octave count terrain_place draws a
// point with, by distance and triangle size.
#include "terrain_relief.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace studio;

namespace {

int g_fail = 0;
void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("  [FAIL] %s\n", what);
    ++g_fail;
  }
}

struct FakeSettings {
  float planet_radius = 0.f, fractal_detail = 0.0025f, fractal_scale = 90.f, fractal_gain = 0.5f;
};

} // namespace

int test_terrain_relief_run() {
  g_fail = 0;
  std::printf("terrain micro-relief...\n");
  const FakeSettings rs;
  const ReliefDials d = relief_dials(rs);
  check(d.amount == 0.0025f && d.scale == 90.f && d.gain == 0.5f, "the dials are the settings'");
  float lo = 1e9f, hi = -1e9f;
  bool same = true;
  for (int i = 0; i < 4000; ++i) {
    const float u = std::fmod(i * 0.61803399f, 1.f), v = std::fmod(i * 0.41421356f, 1.f);
    const float r = relief_at(u, v, d, RELIEF_NEAR_OCTAVES);
    lo = std::fmin(lo, r);
    hi = std::fmax(hi, r);
    same = same && r == relief_at(u, v, d, RELIEF_NEAR_OCTAVES);
  }
  check(lo >= -0.5f * d.amount - 1e-7f && hi <= 0.5f * d.amount + 1e-7f, "within half the amount either way");
  check(hi - lo > 0.3f * d.amount, "and it does vary");
  check(same, "the same point gives the same height");
  // a fading octave: just below an integer count is just about that count
  const float a = relief_at(0.37f, 0.61f, d, 3.9995f), b = relief_at(0.37f, 0.61f, d, 4.f);
  check(std::fabs(a - b) < 1e-3f * d.amount, "an octave fades in without a jump");
  check(relief_at(0.37f, 0.61f, d, 0.f) == 0.f, "no octaves, no relief");
  ReliefDials off = d;
  off.amount = 0.f;
  check(relief_at(0.37f, 0.61f, off, 9.f) == 0.f, "no amount, no relief");
  // a neighbour a hair away is a hair different: continuous, not a hash
  const float c0 = relief_at(0.5f, 0.5f, d, 2.f), c1 = relief_at(0.5f + 1e-6f, 0.5f, d, 2.f);
  check(std::fabs(c0 - c1) < 1e-3f * d.amount, "continuous across a small step");
  FakeSettings small;
  small.planet_radius = 0.01f;
  check(std::fabs(relief_dials(small).amount - 0.0005f) < 1e-9f, "capped at a twentieth of a small planet's radius");
  // the grid limit: 512 cells at base 90 carries about two and a half octaves
  const float oct = relief_octaves_for_grid(511.f, 90.f);
  check(oct > 2.f && oct < 3.f, "a 512 grid carries two to three octaves");
  check(relief_octaves_for_grid(1e6f, 90.f) == 9.f, "a fine grid carries them all");
  check(relief_octaves_for_grid(8.f, 90.f) == 0.f, "a coarse grid carries none");
  check(relief_octaves_for_grid(511.f, 90.f) == relief_octaves_for_edge(1.f / 511.f, 90.f),
        "a grid's limit is its cell's edge limit, to the bit");

  // One pass reads what terrain_place sums twice: the whole octaves, and the
  // one fading in, mixed. Bit for bit, or the bake would change under it.
  bool one_pass = true;
  for (int i = 0; i < 600; ++i) {
    const float u = std::fmod(i * 0.7548777f, 1.f), v = std::fmod(i * 0.5698403f, 1.f);
    const float of = std::fmod(i * 0.1234567f, 9.5f); // whole counts, fading ones, past the top
    const float c = std::clamp(of, 0.f, 9.f);
    const int o0 = (int)std::floor(c);
    const float ft = c - float(o0);
    float f = o0 > 0 ? relief_detail(u, v, d.scale, o0, d.gain) - 0.5f : 0.f;
    if (ft > 0.001f && o0 < 9) f += (relief_detail(u, v, d.scale, o0 + 1, d.gain) - 0.5f - f) * ft;
    one_pass = one_pass && relief_at(u, v, d, of) == f * d.amount;
  }
  check(one_pass, "one pass is the two sums terrain_place mixes, bit for bit");

  // gp_octavesf(dist, 9): 4 at a tile's distance, 0.9 more each halving, 0..9
  check(std::fabs(relief_octaves_at_distance(1.f) - 4.f) < 1e-6f, "four octaves a tile away");
  check(std::fabs(relief_octaves_at_distance(0.5f) - 4.9f) < 1e-5f, "0.9 more at half the distance");
  check(relief_octaves_at_distance(1e-3f) == 9.f && relief_octaves_at_distance(0.f) == 9.f,
        "every octave close by");
  check(relief_octaves_at_distance(1e4f) == 0.f, "none from far off");

  // terrain_place's choice. A 1080-line view at the free orbit's 0.9 rad, 8 px
  // edges: at 100 m (0.02 tile) distance allows all nine, the triangles 6.17.
  const float tri_k = relief_tri_k(0.9f, 1080, true, 8.f);
  check(std::fabs(tri_k - 2.f * std::tan(0.45f) / 1080.f * 8.f) < 1e-9f, "u_tri_k as the pass uploads it");
  check(relief_tri_k(0.9f, 1080, false, 30.f) == tri_k, "the fixed grid counts eight pixels an edge");
  check(relief_tri_k(0.9f, 540, true, 8.f) > tri_k * 1.999f, "half the lines, twice the edge");
  const float at100 = relief_view_octaves(0.02f, tri_k, 90.f);
  check(std::fabs(at100 - 6.167f) < 0.01f, "at 100 m the triangles hold about six octaves");
  check(at100 == relief_octaves_for_edge(0.02f * tri_k, 90.f), "and that is the edge limit there");
  check(relief_view_octaves(0.02f, 0.f, 90.f) == 9.f, "an orthographic view has no triangle limit");
  bool fewer_further = true;
  for (float dd = 1e-4f; dd < 50.f; dd *= 1.3f)
    fewer_further = fewer_further && relief_view_octaves(dd * 1.3f, tri_k, 90.f) <= relief_view_octaves(dd, tri_k, 90.f);
  check(fewer_further, "never more octaves further away");
  check(relief_view_octaves(1e6f, tri_k, 90.f) == 0.f, "none at all from orbit");

  // the finest wave drawn: the fading octave counts
  check(relief_finest_wave(0.f, 90.f) == 0.f, "no octaves, no wave");
  check(std::fabs(relief_finest_wave(1.f, 90.f) - 1.f / 90.f) < 1e-7f, "one octave: the base wave");
  check(relief_finest_wave(2.5f, 90.f) == relief_finest_wave(3.f, 90.f), "a fading octave is drawn");
  check(std::fabs(relief_finest_wave(9.f, 90.f) * 5000.f - 0.1925f) < 0.001f,
        "the ninth octave is 19 cm on a 5 km tile");
  return g_fail;
}
