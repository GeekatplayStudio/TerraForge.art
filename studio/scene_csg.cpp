// Geekatplay TerraForge — booleans and metaballs on scene objects.
//
// The engine's CSG (engine/mesh_csg.cpp) works on meshes in one space and
// knows nothing about a scene. This is the part that does: it bakes each
// selected object's own transform into its vertices, hands the result over,
// and puts one object back where several were.
//
// Baking is not a detail. Two objects only overlap in the world, not in
// their own local spaces, so a boolean of their untransformed meshes would
// be a boolean of two shapes that both think they are at the origin.
#include "scene.hpp"
#include "console.hpp"
#include "gpx/deform.hpp"
#include "gpx/mesh_engines.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace studio {

namespace {

// One object's triangles, in world space, with its deformers applied. The
// vertex layout in a SceneObject is interleaved position+normal; a TriMesh
// is positions and indices, and the boolean discards normals anyway because
// the result's surface is new.
gpx::TriMesh world_mesh(SceneObject &o) {
  gpx::TriMesh m;
  if (o.verts.size() < 18) return m;
  float m16[16], n9[9];
  scene_object_bounds(o);
  scene_object_matrix(o, 1.f, m16, n9);
  const size_t n = o.verts.size() / 6;
  m.v.reserve(n * 3);
  m.f.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    float p[3] = {o.verts[i * 6], o.verts[i * 6 + 1], o.verts[i * 6 + 2]};
    gpx::deform_point(o.deform, o.bmin, o.bmax, p);
    m.f.push_back((uint32_t)(m.v.size() / 3));
    m.v.push_back(m16[0] * p[0] + m16[4] * p[1] + m16[8] * p[2] + m16[12]);
    m.v.push_back(m16[1] * p[0] + m16[5] * p[1] + m16[9] * p[2] + m16[13]);
    m.v.push_back(m16[2] * p[0] + m16[6] * p[1] + m16[10] * p[2] + m16[14]);
  }
  return m;
}

// A TriMesh back into the interleaved position+normal soup a SceneObject
// draws, with a flat normal per face. Flat is right here: the result of a
// boolean has genuine creases along every cut, and smoothing across one
// would round off the very edge the operation was for.
void to_object(const gpx::TriMesh &m, SceneObject &o) {
  o.verts.clear();
  o.verts.reserve(m.f.size() * 6);
  for (size_t t = 0; t + 2 < m.f.size(); t += 3) {
    const float *a = &m.v[(size_t)m.f[t] * 3];
    const float *b = &m.v[(size_t)m.f[t + 1] * 3];
    const float *c = &m.v[(size_t)m.f[t + 2] * 3];
    const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const float wx = c[0] - a[0], wy = c[1] - a[1], wz = c[2] - a[2];
    float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (l > 1e-20f) { nx /= l; ny /= l; nz /= l; }
    for (const float *p : {a, b, c})
      o.verts.insert(o.verts.end(), {p[0], p[1], p[2], nx, ny, nz});
  }
  o.vert_count = (int)(o.verts.size() / 6);
  o.gpu_dirty = true;
}

// The selection, primary first, as indices that still exist and carry
// geometry. The primary leads because its name, material and place in the
// tree are what the result inherits.
std::vector<int> mesh_selection(SceneState &sc) {
  std::vector<int> out;
  auto usable = [&](int i) {
    return i >= 0 && i < (int)sc.objects.size() &&
           sc.objects[i].type == SceneObject::Mesh &&
           sc.objects[i].verts.size() >= 18;
  };
  if (usable(sc.selected)) out.push_back(sc.selected);
  for (int i : sc.selection)
    if (i != sc.selected && usable(i)) out.push_back(i);
  return out;
}

