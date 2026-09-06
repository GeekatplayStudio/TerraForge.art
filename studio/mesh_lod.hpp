// Geekatplay TerraForge - reduced copies of a mesh for its far instances.
//
// A scattered mesh is drawn thousands of times; past the full-detail
// distance the copies are thinned (scatter_lod.hpp) and, when the mesh is a
// plain solid, also drawn from a decimated copy: a quarter of the faces at
// mid distance, a sixteenth far away, made with the same quadric reducer
// the mesh tools use (gpx::mesh_reduce). Meshes with material parts (a
// plant's textured leaf cards) keep their full geometry at every level - a
// reducer would tear their pictures - and rely on thinning alone.
#pragma once

namespace studio {
struct SceneObject;

// Build o.lod_verts[0..1] from o.verts (triangle soup, pos+normal). Returns
// true when at least one level was made. Pure: no GL.
bool mesh_build_lods(SceneObject &o);
} // namespace studio
