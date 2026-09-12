// Geekatplay TerraForge — the world's shape (world_shape.hpp).
#include "world_shape.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <cmath>
#include <cstring>

namespace studio {

gpx::planet::Shape world_shape(const RenderSettings &rs, int side) {
  gpx::planet::Shape S;
  const bool flat = rs.world_shape == WORLD_FLAT;
  S.flat_x = flat;
  S.flat_z = flat || rs.world_shape == WORLD_RING;
  // a flat world has no centre to be inside of
  S.inside = rs.world_inside && !flat;
  // the other face of the same shell, not another shell
  const int own = S.inside ? SIDE_INSIDE : SIDE_OUTSIDE;
  S.flip = side != SIDE_WORLD && side != own;
  S.thick = rs.world_thickness > 0.f ? rs.world_thickness : 0.f;
  return S;
}

int object_side(const RenderSettings &rs, const SceneObject &o) {
  if (o.side == SIDE_OUTSIDE || o.side == SIDE_INSIDE) return o.side;
  return (rs.world_inside && !world_is_flat(rs)) ? SIDE_INSIDE : SIDE_OUTSIDE;
}

gpx::planet::Shape world_shape_of(const RenderSettings &rs, const SceneObject *o) {
  return world_shape(rs, o ? object_side(rs, *o) : SIDE_WORLD);
}

bool world_is_flat(const RenderSettings &rs) { return rs.world_shape == WORLD_FLAT; }

int world_other_side(const RenderSettings &rs) {
  return (rs.world_inside && !world_is_flat(rs)) ? SIDE_OUTSIDE : SIDE_INSIDE;
}

bool world_has_body(const RenderSettings &rs) {
  return rs.world_thickness > 0.f && (rs.world_shape == WORLD_RING || rs.world_shape == WORLD_FLAT);
}

bool world_has_inside(const RenderSettings &rs) {
  if (world_is_flat(rs)) return false; // a plane has no far side to arch overhead
  if (rs.world_inside) return true;
  for (const SceneObject &o : scene().objects)
    if (o.side == SIDE_INSIDE &&
        (o.type == SceneObject::Terrain || o.type == SceneObject::InfiniteSurface))
      return true;
  return false;
}

bool surround_far_shell(const RenderSettings &rs, const float eye[3], float radius,
                        int side, int layer_count) {
  const gpx::planet::Shape S = world_shape(rs, side);
  const bool body_face = world_has_body(rs) && side == world_other_side(rs);
  if (world_is_flat(rs)) return rs.world_width > 58.f;
  // A horizon d away sits at d*d/2R, so on a small world the surround runs
  // out at head height and on a large one only from an aeroplane. Taking it
  // as a fixed tile up left a band of heights on every world where the
  // ground stopped at 30 tiles with the horizon still further out and
  // nothing drawn between - the hard line against the sky.
  const float shell_h = radius > 0.f ? 29.f * 29.f / (2.f * radius) : 1.0e9f;
  const bool aloft = eye[1] > shell_h * 0.5f || std::fabs(eye[0] - 0.5f) > 20.f ||
                     std::fabs(eye[2] - 0.5f) > 20.f;
  return radius > 0.f && (gpx::planet::shape_faces_centre(S) || body_face ||
                          (layer_count > 0 && aloft));
}

const char *world_shape_name(int shape) {
  return shape == WORLD_RING ? "ring" : (shape == WORLD_FLAT ? "flat" : "globe");
}

int world_shape_from_name(const char *name) {
  if (!name) return -1;
  if (!std::strcmp(name, "globe") || !std::strcmp(name, "sphere")) return WORLD_GLOBE;
  if (!std::strcmp(name, "ring") || !std::strcmp(name, "cylinder")) return WORLD_RING;
  if (!std::strcmp(name, "flat") || !std::strcmp(name, "plane") || !std::strcmp(name, "disc"))
    return WORLD_FLAT;
  return -1;
}

const char *world_outline_name(int outline) {
  return outline == OUTLINE_SQUARE ? "square" : "disc";
}

int world_outline_from_name(const char *name) {
  if (!name) return -1;
  if (!std::strcmp(name, "disc") || !std::strcmp(name, "disk") || !std::strcmp(name, "round"))
    return OUTLINE_DISC;
  if (!std::strcmp(name, "square") || !std::strcmp(name, "rectangle")) return OUTLINE_SQUARE;
  return -1;
}

bool world_preset_apply(RenderSettings &rs, const char *name) {
  if (!name) return false;
  if (!std::strcmp(name, "globe") || !std::strcmp(name, "planet")) {
    rs.world_shape = WORLD_GLOBE;
    rs.world_inside = false;
    rs.world_sun_inside = false;
    return true;
  }
  if (!std::strcmp(name, "ring") || !std::strcmp(name, "ringworld")) {
    rs.world_shape = WORLD_RING;
    rs.world_inside = true;
    rs.world_sun_inside = true;
    return true;
  }
  if (!std::strcmp(name, "dyson") || !std::strcmp(name, "dyson_sphere")) {
    rs.world_shape = WORLD_GLOBE;
    rs.world_inside = true;
    rs.world_sun_inside = true;
    return true;
  }
  if (!std::strcmp(name, "flat") || !std::strcmp(name, "flat_world") || !std::strcmp(name, "disc")) {
    rs.world_shape = WORLD_FLAT;
    rs.world_inside = false;
    rs.world_sun_inside = false;
    return true;
  }
  return false;
}

} // namespace studio