// The result takes the primary's place: its name, its transform reset to
// identity (the vertices are already in world space), and the objects it
// consumed removed.
void replace_selection(SceneState &sc, const std::vector<int> &used,
                       const gpx::TriMesh &result, const std::string &suffix) {
  // Everything that writes into the keeper happens before the erases below:
  // `sc.objects` is a vector, and erasing from it invalidates the reference.
  SceneObject &keep = sc.objects[used[0]];
  to_object(result, keep);
  keep.path.clear(); // no longer a primitive or a file: it is its own shape
  if (suffix.size()) keep.name += suffix;
  keep.pos[0] = keep.pos[1] = keep.pos[2] = 0.f;
  keep.yaw = keep.pitch = keep.roll = 0.f;
  keep.scale = 1.f;
  keep.deform = gpx::Deform();
  scene_object_bounds(keep);
  // Highest index first, so removing one does not move the next. Anything
  // parented to a consumed object is re-rooted rather than left pointing at
  // whatever slid into that slot.
  std::vector<int> drop(used.begin() + 1, used.end());
  std::sort(drop.rbegin(), drop.rend());
  for (int d : drop) {
    for (auto &o : sc.objects) {
      if (o.parent > d) --o.parent;
      else if (o.parent == d) o.parent = -1;
    }
    sc.objects.erase(sc.objects.begin() + d);
  }
  // The keeper's index has moved down by however many were removed ahead
  // of it.
  int keep_idx = used[0];
  for (int d : drop)
    if (d < keep_idx) --keep_idx;
  sc.selected = std::clamp(keep_idx, 0, (int)sc.objects.size() - 1);
  sc.selection = {sc.selected};
}

} // namespace

bool scene_boolean_selection(int op, std::string &err) {
  SceneState &sc = scene();
  std::vector<int> sel = mesh_selection(sc);
  if (sel.size() < 2) {
    err = "select two or more mesh objects first";
    return false;
  }
  const gpx::MeshBoolOp o = op == 1   ? gpx::MeshBoolOp::Intersect
                            : op == 2 ? gpx::MeshBoolOp::Difference
                                      : gpx::MeshBoolOp::Union;
  gpx::TriMesh acc = world_mesh(sc.objects[sel[0]]);
  for (size_t i = 1; i < sel.size(); ++i) {
    const gpx::TriMesh b = world_mesh(sc.objects[sel[i]]);
    if (!gpx::mesh_boolean(acc, b, o, err)) {
      err = "'" + sc.objects[sel[i]].name + "': " + err;
      return false;
    }
  }
  const char *suffix = op == 1 ? " intersection" : op == 2 ? " difference" : " union";
  replace_selection(sc, sel, acc, suffix);
  log_info("csg", "combined " + std::to_string(sel.size()) + " objects");
  return true;
}

bool scene_metaball_from_selection(float smoothness, int detail,
                                   std::string &err) {
  SceneState &sc = scene();
  std::vector<int> sel = mesh_selection(sc);
  if (sel.empty()) {
    err = "select the objects to turn into blobs first";
    return false;
  }
  // Each object becomes one blob: its world centre, and a radius from its
  // own bounds. A sphere gives the ball you would expect; anything else
  // gives a ball the size of the thing, which is what a metaball is for -
  // the shapes are a scaffold, not the result.
  std::vector<gpx::MetaBlob> blobs;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (int i : sel) {
    const gpx::TriMesh m = world_mesh(sc.objects[i]);
    if (m.v.empty()) continue;
    float a[3] = {1e30f, 1e30f, 1e30f}, b[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t k = 0; k + 2 < m.v.size(); k += 3)
      for (int c = 0; c < 3; ++c) {
        a[c] = std::min(a[c], m.v[k + c]);
        b[c] = std::max(b[c], m.v[k + c]);
      }
    gpx::MetaBlob blob;
    blob.x = (a[0] + b[0]) * 0.5f;
    blob.y = (a[1] + b[1]) * 0.5f;
    blob.z = (a[2] + b[2]) * 0.5f;
    blob.radius = std::max({b[0] - a[0], b[1] - a[1], b[2] - a[2]}) * 0.5f;
    // A negative scale is how a blob is asked to carve rather than add: it
    // is the only per-object number already on a SceneObject that is free
    // to mean something here.
    blob.strength = sc.objects[i].scale < 0.f ? -1.f : 1.f;
    if (blob.radius <= 1e-6f) continue;
    blobs.push_back(blob);
    for (int c = 0; c < 3; ++c) {
      lo[c] = std::min(lo[c], a[c]);
      hi[c] = std::max(hi[c], b[c]);
    }
  }
  if (blobs.empty()) {
    err = "none of the selected objects has any size";
    return false;
  }
  // `detail` is the voxel count across the longest side, so the control
  // reads the same as a primitive's: a number of divisions, not a distance
  // that means something different in every scene.
  const float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  const float cell = span / (float)std::clamp(detail, 8, 400);
  gpx::TriMesh out;
  if (!gpx::mesh_metaball(blobs, smoothness, cell, out, err)) return false;
  replace_selection(sc, sel, out, " blob");
  log_info("csg", "melted " + std::to_string(blobs.size()) +
                      " objects into one surface");
  return true;
}

} // namespace studio
