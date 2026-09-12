// Geekatplay TerraForge - reduced copies of a mesh for its far instances.
//
// A scattered mesh is drawn thousands of times; past the full-detail
// distance the copies are thinned (scatter_lod.hpp) and, when the mesh is a
// plain solid, also drawn from a decimated copy: a quarter of the faces at
// mid distance, a sixteenth far away, made with the same quadric reducer
// the mesh tools use (gpx::mesh_reduce). A mesh with material parts - a
// plant, whose leaf cards each wear a cut-out picture that IS their shape -
// is reduced part by part instead, each keeping its own run and its own
// texture coordinates (mesh_lod_parts.cpp). Before that it kept its full
// geometry at every level, which meant a wood drew half-million-triangle
// trees a kilometre away.
#pragma once

namespace studio {
struct SceneObject;

// Build o.lod_verts[0..1] from o.verts (triangle soup, pos+normal). Returns
// true when at least one level was made. Pure: no GL.
bool mesh_build_lods(SceneObject &o);

// The same for a mesh divided into parts, each with its own picture: every
// part is reduced on its own and keeps its own run, so a tree drawn at
// distance is still a tree and not a grey shape (studio/mesh_lod_parts.cpp).
bool mesh_build_lods_parts(SceneObject &o);

} // namespace studio
