// Geekatplay TerraForge - optional mesh engines. See gpx/mesh_engines.hpp.
//
// Every path in here either does the thing or says plainly that it did not.
// There is no silent fallback: the report is only worth reading because a
// stage that did not run is reported as a stage that did not run.
#include "gpx/mesh_engines.hpp"
#include "gpx/mesh_report.hpp"
#include <algorithm>
#include <exception>
#include <vector>

#ifdef GPX_HAVE_MANIFOLD
#include <manifold/manifold.h>
#endif

#ifdef GPX_HAVE_QUADRIFLOW
// QuadriFlow warns loudly on a 2026 compiler and its headers are not ours to
// fix; the library itself is built with warnings off in CMakeLists.
#include <optimizer.hpp>
#include <parametrizer.hpp>
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
  s += "Manifold " GPX_MANIFOLD_VERSION;
#endif
#ifdef GPX_HAVE_QUADRIFLOW
  if (!s.empty()) s += ", ";
  s += "QuadriFlow";
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
  if (m.face_count() < 4) {
    err = "too few triangles to retopologise";
    return false;
  }
  // QuadriFlow needs a clean triangle surface to reason about: a mesh whose
  // vertices are split (every STL) has no adjacency at all, and it would
  // rebuild the surface as unrelated islands.
  TriMesh input = m;
  {
    float lo[3], hi[3];
    if (mesh_bounds(input, lo, hi)) {
      float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
      mesh_weld(input, span > 0.f ? span * 1e-6f : 1e-6f);
    }
    mesh_drop_degenerate(input);
    mesh_drop_duplicate_faces(input);
    mesh_drop_unreferenced(input);
  }

  qflow::Parametrizer field;
  field.V.resize(3, (int)input.vert_count());
  for (size_t i = 0; i < input.vert_count(); ++i)
    for (int k = 0; k < 3; ++k)
      field.V(k, (int)i) = input.v[i * 3 + (size_t)k];
  field.F.resize(3, (int)input.face_count());
  for (size_t i = 0; i < input.face_count(); ++i)
    for (int k = 0; k < 3; ++k)
      field.F(k, (int)i) = (int)input.f[i * 3 + (size_t)k];

  // The same sequence QuadriFlow's own front end runs. It works in a
  // normalised space and hands back the transform to undo, which is why the
  // output is scaled and offset on the way out rather than trusted as-is.
  try {
    field.NormalizeMesh();
    field.Initialize((int)target_faces);
    qflow::Optimizer::optimize_orientations(field.hierarchy);
    field.ComputeOrientationSingularities();
    qflow::Optimizer::optimize_scale(field.hierarchy, field.rho,
                                     field.flag_adaptive_scale);
    field.flag_adaptive_scale = 1;
    qflow::Optimizer::optimize_positions(field.hierarchy,
                                         field.flag_adaptive_scale);
    field.ComputePositionSingularities();
    field.ComputeIndexMap();
  } catch (const std::exception &e) {
    err = std::string("quad retopology failed: ") + e.what();
    return false;
  } catch (...) {
    err = "quad retopology failed";
    return false;
  }

  if (field.O_compact.empty() || field.F_compact.empty()) {
    err = "quad retopology produced no geometry";
    return false;
  }

  TriMesh out;
  out.v.reserve(field.O_compact.size() * 3);
  for (const auto &p : field.O_compact) {
    auto t = p * field.normalize_scale + field.normalize_offset;
    out.v.push_back((float)t[0]);
    out.v.push_back((float)t[1]);
    out.v.push_back((float)t[2]);
  }
  // Quads become two triangles: our carrier is triangles, and every consumer
  // downstream - the analysis, the renderer, every exporter - is too.
  const uint32_t nv = (uint32_t)out.vert_count();
  for (const auto &q : field.F_compact) {
    uint32_t a = (uint32_t)q[0], b = (uint32_t)q[1], c = (uint32_t)q[2],
             d = (uint32_t)q[3];
    if (a >= nv || b >= nv || c >= nv || d >= nv) continue;
    out.f.insert(out.f.end(), {a, b, c});
    out.f.insert(out.f.end(), {a, c, d});
  }
  if (out.f.empty()) {
    err = "quad retopology produced no usable faces";
    return false;
  }
  mesh_drop_degenerate(out);
  mesh_drop_unreferenced(out);
  m = std::move(out);
  return true;
#endif
}

} // namespace gpx
