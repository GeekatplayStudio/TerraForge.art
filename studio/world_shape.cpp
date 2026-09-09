// Geekatplay TerraForge — the world's shape (world_shape.hpp).
#include "world_shape.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <cstring>

namespace studio {

gpx::planet::Shape world_shape(const RenderSettings &rs, int side) {
  gpx::planet::Shape S;
  S.flat_z = rs.world_shape == WORLD_RING;
  S.inside = rs.world_inside;
  // the other face of the same shell, not another shell
  const int own = rs.world_inside ? SIDE_INSIDE : SIDE_OUTSIDE;
  S.flip = side != SIDE_WORLD && side != own;
  return S;
}

int object_side(const RenderSettings &rs, const SceneObject &o) {
  if (o.side == SIDE_OUTSIDE || o.side == SIDE_INSIDE) return o.side;
  return rs.world_inside ? SIDE_INSIDE : SIDE_OUTSIDE;
}

gpx::planet::Shape world_shape_of(const RenderSettings &rs, const SceneObject *o) {
  return world_shape(rs, o ? object_side(rs, *o) : SIDE_WORLD);
}

bool world_has_inside(const RenderSettings &rs) {
  if (rs.world_inside) return true;
  for (const SceneObject &o : scene().objects)
    if (o.side == SIDE_INSIDE &&
        (o.type == SceneObject::Terrain || o.type == SceneObject::InfiniteSurface))
      return true;
  return false;
}

const char *world_shape_name(int shape) { return shape == WORLD_RING ? "ring" : "globe"; }

int world_shape_from_name(const char *name) {
  if (!name) return -1;
  if (!std::strcmp(name, "globe") || !std::strcmp(name, "sphere")) return WORLD_GLOBE;
  if (!std::strcmp(name, "ring") || !std::strcmp(name, "cylinder")) return WORLD_RING;
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
  return false;
}

} // namespace studio
