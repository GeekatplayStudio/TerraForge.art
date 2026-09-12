// Geekatplay TerraForge - the growth simulation: shoots from buds.
//
// A tree is not drawn; it grows. Every iteration (a year, if the manual's
// Age node drives it) each living shoot extends its tip by a few internodes,
// turning as it goes toward the angle it likes from the zenith
// (gravitropism), toward the open sky (phototropism, read off a shadow grid
// the crown casts on itself) and a little at random; each new internode
// carries one bud, set a phyllotaxis angle round from the last, and next
// iteration some of those buds break into lateral shoots at an angle with
// their parent. A shoot whose tip stands in too deep a shade starves and
// dies (shedding), and a dead shoot either rots away (decay) or stays as
// leafless wood. What is left is a skeleton; the radii come afterwards from
// the pipe model, and the mesher (plant_growth_mesh.cpp) turns it into tubes.
//
// The manual's "age filter" on a parameter is the along-curve of the
// attribute here: a parameter read per iteration is read at
// primal = iteration / (iterations - 1), so a curve on it is the parameter's
// history over the plant's life. The "parent filter" is the hierarchy
// curve, which ParamReader applies at the growth's own position on the
// segment it grows over - one value for the whole system, not per shoot
// (an approximation: the manual filters per branch).
//
// Choices the manual leaves open, made here: apical dominance is a share -
// the terminal bud takes 0.5 + 5 * apical of a shoot's growth budget and the
// laterals the rest - and, on top of that, each generation's budget is
// scaled by (1 + 10 * apical)^-rank, so a positive apical makes a leader
// with shortening laterals and a negative one favours the sides; a shoot's
// vigour fades with its age as 1 / (1 + 0.15 * age), which is what stops a
// 12-year sapling at a few metres with 0.2 m internodes; a bud breaks with
// probability (1 - share) * resource * placement weight. Growth is counted
// in whole internodes with stochastic rounding so fractional budgets still
// grow. Shadow falls only on the cells strictly below an internode, so an
// exposed tip is never shaded by its own wood.
#include "plant/plant_growth.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace plant {

namespace {

constexpr int SHOOT_CAP = 50000;
constexpr int SHADOW_DEPTH = 6;          // cells a shadow reaches down
constexpr float SHADOW_FALLOFF = 0.75f;  // per cell of depth
constexpr float GRAV_RATE = 0.25f;       // turn per internode at influence 1
constexpr float PHOTO_RATE = 0.3f;
constexpr float VIGOUR_DECAY = 0.15f;    // per iteration of a shoot's age

const V3 UP(0.f, 1.f, 0.f);

struct Bud {
  int shoot = 0, node = 0;
  float az = 0.f, weight = 1.f;
};

// The parameters the age filter may vary, read once per iteration.
struct Yearly {
  float favor_up, favor_sides, apical, growth_speed, light_influence, shedding;
  float grav_inf, grav_angle, photo, phyllo, decay, share, base;
};

Yearly read_yearly(const ParamReader &pr, float primal, uint32_t it) {
  Yearly y;
  y.favor_up = pr.random("favor_up", primal, it, 0.3f);
  y.favor_sides = pr.random("favor_sides", primal, it, 0.f);
  y.apical = pr.random("apical", primal, it, 0.03f);
  y.growth_speed = std::max(0.f, pr.random("growth_speed", primal, it, 5.f));
  y.light_influence = std::max(0.f, pr.random("light_influence", primal, it, 1.f));
  y.shedding = pr.random("shedding", primal, it, 0.1f);
  y.grav_inf = clampf(pr.random("gravitropism_influence", primal, it, 0.2f), 0.f, 1.f);
  y.grav_angle = pr.random("gravitropism_angle", primal, it, 40.f) * DEG;
  y.photo = clampf(pr.random("phototropism", primal, it, 0.3f), 0.f, 1.f);
  y.phyllo = pr.random("phyllotaxis", primal, it, 137.5f) * DEG;
  y.decay = clampf(pr.random("decay", primal, it, 0.5f), 0.f, 1.f);
  y.share = clampf(0.5f + 5.f * y.apical, 0.05f, 1.f);
  y.base = std::max(0.05f, 1.f + 10.f * y.apical);
  return y;
}

// Bud placement: the azimuth is pulled toward the top of the shoot
// (favor_up > 0) or its underside (< 0), and toward its flanks
// (favor_sides > 0) or away from them; the bud's chance of breaking is
// weighted the same way.
float favoured_azimuth(float az, const Yearly &y) {
  az -= y.favor_up * std::sin(az) * (PI * 0.5f);
  az += y.favor_sides * std::sin(2.f * az) * (PI * 0.25f);
  return az;
}
float bud_weight(float az, const Yearly &y) {
  return clampf(1.f + y.favor_up * std::cos(az) - y.favor_sides * std::cos(2.f * az), 0.f, 2.f);
}

struct Sim {
  const BuildCtx &ctx;
  const Instance &inst;
  const GrowthParams &p;
  std::vector<Shoot> &shoots;
  std::vector<std::vector<int>> kids;
  ShadowGrid grid;
  V3 foot;
  float inter = 0.2f, bud_r = 0.006f, height = 0.f;

