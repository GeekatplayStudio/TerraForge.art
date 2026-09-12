// Geekatplay TerraForge - the ground march (studio/ground_march.hpp).
//
// What the sculpt brush and a click on the terrain rely on: a ray lands on the
// ground the viewport draws - the heightmap and the micro-relief over it, at
// the octaves a view draws each point with - where a walk fine enough to see
// every wave lands it, rather than metres off where the heightmap alone puts
// it; the point it reports is on that ground; a ray that points away finds
// nothing; with the relief off it lands on the heightmap; and it gets there
// in a bounded number of probes, which a brush asking once a frame pays for.
#include "ground_march.hpp"
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

// Hills over a tile, in the flat world's units, under the default relief as a
// 1080-line view at the free orbit's lens draws it.
struct Ground {
  float eye[3] = {0.f, 0.f, 0.f}, dir[3] = {0.f, 0.f, 1.f};
  ReliefDials dials{0.0025f, 90.f, 0.5f}; // the default settings: +-6 m on a 5 km tile
  float tri_k = relief_tri_k(0.9f, 1080, true, 8.f);
  int probes = 0, fine_probes = 0; // every probe, and those that paid for the relief
  float hills = 0.02f;

  float bare(float x, float z) const { return 0.05f + hills * std::sin(x * 9.f) * std::cos(z * 7.f); }
  float relief(float x, float z) const {
    const float b = bare(x, z);
    const float dx = eye[0] - x, dy = eye[1] - b, dz = eye[2] - z;
    return relief_at(x, z, dials, relief_view_octaves(std::sqrt(dx * dx + dy * dy + dz * dz), tri_k, dials.scale));
  }
  // the ray's height above the drawn ground at t, and above the band's top
  float gap(float t) const {
    const float x = eye[0] + dir[0] * t, z = eye[2] + dir[2] * t;
    return eye[1] + dir[1] * t - (bare(x, z) + relief(x, z));
  }
  float gap_top(float t) const {
    const float x = eye[0] + dir[0] * t, z = eye[2] + dir[2] * t;
    return eye[1] + dir[1] * t - (bare(x, z) + 0.5f * dials.amount);
  }
  GroundProbe operator()(float t, bool fine) {
    ++probes;
    GroundProbe p;
    const float x = eye[0] + dir[0] * t, y = eye[1] + dir[1] * t, z = eye[2] + dir[2] * t;
    if (x < 0.f || x > 1.f || z < 0.f || z > 1.f) {
      p.where = y < -0.5f ? -1 : 0;
      return p;
    }
    p.where = 1;
    p.top = gap_top(t);
    if (!fine) return p;
    ++fine_probes;
    p.drawn = gap(t);
    const float b = bare(x, z);
    const float dx = eye[0] - x, dy = eye[1] - b, dz = eye[2] - z;
    const float of = relief_view_octaves(std::sqrt(dx * dx + dy * dy + dz * dz), tri_k, dials.scale);
    const float wave = relief_finest_wave(of, dials.scale);
    const float hlen = std::max(std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]), 1e-3f);
    p.step = wave > 0.f ? 0.25f * wave / hlen : 1e30f;
    return p;
  }
};

// The first crossing, walked two centimetres at a time wherever the ray is in
// reach of the relief. Above half its amount nothing can be crossed, and the
// band can come no closer than the ray falls plus the hills rise (their
// slope is under 16 x hills along any ray), so up there it moves by that.
bool walked(const Ground &g, float t_max, float &t_hit) {
  const double fine = 4e-6;
  const float closing = std::fabs(g.dir[1]) + 16.f * g.hills + 1e-3f;
  float prev = 1.f;
  for (double t = fine; t < t_max;) {
    const float x = g.eye[0] + g.dir[0] * float(t), z = g.eye[2] + g.dir[2] * float(t);
    if (x < 0.f || x > 1.f || z < 0.f || z > 1.f) return false;
    const float top = g.gap_top(float(t));
    if (top > 0.f) {
      prev = 1.f;
      t += std::max(fine, 0.9 * double(top / closing));
      continue;
    }
    const float cur = g.gap(float(t));
    if (prev > 0.f && cur <= 0.f) {
      t_hit = float(t - fine * double(cur / (cur - prev)));
      return true;
    }
    prev = cur;
    t += fine;
  }
  return false;
}

// How far under the drawn ground the ray goes between two distances.
float deepest(const Ground &g, float a, float b) {
  if (b < a) std::swap(a, b);
  float worst = 0.f;
  for (float t = a; t <= b; t += 2e-6f) worst = std::min(worst, g.gap(t));
  return -worst;
}

void aim(Ground &g, float yaw_deg, float pitch_deg) {
  const float yw = yaw_deg * 0.017453293f, pt = pitch_deg * 0.017453293f;
  g.dir[0] = std::cos(pt) * std::sin(yw);
  g.dir[1] = -std::sin(pt);
  g.dir[2] = std::cos(pt) * std::cos(yw);
}

} // namespace

