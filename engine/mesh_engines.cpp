// Geekatplay TerraForge - optional mesh engines. See gpx/mesh_engines.hpp.
//
// Every path in here either does the thing or says plainly that it did not.
// There is no silent fallback: the report is only worth reading because a
// stage that did not run is reported as a stage that did not run.
#include "gpx/mesh_engines.hpp"
#include "gpx/mesh_report.hpp"
#include <vector>

#ifdef GPX_HAVE_MANIFOLD
#include <manifold/manifold.h>
#endif

namespace gpx {

MeshEngines mesh_engines() {
  MeshEngines e;
#ifdef GPX_HAVE_MANIFOLD
  e.solidify = true;
#endif
#ifdef GPX_HAVE_QUADRIFLOW
  e.retopo = true;
#endif
  return e;
}

std::string mesh_engines_text() {
  std::string s;
#ifdef GPX_HAVE_MANIFOLD
  s += "Manifold " GPX_MANIFOLD_VERSION " (solidify)";
#endif
#ifdef GPX_HAVE_QUADRIFLOW
  if (!s.empty()) s += ", ";
  s += "QuadriFlow (quad retopology)";
#endif
  return s.empty() ? std::string("none - built without the optional engines")
                   : s;
}

bool mesh_solidify(TriMesh &m, std::string &err) {
#ifndef GPX_HAVE_MANIFOLD
  (void)m;
  err = "this build has no solid reconstruction engine (Manifold was not "
        "compiled in)";
  return false;
#else
  if (m.empty()) {
    err = "nothing to solidify: the mesh has no triangles";
    return false;
  }
  manifold::MeshGL in;
  in.numProp = 3;
  in.vertProperties.assign(m.v.begin(), m.v.end());
  in.triVerts.assign(m.f.begin(), m.f.end());
  // Stitch vertices that sit on the same point but were never joined. An STL
  // is always in that state, and without this every triangle would read as
  // its own island.
  in.Merge();

  manifold::Manifold solid(in);
  if (solid.Status() != manifold::Manifold::Error::NoError) {
    // Manifold's own ToString only exists in a MANIFOLD_DEBUG build, and we
    // build it without debug, so the few statuses a bad mesh can produce are
    // named here instead of depending on their build flags.
    using E = manifold::Manifold::Error;
    const char *why = "the surface could not be made into a solid";
    switch (solid.Status()) {
      case E::NonFiniteVertex: why = "a vertex is infinite or NaN"; break;
      case E::NotManifold: why = "the surface is not manifold"; break;
      case E::VertexOutOfBounds: why = "a face refers to a missing vertex"; break;
      case E::InvalidConstruction: why = "the mesh could not be built"; break;
      case E::ResultTooLarge: why = "the result would be too large"; break;
      default: break;
    }
    err = std::string("solid reconstruction failed: ") + why;
    return false;
  }
  // Overlapping shells are the defect our own stages cannot touch: each
  // piece is closed on its own, so nothing reads as a hole, yet the whole
  // is not a printable solid because the pieces run through each other. A
  // union of the components resolves exactly that, and is a no-op for a
  // model that was already one piece.
  std::vector<manifold::Manifold> parts = solid.Decompose();
  if (parts.size() > 1) {
    manifold::Manifold merged =
        manifold::Manifold::BatchBoolean(parts, manifold::OpType::Add);
    if (merged.Status() == manifold::Manifold::Error::NoError)
      solid = std::move(merged);
  }

  manifold::MeshGL out = solid.GetMeshGL();
  if (out.triVerts.empty()) {
    err = "solid reconstruction produced no geometry";
    return false;
  }
  TriMesh result;
  result.v.reserve(out.vertProperties.size());
  // vertProperties is interleaved and may carry more than position.
  const uint32_t stride = out.numProp ? out.numProp : 3;
  for (size_t i = 0; i + 2 < out.vertProperties.size(); i += stride) {
    result.v.push_back(out.vertProperties[i]);
    result.v.push_back(out.vertProperties[i + 1]);
    result.v.push_back(out.vertProperties[i + 2]);
  }
  result.f.assign(out.triVerts.begin(), out.triVerts.end());
  // Trust nothing: check the result is what was promised before taking it.
  MeshReport check;
  mesh_analyse(result, check);
  if (!check.stats.watertight) {
    err = "solid reconstruction returned a surface that is still open";
    return false;
  }
  m = std::move(result);
  return true;
#endif
}

bool mesh_retopo(TriMesh &m, size_t target_faces, std::string &err) {
#ifndef GPX_HAVE_QUADRIFLOW
  (void)m;
  (void)target_faces;
  err = "this build has no quad retopology engine (QuadriFlow was not "
        "compiled in)";
  return false;
#else
  // Filled in when QuadriFlow is wired up; the seam exists so the caller,
  // the panel and the op are already written against it.
  (void)m;
  (void)target_faces;
  err = "quad retopology is not implemented yet";
  return false;
#endif
}

} // namespace gpx
