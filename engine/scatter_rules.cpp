// Geekatplay TerraForge - scatter stages 2, 4 and 5: whether a candidate
// stands (presence), which species it is, and how it stands (transform).
//
// The presence rule is the material layer's, on purpose: an ecosystem keyed
// on "north slopes above the tree line" must pick the same ground as a
// material layer with the same settings, or the grass and its soil disagree.
#include "gpx/scatter.hpp"
#include <algorithm>
#include <cmath>

namespace gpx::scatter {

namespace {

constexpr float PI_F = 3.14159265358979f;

// Same formulas as nodes_material_layers.cpp, kept in step by the test that
// scatters over a layer's presence and checks both agree.
float band(float x, float lo, float hi, float fuzz) {
  if (fuzz <= 1e-6f) return (x >= lo && x <= hi) ? 1.f : 0.f;
  float a = std::clamp((x - (lo - fuzz)) / (2.f * fuzz), 0.f, 1.f);
  float b = std::clamp(((hi + fuzz) - x) / (2.f * fuzz), 0.f, 1.f);
  a = a * a * (3.f - 2.f * a);
  b = b * b * (3.f - 2.f * b);
  return std::min(a, b);
}
float arc(float a, float b) {
  float d = std::fmod(std::fabs(a - b), 360.f);
  return d > 180.f ? 360.f - d : d;
}

struct TerrainRange {
  float lo = 0.f, hi = 1.f;
  explicit TerrainRange(const Heightmap *t) {
    if (!t || t->empty()) return;
    lo = 1e30f; hi = -1e30f;
    for (float v : t->v) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (hi - lo < 1e-6f) hi = lo + 1.f;
  }
};

// The environment at one spot: normalised height, slope in degrees, aspect.
struct Ground {
  float h = 0.f, slope_deg = 0.f, aspect_deg = 0.f, steep = 0.f;
};
Ground ground_at(const Heightmap *t, float u, float v, const TerrainRange &r, float hs) {
  Ground g;
  if (!t || t->empty()) return g;
  g.h = t->sample(u, v);
  int tx = std::min((int)(u * t->w), t->w - 1);
  int ty = std::min((int)(v * t->h), t->h - 1);
  float dx = 0, dy = 0;
  t->gradient_at(tx, ty, dx, dy);
  const float cell = 1.f / std::max(t->w, 1);
  float gx = dx / cell * hs, gy = dy / cell * hs;
  g.steep = std::sqrt(gx * gx + gy * gy);
  g.slope_deg = std::atan(g.steep) * 180.f / PI_F;
  float az = std::atan2(-gx, -gy) * 180.f / PI_F;
  if (az < 0.f) az += 360.f;
  g.aspect_deg = az;
  (void)r;
  return g;
}

} // namespace

namespace {

// The environment at a world position, from a ground function rather than
// a raster: the height is the function's, the slope and the aspect come
// from two differences a `step` apart. Same formulas as the raster path, so
// a slope band means the same thing on a planet as on the tile.
Ground ground_from_fn(const Presence &p, float x, float z) {
  Ground g;
  const float h = p.ground(x, z);
  const float s = std::max(p.step, 1e-6f);
  const float gx = (p.ground(x + s, z) - p.ground(x - s, z)) / (2.f * s);
  const float gz = (p.ground(x, z + s) - p.ground(x, z - s)) / (2.f * s);
  g.h = h;
  g.steep = std::sqrt(gx * gx + gz * gz);
  g.slope_deg = std::atan(g.steep) * 180.f / PI_F;
  float az = std::atan2(-gx, -gz) * 180.f / PI_F;
  if (az < 0.f) az += 360.f;
  g.aspect_deg = az;
  return g;
}

} // namespace

float presence_at(const Presence &p, float u, float v) {
  float pr = 1.f;
  // a mask is a picture of a tile; a world-domain population has no tile to
  // read it over, so it is the environment alone that places those
  if (!p.ground && p.mask && !p.mask->empty()) {
    float m = std::clamp(p.mask->sample(u, v), 0.f, 1.f);
    pr = p.invert_mask ? 1.f - m : m;
  } else if (!p.ground && p.invert_mask) {
    pr = 0.f; // inverting an absent mask means "nowhere"
  }
  if (pr <= 0.f) return 0.f;
  const bool wants_env =
      p.use_altitude || p.use_slope || p.use_orientation || p.slope_influence > 0.f;
  const bool env = wants_env && (p.ground || (p.terrain && !p.terrain->empty()));
  if (env) {
    Ground g;
    TerrainRange range(nullptr); // absolute: a function has no range to scan
    if (p.ground) {
      g = ground_from_fn(p, u, v);
    } else {
      static thread_local const Heightmap *cached = nullptr;
      static thread_local TerrainRange cached_range(nullptr);
      if (cached != p.terrain) { cached = p.terrain; cached_range = TerrainRange(p.terrain); }
      range = cached_range;
      g = ground_at(p.terrain, u, v, range, std::max(p.height_scale, 1e-6f));
    }
    if (p.use_altitude) {
      float h = g.h;
      // a function's height is absolute already; only a raster has a range
      if (p.altitude_mode == 0 && !p.ground) h = (h - range.lo) / (range.hi - range.lo);
      else if (p.altitude_mode == 2) h -= p.sea;
      pr *= band(h, p.alt_lo, p.alt_hi, p.alt_fuzz);
    }
    if (pr > 0.f && p.use_slope) pr *= band(g.slope_deg, p.slope_lo, p.slope_hi, p.slope_fuzz);
    if (pr > 0.f && p.use_orientation) {
      // flat ground has no facing, so it never counts as oriented
      pr *= band(arc(g.aspect_deg, p.orient), 0.f, p.orient_width, p.orient_fuzz) *
            std::clamp(g.steep * 8.f, 0.f, 1.f);
    }
    if (pr > 0.f && p.slope_influence > 0.f) {
      // Vue's slider: 100% = sparser on steep ground, the way VUE 6 did it
      float s = std::clamp(g.slope_deg / 90.f, 0.f, 1.f);
      pr *= 1.f - p.slope_influence * s;
    }
  }
  // the objects map is a tile raster; a world-domain population reads it
  // only where it has one
  if (pr > 0.f && !p.ground && p.distance && !p.distance->empty() && p.decay_influence > 0.f) {
    // decay near foreign objects: a void that grows with influence and
    // sharpens with falloff, like Vue's two dials (p1101)
    float d = std::clamp(p.distance->sample(u, v) / std::max(p.decay_reach, 1e-6f), 0.f, 1.f);
    float f = p.decay_falloff;
    float t = f >= 0.f ? std::pow(d, 1.f + f * 3.f) : 1.f - std::pow(1.f - d, 1.f - f * 3.f);
    pr *= 1.f - p.decay_influence * (1.f - t);
  }
  pr = std::clamp(pr, 0.f, 1.f);
  return pr < p.threshold ? 0.f : pr;
}

void filter(PointCloud &pc, const Presence &p, float rate) {
  const size_t m = pc.size();
  if (m == 0) return;
  pc.ensure_attrs();
  std::vector<uint8_t> keep(m, 0);
  for (size_t i = 0; i < m; ++i) {
    float pr = presence_at(p, pc.x[i], pc.y[i]);
    if (pr <= 0.f) continue;
    if (unit(pc.id[i], 1) < pr * rate) {
      keep[i] = 1;
      pc.v[i] = pr; // the value carries the presence: strong presence, big plant
    }
  }
  pc.compact(keep);
}

void transform(PointCloud &pc, const Transform &t) {
  const size_t m = pc.size();
  if (m == 0) return;
  pc.ensure_attrs();
  const int ns = std::clamp(t.species_count, 1, 8);
  float cum[8] = {0};
  float total = 0.f;
  for (int s = 0; s < ns; ++s) { total += std::max(t.weights[s], 0.f); cum[s] = total; }
  const bool driven = t.driver && !t.driver->empty();

  // thin spots, for shrink and lean: how many neighbours each point has
  // against the population's own average
  std::vector<int> nb;
  float mean_nb = 0.f;
  if (t.shrink != 0.f || t.lean != 0.f) {
    neighbour_counts(pc, t.shrink_radius, nb);
    double sum = 0;
    for (int c : nb) sum += c;
    mean_nb = (float)(sum / (double)m);
  }

  for (size_t i = 0; i < m; ++i) {
    const uint64_t id = pc.id[i];
    // ---- species ---------------------------------------------------------
    int sp = 0;
    if (ns > 1) {
      if (driven) {
        // the driver's value falls into one of the weighted intervals
        float d = std::clamp(t.driver->sample(pc.x[i], pc.y[i]), 0.f, 1.f) * total;
        while (sp < ns - 1 && d > cum[sp]) ++sp;
      } else if (total > 0.f) {
        float d = unit(id, 2) * total;
        while (sp < ns - 1 && d > cum[sp]) ++sp;
      }
    }
    pc.species[i] = (uint16_t)sp;
    // ---- size ------------------------------------------------------------
    // variation 1 means half to twice: a symmetric draw in log2. Each kind
    // may vary in its own way - a stand of boulders is all sizes, the
    // saplings among them much of a muchness - and 0 takes the layer's.
    const float var = t.species_variation[sp] > 0.f ? t.species_variation[sp] : t.variation;
    float common = std::exp2((unit(id, 3) * 2.f - 1.f) * var);
    float ax[3];
    for (int k = 0; k < 3; ++k) {
      float own = std::exp2((unit(id, 4 + (uint32_t)k) * 2.f - 1.f) * var);
      ax[k] = common * t.keep_proportions + own * (1.f - t.keep_proportions);
    }
    const float base = t.scale * t.species_scale[sp];
    pc.sx[i] = base * ax[0];
    pc.sy[i] = base * ax[1];
    pc.sz[i] = base * ax[2];
    // thin population: smaller and leaning out for the light
    if (!nb.empty() && mean_nb > 0.f) {
      float thin = 1.f - std::clamp(nb[i] / mean_nb, 0.f, 1.f); // 1 = alone
      if (t.shrink != 0.f) {
        float f = std::max(1.f - t.shrink * thin, 0.05f);
        pc.sx[i] *= f; pc.sy[i] *= f; pc.sz[i] *= f;
      }
      if (t.lean != 0.f) pc.tilt[i] = std::clamp(t.direction + t.lean * thin, 0.f, 1.f);
      else pc.tilt[i] = t.direction;
    } else {
      pc.tilt[i] = t.direction;
    }
    // Each kind may tip with the ground in its own way: a boulder sits on the
    // hillside, a tree stands up out of it, and one figure for a whole
    // population cannot say both. Negative takes the layer's.
    if (t.species_lean[sp] >= 0.f) pc.tilt[i] = std::clamp(t.species_lean[sp], 0.f, 1.f);
    // ---- rotation --------------------------------------------------------
    if (t.rotation == 1) pc.yaw[i] = 0.f;
    else if (t.rotation == 2 && driven)
      pc.yaw[i] = (t.driver->sample(pc.x[i], pc.y[i]) * 2.f - 1.f) * PI_F * t.rotation_max;
    else pc.yaw[i] = (unit(id, 7) * 2.f - 1.f) * PI_F * t.rotation_max;
    // ---- the rest --------------------------------------------------------
    pc.offset[i] = t.offset;
    pc.tint[i] = 1.f + (unit(id, 8) - 0.5f) * t.color_variation;
    pc.phase[i] = unit(id, 9) * t.phase_range;
    pc.radius[i] = t.radius * std::max(pc.sx[i], pc.sz[i]);
  }
}

} // namespace gpx::scatter
