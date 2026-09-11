// Geekatplay TerraForge - the renderer's cameras and rays: the free orbit
// camera, scene cameras, orthographic views, the view/projection matrices,
// and every pick that casts a ray through them. Split from renderer.cpp for
// the 500-line module rule; state lives in renderer_internal.hpp.
#include "renderer_internal.hpp"
#include "terrain_tiles.hpp"
#include "terrain_xform.hpp"
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "world_shape.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stb_image_write.h"
#include "renderer_shaders.hpp"

namespace studio {


// Flies one named scene camera. A scene camera stores an explicit
// eye/target pair, so orbit, pan and dolly operate on that pair directly.
//
// The camera is named by the caller rather than looked up here, and that is
// the whole point: it used to drive `scene_active_camera()` whatever window
// the mouse was in, so a drag in a free viewport moved the active camera
// instead of the view, and a drag in a viewport locked to some other camera
// moved the active one - the picture that changed was never the one under
// the pointer. Each viewport passes the camera it is actually showing
// (view_camera_index), and -1 for a free orbit.
bool camera_fly(int cam, float dx, float dy, float wheel, bool rotating,
                bool panning, bool dolly, bool screen_pan) {
  SceneState &sc = scene();
  if (cam < 0 || cam >= (int)sc.objects.size() ||
      sc.objects[(size_t)cam].type != SceneObject::Camera)
    return false;
  CameraData &cd = sc.objects[(size_t)cam].cam;
  float d[3] = {cd.eye[0] - cd.target[0], cd.eye[1] - cd.target[1],
                cd.eye[2] - cd.target[2]};
  float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (dist < 1e-5f) dist = 1e-5f;
  float yaw = std::atan2(d[0], d[2]);
  float pitch = std::asin(std::clamp(d[1] / dist, -1.f, 1.f));
  if (rotating) {
    yaw += dx * 0.01f;
    pitch = std::clamp(pitch + dy * 0.01f, -1.5707963f, 1.5707963f);
  }
  // 400 tile units is two thousand kilometres on the default world, which
  // is nowhere near far enough to back a camera off a planet
  if (wheel != 0.f) dist = std::clamp(dist * (1.f - wheel * 0.12f), 1e-8f, 1.0e9f);
  if (dolly) dist = std::clamp(dist * (1.f + dy * 0.005f), 1e-8f, 1.0e9f);
  if (screen_pan) {
    // up, down, left and right on the screen: the camera's own right and up
    // axes, so the ground slides under the pointer whichever way it is
    // tipped. The same two axes the free orbit pans along, because a person
    // who has learned one viewport should not have to learn the next.
    const float s = dist * 0.0015f;
    const float cp2 = std::cos(pitch), sp2 = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float R[3] = {cy, 0.f, -sy};
    const float U[3] = {-sp2 * sy, cp2, -sp2 * cy};
    for (int i = 0; i < 3; ++i) cd.target[i] += (-dx * R[i] + dy * U[i]) * s;
  } else if (panning) {
    // across the ground, as the middle and right buttons have always done
    float s = dist * 0.0015f;
    float cy = std::cos(yaw), sy = std::sin(yaw);
    float mx = (-dx * cy - dy * sy) * s, mz = (dx * sy - dy * cy) * s;
    cd.target[0] += mx;
    cd.target[2] += mz;
  }
  float cp = std::cos(pitch);
  cd.eye[0] = cd.target[0] + dist * cp * std::sin(yaw);
  cd.eye[1] = cd.target[1] + dist * std::sin(pitch);
  cd.eye[2] = cd.target[2] + dist * cp * std::cos(yaw);
  return true;
}


static void zoom_toward_ground(float before); // below

bool renderer_camera_input(int cam, float dx, float dy, float wheel,
                           bool rotating, bool panning, bool dolly) {
  if (camera_fly(cam, dx, dy, wheel, rotating, panning, dolly, false))
    return true;
  if (dolly) {
    const float before = CAM.dist;
    CAM.dist = std::fmin(
        std::fmax(CAM.dist * (1.f + dy * 0.005f), 1e-8f), 100000.f);
    zoom_toward_ground(before); // a dolly in converges on the ground too
  }
  renderer_handle_input(dx, dy, wheel, rotating, panning);
  return false;
}


// point the active camera at a world position, keeping its distance
void renderer_camera_look_at(const float target[3], float distance) {
  SceneState &sc = scene();
  int active = scene_active_camera();
  if (active >= 0 && active < (int)sc.objects.size() &&
      sc.objects[active].type == SceneObject::Camera) {
    CameraData &cd = sc.objects[active].cam;
    float d[3] = {cd.eye[0] - cd.target[0], cd.eye[1] - cd.target[1],
                  cd.eye[2] - cd.target[2]};
    float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (distance > 0) dist = distance;
    if (dist < 1e-4f) dist = 1.f;
    float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-5f) { d[0] = 0; d[1] = 0.4f; d[2] = 1.f; len = 1.077f; }
    for (int i = 0; i < 3; ++i) {
      cd.target[i] = target[i];
      cd.eye[i] = target[i] + d[i] / len * dist;
    }
    return;
  }
  for (int i = 0; i < 3; ++i) CAM.target[i] = target[i];
  if (distance > 0) CAM.dist = distance;
}


