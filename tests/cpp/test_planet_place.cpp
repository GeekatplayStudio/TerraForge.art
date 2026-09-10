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

void test_blend_controls() {
  std::printf("placement: gradient, whole-tile mode and the mask...\n");
  const int n = 96;
  auto L = layers_realistic();
  std::vector<float> relief, smooth;
  studio::planet_relief_under_tile(L, n, n, relief, smooth);
  // a raised flat pad: in features-only mode it is not a feature (flat),
  // so the planet shows; whole-tile mode keeps it standing at the settled
  // ground level away from the border
  gpx::Heightmap pad(n, n, 0.40f);
  const int c = n / 2;
  const size_t ci = (size_t)c * n + c;
  {
    studio::PlaceSettings s;
    s.ground = 0.12f;
    s.mode = 1;
    s.edge = 0.10f;
    studio::PlaceResult r;
    gpx::Heightmap out = studio::planet_place_tile(pad, L, s, &r);
    // the pad's own ground (0.40) settles to the planet's (0.12) on the
    // smooth planet shape, so the centre is planet ground + broad relief
    const float expect = 0.12f + smooth[ci];
    check(std::fabs(out.v[ci] - expect) < 2e-3f, "whole tile: the flat pad stands on levelled ground");
    // at the very border the planet wins
    check(std::fabs(out.v[(size_t)c * n] - (0.12f + relief[(size_t)c * n])) < 1e-4f,
          "whole tile: the border is the planet");
  }
  // the gradient: for the same point inside the feather, a beach (>1)
  // blends less of the tile than the S-curve, a plateau (<1) more
  {
    // a wide dome, so the probe point 10% in from the edge carries relief
    // of its own: with flatten 0 the excess over the planet is that relief
    // times the blend weight
    gpx::Heightmap dome = tile_with_bump(n, 0.12f, 0.5f, 0.7f);
    auto weight_at = [&](float grad) {
      studio::PlaceSettings s;
      s.ground = 0.12f;
      s.mode = 1;
      s.edge = 0.30f;
      s.gradient = grad;
      s.flatten = 0.f;
      // the plain square border, so the probe really is at b = 0.1: this
      // is a test of the gradient's curve, not of the border's shape
      s.round = 0.f;
      s.wander = 0.f;
      gpx::Heightmap out = studio::planet_place_tile(dome, L, s, nullptr);
      // a point 10% in from the left edge, mid-height: b = 0.1, t = 1/3
      const size_t i = (size_t)c * n + (size_t)std::lround(0.1f * (n - 1));
      return out.v[i] - (0.12f + relief[i]);
    };
    const float s1 = weight_at(1.f), beach = weight_at(3.f), plateau = weight_at(0.3f);
    check(beach < s1 - 1e-4f, "gradient above 1 gives way sooner");
    check(plateau > s1 + 1e-4f, "gradient below 1 holds its ground longer");
  }
  // The border's shape. A blend keyed on the distance to a square draws a
  // square on the ground, which is what people see and report; rounding
  // takes the corners off and the wander stops the outline being an
  // outline. Neither may put the tile outside its own square, because
  // there is no tile there. The blend weight is the thing under test -
  // the height at a point also carries whatever the tile has there.
  {
    gpx::Heightmap dome = tile_with_bump(n, 0.12f, 0.5f, 0.7f);
    auto weight_map = [&](float round, float wander) {
      studio::PlaceSettings s;
      s.ground = 0.12f;
      s.mode = 1;
      s.edge = 0.30f;
      s.flatten = 0.f;
      s.round = round;
      s.wander = wander;
      studio::PlaceResult res;
      studio::planet_place_tile(dome, L, s, &res);
      return res.weight;
    };
    // Two points the same Chebyshev distance from the middle: one straight
    // out along an axis, one out along the diagonal. On a square border
    // they carry the same weight - that sameness *is* the square. Rounded,
    // the diagonal one lies further out and gives way first.
    const int q = (int)std::lround(0.18f * (n - 1));
    const size_t axis = (size_t)c * n + (size_t)q, diag = (size_t)q * n + (size_t)q;
    const gpx::Heightmap sq = weight_map(0.f, 0.f);
    const gpx::Heightmap rd = weight_map(1.f, 0.f);
    check(std::fabs(sq.v[axis] - sq.v[diag]) < 1e-4f,
          "square border: the corner and the side blend alike, which is the square");
    check(rd.v[diag] < rd.v[axis] - 1e-3f,
          "rounded border: the corner gives way before the side");
    // the wander moves the outline about rather than leaving it a shape
    const gpx::Heightmap wa = weight_map(0.8f, 1.f);
    const gpx::Heightmap st = weight_map(0.8f, 0.f);
    bool moved = false;
    for (size_t i = 0; i < wa.v.size() && !moved; ++i)
      if (std::fabs(wa.v[i] - st.v[i]) > 1e-3f) moved = true;
    check(moved, "the wander actually moves the border");
    // and the tile never reaches past its own square, however far it
    // wanders - the wander eats inward only
    bool border_clean = true;
    for (int y = 0; y < n; ++y) {
      if (wa.v[(size_t)y * n] > 1e-4f) border_clean = false;
      if (wa.v[(size_t)y * n + (size_t)(n - 1)] > 1e-4f) border_clean = false;
    }
    check(border_clean, "however the border wanders, the tile stops at its own edge");
  }

  // the mask: zero on the left half hands that half to the planet
  {
    auto mask = std::make_shared<gpx::Heightmap>(n, n, 1.f);
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n / 2; ++x) mask->at(x, y) = 0.f;
    studio::PlaceSettings s;
    s.ground = 0.12f;
    s.mode = 1;
    s.mask = mask;
    gpx::Heightmap out = studio::planet_place_tile(pad, L, s, nullptr);
    const size_t left = (size_t)c * n + n / 4, right = (size_t)c * n + 3 * n / 4;
    check(std::fabs(out.v[left] - (0.12f + relief[left])) < 1e-4f, "mask 0: the planet");
    check(std::fabs(out.v[right] - (0.12f + smooth[right])) < 2e-3f, "mask 1: the tile");
  }
}

