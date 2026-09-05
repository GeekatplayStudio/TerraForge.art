// Geekatplay TerraForge - the viewport transform gizmo.
//
// Screen-space throughout. The handles are the object's world axes projected
// with the very view-projection the frame was drawn with (renderer_last_mvp),
// and a drag converts pointer movement back into world units through the same
// projection. That is what lets one implementation serve a perspective view
// and three orthographic ones with no special cases, and it is why the gizmo
// never disagrees with what you see.
//
// Everything it writes is a field the Properties transform block also edits,
// so dragging and typing are two doors into the same numbers - and both go
// through undo the same way, one entry per drag rather than one per frame.
#include "gizmo_internal.hpp"
#include "app.hpp"
#include "icons.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

namespace gizmo_detail {

extern const float HANDLE_PX;
const float HANDLE_PX = 92.f;  // how long an axis reads on screen
const float GRAB_PX = 8.f;     // how close the pointer must come to grab one
extern const ImU32 AXIS_COL[3];
const ImU32 AXIS_COL[3] = {IM_COL32(226, 92, 84, 255), IM_COL32(150, 200, 92, 255),
                           IM_COL32(88, 150, 235, 255)};

GizmoMode g_mode = GizmoMode::Move;
bool g_visible = true;
gpx::Deform g_start_deform;

Drag g_drag;

// ---------------------------------------------------------------- projection
bool project(const float *mvp, const float p[3], ImVec2 origin, int w, int h,
             ImVec2 &out) {
  float x = mvp[0] * p[0] + mvp[4] * p[1] + mvp[8] * p[2] + mvp[12];
  float y = mvp[1] * p[0] + mvp[5] * p[1] + mvp[9] * p[2] + mvp[13];
  float cw = mvp[3] * p[0] + mvp[7] * p[1] + mvp[11] * p[2] + mvp[15];
  if (cw <= 1e-6f) return false; // behind the eye
  out.x = origin.x + (x / cw * 0.5f + 0.5f) * (float)w;
  // the view texture is drawn flipped, so +Y in clip space is up on screen
  out.y = origin.y + (0.5f - y / cw * 0.5f) * (float)h;
  return true;
}

float len2(ImVec2 v) { return v.x * v.x + v.y * v.y; }

// distance from `p` to the segment ab, in pixels
float dist_to_segment(ImVec2 p, ImVec2 a, ImVec2 b) {
  ImVec2 ab(b.x - a.x, b.y - a.y);
  float l = len2(ab);
  float t = l > 1e-6f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  ImVec2 c(a.x + ab.x * t, a.y + ab.y * t);
  return std::sqrt((p.x - c.x) * (p.x - c.x) + (p.y - c.y) * (p.y - c.y));
}

// -------------------------------------------------------------- what it edits
// Where the gadget sits, in world space, and what dragging it would change.
// An object with no place in the world (the terrain tile is the world; a group
// is bookkeeping) reports false and gets no gizmo rather than a lying one.
bool anchor_of(const SceneObject &o, float out[3]) {
  const RenderSettings &rs = render_settings();
  switch (o.type) {
    case SceneObject::Mesh:
    case SceneObject::Light:
      out[0] = o.pos[0];
      out[1] = o.pos[1] * rs.height_scale;
      out[2] = o.pos[2];
      return true;
    case SceneObject::Planet:
      out[0] = o.pos[0];
      out[1] = o.pos[1];
      out[2] = o.pos[2];
      return true;
    case SceneObject::Camera:
      out[0] = o.cam.eye[0];
      out[1] = o.cam.eye[1];
      out[2] = o.cam.eye[2];
      return true;
    case SceneObject::Water:
      out[0] = 0.5f;
      out[1] = rs.water_level * rs.height_scale;
      out[2] = 0.5f;
      return true;
    case SceneObject::Sun: {
      float d[3];
      compute_sun_dir(render_settings(), d);
      const float gd = 1.9f;
      out[0] = 0.5f + d[0] * gd;
      out[1] = rs.height_scale + d[1] * gd;
      out[2] = 0.5f + d[2] * gd;
      return true;
    }
    default:
      return false;
  }
}

// Which axes a mode offers for this object. Bit 0/1/2 = X/Y/Z, bit 3 = the
// uniform centre handle.
int axis_mask(const SceneObject &o, GizmoMode m) {
  switch (o.type) {
    case SceneObject::Mesh:
      if (m == GizmoMode::Scale) return 0xF;
      if (m == GizmoMode::Taper) return 0x8;   // one dial, the centre
      return 0x7;                              // move, rotate, twist, bend, skew: an axis each
    case SceneObject::Light:
      // a point light moves; a spot also turns; neither scales
      return m == GizmoMode::Move ? 0x7 : (m == GizmoMode::Rotate ? 0x3 : 0);
    case SceneObject::Planet:
      if (m == GizmoMode::Move) return 0x7;
      if (m == GizmoMode::Rotate) return 0x2; // spin about its own axis
      return 0x8;                             // radius, uniformly
    case SceneObject::Camera:
      return m == GizmoMode::Move ? 0x7 : 0;
    case SceneObject::Water:
      return m == GizmoMode::Move ? 0x2 : 0; // the level, and only the level
    case SceneObject::Sun:
      return m == GizmoMode::Move ? 0x7 : 0;
    default:
      return 0;
  }
}

const char *undo_label(GizmoMode m) {
  switch (m) {
    case GizmoMode::Move: return "Move object";
    case GizmoMode::Rotate: return "Rotate object";
    case GizmoMode::Scale: return "Scale object";
    case GizmoMode::Twist: return "Twist object";
    case GizmoMode::Bend: return "Bend object";
    case GizmoMode::Skew: return "Skew object";
    default: return "Taper object";
  }
}
bool ring_mode(GizmoMode m) {
  return m == GizmoMode::Rotate || m == GizmoMode::Twist || m == GizmoMode::Bend;
}

// Apply a world-space translation to whatever the object actually stores.
void apply_move(SceneObject &o, const float d[3]) {
  RenderSettings &rs = render_settings();
  switch (o.type) {
    case SceneObject::Mesh:
    case SceneObject::Light:
      o.pos[0] = g_drag.start_pos[0] + d[0];
      o.pos[1] = g_drag.start_pos[1] +
                 d[1] / std::max(rs.height_scale, 1e-5f);
      o.pos[2] = g_drag.start_pos[2] + d[2];
      break;
    case SceneObject::Planet:
      for (int i = 0; i < 3; ++i) o.pos[i] = g_drag.start_pos[i] + d[i];
      break;
    case SceneObject::Camera:
      // the aim point travels with the eye, so moving a camera does not also
      // swing it round to keep staring at wherever it was pointed
      for (int i = 0; i < 3; ++i) {
        float e = g_drag.start_pos[i] + d[i];
        o.cam.target[i] += e - o.cam.eye[i];
        o.cam.eye[i] = e;
      }
      break;
    case SceneObject::Water:
      rs.water_level = std::clamp(
          g_drag.start_extra + d[1] / std::max(rs.height_scale, 1e-5f), 0.f, 1.f);
      break;
    case SceneObject::Sun: {
      // move the light gadget, then read the direction back out of where it
      // ended up - so dragging it round the sky is exactly azimuth and altitude
      float p[3] = {g_drag.start_pos[0] + d[0], g_drag.start_pos[1] + d[1],
                    g_drag.start_pos[2] + d[2]};
      float v[3] = {p[0] - 0.5f, p[1] - rs.height_scale, p[2] - 0.5f};
      float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
      if (l < 1e-5f) return;
      for (float &c : v) c /= l;
      rs.sun_azimuth = std::atan2(v[0], v[2]) * 57.29578f;
      if (rs.sun_azimuth < 0.f) rs.sun_azimuth += 360.f;
      rs.sun_altitude = std::clamp(std::asin(std::clamp(v[1], -1.f, 1.f)) *
                                       57.29578f, 1.f, 89.f);
    } break;
    default:
      break;
  }
}

} // namespace gizmo_detail

