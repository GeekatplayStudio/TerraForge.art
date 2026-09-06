// Geekatplay TerraForge - see mesh_lod.hpp.
#include "mesh_lod.hpp"
#include "gpx/mesh.hpp"
#include "gpx/mesh_report.hpp"
#include "scene.hpp"
#include <cmath>
#include <unordered_map>

namespace studio {

namespace {

// The soup welded back into an indexed mesh: identical positions become one
// vertex, which is what gives the reducer edges to collapse.
gpx::TriMesh weld(const std::vector<float> &soup) {
  gpx::TriMesh m;
  struct Key { long long x, y, z; bool operator==(const Key &o) const { return x == o.x && y == o.y && z == o.z; } };
  struct Hash { size_t operator()(const Key &k) const { return (size_t)(k.x * 73856093ll ^ k.y * 19349663ll ^ k.z * 83492791ll); } };
  std::unordered_map<Key, uint32_t, Hash> seen;
  const size_t n = soup.size() / 6;
  m.f.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const float *p = &soup[i * 6];
    Key k{(long long)std::llround(p[0] * 1e5), (long long)std::llround(p[1] * 1e5), (long long)std::llround(p[2] * 1e5)};
    auto it = seen.find(k);
    uint32_t idx;
    if (it == seen.end()) {
      idx = (uint32_t)m.vert_count();
      m.v.insert(m.v.end(), p, p + 3);
      seen.emplace(k, idx);
    } else idx = it->second;
    m.f.push_back(idx);
  }
  return m;
}

void flatten(const gpx::TriMesh &m, std::vector<float> &out) {
  out.clear();
  out.reserve(m.face_count() * 18);
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
    float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    float nx = u[1] * v[2] - u[2] * v[1], ny = u[2] * v[0] - u[0] * v[2], nz = u[0] * v[1] - u[1] * v[0];
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.f) { nx /= len; ny /= len; nz /= len; } else ny = 1.f;
    for (const float *p : {p0, p1, p2}) {
      out.insert(out.end(), p, p + 3);
      out.push_back(nx); out.push_back(ny); out.push_back(nz);
    }
  }
}

} // namespace

bool mesh_build_lods(SceneObject &o) {
  o.lod_tried = true;
  o.lod_verts[0].clear();
  o.lod_verts[1].clear();
  o.lod_count[0] = o.lod_count[1] = 0;
  if (!o.parts.empty()) return false; // pictures would tear
  const size_t faces = o.verts.size() / 18;
  if (faces < 300) return false;      // nothing worth reducing
  gpx::TriMesh m = weld(o.verts);
  const float targets[2] = {0.25f, 0.0625f};
  bool any = false;
  for (int k = 0; k < 2; ++k) {
    size_t target = std::max<size_t>((size_t)(faces * targets[k]), 24);
    gpx::MeshReduceResult r;
    if (!gpx::mesh_reduce(m, target, r) || m.face_count() == 0) break;
    flatten(m, o.lod_verts[k]);
    o.lod_count[k] = (int)(o.lod_verts[k].size() / 6);
    any = true;
  }
  return any;
}

} // namespace studio