int test_ground_march_run() {
  g_fail = 0;
  std::printf("ground march...\n");
  struct Eye {
    float x, z, above;
  };
  // 2 km up, 100 m, 15 m and 3 m over the hills
  const Eye eyes[] = {{0.72f, 0.81f, 0.4f}, {0.31f, 0.28f, 0.02f}, {0.52f, 0.21f, 0.003f}, {0.23f, 0.62f, 0.0006f}};
  const float pitches[] = {70.f, 35.f, 12.f, 5.f, 2.f};
  int rays = 0, hits = 0, agree = 0, on_ground = 0, heightmap_off = 0;
  int worst_probes = 0, worst_fine = 0, worst_steep_fine = 0;
  for (int e = 0; e < 4; ++e)
    for (int p = 0; p < 5; ++p) {
      Ground g;
      g.eye[0] = eyes[e].x;
      g.eye[2] = eyes[e].z;
      g.eye[1] = g.bare(g.eye[0], g.eye[2]) + eyes[e].above;
      aim(g, 40.f + 67.f * float(e + p), pitches[p]);
      // a ray starting under the drawn ground has nothing to hit; lift it out
      if (g.gap(0.f) <= 0.f) g.eye[1] += 0.5f * g.dials.amount;
      ++rays;
      float t_ref = 0.f, t_hit = 0.f;
      const bool ref = walked(g, 2.f, t_ref); // the 2 km eye's shallow rays leave the tile
      const bool hit = ground_march(GroundWalk{}, g, t_hit);
      worst_probes = std::max(worst_probes, g.probes);
      worst_fine = std::max(worst_fine, g.fine_probes);
      if (pitches[p] >= 12.f) worst_steep_fine = std::max(worst_steep_fine, g.fine_probes);
      if (!ref || !hit) {
        if (ref == hit) ++agree;
        continue;
      }
      ++hits;
      // the same crossing, or one only a graze away: between the two the ray
      // never goes more than a few centimetres under the drawn ground
      if (std::fabs(t_hit - t_ref) < 2e-6f || deepest(g, t_hit, t_ref) < 1e-5f) ++agree;
      if (std::fabs(g.gap(t_hit)) < 2e-7f) ++on_ground; // a millimetre
      // where the heightmap alone would have put the click
      Ground bare = g;
      bare.dials.amount = 0.f;
      float t_bare = 0.f;
      if (!walked(bare, 2.f, t_bare) || std::fabs(t_bare - t_ref) * std::sqrt(1.f - g.dir[1] * g.dir[1]) > 1e-3f)
        ++heightmap_off; // five metres across the ground
    }
  std::printf("  %d rays, %d hits, %d agree, %d on the ground, %d that the heightmap alone misplaces; "
              "at most %d probes, %d with the relief (%d from 12 degrees down)\n",
              rays, hits, agree, on_ground, heightmap_off, worst_probes, worst_fine, worst_steep_fine);
  check(hits >= 12, "the rays from near the ground reach it");
  check(agree == rays, "every ray lands where a two-centimetre walk lands it, or misses as it does");
  check(on_ground == hits, "the point reported is on the drawn ground");
  check(heightmap_off >= 3, "and the heightmap alone would put several of them metres away");
  check(worst_steep_fine <= 600, "from twelve degrees down, a few hundred reads of the relief");
  check(worst_fine <= 2000, "a grazing ray from three metres up, a thousand-odd");

  // The worst ray there is: level, inside the band the whole way across the
  // tile and never under the relief's crests. The budget is what bounds it.
  {
    Ground g;
    g.hills = 0.f;
    g.eye[0] = 0.02f;
    g.eye[2] = 0.5f;
    g.eye[1] = g.bare(0.f, 0.f) + 0.49f * g.dials.amount;
    aim(g, 90.f, 0.f);
    GroundWalk w;
    float t = 0.f;
    const bool hit_full = ground_march(w, g, t);
    const int full = g.fine_probes;
    check(!hit_full || std::fabs(g.gap(t)) < 2e-7f, "a level ray through the band lands on the ground or nowhere");
    g.fine_probes = 0;
    w.fine_budget = 200;
    const bool hit_small = ground_march(w, g, t);
    std::printf("  a level ray through the band: %d reads of the relief, %d on a budget of 200\n", full,
                g.fine_probes);
    check(full <= GroundWalk{}.fine_budget + 600, "the default budget bounds the worst ray");
    check(g.fine_probes <= 200 + 600, "and a smaller one bounds it lower: strides after it");
    check(!hit_small || std::fabs(g.gap(t)) < 2e-7f, "out of budget, a hit is still on the ground");
  }

  // pointing at the sky, or off the tile and down past the stop: nothing
  {
    Ground g;
    g.eye[0] = 0.5f;
    g.eye[2] = 0.5f;
    g.eye[1] = 0.2f;
    aim(g, 10.f, -20.f);
    float t = 0.f;
    check(!ground_march(GroundWalk{}, g, t), "a ray into the sky hits nothing");
    g.eye[0] = -0.2f;
    aim(g, 180.f, 60.f);
    g.probes = 0;
    check(!ground_march(GroundWalk{}, g, t), "a ray off the tile and under it hits nothing");
    // off the tile the stride does not grow, as the pickers always walked:
    // 0.81 along the ray to fall below -0.5 is 270 first strides
    check(g.probes <= 300, "and stops walking once it is below everything");
  }
  // the relief off: the heightmap's own crossing, found by bisection
  {
    Ground g;
    g.dials.amount = 0.f;
    g.eye[0] = 0.4f;
    g.eye[2] = 0.45f;
    g.eye[1] = g.bare(0.4f, 0.45f) + 0.05f;
    aim(g, 75.f, 20.f);
    float t = 0.f;
    const bool hit = ground_march(GroundWalk{}, g, t);
    check(hit && std::fabs(g.gap(t)) < 2e-7f, "with the relief off it lands on the heightmap");
  }
  return g_fail;
}