  Draw draw_of(int idx) const {
    Draw d(ctx.seed, inst.id ^ (uint32_t)idx, hash_str("growth"));
    d.i = shoots[(size_t)idx].draws;
    return d;
  }
  static bool living(const Shoot &s) { return !s.removed && s.dead_since < 0; }

  void rebuild_shadow() {
    V3 lo = foot, hi = foot;
    for (const Shoot &s : shoots) {
      if (!living(s)) continue;
      for (const V3 &q : s.nodes) {
        lo = V3(std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z));
        hi = V3(std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z));
      }
    }
    height = std::max(hi.y - foot.y, inter);
    const float cell = std::max(0.01f, p.shadow_size * p.scale);
    grid.reset(lo - V3(cell, cell, cell), hi + V3(cell, cell, cell), cell);
    for (const Shoot &s : shoots)
      if (living(s))
        for (const V3 &q : s.nodes) grid.cast(q, p.shadow_strength);
  }

  // The profile cut: is a point outside the volume of revolution?
  bool outside_profile(V3 q) const {
    if (!p.profile_cut || !p.profile) return false;
    const float ref = p.profile_cut_mode == 1 ? std::max(height, 10.f * inter) : p.scale;
    const float bottom = p.profile_bottom * ref, top = p.profile_top * ref, R = p.profile_radius * ref;
    const float h = q.y - foot.y;
    const float t = top > bottom ? clampf((h - bottom) / (top - bottom), 0.f, 1.f) : (h < bottom ? 0.f : 1.f);
    const float allowed = std::max(0.f, p.profile->eval(t)) * R;
    const float dx = q.x - foot.x, dz = q.z - foot.z;
    return std::sqrt(dx * dx + dz * dz) > allowed;
  }

  // Kill a shoot and everything on it; `remove` takes the wood away too.
  void kill(int idx, int it, bool remove) {
    std::vector<int> stack{idx};
    while (!stack.empty()) {
      const int i = stack.back();
      stack.pop_back();
      Shoot &s = shoots[(size_t)i];
      s.alive = false;
      if (s.dead_since < 0) s.dead_since = it;
      if (remove) s.removed = true;
      for (int k : kids[(size_t)i]) stack.push_back(k);
    }
  }

  // One internode at the tip of `s`: turn, step, place a bud. False when
  // the profile cut stops the shoot.
  bool extend(Shoot &s, int idx, int it, const Yearly &y, float noise, Draw &d, std::vector<Bud> &buds) {
    const V3 tip = s.nodes.empty() ? s.start : s.nodes.back();
    V3 dir = s.dir;
    // gravitropism: toward the direction `angle` from the zenith on this
    // shoot's own vertical plane (the trunk aims at the zenith when vertical)
    if (y.grav_inf > 0.f) {
      const float a = (s.rank == 0 && p.vertical_trunk) ? 0.f : y.grav_angle;
      const V3 h = dir - UP * dot(dir, UP);
      const V3 hn = length(h) > 1e-4f ? normalize(h) : perpendicular(UP);
      const V3 target = UP * std::cos(a) + hn * std::sin(a);
      dir = normalize(lerp(dir, target, y.grav_inf * GRAV_RATE), dir);
    }
    // phototropism: of six directions, turn toward the brightest when it
    // beats where the shoot is already heading
    if (y.photo > 0.f) {
      const float cell = grid.cell;
      float best = grid.light_at(tip + dir * cell);
      V3 best_dir = dir;
      const V3 axes[6] = {V3(1, 0, 0), V3(-1, 0, 0), V3(0, 1, 0), V3(0, -1, 0), V3(0, 0, 1), V3(0, 0, -1)};
      for (const V3 &ax : axes) {
        const float l = grid.light_at(tip + ax * cell);
        if (l > best + 1e-3f) { best = l; best_dir = ax; }
      }
      if (dot(best_dir, dir) < 0.999f) dir = normalize(lerp(dir, best_dir, y.photo * PHOTO_RATE), dir);
    }
    // angular noise: a random tilt about a random axis across the shoot
    if (noise > 0.f) {
      const V3 axis = rotate(perpendicular(dir), dir, d.unit() * TAU);
      dir = normalize(rotate(dir, axis, d.signed_unit() * noise), dir);
    }
    const V3 q = tip + dir * inter;
    if (outside_profile(q)) return false;
    s.dir = dir;
    s.nodes.push_back(q);
    s.node_iter.push_back(it);
    s.azimuth += y.phyllo;
    const float az = favoured_azimuth(s.azimuth, y);
    s.bud_az.push_back(az);
    buds.push_back({idx, (int)s.nodes.size() - 1, az, bud_weight(az, y)});
    return true;
  }

  bool run(const Node &n) {
    ParamReader pr(ctx, n, inst);
    const int N = std::max(1, p.iterations);
    bool capped = false;
    std::vector<Bud> buds, next_buds;
    for (int it = 0; it < N; ++it) {
      const float primal = (float)it / (float)std::max(N - 1, 1);
      const Yearly y = read_yearly(pr, primal, (uint32_t)it);
      rebuild_shadow();
      // light, and death by shade
      for (size_t i = 0; i < shoots.size(); ++i) {
        Shoot &s = shoots[i];
        if (!s.alive) continue;
        const V3 tip = s.nodes.empty() ? s.start : s.nodes.back();
        s.light = std::pow(clampf(grid.light_at(tip), 0.f, 1.f), y.light_influence);
        if (it > 0 && s.rank > 0 && s.light < y.shedding) {
          Draw d = draw_of((int)i);
          const bool rot = d.unit() < y.decay;
          s.draws = d.i;
          kill((int)i, it, rot);
        }
      }
      // the tips extend
      next_buds.clear();
      const size_t count = shoots.size();
      for (size_t i = 0; i < count; ++i) {
        Shoot &s = shoots[i];
        if (!s.alive || (s.born == it && s.rank > 0)) continue;
        Draw d = draw_of((int)i);
        const float vigour = 1.f / (1.f + VIGOUR_DECAY * (float)(it - s.born));
        const float rank_f = clampf(std::pow(y.base, -(float)s.rank), 0.2f, 4.f);
        const float want = y.growth_speed * s.light * y.share * vigour * rank_f;
        int steps = (int)std::floor(want);
        if (d.unit() < want - (float)steps) ++steps;
        const float noise = std::max(0.f, pr.random("angular_noise", primal, (uint32_t)i, 10.f)) * DEG;
        for (int k = 0; k < steps; ++k)
          if (!extend(s, (int)i, it, y, noise, d, next_buds)) { s.alive = false; break; }
        s.draws = d.i;
      }
      // last iteration's buds break into laterals
      for (const Bud &b : buds) {
        Shoot &ps = shoots[(size_t)b.shoot];
        if (!ps.alive) continue;
        Draw d = draw_of(b.shoot);
        const bool breaks = d.unit() < (1.f - y.share) * ps.light * b.weight;
        ps.draws = d.i;
        if (!breaks) continue;
        if ((int)shoots.size() >= SHOOT_CAP) { capped = true; break; }
        const int idx = (int)shoots.size();
        Shoot c;
        c.parent = b.shoot;
        c.parent_node = b.node;
        c.start = ps.nodes[(size_t)b.node];
        c.rank = ps.rank + 1;
        c.born = it;
        c.light = ps.light;
        c.dist_base = ps.dist_base + (float)(b.node + 1) * inter;
        const float angle = clampf(pr.random("angle_with_parent", primal, (uint32_t)idx, 40.f), 0.f, 180.f) * DEG;
        const V3 t = ps.tangent_at(b.node), r = ps.radial_at(b.node, b.az);
        c.dir = normalize(t * std::cos(angle) + r * std::sin(angle), r);
        shoots.push_back(c);
        kids.push_back({});
        kids[(size_t)b.shoot].push_back(idx);
        Shoot &cs = shoots[(size_t)idx];
        Draw cd = draw_of(idx);
        cs.azimuth = cd.unit() * TAU;
        const float noise = std::max(0.f, pr.random("angular_noise", primal, (uint32_t)idx, 10.f)) * DEG;
        if (!extend(cs, idx, it, y, noise, cd, next_buds)) cs.alive = false;
        cs.draws = cd.i;
      }
      buds.swap(next_buds);
    }
    rebuild_shadow(); // for the final height
    bottom_cut(N);
    pipe_radii();
    return capped;
  }

  void bottom_cut(int it) {
    const float h = p.bottom_cut_mode == 1 ? p.bottom_cut_height * height : p.bottom_cut_height * p.scale;
    if (h <= 0.f) return;
    for (size_t i = 0; i < shoots.size(); ++i) {
      const Shoot &s = shoots[i];
      if (s.removed || s.rank <= p.bottom_cut_rank) continue;
      if (s.start.y - foot.y < h) kill((int)i, it, true);
    }
  }

  // Leonardo: a radius squared is the sum of what it carries; every tip is
  // one bud. Children come after parents in the list, so one backward pass.
  void pipe_radii() {
    std::vector<std::vector<float>> acc(shoots.size());
    for (size_t i = shoots.size(); i-- > 0;) {
      Shoot &s = shoots[i];
      if (s.removed || s.nodes.empty()) continue;
      std::vector<float> &a = acc[i];
      if (a.size() < s.nodes.size()) a.resize(s.nodes.size(), 0.f);
      s.radii.resize(s.nodes.size());
      for (size_t k = 0; k < s.nodes.size(); ++k) s.radii[k] = std::sqrt(bud_r * bud_r + a[k]);
      s.radius = s.radii[0];
      if (s.parent >= 0) {
        std::vector<float> &pa = acc[(size_t)s.parent];
        const size_t need = shoots[(size_t)s.parent].nodes.size();
        if (pa.size() < need) pa.resize(need, 0.f);
        const float r2 = s.radius * s.radius;
        for (int k = 0; k <= s.parent_node && k < (int)pa.size(); ++k) pa[(size_t)k] += r2;
      }
    }
    if (p.over_parent && p.parent_radius > 0.f && !shoots.empty() && shoots[0].radius > p.parent_radius) {
      const float f = p.parent_radius / shoots[0].radius;
      for (Shoot &s : shoots) {
        s.radius *= f;
        for (float &r : s.radii) r *= f;
      }
    }
  }
};

} // namespace