// The ground under a point of the tile, in world units. The picking copy is
// coarse (256 across, ~20 m a texel at 5 km) but it is the only ground the
// CPU has, and it is four hundred metres better than the alternative below.
float renderer_ground_under(float x, float z) {
  if (cpu_height.empty()) return 0.f;
  return cpu_height.sample(std::clamp(x, 0.f, 1.f), std::clamp(z, 0.f, 1.f)) *
         render_settings().height_scale;
}

// Zooming in pulls the pivot down onto the ground.
//
// The pivot's height was set once and never moved again: 0.08 world units,
// which is 400 m on a 5 km tile. Since the eye converges on the pivot, over
// any ground lower than that the zoom simply stopped, hundreds of metres up,
// with nothing in the code that looked like a limit - the distance clamp
// reaches 0.05 mm and never gets the chance. Keeping the pivot's height
// above the ground proportional to the distance is what makes a zoom
// converge on the surface: halve the distance, halve the gap, and centimetre
// range is reachable in the same number of notches as everything else.
static void zoom_toward_ground(float before) {
  if (CAM.dist >= before) return; // pulling out leaves the pivot alone
  const float k = CAM.dist / std::max(before, 1e-30f);
  const float g = renderer_ground_under(CAM.target[0], CAM.target[2]);
  CAM.target[1] = g + (CAM.target[1] - g) * k;
}

bool renderer_pan_screen(int cam, float dx, float dy) {
  // a scene camera pans its own view plane, by the same axes as the free one
  if (camera_fly(cam, dx, dy, 0.f, false, false, false, true)) return true;
  const float s = CAM.dist * 0.0015f;
  const float cp = std::cos(CAM.pitch), sp = std::sin(CAM.pitch);
  const float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
  // screen right, and the camera's own up (the eye's derivative in pitch -
  // the same axis the view matrix uses), so the ground slides under the
  // pointer whichever way the camera is tipped
  const float R[3] = {cy, 0.f, -sy};
  const float U[3] = {-sp * sy, cp, -sp * cy};
  for (int i = 0; i < 3; ++i) CAM.target[i] += (-dx * R[i] + dy * U[i]) * s;
  return false;
}

void renderer_handle_input(float dx, float dy, float wheel, bool rotating,
                           bool panning) {
  if (rotating) {
    CAM.yaw += dx * 0.01f; // unbounded: full 360° orbit
    // Straight down and straight up are both reachable now. The limit used to
    // stop 1.2 degrees short of each, because the up vector was guessed from
    // world up and went singular there; it comes from the camera's own angles
    // instead, so the pole is an ordinary place to be.
    CAM.pitch = std::fmin(std::fmax(CAM.pitch + dy * 0.01f, -1.5707963f),
                          1.5707963f);
  }
  if (panning) {
    float s = CAM.dist * 0.0015f;
    float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
    CAM.target[0] += (-dx * cy - dy * sy) * s;
    CAM.target[2] += (dx * sy - dy * cy) * s;
    // panning across the ground keeps the pivot on the ground, so a zoom
    // from anywhere lands on the surface rather than in the air over it
    const float g = renderer_ground_under(CAM.target[0], CAM.target[2]);
    const float gap = CAM.target[1] - g;
    if (std::fabs(gap) < CAM.dist) CAM.target[1] = g + gap * 0.9f;
  }
  // zoom range spans a grain of sand to a whole planetary neighbourhood
  if (wheel != 0) {
    const float before = CAM.dist;
    CAM.dist = std::fmin(
        std::fmax(CAM.dist * (1.f - wheel * 0.12f), 1e-8f), 100000.f);
    zoom_toward_ground(before);
  }
}


