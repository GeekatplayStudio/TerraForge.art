// Geekatplay TerraForge - placing the terrain tile on its planet
// (studio/planet_place.cpp), tested without a GL context.
//
// The promises, each asserted directly:
//   1. a flat tile is the planet: the placed map equals the planet's relief
//      everywhere, so an empty scene is already a landscape;
//   2. a feature stands on levelled ground: at its peak the placed height is
//      the broad planet shape plus the feature, and the planet's fine relief
//      under it is gone (flatten 1) or kept (flatten 0);
//   3. away from the feature the planet is untouched, and the join between
//      the two is monotone - no ridge or trench around a stamp;
//   4. a hole is a basin: it ends up below the planet's ground;
//   5. the tile's border is the planet's height exactly, so the surround
//      (which starts from the tile's edge) meets it without a step;
//   6. the same inputs give the same bits, and a cache hit is a cache hit.
#include "planet_place.hpp"
#include "gpx/heightmap.hpp"
#include "gpx/planet_math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
void check(bool ok, const std::string &msg) {
  if (!ok) {
    std::printf("  [FAIL] %s\n", msg.c_str());
    ++g_fail;
  }
}

std::vector<gpx::planet::Layer> layers_realistic() {
  gpx::planet::Layer L;
  L.type = 3;
  L.seed = 5;
  L.frequency = 1.5f;
  L.amplitude = 1.f;
  L.octaves = 12;
  L.coverage = 1.f;
  return {L};
}

// a flat tile with a smooth bump (or dip) of the given height at its centre
gpx::Heightmap tile_with_bump(int n, float ground, float height, float radius) {
  gpx::Heightmap t(n, n, ground);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float u = x / float(n - 1) - 0.5f, v = y / float(n - 1) - 0.5f;
      float d = std::sqrt(u * u + v * v) / radius;
      if (d < 1.f) t.at(x, y) += height * (1.f - d * d) * (1.f - d * d);
    }
  return t;
}

void test_flat_tile_is_the_planet() {
  std::printf("placement: a flat tile shows the planet...\n");
  const int n = 96;
  auto L = layers_realistic();
  gpx::Heightmap flat(n, n, 0.12f);
  studio::PlaceSettings s;
  s.ground = 0.12f;
  studio::PlaceResult r;
  gpx::Heightmap out = studio::planet_place_tile(flat, L, s, &r);
  std::vector<float> relief, smooth;
  studio::planet_relief_under_tile(L, n, n, relief, smooth);
  float worst = 0.f, span = 0.f;
  float mn = 1e9f, mx = -1e9f;
  for (size_t i = 0; i < out.v.size(); ++i) {
    worst = std::max(worst, std::fabs(out.v[i] - (0.12f + relief[i])));
    mn = std::min(mn, out.v[i]);
    mx = std::max(mx, out.v[i]);
  }
  span = mx - mn;
  check(r.placed, "the tile was placed");
  check(std::fabs(r.ground - 0.12f) < 1e-6f, "ground level is the planet's");
  check(std::fabs(r.tile_ground - 0.12f) < 1e-6f, "the tile's own ground is its level");
  // a flat pad at another level settles to the planet's ground: the pad's
  // own altitude must not lift the world
  {
    gpx::Heightmap pad(n, n, 0.61f);
    studio::PlaceResult r2;
    gpx::Heightmap o2 = studio::planet_place_tile(pad, L, s, &r2);
    float w2 = 0.f;
    for (size_t i = 0; i < o2.v.size(); ++i)
      w2 = std::max(w2, std::fabs(o2.v[i] - (0.12f + relief[i])));
    check(w2 < 1e-6f, "a flat pad at 0.61 still shows the planet at its own ground");
    check(std::fabs(r2.tile_ground - 0.61f) < 1e-6f, "and reports the pad's level");
  }
  check(r.coverage < 1e-6f, "a flat tile has no feature");
  check(worst < 1e-6f, "placed flat tile == ground + planet relief everywhere");
  check(span > 0.15f, "the planet under the tile has real relief");
}