// ------------------------------------------------------------------ Shoot
V3 Shoot::tangent_at(int k) const {
  if (nodes.empty()) return dir;
  const size_t n = nodes.size();
  const V3 prev = k > 0 ? nodes[(size_t)k - 1] : start;
  const V3 next = (size_t)k + 1 < n ? nodes[(size_t)k + 1] : nodes[(size_t)k];
  return normalize(next - prev, dir);
}
V3 Shoot::radial_at(int k, float az) const {
  const V3 t = tangent_at(k);
  const V3 h = UP - t * dot(UP, t);
  const V3 u = length(h) > 1e-4f ? normalize(h) : perpendicular(t);
  const V3 v = cross(t, u);
  return u * std::cos(az) + v * std::sin(az);
}

// ------------------------------------------------------------- ShadowGrid
void ShadowGrid::reset(V3 lo, V3 hi, float cell_size) {
  cell = cell_size;
  // never more than a few million cells: coarsen instead
  for (;;) {
    nx = (int)std::ceil((hi.x - lo.x) / cell) + 1;
    ny = (int)std::ceil((hi.y - lo.y) / cell) + 1;
    nz = (int)std::ceil((hi.z - lo.z) / cell) + 1;
    if ((double)nx * ny * nz <= 4.0e6) break;
    cell *= 1.5f;
  }
  origin = lo;
  shadow.assign((size_t)nx * ny * nz, 0.f);
}
void ShadowGrid::cast(V3 p, float strength) {
  const int ix = (int)std::floor((p.x - origin.x) / cell), iy = (int)std::floor((p.y - origin.y) / cell),
            iz = (int)std::floor((p.z - origin.z) / cell);
  if (ix < 0 || ix >= nx || iz < 0 || iz >= nz) return;
  float w = strength;
  for (int d = 1; d <= SHADOW_DEPTH; ++d, w *= SHADOW_FALLOFF) {
    const int y = iy - d;
    if (y < 0) break;
    if (y < ny) shadow[((size_t)y * nz + iz) * nx + ix] += w;
  }
}
float ShadowGrid::light_at(V3 p) const {
  const int ix = (int)std::floor((p.x - origin.x) / cell), iy = (int)std::floor((p.y - origin.y) / cell),
            iz = (int)std::floor((p.z - origin.z) / cell);
  if (ix < 0 || ix >= nx || iy < 0 || iy >= ny || iz < 0 || iz >= nz) return 1.f;
  return clampf(1.f - shadow[((size_t)iy * nz + iz) * nx + ix], 0.f, 1.f);
}

// ----------------------------------------------------------------- entry
bool growth_simulate(const BuildCtx &ctx, const Node &n, const Instance &inst, GrowthParams p,
                     std::vector<Shoot> &shoots) {
  shoots.clear();
  Sim sim{ctx, inst, p, shoots, {}, {}, inst.frame.o};
  sim.inter = std::max(1e-4f, p.internode * p.scale);
  sim.bud_r = std::max(1e-5f, p.bud_radius * p.scale);
  Shoot trunk;
  trunk.start = inst.frame.o;
  trunk.dir = normalize(inst.frame.z, UP);
  trunk.dist_base = inst.dist_root;
  shoots.push_back(trunk);
  sim.kids.push_back({});
  return sim.run(n);
}

} // namespace plant
} // namespace gpx
