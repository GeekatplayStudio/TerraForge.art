// Geekatplay TerraForge - the light the sky casts.
//
// What a diffuse surface takes from the sky is the integral of the sky's
// radiance against the cosine of the angle from its normal. The viewport
// used to substitute the average of the two sky *colours* for that - a
// number between 0 and 1, where the real one is several times larger - and
// multiply it by the ambient dial. That is why a path-traced frame, which
// integrates the real sky, came out far brighter than its own preview: what
// you saw was not what you got.
//
// So the sky is measured instead of guessed: 512 rays through the same sky
// and cloud march the viewport draws, averaged into one radiance
// (renderer_export.cpp, panorama mode 2). It is taken again only when the
// sky itself changes, so a still frame costs nothing at all, and every
// shader that lights a surface with skylight reads the one number.
#include "render_settings.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace studio {

bool renderer_sky_light(float out_rgb[3]); // renderer_export.cpp
void compute_sun_dir(const RenderSettings &rs, float out[3]);

namespace {
float g_light[3] = {0.f, 0.f, 0.f};
bool g_have = false;
uint64_t g_key = 0;

void mix_in(uint64_t &h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
}
template <class T> void mix_in(uint64_t &h, const T &v) { mix_in(h, &v, sizeof(T)); }

// Everything the sky's own brightness depends on. Deliberately not the cloud
// clock: clouds drifting change where the light comes from by a hair and how
// much of it by less, and re-measuring every frame would spend a cloud march
// on a number that did not move.
uint64_t sky_key(const RenderSettings &rs) {
  uint64_t h = 1469598103934665603ull;
  float sun[3];
  compute_sun_dir(rs, sun);
  mix_in(h, sun);
  mix_in(h, rs.sun_color);
  mix_in(h, rs.sun_intensity);
  mix_in(h, rs.sky_zenith);
  mix_in(h, rs.sky_horizon);
  mix_in(h, rs.atmosphere_density);
  mix_in(h, rs.atmosphere_height);
  mix_in(h, rs.atmosphere_falloff);
  mix_in(h, rs.clouds_on);
  mix_in(h, rs.cloud_type);
  mix_in(h, rs.cloud_coverage);
  mix_in(h, rs.cloud_density);
  mix_in(h, rs.cloud_altitude);
  mix_in(h, rs.cloud_thickness);
  mix_in(h, rs.cloud_color);
  mix_in(h, rs.cloud_ambient);
  mix_in(h, rs.cloud_volumetric);
  mix_in(h, rs.cloud2_on);
  mix_in(h, rs.cloud2_coverage);
  mix_in(h, rs.cloud2_density);
  mix_in(h, rs.backdrop.enabled);
  mix_in(h, rs.backdrop.exposure_ev);
  mix_in(h, rs.backdrop.blend);
  mix_in(h, rs.backdrop.tint);
  mix_in(h, rs.world_shape);
  mix_in(h, rs.planet_radius);
  const int n = (int)rs.cloud_layers.size();
  mix_in(h, n);
  for (const auto &L : rs.cloud_layers) {
    mix_in(h, L.type);
    mix_in(h, L.coverage);
    mix_in(h, L.density);
    mix_in(h, L.altitude);
    mix_in(h, L.thickness);
  }
  return h;
}
} // namespace

// Called once a frame with a live GL context, before anything reads the
// value. Cheap unless the sky moved.
void sky_light_update() {
  const RenderSettings &rs = render_settings();
  const uint64_t k = sky_key(rs);
  if (g_have && k == g_key) return;
  float v[3];
  if (renderer_sky_light(v)) {
    for (int i = 0; i < 3; ++i) g_light[i] = v[i];
    g_have = true;
    g_key = k;
  }
}

void sky_light_invalidate() { g_have = false; }

// The measured skylight, or - before the first measurement, and if the probe
// ever fails - the average of the two sky colours the viewport used to use,
// so a scene is never lit by nothing.
const float *sky_light_rgb() {
  static float fallback[3];
  if (g_have) return g_light;
  const RenderSettings &rs = render_settings();
  for (int i = 0; i < 3; ++i)
    fallback[i] = 0.5f * (rs.sky_zenith[i] + rs.sky_horizon[i]);
  return fallback;
}

} // namespace studio