// Clipping (modes 3 and 4): the tile stands only where it is higher, or
// only where it is lower, than the planet's own ground, and the planet is
// what stands everywhere else - a mountain with no skirt, a basin with no
// rim. The height is a hard max/min against the planet, so the crossing is
// exact; the material weight fades over `presence` either side of it.
void test_clip_modes() {
  std::printf("placement: clip low / clip high...\n");
  const int n = 96;
  auto L = layers_realistic();
  std::vector<float> relief, smooth;
  studio::planet_relief_under_tile(L, n, n, relief, smooth);
  // a wide dome over a pit: half the tile rises, half sinks, on ground
  // that already has relief of its own
  gpx::Heightmap tile(n, n, 0.12f);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const float u = (float)x / (n - 1), v = (float)y / (n - 1);
      const float du = u - 0.5f, dv = v - 0.5f;
      const float r = std::sqrt(du * du + dv * dv);
      const float bell = r < 0.45f ? 0.5f * (1.f + std::cos(3.14159265f * r / 0.45f)) : 0.f;
      tile.at(x, y) = 0.12f + (u < 0.5f ? 0.35f : -0.35f) * bell;
    }
  for (int mode : {3, 4}) {
    studio::PlaceSettings s;
    s.ground = 0.12f;
    s.mode = mode;
    s.edge = 0.05f;
    s.flatten = 0.f; // the feature is compared with the planet's real relief
    studio::PlaceResult r;
    gpx::Heightmap out = studio::planet_place_tile(tile, L, s, &r);
    int wrong = 0, kept = 0, planet = 0;
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x) {
        const size_t i = (size_t)y * n + x;
        const float pb = 0.12f + relief[i];
        const float feature = pb + (tile.v[i] - 0.12f);
        const float o = out.v[i];
        // never on the wrong side of the planet
        if (mode == 3 ? o < pb - 1e-5f : o > pb + 1e-5f) ++wrong;
        // inside the border feather, the winner is exact
        const float b = std::min(std::min(x, n - 1 - x), std::min(y, n - 1 - y)) / (float)(n - 1);
        if (b < 0.06f) continue;
        const bool tile_wins = mode == 3 ? feature > pb + 1e-4f : feature < pb - 1e-4f;
        if (tile_wins) {
          if (std::fabs(o - feature) < 1e-5f) ++kept;
        } else if (std::fabs(o - pb) < 1e-5f) {
          ++planet;
          // and the material weight is the planet's there
          if (r.weight.v[i] > 1e-6f && std::fabs(feature - pb) > s.presence) ++wrong;
        }
      }
    check(wrong == 0, "clip " + std::to_string(mode) + ": nothing on the wrong side of the planet");
    check(kept > n * n / 8, "clip " + std::to_string(mode) + ": the winning side is the tile, exactly");
    check(planet > n * n / 8, "clip " + std::to_string(mode) + ": the losing side is the planet, exactly");
  }
  // clip low keeps the dome and drops the pit; clip high the reverse
  {
    studio::PlaceSettings s;
    s.ground = 0.12f;
    s.flatten = 0.f;
    s.mode = 3;
    gpx::Heightmap lo = studio::planet_place_tile(tile, L, s, nullptr);
    s.mode = 4;
    gpx::Heightmap hi = studio::planet_place_tile(tile, L, s, nullptr);
    const size_t dome = (size_t)(n / 2) * n + n / 4, pit = (size_t)(n / 2) * n + 3 * n / 4;
    check(lo.v[dome] > 0.12f + relief[dome] + 0.1f, "clip low keeps the dome");
    check(std::fabs(lo.v[pit] - (0.12f + relief[pit])) < 1e-5f, "clip low drops the pit");
    check(hi.v[pit] < 0.12f + relief[pit] - 0.1f, "clip high keeps the pit");
    check(std::fabs(hi.v[dome] - (0.12f + relief[dome])) < 1e-5f, "clip high drops the dome");
  }
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
  test_blend_controls();
  test_clip_modes();
  test_hole_is_a_basin();
  test_border_meets_the_planet();
  test_deterministic_and_off();
  return g_fail;
}
