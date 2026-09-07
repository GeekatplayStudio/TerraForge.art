// Geekatplay TerraForge - constructive solid geometry and metaballs.
//
// Split from mesh_engines.cpp for the 500-line module rule; the same seam
// and the same rule applies. A build without Manifold has neither of these,
// and says so rather than doing something weaker and claiming otherwise.
#include "gpx/mesh_engines.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#ifdef GPX_HAVE_MANIFOLD
#include <manifold/manifold.h>
#endif

namespace gpx {

#ifdef GPX_HAVE_MANIFOLD
namespace {

// The few statuses a bad mesh can produce, named here because Manifold's own
// ToString only exists in a MANIFOLD_DEBUG build and we do not build one.
const char *why(manifold::Manifold::Error e) {
  using E = manifold::Manifold::Error;
  switch (e) {
    case E::NonFiniteVertex: return "a vertex is infinite or NaN";
    case E::NotManifold: return "the surface is not manifold";
    case E::VertexOutOfBounds: return "a face refers to a missing vertex";
    case E::InvalidConstruction: return "the mesh could not be built";
    case E::ResultTooLarge: return "the result would be too large";
    default: return "the surface could not be made into a solid";
  }
}

// A TriMesh as a Manifold solid. Vertices are stitched first: a mesh whose
// triangles never shared a vertex - which is every STL, and every mesh our
// own primitives emit - reads as a cloud of islands otherwise.
bool to_solid(const TriMesh &m, manifold::Manifold &out, std::string &err,
              const char *which) {
  if (m.empty()) {
    err = std::string(which) + " has no triangles";
    return false;
  }
  manifold::MeshGL in;
  in.numProp = 3;
  in.vertProperties.assign(m.v.begin(), m.v.end());
  in.triVerts.assign(m.f.begin(), m.f.end());
  in.Merge();
  out = manifold::Manifold(in);
  if (out.Status() != manifold::Manifold::Error::NoError) {
    err = std::string(which) + " is not a solid: " + why(out.Status());
    return false;
  }
  return true;
}

bool from_solid(const manifold::Manifold &s, TriMesh &out, std::string &err) {
  manifold::MeshGL g = s.GetMeshGL();
  if (g.triVerts.empty()) {
    err = "the result is empty - the shapes may not overlap at all";
    return false;
  }
  TriMesh r;
  const uint32_t props = g.numProp ? g.numProp : 3;
  r.v.reserve(g.vertProperties.size() / props * 3);
  for (size_t i = 0; i + props <= g.vertProperties.size(); i += props) {
    r.v.push_back((float)g.vertProperties[i]);
    r.v.push_back((float)g.vertProperties[i + 1]);
    r.v.push_back((float)g.vertProperties[i + 2]);
  }
  r.f.assign(g.triVerts.begin(), g.triVerts.end());
  out = std::move(r);
  return true;
}

} // namespace
#endif

bool mesh_boolean(TriMesh &a, const TriMesh &b, MeshBoolOp op,
                  std::string &err) {
#ifndef GPX_HAVE_MANIFOLD
  (void)a; (void)b; (void)op;
  err = "this build has no CSG engine (Manifold was not compiled in)";
  return false;
#else
  try {
    manifold::Manifold ma, mb;
    if (!to_solid(a, ma, err, "the first mesh")) return false;
    if (!to_solid(b, mb, err, "the second mesh")) return false;
    manifold::OpType t = op == MeshBoolOp::Union        ? manifold::OpType::Add
                         : op == MeshBoolOp::Intersect  ? manifold::OpType::Intersect
                                                        : manifold::OpType::Subtract;
    manifold::Manifold r = ma.Boolean(mb, t);
    if (r.Status() != manifold::Manifold::Error::NoError) {
      err = std::string("the operation failed: ") + why(r.Status());
      return false;
    }
    TriMesh out;
    if (!from_solid(r, out, err)) return false;
    a = std::move(out);
    return true;
  } catch (const std::exception &e) {
    // Manifold throws on a few inputs rather than reporting a status, and a
    // CSG failure must not take the application with it.
    err = std::string("the operation threw: ") + e.what();
    return false;
  }
#endif
}

bool mesh_metaball(const std::vector<MetaBlob> &blobs, float smoothness,
                   float cell, TriMesh &out, std::string &err) {
#ifndef GPX_HAVE_MANIFOLD
  (void)blobs; (void)smoothness; (void)cell; (void)out;
  err = "this build has no level-set engine (Manifold was not compiled in)";
  return false;
#else
  if (blobs.empty()) {
    err = "no blobs to mesh";
    return false;
  }
  // Each blob's field reaches `reach` times its radius. Widening the reach
  // without moving the radius is what turns a row of separate beads into one
  // poured mass: the fields overlap further out, so they meet and merge
  // sooner.
  //
  // Never 1: at reach 1 the field is already zero at the radius, and the
  // threshold below would be zero too - the surface would be wherever the
  // field is nothing at all, which is a point rather than a ball.
  const float reach = 1.2f + std::clamp(smoothness, 0.f, 4.f);
  struct Ball { double x, y, z, r2, k; };
  std::vector<Ball> bs;
  bs.reserve(blobs.size());
  double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
  for (const MetaBlob &b : blobs) {
    const double R = std::max((double)b.radius, 1e-6) * reach;
    bs.push_back({b.x, b.y, b.z, R * R, (double)b.strength});
    lo[0] = std::min(lo[0], b.x - R); hi[0] = std::max(hi[0], b.x + R);
    lo[1] = std::min(lo[1], b.y - R); hi[1] = std::max(hi[1], b.y + R);
    lo[2] = std::min(lo[2], b.z - R); hi[2] = std::max(hi[2], b.z + R);
  }
  // A voxel of margin all round, so the surface is never clipped by the box
  // it is being sampled in.
  const double c = std::max((double)cell, 1e-6);
  for (int i = 0; i < 3; ++i) { lo[i] -= c * 2; hi[i] += c * 2; }

  // The threshold, chosen so a lone ball's surface lands exactly on its own
  // radius whatever the smoothness. The obvious choice - a fixed level of 1,
  // or the textbook 0.5 - makes the radius mean something different at every
  // smoothness, so widening the merge would quietly inflate every ball. Here
  // smoothness changes only how readily blobs join, which is the one thing
  // it is for.
  const double iso = std::pow(1.0 - 1.0 / ((double)reach * reach), 3.0);

  // The Wyvill falloff: 1 at the centre, 0 at the reach, and with a zero
  // derivative at both ends - which is precisely what makes two blobs merge
  // into one smooth surface instead of intersecting in a crease.
  auto field = [bs, iso](manifold::vec3 p) {
    double sum = 0;
    for (const Ball &b : bs) {
      const double dx = p.x - b.x, dy = p.y - b.y, dz = p.z - b.z;
      const double d2 = dx * dx + dy * dy + dz * dz;
      if (d2 >= b.r2) continue;
      const double t = 1.0 - d2 / b.r2;
      sum += b.k * t * t * t;
    }
    // Manifold meshes where the function is zero and treats positive as
    // inside, so the surface is the level set of (field - threshold).
    return sum - iso;
  };

  const size_t voxels =
      (size_t)((hi[0] - lo[0]) / c + 2) * (size_t)((hi[1] - lo[1]) / c + 2) *
      (size_t)((hi[2] - lo[2]) / c + 2);
  if (voxels > 400ull * 1000ull * 1000ull) {
    err = "that resolution would need " + std::to_string(voxels / 1000000) +
          " million voxels - raise the cell size or shrink the blobs";
    return false;
  }
  try {
    manifold::Box box({lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]});
    manifold::Manifold m = manifold::Manifold::LevelSet(field, box, c, 0.0);
    if (m.Status() != manifold::Manifold::Error::NoError) {
      err = std::string("the level set failed: ") + why(m.Status());
      return false;
    }
    return from_solid(m, out, err);
  } catch (const std::exception &e) {
    err = std::string("the level set threw: ") + e.what();
    return false;
  }
#endif
}

} // namespace gpx
