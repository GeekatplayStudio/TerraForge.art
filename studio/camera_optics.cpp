// Geekatplay TerraForge - which lens a view is looking through.
//
// A view can look through the active camera, through a specific one (the
// Preview panel does), or through the free orbit which has no lens at all.
// This is the one place that decides, so the optical pass, the render
// exporter and the properties panel can never disagree about it.
#include "app.hpp"
#include "gpx/camera_math.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

namespace {

// Where the sun is on screen, so a flare can sit on it. Returns false when
// the sun is behind the camera or well outside the frame: a flare from a
// light that is not in the picture is the thing that gives cheap flares away.
//
// The direction is the one the sky draws its disc in (compute_sun_dir, which
// also knows date and time and a sun inside the world), projected at
// infinity. This used to rebuild the direction from azimuth and altitude with
// the axes the other way round - at the default azimuth the flare sat half
// the sky away from the sun - and then flipped the result upside down into a
// texture space the pass does not use.
bool sun_on_screen(const float *mvp, float out[2]) {
  const RenderSettings &rs = render_settings();
  float d[3];
  compute_sun_dir(rs, d);
  float c[4];
  for (int i = 0; i < 4; ++i) c[i] = mvp[i] * d[0] + mvp[4 + i] * d[1] + mvp[8 + i] * d[2];
  if (c[3] <= 1e-6f) return false; // behind the camera
  const float x = c[0] / c[3], y = c[1] / c[3];
  // a little past the edge still throws its streak and ghosts into the frame
  if (x < -1.6f || x > 1.6f || y < -1.6f || y > 1.6f) return false;
  out[0] = x * 0.5f + 0.5f;
  out[1] = y * 0.5f + 0.5f; // the pass's uv: y up, as the target's rows run
  return true;
}

// Is the Sun object shown? A hidden sun throws no flare.
bool sun_shown() {
  const SceneState &sc = scene();
  for (const SceneObject &o : sc.objects)
    if (o.type == SceneObject::Sun) return sc.object_visible(o);
  return true;
}

} // namespace

// The camera a view looks through: an index into the scene, or -1 for none.
int view_camera_index(const RenderSettings::ViewConfig &vc) {
  SceneState &sc = scene();
  int idx = vc.scene_camera;
  if (idx == -2) idx = scene_active_camera();
  if (idx < 0 || idx >= (int)sc.objects.size()) return -1;
  return sc.objects[(size_t)idx].type == SceneObject::Camera ? idx : -1;
}

LensOptics camera_optics_for_view(const RenderSettings::ViewConfig &vc,
                                  const float *mvp) {
  LensOptics o;
  // Orthographic views are drafting views, not photographs: no lens.
  if (vc.camera != 0) return o;
  int idx = view_camera_index(vc);
  if (idx < 0) return o;
  const CameraData &cd = scene().objects[(size_t)idx].cam;
  if (!cd.optics) return o;

  int nf = 0;
  const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&nf);
  const float sensor_w =
      F ? F[std::clamp(cd.format, 0, nf - 1)].width_mm : 36.f;

  o.on = true;
  o.k1 = cd.distortion_auto
             ? gpx::cam::lens_distortion_k1(cd.focal_mm, sensor_w)
             : cd.distortion;
  // The aperture decides how much a lens falls off; the user's own amount
  // scales that, so "1" means "what this lens would really do".
  o.vignette =
      std::clamp(gpx::cam::lens_vignette(cd.aperture) * cd.vignette, 0.f, 1.f);
  o.chromatic = std::max(0.f, cd.chromatic);
  o.flare = (cd.flare && sun_shown()) ? std::max(0.f, cd.flare_strength) : 0.f;
  o.flare_style = std::clamp(cd.flare_style, 0, 2);
  o.flare_rays = std::max(cd.flare_rays, 0.f);
  o.flare_streak = std::max(cd.flare_streak, 0.f);
  o.flare_ghosts = std::max(cd.flare_ghosts, 0.f);
  o.flare_halo = std::max(cd.flare_halo, 0.f);
  o.flare_core = std::max(cd.flare_core, 0.f);
  o.flare_ray_count = std::clamp(cd.flare_ray_count, 4, 64);
  o.flare_ray_length = std::clamp(cd.flare_ray_length, 0.05f, 5.f);
  o.flare_streak_length = std::clamp(cd.flare_streak_length, 0.05f, 5.f);
  for (int k = 0; k < 3; ++k) o.flare_streak_tint[k] = std::max(cd.flare_streak_tint[k], 0.f);
  o.flare_halo_radius = std::clamp(cd.flare_halo_radius, 0.01f, 1.f);
  o.flare_ghost_count = std::clamp(cd.flare_ghost_count, 0, 12);
  o.flare_blades = std::clamp(cd.flare_blades, 5, 9);
  o.flare_chroma = std::max(cd.flare_chroma, 0.f);
  o.flare_seed = std::max(cd.flare_seed, 0);
  o.bloom = std::clamp(cd.bloom, 0.f, 4.f);
  o.bloom_threshold = std::clamp(cd.bloom_threshold, 0.f, 0.99f);
  o.bloom_size = std::clamp(cd.bloom_size, 0.f, 1.f);
  if (o.flare > 0.f && mvp) {
    float s[2];
    if (sun_on_screen(mvp, s)) {
      o.sun[0] = s[0];
      o.sun[1] = s[1];
    }
    const RenderSettings &rs = render_settings();
    const float m = std::max({rs.sun_color[0], rs.sun_color[1], rs.sun_color[2], 1e-4f});
    for (int k = 0; k < 3; ++k) o.sun_rgb[k] = rs.sun_color[k] / m;
    // the disc's radius in frame heights: half its angle against the
    // camera's own vertical field of view
    const float fov = gpx::cam::fov_y_deg(cd.focal_mm, F ? F[std::clamp(cd.format, 0, nf - 1)].height_mm : 24.f);
    const float half = std::max(rs.sun_angle_deg, 0.05f) * 0.5f * 0.017453293f;
    o.sun_radius = std::tan(half) / std::max(2.f * std::tan(fov * 0.5f * 0.017453293f), 1e-4f);
  }
  // Motion blur follows the shutter: a long exposure smears more of the
  // camera's own movement into the frame. The movement itself is measured by
  // the renderer, which is the only thing that knows where the eye was last
  // frame; this only sets the scale.
  if (cd.motion_blur > 0.f)
    o.blur[0] = o.blur[1] = 0.f; // filled in by the renderer
  return o;
}

float camera_motion_blur_amount(const RenderSettings::ViewConfig &vc) {
  int idx = view_camera_index(vc);
  if (idx < 0) return 0.f;
  const CameraData &cd = scene().objects[(size_t)idx].cam;
  if (!cd.optics || cd.motion_blur <= 0.f) return 0.f;
  // Scaled by the shutter itself: 1/30 s smears about four times what
  // 1/125 s does, which is the whole reason the setting is called shutter.
  const float ref = 1.f / 125.f;
  float t = cd.shutter > 0.f ? cd.shutter / ref : 1.f;
  return cd.motion_blur * std::clamp(t, 0.25f, 8.f);
}

} // namespace studio
