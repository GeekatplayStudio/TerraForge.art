// Geekatplay TerraForge — the terrain tile as an object with a transform.
//
// The tile used to be the world: a square from (0,0) to (1,1) that nothing
// could move, turn or stretch, whose only "size" was the number of metres
// the unit stands for - a figure that rescales every readout and changes
// nothing on screen. Every other object has a position, a heading, pitch
// and bank, a size per axis and the four deformers. So does the terrain
// now, through the same fields on its SceneObject (pos, yaw/pitch/roll,
// scl, deform), which means the same gizmo, the same undo, the same
// animation tracks, the same `place_object` op and the same saved form.
//
// The transform applies to the tile in its own frame, before the planet
// placement: a point (u, h, v) is deformed, scaled about the tile's centre
// at ground level, rotated HPB, and offset. The GLSL twin runs in every
// pass that draws the terrain (colour, tessellated, shadow); the CPU form
// grounds objects and answers height probes. Pure maths here, no GL: the
// uniform upload lives with the passes, and this file is unit-tested.
#pragma once
#include "gpx/deform.hpp"

namespace studio {
struct SceneObject;

struct TerrainXform {
  bool on = false;      // false: identity, the shaders skip it entirely
  float pos[3] = {0.f, 0.f, 0.f};
  float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // row-major, R = H * P * B
  float scl[3] = {1.f, 1.f, 1.f};
  gpx::Deform deform;
  float bmin[3] = {0.f, 0.f, 0.f}; // the tile's box: 0..1 across, 0..height_scale up
  float bmax[3] = {1.f, 1.f, 1.f};
  float yaw = 0.f, pitch = 0.f, roll = 0.f; // degrees, kept for the inverse
};

// From the Terrain object's fields and the height scale (the deformers'
// vertical bound). `on` is false when every field is at its default.
TerrainXform terrain_xform_of(const SceneObject &terrain, float height_scale);
// The current scene's, or an identity when there is no Terrain object
// (renderer_passes.cpp, which also uploads it; this file stays GL- and
// scene-free so it can be tested alone).
TerrainXform terrain_xform_current();

// Tile frame -> transformed tile frame (still before the planet placement).
void terrain_xform_apply(const TerrainXform &t, float p[3]);
// The inverse on the ground plane: which tile point (u, v) lies under the
// transformed point (x, z). Exact for offset, heading and scale; pitch,
// bank and the deformers are ignored (they move points vertically far more
// than sideways). Returns false when the scale is degenerate.
bool terrain_xform_unapply_xz(const TerrainXform &t, float x, float z, float &u, float &v);
// The transformed height of the tile at world (x, z), given a sampler of
// the tile's own height in tile units: the grounding query.
template <class Sampler>
float terrain_xform_ground(const TerrainXform &t, float x, float z, Sampler sample) {
  float u = x, v = z;
  if (t.on && !terrain_xform_unapply_xz(t, x, z, u, v)) return sample(x, z);
  float p[3] = {u, sample(u, v), v};
  if (t.on) terrain_xform_apply(t, p);
  return p[1];
}

// The GLSL. TERRAIN_XFORM_GLSL follows DEFORM_FN_GLSL in a vertex or
// control stage and defines tile_xform(vec3); TERRAIN_XFORM_FS_GLSL is the
// fragment stage's share: the uniforms it reads, tile_xform_normal(vec3)
// and tile_cut(vec2) for the outline.
extern const char *const TERRAIN_XFORM_GLSL;
extern const char *const TERRAIN_XFORM_FS_GLSL;
// The ground-plane inverse for a pass that works in world tile units and
// needs to know where the tile *is* - the planet surround's hole and its
// border blend: tile_unapply_xz(vec2) is the GLSL twin of
// terrain_xform_unapply_xz, and u_txi_y carries the tile's vertical scale
// and offset for meeting its border level.
extern const char *const TERRAIN_XFORM_INV_GLSL;
// Uploads for the two (renderer_passes.cpp; GL lives there, not here).
void upload_terrain_xform(unsigned prog);
void upload_terrain_xform_inverse(unsigned prog);

} // namespace studio