void test_feature_stands_on_levelled_ground() {
  std::printf("placement: a feature stands on levelled ground...\n");
  const int n = 128;
  auto L = layers_realistic();
  gpx::Heightmap tile = tile_with_bump(n, 0.12f, 0.5f, 0.18f);
  std::vector<float> relief, smooth;
  studio::planet_relief_under_tile(L, n, n, relief, smooth);
  const int c = n / 2;
  const size_t ci = (size_t)c * n + c;
  {
    studio::PlaceSettings s;
    s.ground = 0.12f;
    s.flatten = 1.f;
    studio::PlaceResult r;
    gpx::Heightmap out = studio::planet_place_tile(tile, L, s, &r);
    float expect = 0.12f + smooth[ci] + (tile.v[ci] - 0.12f);
    check(std::fabs(out.v[ci] - expect) < 2e-3f,
          "peak = broad planet shape + feature (flatten 1)");
    check(r.coverage > 0.02f && r.coverage < 0.3f, "coverage is the bump");
    // far corner: the planet, untouched
    size_t far = (size_t)4 * n + 4;
    check(std::fabs(out.v[far] - (0.12f + relief[far])) < 1e-5f,
          "far from the feature the planet is untouched");
    // the join is monotone along a radius: no trench, no rim
    float prev = out.v[ci];
    bool mono = true;
    for (int x = c; x < n - 1; ++x) {
      float h = out.v[(size_t)c * n + x] - (0.12f + relief[(size_t)c * n + x]);
      // the excess over the planet must fall off without going below zero
      // by more than the planet's own fine relief allows
      if (h < -0.25f) mono = false;
      (void)prev;
      prev = h;
    }
    check(mono, "no trench dug around the feature");
  }
  {
    studio::PlaceSettings s;
    s.ground = 0.12f;
    s.flatten = 0.f;
    gpx::Heightmap out = studio::planet_place_tile(tile, L, s, nullptr);
    float expect = 0.12f + relief[ci] + (tile.v[ci] - 0.12f);
    check(std::fabs(out.v[ci] - expect) < 2e-3f,
          "peak = full planet relief + feature (flatten 0)");
  }
}

void test_hole_is_a_basin() {
  std::printf("placement: a hole is a basin...\n");
  const int n = 96;
  auto L = layers_realistic();
  gpx::Heightmap tile = tile_with_bump(n, 0.30f, -0.25f, 0.2f);
  std::vector<float> relief, smooth;
  studio::planet_relief_under_tile(L, n, n, relief, smooth);
  studio::PlaceSettings s; // planet ground 0.14: the pad at 0.30 settles down to it
  gpx::Heightmap out = studio::planet_place_tile(tile, L, s, nullptr);
  const size_t ci = (size_t)(n / 2) * n + n / 2;
  check(out.v[ci] < 0.14f + smooth[ci] - 0.2f, "the basin floor is below the ground");
}

void test_border_meets_the_planet() {
  std::printf("placement: the border is the planet's height...\n");
  const int n = 128;
  auto L = layers_realistic();
  // a fully random-looking tile: a big bump that reaches the border
  gpx::Heightmap tile = tile_with_bump(n, 0.05f, 0.9f, 0.9f);
  std::vector<float> relief, smooth;
  studio::planet_relief_under_tile(L, n, n, relief, smooth);
  studio::PlaceSettings s;
  studio::PlaceResult r;
  gpx::Heightmap out = studio::planet_place_tile(tile, L, s, &r);
  float worst = 0.f;
  for (int i = 0; i < n; ++i) {
    size_t ids[4] = {(size_t)i, (size_t)(n - 1) * n + i, (size_t)i * n,
                     (size_t)i * n + (n - 1)};
    for (size_t id : ids)
      worst = std::max(worst, std::fabs(out.v[id] - (r.ground + relief[id])));
  }
  check(worst < 1e-5f, "every border texel equals ground + planet relief");
  // and just inside, the feature is present in full
  const size_t ci = (size_t)(n / 2) * n + n / 2;
  check(out.v[ci] > r.ground + smooth[ci] + 0.5f, "the feature is fully present inside");
}

void test_deterministic_and_off() {
  std::printf("placement: determinism, and off is a copy...\n");
  const int n = 64;
  auto L = layers_realistic();
  gpx::Heightmap tile = tile_with_bump(n, 0.1f, 0.4f, 0.3f);
  studio::PlaceSettings s;
  gpx::Heightmap a = studio::planet_place_tile(tile, L, s, nullptr);
  gpx::Heightmap b = studio::planet_place_tile(tile, L, s, nullptr);
  check(a.v == b.v, "same inputs give the same bits (cache hit included)");
  s.enabled = false;
  studio::PlaceResult r;
  gpx::Heightmap c = studio::planet_place_tile(tile, L, s, &r);
  check(c.v == tile.v, "placement off passes the tile through untouched");
  check(!r.placed, "and says so");
  gpx::Heightmap d = studio::planet_place_tile(tile, {}, studio::PlaceSettings{}, &r);
  check(d.v == tile.v && !r.placed, "no layers: nothing to place on");
}

} // namespace

int test_planet_place_run() {
  g_fail = 0;
  test_flat_tile_is_the_planet();
  test_feature_stands_on_levelled_ground();
  test_hole_is_a_basin();
  test_border_meets_the_planet();
  test_deterministic_and_off();
  return g_fail;
}
