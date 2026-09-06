// Geekatplay TerraForge - the triangle mesh, and everything we measure and
// repair on it.
//
// Ported from Meshwright (Geekatplay Studio, MIT), which does this in Python
// on top of trimesh/numpy. Here it is plain C++ with no dependencies at all:
// the algorithms are small, the licence is ours to keep, and the same code
// runs in the studio, in a test with no window, and in a script.
//
// Everything is triangles. A quad or an n-gon coming in from a file is fanned
// on load, because every question below - is it closed, which way does it
// face, what does it enclose - has a clean answer only on triangles.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpx {

// A picture a mesh file carries or points at: the bytes of an embedded
// image (GLB, FBX), or the path of one on disk (OBJ's MTL, glTF's uri).
struct MeshImage {
  std::string name;
  std::string path;            // empty when the bytes are embedded
  std::vector<uint8_t> bytes;  // the encoded file (PNG/JPG), empty when on disk
};

// A run of faces that share one material: which image colours it, or a
// flat colour when it has none.
struct MeshPart {
  uint32_t first_face = 0, face_count = 0;
  int image = -1;              // index into TriMesh::images, -1 = none
  float color[4] = {1.f, 1.f, 1.f, 1.f};
  std::string name;
};

struct TriMesh {
  // x,y,z per vertex; three vertex indices per face
  std::vector<float> v;
  std::vector<uint32_t> f;
  // u,v per vertex when the file has them (same count as v/3), else empty.
  // Faces that need different texture coordinates at a shared position are
  // split on load, so one vertex has one uv.
  std::vector<float> uv;
  // the materials, as face runs, and the images they use; both may be empty
  std::vector<MeshPart> parts;
  std::vector<MeshImage> images;

  size_t vert_count() const { return v.size() / 3; }
  size_t face_count() const { return f.size() / 3; }
  bool empty() const { return f.empty(); }
  void clear() { v.clear(); f.clear(); uv.clear(); parts.clear(); images.clear(); }

  const float *vert(size_t i) const { return &v[i * 3]; }
  const uint32_t *face(size_t i) const { return &f[i * 3]; }
};

// ----------------------------------------------------------------- topology
// One pass over the edges answers most of the diagnosis: an edge used by one
// face is a boundary, by more than two a non-manifold junction.
struct EdgeTable {
  // Per undirected edge: its two vertices, how many faces use it, and whether
  // those uses agree on direction (a consistently wound pair uses an edge once
  // in each direction).
  std::vector<uint32_t> a, b;
  std::vector<uint32_t> uses;
  std::vector<uint8_t> consistent;
  size_t size() const { return a.size(); }
};

EdgeTable mesh_edges(const TriMesh &m);

// Connected components over faces sharing an edge: the "separate shells" a
// print is made of. Returns the count and fills `label` per face.
int mesh_components(const TriMesh &m, std::vector<int> &label);

// Signed volume (positive when the winding points outward) and surface area.
double mesh_volume(const TriMesh &m);
double mesh_area(const TriMesh &m);

// Axis-aligned bounds. Returns false for an empty mesh.
bool mesh_bounds(const TriMesh &m, float lo[3], float hi[3]);

// The closed rings of boundary edges. Each ring is a hole; the returned value
// is the list of rings, each a list of vertex indices in order.
std::vector<std::vector<uint32_t>> mesh_boundary_loops(const TriMesh &m);

// ------------------------------------------------------------------ cleanup
// Every one of these returns how many elements it removed or changed, so a
// repair stage can report what it actually did rather than that it ran.

// Merge vertices closer than `tol` (absolute). Faces are rewritten.
size_t mesh_weld(TriMesh &m, float tol);
// Drop faces with a repeated index or zero area.
size_t mesh_drop_degenerate(TriMesh &m);
// Drop faces that repeat another face's vertex set.
size_t mesh_drop_duplicate_faces(TriMesh &m);
// Drop vertices no face refers to.
size_t mesh_drop_unreferenced(TriMesh &m);
// Make neighbouring faces agree on direction; returns how many were flipped.
size_t mesh_fix_winding(TriMesh &m);
// Flip everything if the mesh encloses a negative volume; returns 1 if flipped.
size_t mesh_fix_inversion(TriMesh &m);
// Fill every boundary loop with a triangle fan. Returns the number of loops
// closed; `max_edges` skips loops longer than that (a huge ring is usually a
// missing part rather than a hole, and fanning it makes a lid).
size_t mesh_fill_holes(TriMesh &m, size_t max_edges = 5000);
// Remove connected shells with fewer than `min_faces` faces, or smaller than
// `min_extent_frac` of the model's own size. Returns shells removed.
size_t mesh_drop_small_shells(TriMesh &m, size_t min_faces,
                              float min_extent_frac);

} // namespace gpx
