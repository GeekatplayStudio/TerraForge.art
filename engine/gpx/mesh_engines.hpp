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

// Rebuild the surface as evenly sized, curvature-aligned quads, triangulated
// on the way back (our TriMesh is triangles). `target_faces` is the quad
// count asked for; the result is about twice that in triangles.
bool mesh_retopo(TriMesh &m, size_t target_faces, std::string &err);

} // namespace gpx
