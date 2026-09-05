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
// the sun is behind the camera or outside the frame: a flare from a light
// that is not in the picture is the thing that gives cheap flares away.
bool sun_on_screen(const float *mvp, float out[2]) {
  const RenderSettings &rs = render_settings();
  // The sun is a direction, placed far away along it.
  const float az = rs.sun_azimuth * 0.017453292519943295f;
  const float al = rs.sun_altitude * 0.017453292519943295f;
  const float d = 900.f;
  const float p[4] = {0.5f + std::cos(al) * std::sin(az) * d,
                      std::sin(al) * d,
                      0.5f + std::cos(al) * std::cos(az) * d, 1.f};
  float c[4];
  for (int i = 0; i < 4; ++i)
    c[i] = mvp[i] * p[0] + mvp[4 + i] * p[1] + mvp[8 + i] * p[2] +
           mvp[12 + i] * p[3];
  if (c[3] <= 0.f) return false; // behind the camera
  float x = c[0] / c[3], y = c[1] / c[3];
  if (x < -1.2f || x > 1.2f || y < -1.2f || y > 1.2f) return false;
  out[0] = x * 0.5f + 0.5f;
  out[1] = 1.f - (y * 0.5f + 0.5f); // the pass works in texture space
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
  o.flare = cd.flare ? std::max(0.f, cd.flare_strength) : 0.f;
  if (o.flare > 0.f && mvp) {
    float s[2];
    if (sun_on_screen(mvp, s)) {
      o.sun[0] = s[0];
      o.sun[1] = s[1];
    }
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