using namespace gizmo_detail;

GizmoMode &gizmo_mode() { return g_mode; }
bool &gizmo_visible() { return g_visible; }
const char *gizmo_mode_name(GizmoMode m) {
  switch (m) {
    case GizmoMode::Move: return "Move";
    case GizmoMode::Rotate: return "Rotate";
    case GizmoMode::Scale: return "Scale";
    case GizmoMode::Twist: return "Twist";
    case GizmoMode::Bend: return "Bend";
    case GizmoMode::Skew: return "Skew";
    case GizmoMode::Taper: return "Taper";
    default: return "None";
  }
}

// ------------------------------------------------------------------- update
bool gizmo_update(App &a, int slot, const RenderSettings::ViewConfig &vc,
                  ImVec2 origin, int w, int h, bool view_hovered) {
  (void)vc;
  if (g_mode == GizmoMode::None || !g_visible) return false;
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) return false;
  SceneObject &o = sc.objects[sc.selected];
  if (!o.show_gizmo) return false;
  if (g_mode >= GizmoMode::Twist && o.type != SceneObject::Mesh) return false;
  // a locked object has no gizmo and cannot be dragged; a drag in flight
  // when the lock lands ends right there
  if (o.locked) {
    g_drag.active = false;
    return false;
  }
  const float *mvp = renderer_last_mvp(slot);
  if (!mvp) return false;
  float anchor[3];
  if (!anchor_of(o, anchor)) return false;
  int mask = axis_mask(o, g_mode);
  if (!mask) return false;

  ImVec2 c;
  if (!project(mvp, anchor, origin, w, h, c)) return false;
  ImGuiIO &io = ImGui::GetIO();

  // A drag already in progress owns the mouse until the button comes up, even
  // if the pointer has left the view - releasing outside must not strand it.
  if (g_drag.active && g_drag.object == sc.selected) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      g_drag.active = false;
      return true;
    }
    ImVec2 m = io.MousePos;
    if (g_drag.mode == GizmoMode::Move) {
      float dpx = (m.x - g_drag.start_mouse.x) * g_drag.dir_screen.x +
                  (m.y - g_drag.start_mouse.y) * g_drag.dir_screen.y;
      float units = g_drag.px_per_unit > 1e-4f ? dpx / g_drag.px_per_unit : 0.f;
      float d[3] = {0, 0, 0};
      d[g_drag.axis] = units;
      apply_move(o, d);
    } else if (ring_mode(g_drag.mode)) {
      float ang = std::atan2(m.y - c.y, m.x - c.x);
      float delta = (ang - g_drag.start_angle) * 57.29578f * g_drag.sign;
      while (delta > 180.f) delta -= 360.f;
      while (delta < -180.f) delta += 360.f;
      if (g_drag.mode == GizmoMode::Twist) {
        o.deform.twist[g_drag.axis] = std::clamp(g_start_deform.twist[g_drag.axis] + delta, -720.f, 720.f);
      } else if (g_drag.mode == GizmoMode::Bend) {
        o.deform.bend_axis = g_drag.axis;
        o.deform.bend = std::clamp(g_start_deform.bend + delta, -180.f, 180.f);
      } else if (o.type == SceneObject::Planet) {
        o.planet.spin_deg = g_drag.start_extra + delta;
      } else {
        float *dst = g_drag.axis == 0 ? &o.pitch
                     : g_drag.axis == 1 ? &o.yaw
                                        : &o.roll;
        *dst = g_drag.start_rot[g_drag.axis] + delta;
      }
    } else if (g_drag.mode == GizmoMode::Skew) {
      float dpx = (m.x - g_drag.start_mouse.x) * g_drag.dir_screen.x +
                  (m.y - g_drag.start_mouse.y) * g_drag.dir_screen.y;
      o.deform.shear[g_drag.axis] = std::clamp(g_start_deform.shear[g_drag.axis] + dpx / 150.f, -4.f, 4.f);
    } else if (g_drag.mode == GizmoMode::Taper) {
      float dpx = (m.x - g_drag.start_mouse.x);
      o.deform.taper = std::clamp(g_start_deform.taper + dpx / 150.f, -1.f, 3.f);
    } else { // scale
      float dpx = (m.x - g_drag.start_mouse.x) * g_drag.dir_screen.x +
                  (m.y - g_drag.start_mouse.y) * g_drag.dir_screen.y;
      float k = std::max(0.02f, 1.f + dpx / 120.f);
      if (g_drag.axis == 3) {
        if (o.type == SceneObject::Planet)
          o.planet.radius = std::max(0.001f, g_drag.start_extra * k);
        else
          o.scale = std::max(1e-5f, g_drag.start_scale * k);
      } else {
        o.scl[g_drag.axis] = std::clamp(g_drag.start_scl[g_drag.axis] * k,
                                        0.01f, 50.f);
      }
    }
    return true;
  }

  if (!view_hovered) return false;

  // --- hit test -------------------------------------------------------
  // A shared world length keeps the tripod a rigid frame, so foreshortening
  // reads as foreshortening rather than as three unrelated sticks.
  float px_per_unit[3] = {0, 0, 0};
  ImVec2 probe_dir[3];
  const float probe = 0.01f;
  for (int ax = 0; ax < 3; ++ax) {
    float p[3] = {anchor[0], anchor[1], anchor[2]};
    p[ax] += probe;
    ImVec2 q;
    if (!project(mvp, p, origin, w, h, q)) continue;
    ImVec2 d(q.x - c.x, q.y - c.y);
    float l = std::sqrt(len2(d));
    px_per_unit[ax] = l / probe;
    if (l > 1e-5f) probe_dir[ax] = ImVec2(d.x / l, d.y / l);
    else probe_dir[ax] = ImVec2(0, 0);
  }
  float best_px = std::max({px_per_unit[0], px_per_unit[1], px_per_unit[2]});
  if (best_px < 1e-4f) return false;
  float L = HANDLE_PX / best_px; // world length of a full handle

  int hot = -1;
  float hot_d = GRAB_PX;
  if (ring_mode(g_mode)) {
    // Rings are sampled rather than solved: 32 points is plenty to grab by,
    // and it needs no ellipse maths that a degenerate view could break.
    for (int ax = 0; ax < 3; ++ax) {
      if (!(mask & (1 << ax))) continue;
      int u = (ax + 1) % 3, v = (ax + 2) % 3;
      ImVec2 prev;
      bool have_prev = false;
      for (int i = 0; i <= 32; ++i) {
        float t = (float)i / 32.f * 6.2831853f;
        float p[3] = {anchor[0], anchor[1], anchor[2]};
        p[u] += std::cos(t) * L;
        p[v] += std::sin(t) * L;
        ImVec2 q;
        if (!project(mvp, p, origin, w, h, q)) { have_prev = false; continue; }
        if (have_prev) {
          float d = dist_to_segment(io.MousePos, prev, q);
          if (d < hot_d) { hot_d = d; hot = ax; }
        }
        prev = q;
        have_prev = true;
      }
    }
  } else {
    for (int ax = 0; ax < 3; ++ax) {
      if (!(mask & (1 << ax))) continue;
      float p[3] = {anchor[0], anchor[1], anchor[2]};
      p[ax] += L;
      ImVec2 q;
      if (!project(mvp, p, origin, w, h, q)) continue;
      float d = dist_to_segment(io.MousePos, c, q);
      if (d < hot_d) { hot_d = d; hot = ax; }
    }
    if (mask & 0x8) { // uniform handle: the box at the centre
      float d = std::sqrt((io.MousePos.x - c.x) * (io.MousePos.x - c.x) +
                          (io.MousePos.y - c.y) * (io.MousePos.y - c.y));
      if (d < 9.f) { hot = 3; hot_d = 0.f; }
    }
  }
  if (hot < 0) return false;

  ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return true; // hovering

  undo_push(a, undo_label(g_mode));
  g_drag.active = true;
  g_drag.axis = hot;
  g_drag.object = sc.selected;
  g_drag.mode = g_mode;
  g_drag.start_mouse = io.MousePos;
  g_drag.start_scale = o.scale;
  for (int i = 0; i < 3; ++i) g_drag.start_scl[i] = o.scl[i];
  g_drag.start_rot[0] = o.pitch;
  g_drag.start_rot[1] = o.yaw;
  g_drag.start_rot[2] = o.roll;
  g_start_deform = o.deform;
  if (o.type == SceneObject::Camera)
    for (int i = 0; i < 3; ++i) g_drag.start_pos[i] = o.cam.eye[i];
  else if (o.type == SceneObject::Sun || o.type == SceneObject::Water)
    for (int i = 0; i < 3; ++i) g_drag.start_pos[i] = anchor[i];
  else
    for (int i = 0; i < 3; ++i) g_drag.start_pos[i] = o.pos[i];
  g_drag.start_extra = o.type == SceneObject::Water ? render_settings().water_level
                       : o.type == SceneObject::Planet
                           ? (g_mode == GizmoMode::Rotate ? o.planet.spin_deg
                                                          : o.planet.radius)
                           : 0.f;
  if (hot < 3) {
    g_drag.dir_screen = probe_dir[hot];
    g_drag.px_per_unit = px_per_unit[hot];
  } else {
    g_drag.dir_screen = ImVec2(1.f, 0.f); // uniform: drag right to grow
    g_drag.px_per_unit = 1.f;
  }
  if (ring_mode(g_mode)) {
    g_drag.start_angle = std::atan2(io.MousePos.y - c.y, io.MousePos.x - c.x);
    float rt[3], up[3], fw[3];
    renderer_view_basis(vc, rt, up, fw);
    // a ring turning away from the eye reads as turning the other way
    float axis[3] = {0, 0, 0};
    axis[hot] = 1.f;
    float facing = axis[0] * fw[0] + axis[1] * fw[1] + axis[2] * fw[2];
    g_drag.sign = facing > 0.f ? -1.f : 1.f;
  }
  return true;
}

} // namespace studio
