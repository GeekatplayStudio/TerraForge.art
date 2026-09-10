// Geekatplay TerraForge — the shape of the world and which side of it a
// thing stands on.
//
// The home planet (scene.hpp) is a globe by default: the tile lies on a
// sphere R below it and the horizon falls away. Science fiction has two
// other worlds worth standing on, and both are the same sphere maths with
// the surface on the other side: a ring world - a cylinder curving along the
// tile's x only, its ground on the inside, the sun on the axis - and a Dyson
// sphere, a globe with the ground on the inside and the sun at the centre.
// From an inside surface the far side of the world arches overhead, so the
// surround (planet_renderer.cpp) is followed by a far shell that draws the
// whole shape, and the sun is a body at the centre rather than a direction.
//
// A shape can carry ground on both faces: every terrain tile and surface
// layer has a side - the world's own, or the other one - so a shell with
// land outside and land inside is one planet with children on each face.
// The other face is the same shell with the heights going the other way
// (gpx::planet::Shape::flip): a globe's is the inside of its crust, seen
// from within the hollow planet; a Dyson sphere's is its dark outside.
// (gpx::planet::Shape is the maths; RenderSettings holds the home world's
// shape; SceneObject::side holds the choice per object.)
//
// A world is a body, not a sheet: RenderSettings::world_thickness is how
// far the other face lies below the ground (Shape::thick), and with it a
// ring shows its rim wall and a flat world its edge and its underside
// (planet_rim.cpp). The flat world is the third shape: a plane the size of
// world_width, cut to a disc or a square (world_outline), with nothing
// beyond its edge.
//
// The air lies on the world the same way the ground does: the sky's
// gradient, the cloud layers, the cloud shadows and the water are measured
// from the surface (gpx::planet::world_alt / world_up), so on a ring the
// clouds are a band round the inside of the ring and not a slab over the
// tile.
#pragma once
#include "gpx/planet_math.hpp"

namespace studio {

struct RenderSettings;
struct SceneObject;

enum WorldShape { WORLD_GLOBE = 0, WORLD_RING = 1, WORLD_FLAT = 2 };
enum WorldOutline { OUTLINE_DISC = 0, OUTLINE_SQUARE = 1 };
enum ObjectSide { SIDE_WORLD = 0, SIDE_OUTSIDE = 1, SIDE_INSIDE = 2 };

// The home world's shape as seen from `side` (SIDE_WORLD = the world's own
// face). The default settings give the globe every scene had.
gpx::planet::Shape world_shape(const RenderSettings &rs, int side = SIDE_WORLD);
// The face an object stands on, resolved: SIDE_OUTSIDE or SIDE_INSIDE.
int object_side(const RenderSettings &rs, const SceneObject &o);
// The shape a tile, a surface layer or the water under it is drawn with.
gpx::planet::Shape world_shape_of(const RenderSettings &rs, const SceneObject *o);
// Does any face of the world face its centre (the far shell is needed)?
bool world_has_inside(const RenderSettings &rs);
// A flat world (WORLD_FLAT): a plane, whatever the radius setting says.
bool world_is_flat(const RenderSettings &rs);
// The face that is not the world's own: SIDE_INSIDE for a globe, SIDE_OUTSIDE
// for a ring world or a Dyson sphere.
int world_other_side(const RenderSettings &rs);
// Is the world's other face drawn as its body even with nothing standing
// on it: a thickness above 0 on a ring or a flat world (planet_rim.cpp
// draws the rim between the two faces then).
bool world_has_body(const RenderSettings &rs);
// u_world_shape and u_world_thick for a program (planet_shaders_common.cpp).
// Zero is the globe. Defined in renderer_passes.cpp, so this module links
// without GL (tests).
void upload_world_shape(unsigned prog, const gpx::planet::Shape &S);
// "globe" / "ring" / "flat" and back; -1 for a name that is neither.
const char *world_shape_name(int shape);
int world_shape_from_name(const char *name);
// "disc" / "square" and back; -1 for a name that is neither.
const char *world_outline_name(int outline);
int world_outline_from_name(const char *name);
// A preset by name - "globe", "ring", "dyson", "flat" - sets the shape, the
// face and the sun together; false for an unknown name.
bool world_preset_apply(RenderSettings &rs, const char *name);

} // namespace studio
