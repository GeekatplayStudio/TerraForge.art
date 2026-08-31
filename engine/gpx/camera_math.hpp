// Geekatplay TerraForge — physical camera math (pure, unit-tested).
// Sensor formats, focal-length -> field of view, photographic exposure from
// aperture / shutter / ISO, and film-stock color response presets.
#pragma once
#include <cmath>
#include <cstddef>

namespace gpx::cam {

struct SensorFormat {
  const char *name;
  float width_mm, height_mm;
};

// classic capture formats (sensor / film gate sizes)
inline const SensorFormat *sensor_formats(int *count) {
  static const SensorFormat F[] = {
      {"Full frame 35mm", 36.0f, 24.0f},
      {"APS-C", 23.6f, 15.7f},
      {"Super 35 (cine)", 24.89f, 18.66f},
      {"Micro Four Thirds", 17.3f, 13.0f},
      {"16mm film", 10.26f, 7.49f},
      {"65mm / IMAX", 70.41f, 52.63f},
      {"Large format 4x5", 121.0f, 97.0f},
  };
  if (count) *count = (int)(sizeof(F) / sizeof(F[0]));
  return F;
}

// vertical field of view in degrees from focal length and sensor height
inline float fov_y_deg(float focal_mm, float sensor_h_mm) {
  if (focal_mm < 1e-3f) focal_mm = 1e-3f;
  return 2.0f * std::atan(sensor_h_mm * 0.5f / focal_mm) * 57.29577951f;
}

// horizontal fov, for completeness
inline float fov_x_deg(float focal_mm, float sensor_w_mm) {
  return fov_y_deg(focal_mm, sensor_w_mm);
}

// EV at ISO 100 for aperture N (f-number) and shutter time t (seconds)
inline float ev100(float f_number, float shutter_s, float iso) {
  if (shutter_s < 1e-6f) shutter_s = 1e-6f;
  if (f_number < 0.5f) f_number = 0.5f;
  if (iso < 1.f) iso = 1.f;
  return std::log2(f_number * f_number / shutter_s) - std::log2(iso / 100.0f);
}

// scene exposure multiplier, normalised so f/8, 1/125s, ISO 100 -> 1.0
inline float exposure_multiplier(float f_number, float shutter_s, float iso) {
  const float ref = 13.0f; // ev100(8, 1/125, 100) = log2(64*125) = 12.97
  float ev = ev100(f_number, shutter_s, iso);
  float m = std::pow(2.0f, ref - ev);
  // clamp so a wildly wrong triangle dims or blooms without going pure white
  if (m < 0.03f) m = 0.03f;
  if (m > 12.f) m = 12.f;
  return m;
}

// thin-lens aperture radius in world units for depth of field
// (world_per_mm converts the physical focal length into scene units)
inline float aperture_radius(float focal_mm, float f_number, float world_per_mm) {
  if (f_number < 0.5f) f_number = 0.5f;
  return (focal_mm / f_number) * 0.5f * world_per_mm;
}

struct FilmStock {
  const char *name;
  float tint[3];    // multiplied into the image before tone mapping
  float saturation; // 1 = neutral
  float grain;      // 0..1 strength hint for renderers
};

inline const FilmStock *film_stocks(int *count) {
  static const FilmStock F[] = {
      {"Digital (neutral)", {1.00f, 1.00f, 1.00f}, 1.00f, 0.00f},
      {"Kodak Portra 400", {1.06f, 1.00f, 0.92f}, 0.92f, 0.25f},
      {"Kodak Kodachrome 64", {1.08f, 0.98f, 0.88f}, 1.18f, 0.20f},
      {"Kodak Vision3 500T", {0.94f, 0.98f, 1.08f}, 1.02f, 0.30f},
      {"Fuji Ektachrome-style", {0.96f, 1.01f, 1.06f}, 1.10f, 0.15f},
      {"Ilford HP5 (B&W)", {1.00f, 1.00f, 1.00f}, 0.00f, 0.45f},
  };
  if (count) *count = (int)(sizeof(F) / sizeof(F[0]));
  return F;
}

} // namespace gpx::cam
