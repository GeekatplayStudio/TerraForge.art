// Geekatplay TerraForge - the environment settings singleton and the sun.
//
// Out of renderer.cpp on purpose: the settings and the astronomy are pure
// state and pure maths, and keeping them GL-free is what lets the persistence
// tests exercise a saved environment without a graphics context.
#include "render_settings.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

RenderSettings &render_settings() {
  static RenderSettings rs;
  return rs;
}

void compute_sun_dir(const RenderSettings &rs, float out[3]) {
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
