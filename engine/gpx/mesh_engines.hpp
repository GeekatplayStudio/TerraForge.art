// Geekatplay TerraForge - optional mesh engines.
//
// Two capabilities are worth more than we can sensibly write ourselves, and
// both are available under licences we can carry (see docs/LICENSING.md):
//
//   Manifold   (Apache-2.0)  guaranteed-manifold solid reconstruction
//   QuadriFlow (BSD-3)       quad retopology
//
// They sit behind this seam so the mesh module still builds and runs with
// neither of them present. The rule that goes with that: a missing engine
// disables its stage and SAYS SO. It never falls back to something weaker
// while claiming to have done the stronger thing - which is the whole reason
// the repair report is trustworthy at all.
#pragma once
#include "mesh.hpp"
#include <string>

namespace gpx {

// Which engines this build actually has, for the panel and the log.
struct MeshEngines {
  bool solidify = false; // Manifold
  bool retopo = false;   // QuadriFlow
};
MeshEngines mesh_engines();

// One line naming what is compiled in, e.g. "Manifold 3.5.2". Never empty.
std::string mesh_engines_text();

// Rebuild the surface as a solid that is manifold by construction. Split
// vertices are stitched first (an STL arrives with every triangle carrying
// its own three corners), so this recovers a solid from meshes our own
// stages cannot close.
//
// Returns false and leaves `m` untouched when the engine is absent or the
// input cannot be made into a solid; `err` always says which.
bool mesh_solidify(TriMesh &m, std::string &err);

// Constructive solid geometry between two meshes.
//
// Union, intersection and difference, each guaranteed manifold by
// construction because Manifold guarantees it. Both meshes are stitched
// first, for the same reason solidify stitches: a mesh whose triangles never
// shared a vertex reads as a cloud of islands and cannot be a solid.
//
// The two must already be in the same space. Nothing here knows about a
// scene transform, so the caller bakes.
enum class MeshBoolOp { Union, Intersect, Difference };
bool mesh_boolean(TriMesh &a, const TriMesh &b, MeshBoolOp op,
                  std::string &err);

// One ball of a metaball field: a centre, a radius, and how strongly it
// pulls. Strength may be negative, which carves rather than adds - a
// negative blob dropped through a positive one is how a hollow is made.
struct MetaBlob {
  float x = 0, y = 0, z = 0;
  float radius = 1.f;
  float strength = 1.f;
};

// Mesh the surface where the blobs' summed field reaches 1.
//
// The field of one blob is the classic Wyvill falloff: 1 at the centre,
// smoothly 0 at the radius, and with a continuous derivative at both ends,
// which is what makes two blobs *merge* rather than intersect. `smoothness`
// widens each blob's reach without moving its surface, so the same set of
// balls can read as separate beads or as one poured mass.
//
// `cell` is the voxel edge: the smaller it is, the finer the result and the
// more of them there are, cubically. Manifold's level set is used rather
// than a marching-cubes of our own, so the output is manifold and can go
// straight into mesh_boolean.
bool mesh_metaball(const std::vector<MetaBlob> &blobs, float smoothness,
                   float cell, TriMesh &out, std::string &err);

// Rebuild the surface as evenly sized, curvature-aligned quads, triangulated
// on the way back (our TriMesh is triangles). `target_faces` is the quad
// count asked for; the result is about twice that in triangles.
bool mesh_retopo(TriMesh &m, size_t target_faces, std::string &err);

} // namespace gpx
