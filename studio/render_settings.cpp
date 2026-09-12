// Geekatplay TerraForge - the environment settings singleton and the sun.
//
// Out of renderer.cpp on purpose: the settings and the astronomy are pure
// state and pure maths, and keeping them GL-free is what lets the persistence
// tests exercise a saved environment without a graphics context.
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>

namespace studio {


// --------------------------------------------------------------- the wind
// What the wind is doing at a moment, which every moving thing in the scene
// asks and none of them decides for itself.
//
// A gust is not a random number per frame - that would make everything
// jitter and nothing travel. It is a slow wave passing over the ground, so
// two trees a hundred metres apart are caught by the same gust a moment
// apart, and the whole wood leans and recovers together. Here only the time
// part is evaluated; whatever is being moved adds its own place through
// `gust_size_m` where it can (the sea and the clouds already march in world
// space, and a plant is small enough that one plant is one place).
//
// The turn is the same wave a quarter cycle behind: real gusts back and veer
// as they arrive rather than strengthening along a fixed line.
void RenderSettings::wind_at(float t, float alt, float &speed_ms, float &dir_deg) const {
  const float TAU = 6.28318530717959f;
  const float f = std::max(wind.gust_frequency, 0.f);
  // two frequencies a fifth apart, so the gusts never fall into a rhythm
  const float a = std::sin(t * f * TAU) * 0.65f + std::sin(t * f * TAU * 1.7f + 1.3f) * 0.35f;
  const float b = std::sin(t * f * TAU - 1.5708f) * 0.65f + std::sin(t * f * TAU * 1.7f - 0.27f) * 0.35f;
  const float gust = 1.f + std::max(wind.gust_strength, 0.f) * a;
  // the shear: still air at the ground, faster aloft
  const float lift = 1.f + (std::max(wind.shear, 0.f) - 1.f) * std::clamp(alt, 0.f, 1.f);
  speed_ms = std::max(wind.speed_ms, 0.f) * std::max(gust, 0.f) * lift;
  dir_deg = wind.direction_deg + wind.turbulence_deg * b;
}

void RenderSettings::wind_vector(float t, float alt, float &vx, float &vz, float &speed_ms) const {
  float dir = 0.f;
  wind_at(t, alt, speed_ms, dir);
  const float r = dir * 0.01745329251994f;
  vx = std::cos(r);
  vz = std::sin(r);
}


// One step of the air, and what everything following it is blown by.
void RenderSettings::wind_advance(float t, float dt) {
  dt = std::clamp(dt, 0.f, 0.25f);
  const float size = std::max(terrain_size_m, 1.f);
  float vx = 0.f, vz = 0.f, sp = 0.f;
  // at the ground: the sea, the fog, the plants
  wind_vector(t, 0.f, vx, vz, sp);
  const float step = sp * dt / size; // metres a second into tiles a second
  wind_drift[0] += vx * step;
  wind_drift[1] += vz * step;
  wind_drift_len += step;
  // and aloft, where the shear has it moving faster: the clouds
  float hx = 0.f, hz = 0.f, hs = 0.f;
  wind_vector(t, 1.f, hx, hz, hs);
  const float hstep = hs * dt / size;
  wind_drift_hi[0] += hx * hstep;
  wind_drift_hi[1] += hz * hstep;
  wind_drift_hi_len += hstep;

  // The sea is built from a wind that has been blowing for hours, so it takes
  // the mean and not the gust: a sea state does not answer a single squall,
  // and rebuilding the wave spectrum every frame because the speed wobbled
  // would cost more than the whole water pass.
  if (water_wind_follow) {
    water_wind_speed = std::max(wind.speed_ms, 0.f);
    water_wind_dir = wind.direction_deg;
  }
  // The clouds are given the direction for their detail terms; how far they
  // have actually travelled is the drift above.
  if (cloud_wind_follow) {
    cloud_wind_speed = hs / size;
    cloud_wind_dir = wind.direction_deg;
  }
}

RenderSettings &render_settings() {
  static RenderSettings rs;
  return rs;
}

void compute_sun_dir(const RenderSettings &rs, float out[3]) {
  if (rs.world_sun_inside && rs.world_inside) {
    // a sun on a ring world's axis or at a Dyson sphere's centre stands
    // straight above the tile (world_shape.hpp); the far shell lights each
    // of its points toward that body itself
    out[0] = 0.f; out[1] = 1.f; out[2] = 0.f;
    return;
  }
  float az, alt;
  if (rs.sun_mode == 0) {
    az = rs.sun_azimuth * 0.017453293f;
    alt = rs.sun_altitude * 0.017453293f;
  } else {
    static const int mdays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int m = std::clamp(rs.month, 1, 12);
    int doy = mdays[m - 1] + std::clamp(rs.day, 1, 31);
    float frac_year = 2.f * 3.14159265f / 365.f * (doy - 1 + (rs.hour - 12.f) / 24.f);
    float decl = 0.006918f - 0.399912f * std::cos(frac_year) +
                 0.070257f * std::sin(frac_year) - 0.006758f * std::cos(2 * frac_year) +
                 0.000907f * std::sin(2 * frac_year) - 0.002697f * std::cos(3 * frac_year) +
                 0.00148f * std::sin(3 * frac_year);
    float eqtime = 229.18f * (0.000075f + 0.001868f * std::cos(frac_year) -
                              0.032077f * std::sin(frac_year) -
                              0.014615f * std::cos(2 * frac_year) -
                              0.040849f * std::sin(2 * frac_year));
    float time_offset = eqtime + 4.f * rs.longitude - 60.f * rs.utc_offset;
    float tst = rs.hour * 60.f + time_offset;
    float ha = (tst / 4.f - 180.f) * 0.017453293f;
    float lat = rs.latitude * 0.017453293f;
    float cos_zen = std::sin(lat) * std::sin(decl) +
                    std::cos(lat) * std::cos(decl) * std::cos(ha);
    cos_zen = std::clamp(cos_zen, -1.f, 1.f);
    alt = 1.5707963f - std::acos(cos_zen);
    float sin_az = -std::cos(decl) * std::sin(ha) / std::max(std::cos(alt), 1e-4f);
    float cos_az = (std::sin(decl) - std::sin(lat) * cos_zen) /
                   std::max(std::cos(lat) * std::cos(alt), 1e-4f);
    az = std::atan2(sin_az, cos_az);
    az = 1.5707963f - az;
    alt = std::max(alt, 0.02f);
  }
  out[0] = std::cos(alt) * std::cos(az);
  out[1] = std::sin(alt);
  out[2] = std::cos(alt) * std::sin(az);
}

} // namespace studio

// ---- render passes -------------------------------------------------------
// Names double as file suffixes ("<beauty>_depth.exr"), so they stay short,
// lower case and stable; the labels are what the Render panel shows.
namespace studio {

const char *render_pass_name(int index) {
  static const char *N[RENDER_PASS_COUNT] = {
      "depth",  "normal", "position", "object_id", "water_mask", "albedo",
      "direct", "shadow", "ambient",  "specular",  "atmosphere", "environment"};
  return (index >= 0 && index < RENDER_PASS_COUNT) ? N[index] : "";
}

const char *render_pass_label(int index) {
  static const char *L[RENDER_PASS_COUNT] = {
      "Depth (metres)",      "World normal",       "World position",
      "Object id",           "Water mask",         "Albedo",
      "Direct sun light",    "Shadow mask",        "Sky / ambient light",
      "Specular / reflection", "Fog & haze (rgb + transmittance)",
      "Sky & backdrop only"};
  return (index >= 0 && index < RENDER_PASS_COUNT) ? L[index] : "";
}

} // namespace studio