void ortho_matrices(const RenderSettings::ViewConfig &vc, int w, int h,
                           float hscale, float *eye, float *mvp, float *inv_vp) {
  float cx = vc.ortho_cx, cy = vc.ortho_cy;
  float sx3[3], uy3[3], fz3[3];
  switch (vc.camera) {
    case 1:
      eye[0] = cx; eye[1] = 3.f; eye[2] = cy;
      fz3[0] = 0; fz3[1] = -1; fz3[2] = 0;
      sx3[0] = 1; sx3[1] = 0; sx3[2] = 0;
      uy3[0] = 0; uy3[1] = 0; uy3[2] = -1;
      break;
    case 2:
      eye[0] = cx; eye[1] = cy * hscale * 2.f; eye[2] = -3.f;
      fz3[0] = 0; fz3[1] = 0; fz3[2] = 1;
      sx3[0] = 1; sx3[1] = 0; sx3[2] = 0;
      uy3[0] = 0; uy3[1] = 1; uy3[2] = 0;
      break;
    default:
      eye[0] = 3.f; eye[1] = cy * hscale * 2.f; eye[2] = cx;
      fz3[0] = -1; fz3[1] = 0; fz3[2] = 0;
      sx3[0] = 0; sx3[1] = 0; sx3[2] = 1;
      uy3[0] = 0; uy3[1] = 1; uy3[2] = 0;
      break;
  }
  float view[16] = {sx3[0], uy3[0], -fz3[0], 0, sx3[1], uy3[1], -fz3[1], 0,
                    sx3[2], uy3[2], -fz3[2], 0,
                    -(sx3[0] * eye[0] + sx3[1] * eye[1] + sx3[2] * eye[2]),
                    -(uy3[0] * eye[0] + uy3[1] * eye[1] + uy3[2] * eye[2]),
                    fz3[0] * eye[0] + fz3[1] * eye[1] + fz3[2] * eye[2], 1};
  float aspect = w / float(h);
  float r = vc.ortho_zoom * 0.5f, znear = 0.01f, zfar = 10.f;
  float proj[16] = {1.f / (r * aspect), 0, 0, 0, 0, 1.f / r, 0, 0,
                    0, 0, -2.f / (zfar - znear), 0,
                    0, 0, -(zfar + znear) / (zfar - znear), 1};
  mat_mul(mvp, proj, view);
  mat_inverse(inv_vp, mvp);
}


// Builds the view/projection for either the free viewport camera or a scene
// camera object (which carries an explicit eye/target and a physical lens).
// Where the perspective view is looking from and at: the free orbit camera,
// or the scene camera the user is looking through. One place, so the matrices
// and the orientation gizmo can never disagree about which way is up.
int &renderer_camera_override() {
  static int v = -2; // -2 = none: use the active camera
  return v;
}

void renderer_orbit_get(float target[3], float &yaw, float &pitch, float &dist) {
  for (int i = 0; i < 3; ++i) target[i] = CAM.target[i];
  yaw = CAM.yaw; pitch = CAM.pitch; dist = CAM.dist;
}
void renderer_orbit_set(const float target[3], float yaw, float pitch, float dist) {
  for (int i = 0; i < 3; ++i) CAM.target[i] = target[i];
  CAM.yaw = yaw;
  CAM.pitch = std::clamp(pitch, -1.55f, 1.55f);
  CAM.dist = std::clamp(dist, 1e-8f, 100000.f);
}

