// Geekatplay TerraForge — the sea over the world, without GL (water_surface.hpp).
#include "water_surface.hpp"
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

gpx::water::Params water_params(const RenderSettings &rs) {
  gpx::water::Params p;
  p.wind_speed = rs.water_wind_speed;
  p.wind_dir_deg = rs.water_wind_dir;
  p.height = rs.water_wave_amp;
  p.scale = rs.water_wave_scale;
  p.agitation = rs.water_wave_speed;
  p.choppiness = rs.water_choppiness;
  return p;
}

const WaterWaves &water_waves(const RenderSettings &rs) {
  static WaterWaves cache;
  static gpx::water::Params built;
  static bool have = false;
  const gpx::water::Params p = water_params(rs);
  // compared field by field, never as bytes (AGENTS.md, performance rule 1)
  const bool same = have && p.wind_speed == built.wind_speed &&
                    p.wind_dir_deg == built.wind_dir_deg && p.height == built.height &&
                    p.scale == built.scale && p.agitation == built.agitation &&
                    p.choppiness == built.choppiness;
  if (!same) {
    cache.n = gpx::water::build(p, cache.w);
    cache.slope_var = 0.f;
    for (int i = 0; i < cache.n; ++i) {
      const float ak = cache.w[i].amp * cache.w[i].k;
      cache.slope_var += ak * ak * 0.5f;
    }
    built = p;
    have = true;
  }
  return cache;
}

void water_eye_param(const float eye[3], float R, const gpx::planet::Shape &S,
                     double &u, double &v) {
  if (R <= 0.f || gpx::planet::shape_is_flat(S)) {
    u = eye[0];
    v = eye[2];
    return;
  }
  // sphere_place puts a point R*(sin ax cos ay, cos ax cos ay, sin ay) from
  // the centre (the height leaning toward the centre on an inside world),
  // so the angles come back through atan2 about that centre
  const double s = S.inside ? -1.0 : 1.0;
  const double cy = S.inside ? double(R) : -double(R);
  const double dx = double(eye[0]) - 0.5, dy = double(eye[1]) - cy;
  const double k = std::min(1.0 / double(R), 6.283185307179586);
  const double ax = std::atan2(dx, s * dy);
  u = 0.5 + ax / k;
  if (S.flat_z) {
    v = eye[2];
    return;
  }
  const double kl = std::min(1.0 / double(R), 3.141592653589793);
  const double dz = double(eye[2]) - 0.5;
  const double ay = std::atan2(dz, std::sqrt(dx * dx + dy * dy));
  v = 0.5 + ay / kl;
}

int water_levels(double eu, double ev, double c0, double need, WaterLevel out[], int max) {
  int n = 0;
  for (; n < max; ++n) {
    const double cell = c0 * std::ldexp(1.0, n);
    out[n].cell = cell;
    out[n].ox = std::floor(eu / (2.0 * cell) + 0.5) * 2.0 * cell;
    out[n].oz = std::floor(ev / (2.0 * cell) + 0.5) * 2.0 * cell;
    if (WATER_OWN * cell >= need) return n + 1;
  }
  return n;
}

int water_level_owner(const WaterLevel *levels, int n, double x, double z) {
  for (int l = 0; l < n; ++l) {
    const WaterLevel &L = levels[l];
    const double d = std::max(std::fabs(x - L.ox), std::fabs(z - L.oz));
    // the last level draws out to its mesh's edge; every other stops at what
    // it owns, and the next one takes over from exactly there
    const double r = l + 1 < n ? WATER_OWN * L.cell : WATER_GRID * 0.5 * L.cell;
    if (d < r) return l;
  }
  return -1;
}

void water_param_limits(float R, const gpx::planet::Shape &S, float &lim_x, float &lim_z) {
  const float big = 1.0e9f;
  if (R <= 0.f || gpx::planet::shape_is_flat(S)) {
    lim_x = lim_z = big;
    return;
  }
  const float k = std::min(1.f / R, 6.2831853f), kl = std::min(1.f / R, 3.14159265f);
  lim_x = S.flat_x ? big : 3.14159265f / k;
  lim_z = S.flat_z ? big : 1.5707963f / kl;
}

} // namespace studio