// The free orbit's own field of view. Named here because two places need
// to agree on it: the projection this view draws with, and the focal length
// "copy this view to a camera" gives that camera so it frames the same
// picture (layout_store.cpp).
float renderer_free_fovy_rad() { return 0.9f; }

float perspective_eye_target(float *eye, float *target, float *up_out) {
  float fovy_rad = renderer_free_fovy_rad();
  SceneState &sc = scene();
  int ov = renderer_camera_override();
  int active = ov != -2 ? ov : scene_active_camera();
  bool have_up = false;
  if (active >= 0 && active < (int)sc.objects.size() &&
      sc.objects[active].type == SceneObject::Camera) {
    const CameraData &cd = sc.objects[active].cam;
    for (int i = 0; i < 3; ++i) {
      eye[i] = cd.eye[i];
      target[i] = cd.target[i];
    }
    int nf = 0;
    const gpx::cam::SensorFormat *F = gpx::cam::sensor_formats(&nf);
    const gpx::cam::SensorFormat &f = F[std::clamp(cd.format, 0, nf - 1)];
    fovy_rad = gpx::cam::fov_y_deg(cd.focal_mm, f.height_mm) * 0.017453293f;
  } else {
    float cp = std::cos(CAM.pitch), sp = std::sin(CAM.pitch);
    float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
    eye[0] = CAM.target[0] + CAM.dist * cp * sy;
    eye[1] = CAM.target[1] + CAM.dist * sp;
    eye[2] = CAM.target[2] + CAM.dist * cp * cy;
    for (int i = 0; i < 3; ++i) target[i] = CAM.target[i];
    // The orbit camera's own up axis, exactly: the derivative of the eye
    // position with respect to pitch. It never needs guessing at, it is
    // continuous straight through looking-vertically-down, and at the pole it
    // points the way the camera is facing - which is what "up on screen"
    // should mean when you are directly overhead.
    if (up_out) {
      up_out[0] = -sp * sy;
      up_out[1] = cp;
      up_out[2] = -sp * cy;
      have_up = true;
    }
  }
  if (up_out && !have_up) {
    // A scene camera stores only an eye and a target, so the up has to be
    // reconstructed. World up does it everywhere except near vertical, where
    // crossed with a vertical forward it vanishes.
    //
    // The old code answered that by *snapping* the reference sideways the
    // moment the view came within 2.6 degrees of straight down, which rotates
    // the picture in a single frame - the terrain appearing to flip as the
    // camera passed over the top. Instead the reference eases into the
    // camera's own heading over those last degrees, which is where the orbit
    // camera's up tends analytically, so the two agree rather than fight.
    float fz[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float fl = std::sqrt(fz[0]*fz[0] + fz[1]*fz[1] + fz[2]*fz[2]);
    if (fl > 1e-9f) for (float &v : fz) v /= fl;
    up_out[0] = 0.f; up_out[1] = 1.f; up_out[2] = 0.f;
    const float ay = std::fabs(fz[1]);
    const float hl = std::sqrt(fz[0]*fz[0] + fz[2]*fz[2]);
    if (ay > 0.985f && hl > 1e-6f) {
      const float s = fz[1] > 0.f ? -1.f : 1.f; // looking up flips the heading
      const float t = std::min((ay - 0.985f) / (0.9999f - 0.985f), 1.f);
      up_out[0] = up_out[0] * (1.f - t) + (s * fz[0] / hl) * t;
      up_out[1] = up_out[1] * (1.f - t);
      up_out[2] = up_out[2] * (1.f - t) + (s * fz[2] / hl) * t;
      const float ul = std::sqrt(up_out[0]*up_out[0] + up_out[1]*up_out[1] +
                                 up_out[2]*up_out[2]);
      if (ul > 1e-9f) for (int i = 0; i < 3; ++i) up_out[i] /= ul;
      else { up_out[0] = 1.f; up_out[1] = 0.f; up_out[2] = 0.f; }
    }
  }
  return fovy_rad;
}


// The view's right and up axes in world space. That is everything the corner
// orientation gizmo needs in order to project the world axes onto the screen,
// and it is derived from the same numbers the view matrix is built from.
void renderer_view_basis(const RenderSettings::ViewConfig &vc, float *right,
                         float *up, float *fwd) {
  auto set = [](float *v, float x, float y, float z) { v[0]=x; v[1]=y; v[2]=z; };
  if (vc.camera == 1) { set(right,1,0,0); set(up,0,0,-1); set(fwd,0,-1,0); return; }
  if (vc.camera == 2) { set(right,1,0,0); set(up,0,1,0);  set(fwd,0,0,1);  return; }
  if (vc.camera == 3) { set(right,0,0,1); set(up,0,1,0);  set(fwd,-1,0,0); return; }
  float eye[3], target[3], u0[3];
  perspective_eye_target(eye, target, u0);
  float fz[3] = {target[0]-eye[0], target[1]-eye[1], target[2]-eye[2]};
  float fl = std::sqrt(fz[0]*fz[0] + fz[1]*fz[1] + fz[2]*fz[2]);
  if (fl < 1e-8f) { set(right,1,0,0); set(up,0,1,0); set(fwd,0,0,-1); return; }
  for (float &v : fz) v /= fl;
  // the same up the view matrix uses, so the corner gizmo can never disagree
  // with what is on screen
  float sx[3] = {fz[1]*u0[2]-fz[2]*u0[1], fz[2]*u0[0]-fz[0]*u0[2],
                 fz[0]*u0[1]-fz[1]*u0[0]};
  float sl = std::sqrt(sx[0]*sx[0] + sx[1]*sx[1] + sx[2]*sx[2]);
  for (float &v : sx) v /= sl;
  float uy[3] = {sx[1]*fz[2]-sx[2]*fz[1], sx[2]*fz[0]-sx[0]*fz[2],
                 sx[0]*fz[1]-sx[1]*fz[0]};
  for (int i = 0; i < 3; ++i) { right[i] = sx[i]; up[i] = uy[i]; fwd[i] = fz[i]; }
}


// Swing the free orbit camera round to look straight down a world axis, the
// way clicking a ball on the gizmo does in every other 3D application.
void renderer_camera_snap_axis(int axis, bool negative) {
  const float HALF_PI = 1.5707963f;
  float s = negative ? -1.f : 1.f;
  switch (axis) {
    case 0: CAM.yaw = s * HALF_PI;  CAM.pitch = 0.f; break;          // X
    case 1: CAM.yaw = 0.f;          CAM.pitch = s * 1.5533f; break;  // Y
    default: CAM.yaw = negative ? 3.14159265f : 0.f; CAM.pitch = 0.f; break;
  }
}


void camera_matrices(int w, int h, float *eye, float *mvp, float *inv_vp) {
  float target[3], up[3];
  float fovy_rad = perspective_eye_target(eye, target, up);
  float fz[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
  float fl = std::sqrt(fz[0] * fz[0] + fz[1] * fz[1] + fz[2] * fz[2]);
  // A zoom that reaches the distance floor puts the eye on the pivot, and
  // dividing by that length would make every matrix in the frame NaN - a
  // black window with no error anywhere. renderer_view_basis already
  // guards the same computation; this one did not.
  if (!(fl > 1e-9f)) {
    fz[0] = 0.f; fz[1] = 0.f; fz[2] = 1.f;
    fl = 1.f;
  }
  for (float &v : fz) v /= fl;
  // `up` comes from the camera itself (see perspective_eye_target); this used
  // to guess at it from world up and snap near vertical, which is what made
  // the view flip as it passed over the top.
  float sx[3] = {fz[1] * up[2] - fz[2] * up[1], fz[2] * up[0] - fz[0] * up[2],
                 fz[0] * up[1] - fz[1] * up[0]};
  float sl = std::sqrt(sx[0] * sx[0] + sx[1] * sx[1] + sx[2] * sx[2]);
  for (float &v : sx) v /= sl;
  float uy[3] = {sx[1] * fz[2] - sx[2] * fz[1], sx[2] * fz[0] - sx[0] * fz[2],
                 sx[0] * fz[1] - sx[1] * fz[0]};
  float view[16] = {sx[0], uy[0], -fz[0], 0, sx[1], uy[1], -fz[1], 0,
                    sx[2], uy[2], -fz[2], 0,
                    -(sx[0] * eye[0] + sx[1] * eye[1] + sx[2] * eye[2]),
                    -(uy[0] * eye[0] + uy[1] * eye[1] + uy[2] * eye[2]),
                    fz[0] * eye[0] + fz[1] * eye[1] + fz[2] * eye[2], 1};
  float aspect = w / float(h);
  g_last_fovy = fovy_rad;
  float cam_d = std::sqrt((eye[0]-target[0])*(eye[0]-target[0]) + (eye[1]-target[1])*(eye[1]-target[1]) + (eye[2]-target[2])*(eye[2]-target[2]));
  // The near plane follows the zoom down, but only so far.
  //
  // A 24-bit fixed-point depth buffer resolves a fragment from the cleared
  // 1.0 only while `znear / z > 2^-24`, so the furthest thing that can write
  // depth at all is `znear * 2^24`. With znear = cam_d * 0.002 that is
  // cam_d * 33554: at a camera distance of 1 cm it is 335 m, and every piece
  // of ground beyond quantises to exactly 1.0, fails GL_LESS against the
  // clear, and is never drawn - the sky shows through instead and the view
  // goes pale. That is the whole of the "it goes white when I zoom in" bug,
  // and it starts at about 1.6 m.
  //
  // The floor is the fix: 5e-7 tile units keeps `znear * 2^24` at 8.4 tiles,
  // past the horizon of the tile and its surround, for any zoom. It costs
  // the ability to see something closer than 2.5 mm to the eye, which is
  // below the 0.3 mm the world's float coordinates can express anyway.
  float znear = std::clamp(cam_d * 0.002f, 5e-7f, 0.5f);
  // the far plane follows the zoom so pulling out reveals the whole planetary
  // neighborhood; planets themselves render as a depth-write-free sky layer,
  // so they are never clipped by it regardless
  float zfar = std::max(cam_d * 40.f, 60.f);
  // an inside world's far side is 2R away and is the picture (world_shape.hpp)
  {
    const RenderSettings &rsw = render_settings();
    if (world_has_inside(rsw) && rsw.planet_radius > 0.f)
      zfar = std::max(zfar, rsw.planet_radius * 2.4f + 10.f);
  }
  float f = 1.f / std::tan(fovy_rad * 0.5f);
  float proj[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0,
                    0, 0, (zfar + znear) / (znear - zfar), -1,
                    0, 0, 2 * zfar * znear / (znear - zfar), 0};
  mat_mul(mvp, proj, view);
  mat_inverse(inv_vp, mvp);
}


float renderer_view_width_m(const RenderSettings::ViewConfig &vc) {
  const RenderSettings &RS = render_settings();
  if (vc.camera == 0) return CAM.dist * RS.terrain_size_m;
  return vc.ortho_zoom * RS.terrain_size_m;
}


void renderer_view_input(RenderSettings::ViewConfig &vc, float dx, float dy,
                         float wheel, bool rotating, bool panning, int view_w) {
  if (vc.camera == 0) {
    renderer_handle_input(dx, dy, wheel, rotating, panning);
    return;
  }
  if (wheel != 0)
    vc.ortho_zoom = std::fmin(std::fmax(vc.ortho_zoom * (1.f - wheel * 0.12f), 0.0004f), 400.f);
  if (rotating || panning) {
    float s = vc.ortho_zoom / std::max(view_w, 1);
    if (vc.camera == 1) {
      vc.ortho_cx -= dx * s;
      vc.ortho_cy -= dy * s;
    } else {
      vc.ortho_cx -= dx * s;
      vc.ortho_cy += dy * s;
    }
  }
}


void renderer_get_camera(float eye[3], float target[3], float *fovy_deg) {
  float cp = std::cos(CAM.pitch), sp = std::sin(CAM.pitch);
  float cy = std::cos(CAM.yaw), sy = std::sin(CAM.yaw);
  eye[0] = CAM.target[0] + CAM.dist * cp * sy;
  eye[1] = CAM.target[1] + CAM.dist * sp;
  eye[2] = CAM.target[2] + CAM.dist * cp * cy;
  for (int i = 0; i < 3; ++i) target[i] = CAM.target[i];
  *fovy_deg = 0.9f * 57.29578f;
}


void renderer_set_brush_cursor(float tx, float tz, float radius, bool erasing) {
  g_brush[0] = tx;
  g_brush[1] = tz;
  g_brush[2] = radius;
  g_brush[3] = erasing ? 1.f : 0.f;
}


// ------------------------------------------------------------------ picking
bool ray_sphere(const float *ro, const float *rd, const float *c, float r,
                       float &t) {
  float oc[3] = {ro[0] - c[0], ro[1] - c[1], ro[2] - c[2]};
  float b = oc[0] * rd[0] + oc[1] * rd[1] + oc[2] * rd[2];
  float cc = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - r * r;
  float disc = b * b - cc;
  if (disc < 0) return false;
  float sq = std::sqrt(disc);
  t = -b - sq;
  if (t < 0) t = -b + sq;
  return t > 0;
}


// World-space ray through a view coordinate. Shared by object picking and by
// the terrain brushes, so both agree on where the cursor is pointing.
bool view_ray(const RenderSettings::ViewConfig &vc, float u, float v, int w,
                     int h, float ro[3], float rd[3]) {
  RenderSettings &RS = render_settings();
  float eye[3], mvp[16], inv_vp[16];
  if (vc.camera == 0) camera_matrices(w, h, eye, mvp, inv_vp);
  else ortho_matrices(vc, w, h, RS.height_scale, eye, mvp, inv_vp);
  float ndc_x = u * 2.f - 1.f, ndc_y = 1.f - v * 2.f;
  auto unproject = [&](float z, float *out) {
    float p[4] = {ndc_x, ndc_y, z, 1.f};
    float r[4];
    for (int i = 0; i < 4; ++i) {
      r[i] = 0;
      for (int k = 0; k < 4; ++k) r[i] += inv_vp[k * 4 + i] * p[k];
    }
    float iw = std::fabs(r[3]) > 1e-9f ? 1.f / r[3] : 1.f;
    out[0] = r[0] * iw; out[1] = r[1] * iw; out[2] = r[2] * iw;
  };
  float pf[3];
  unproject(-1.f, ro);
  unproject(1.f, pf);
  rd[0] = pf[0] - ro[0];
  rd[1] = pf[1] - ro[1];
  rd[2] = pf[2] - ro[2];
  float rl = std::sqrt(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
  if (rl < 1e-9f) return false;
  for (int i = 0; i < 3; ++i) rd[i] /= rl;
  return true;
}


// March the height field and report where the cursor lands on it, in
// normalized terrain coordinates. This is what positions a sculpt brush.
bool renderer_pick_terrain(int slot, const RenderSettings::ViewConfig &vc, float u,
                           float v, int w, int h, float &tx, float &tz) {
  (void)slot;
  RenderSettings &RS = render_settings();
  if (cpu_height.empty()) return false;
  float pn[3], rd[3];
  if (!view_ray(vc, u, v, w, h, pn, rd)) return false;
  float prev_diff = 0;
  bool have_prev = false;
  float step = 0.003f;
  for (float tt = 0.f; tt < 12.f; tt += step) {
    float x = pn[0] + rd[0] * tt, y = pn[1] + rd[1] * tt, z = pn[2] + rd[2] * tt;
    if (x < 0.f || x > 1.f || z < 0.f || z > 1.f) {
      have_prev = false;
      if (y < -0.5f) break;
      continue;
    }
    float terr = cpu_height.sample(x, z) * RS.height_scale;
    float diff = y - terr;
    if (have_prev && prev_diff > 0 && diff <= 0) {
      float f = diff / (diff - prev_diff + 1e-9f);
      float hit = tt - step * f;
      tx = std::clamp(pn[0] + rd[0] * hit, 0.f, 1.f);
      tz = std::clamp(pn[2] + rd[2] * hit, 0.f, 1.f);
      return true;
    }
    prev_diff = diff;
    have_prev = true;
    step = std::min(step * 1.02f, 0.04f);
  }
  return false;
}


int renderer_pick(int slot, const RenderSettings::ViewConfig &vc, float u, float v,
                  int w, int h) {
  (void)slot;
  RenderSettings &RS = render_settings();
  float pn[3], rd[3];
  if (!view_ray(vc, u, v, w, h, pn, rd)) return -1;

  SceneState &sc = scene();
  int best_idx = -1;
  float best_t = 1e30f;
  float sun[3];
  compute_sun_dir(RS, sun);

  // planets first: they are behind everything else, so any closer hit below
  // simply replaces this one
  {
    float pt;
    int p = planet_pick(pn, rd, pt);
    if (p >= 0) {
      best_idx = p;
      best_t = pt;
    }
  }

  for (size_t i = 0; i < sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    if (!sc.object_visible(o)) continue;
    float t = 0;
    if (o.type == SceneObject::Sun) {
      float gd = 1.9f;
      float c[3] = {0.5f + sun[0] * gd, RS.height_scale + sun[1] * gd,
                    0.5f + sun[2] * gd};
      if (ray_sphere(pn, rd, c, 0.07f, t) && t < best_t) {
        best_t = t;
        best_idx = (int)i;
      }
    } else if (o.type == SceneObject::Mesh) {
      float rr = scene_object_radius(o);
      float c[3] = {o.pos[0], o.pos[1] * RS.height_scale + rr * 0.5f, o.pos[2]};
      if (ray_sphere(pn, rd, c, rr * 0.75f, t) && t < best_t) {
        best_t = t;
        best_idx = (int)i;
      }
    } else if (o.type == SceneObject::Water && RS.show_water) {
      float lv = RS.water_level * RS.height_scale;
      if (std::fabs(rd[1]) > 1e-6f) {
        t = (lv - pn[1]) / rd[1];
        if (t > 0) {
          float x = pn[0] + rd[0] * t, z = pn[2] + rd[2] * t;
          if (x >= 0 && x <= 1 && z >= 0 && z <= 1) {
            float bed = cpu_height.empty() ? 0.f
                                           : cpu_height.sample(x, z) * RS.height_scale;
            if (bed < lv && t < best_t) {
              best_t = t;
              best_idx = (int)i;
            }
          }
        }
      }
    } else if (o.type == SceneObject::Terrain) {
      // this object's tile (terrain_tiles.hpp), through its transform: the
      // ray is walked in world units and the heightmap read where the
      // tile's inverse says it lies
      const int tile = terrain_tile_for_object((int)i);
      if (tile < 0) continue;
      TileSwap swap(tile);
      if (cpu_height.empty()) continue;
      const TerrainXform tx = terrain_xform_of(o, RS.height_scale);
      // march the heightfield
      float t0 = 0.f, t1 = 12.f;
      float prev_diff = 0;
      bool have_prev = false;
      float step = 0.004f;
      for (float tt = t0; tt < t1; tt += step) {
        float x = pn[0] + rd[0] * tt, y = pn[1] + rd[1] * tt, z = pn[2] + rd[2] * tt;
        float u = x, v = z;
        terrain_xform_unapply_xz(tx, x, z, u, v);
        if (u < -0.05f || u > 1.05f || v < -0.05f || v > 1.05f) {
          have_prev = false;
          if (y < -0.5f) break;
          continue;
        }
        float terr = cpu_height.sample(std::clamp(u, 0.f, 1.f),
                                       std::clamp(v, 0.f, 1.f)) * RS.height_scale * tx.scl[1] +
                     (tx.on ? tx.pos[1] : 0.f);
        float diff = y - terr;
        if (have_prev && prev_diff > 0 && diff <= 0) {
          float hit_t = tt - step * (diff / (diff - prev_diff + 1e-9f));
          if (hit_t < best_t) {
            best_t = hit_t;
            best_idx = (int)i;
          }
          break;
        }
        prev_diff = diff;
        have_prev = true;
        step = std::min(step * 1.02f, 0.05f);
      }
    }
  }
  return best_idx;
}

} // namespace studio
