// Geekatplay TerraForge - mesh topology: edges, shells, volume, boundaries.
// See gpx/mesh.hpp. Ported from Meshwright's engine (MIT, Geekatplay Studio).
#include "gpx/mesh.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace gpx {

namespace {

// An undirected edge as one 64-bit key: the pair packed low-first, so the two
// directions of the same edge land on the same key. This is the whole reason
// the diagnosis is one pass rather than a sort of every edge row.
inline uint64_t edge_key(uint32_t a, uint32_t b) {
  return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
}

} // namespace

EdgeTable mesh_edges(const TriMesh &m) {
  EdgeTable t;
  const size_t nf = m.face_count();
  std::unordered_map<uint64_t, uint32_t> index;
  index.reserve(nf * 3);
  // Per edge, count of uses in each direction: a closed, consistently wound
  // surface uses every edge exactly once each way.
  std::vector<uint32_t> fwd, rev;
  for (size_t i = 0; i < nf; ++i) {
    const uint32_t *fc = m.face(i);
    for (int e = 0; e < 3; ++e) {
      uint32_t a = fc[e], b = fc[(e + 1) % 3];
      uint64_t k = edge_key(a, b);
      auto it = index.find(k);
      uint32_t slot;
      if (it == index.end()) {
        slot = (uint32_t)t.a.size();
        index.emplace(k, slot);
        t.a.push_back(std::min(a, b));
        t.b.push_back(std::max(a, b));
        t.uses.push_back(0);
        fwd.push_back(0);
        rev.push_back(0);
      } else {
        slot = it->second;
      }
      ++t.uses[slot];
      (a < b ? fwd : rev)[slot]++;
    }
  }
  t.consistent.resize(t.a.size());
  for (size_t i = 0; i < t.a.size(); ++i)
    // Two faces on an edge are consistent when they traverse it in opposite
    // directions. One-sided and non-manifold edges are not judged here.
    t.consistent[i] = (t.uses[i] != 2) ? 1 : (fwd[i] == 1 && rev[i] == 1);
  return t;
}

int mesh_components(const TriMesh &m, std::vector<int> &label) {
  const size_t nf = m.face_count();
  label.assign(nf, -1);
  if (!nf) return 0;
  // face lists per undirected edge
  std::unordered_map<uint64_t, std::vector<uint32_t>> at;
  at.reserve(nf * 3);
  for (size_t i = 0; i < nf; ++i) {
    const uint32_t *fc = m.face(i);
    for (int e = 0; e < 3; ++e)
      at[edge_key(fc[e], fc[(e + 1) % 3])].push_back((uint32_t)i);
  }
  int comp = 0;
  std::vector<uint32_t> stack;
  for (size_t seed = 0; seed < nf; ++seed) {
    if (label[seed] >= 0) continue;
    label[seed] = comp;
    stack.push_back((uint32_t)seed);
    while (!stack.empty()) {
      uint32_t fi = stack.back();
      stack.pop_back();
      const uint32_t *fc = m.face(fi);
      for (int e = 0; e < 3; ++e) {
        auto it = at.find(edge_key(fc[e], fc[(e + 1) % 3]));
        if (it == at.end()) continue;
        for (uint32_t nb : it->second)
          if (label[nb] < 0) {
            label[nb] = comp;
            stack.push_back(nb);
          }
      }
    }
    ++comp;
  }
  return comp;
}

double mesh_volume(const TriMesh &m) {
  // Signed volume as the sum of tetrahedra from the origin. Correct for any
  // closed surface wherever the origin sits; meaningless for an open one,
  // which is why the report only shows it when the mesh is watertight.
  double vol = 0.0;
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
    double cx = (double)p1[1] * p2[2] - (double)p1[2] * p2[1];
    double cy = (double)p1[2] * p2[0] - (double)p1[0] * p2[2];
    double cz = (double)p1[0] * p2[1] - (double)p1[1] * p2[0];
    vol += (p0[0] * cx + p0[1] * cy + p0[2] * cz) / 6.0;
  }
  return vol;
}

double mesh_area(const TriMesh &m) {
  double area = 0.0;
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
    double ux = (double)p1[0] - p0[0], uy = (double)p1[1] - p0[1],
           uz = (double)p1[2] - p0[2];
    double vx = (double)p2[0] - p0[0], vy = (double)p2[1] - p0[1],
           vz = (double)p2[2] - p0[2];
    double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz,
           cz = ux * vy - uy * vx;
    area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
  }
  return area;
}

bool mesh_bounds(const TriMesh &m, float lo[3], float hi[3]) {
  if (m.v.empty()) return false;
  for (int k = 0; k < 3; ++k) {
    lo[k] = m.v[k];
    hi[k] = m.v[k];
  }
  for (size_t i = 1; i < m.vert_count(); ++i)
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], m.v[i * 3 + k]);
      hi[k] = std::max(hi[k], m.v[i * 3 + k]);
    }
  return true;
}

std::vector<std::vector<uint32_t>> mesh_boundary_loops(const TriMesh &m) {
  std::vector<std::vector<uint32_t>> loops;
  // Directed boundary edges: on a boundary, exactly one face uses the edge, so
  // its direction is the direction the hole must be walked to stay consistent
  // with the surface - which is what makes the fan fill face the right way.
  std::unordered_map<uint64_t, int> count;
  std::unordered_map<uint32_t, uint32_t> next;
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    for (int e = 0; e < 3; ++e)
      ++count[edge_key(fc[e], fc[(e + 1) % 3])];
  }
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    for (int e = 0; e < 3; ++e) {
      uint32_t a = fc[e], b = fc[(e + 1) % 3];
      if (count[edge_key(a, b)] == 1) next.emplace(a, b);
    }
  }
  std::vector<uint32_t> start;
  start.reserve(next.size());
  for (const auto &kv : next) start.push_back(kv.first);
  std::sort(start.begin(), start.end()); // deterministic loop order
  std::unordered_map<uint32_t, bool> used;
  for (uint32_t s : start) {
    if (used[s]) continue;
    std::vector<uint32_t> loop;
    uint32_t cur = s;
    while (true) {
      if (used[cur]) break;
      used[cur] = true;
      loop.push_back(cur);
      auto it = next.find(cur);
      if (it == next.end()) break;
      cur = it->second;
      if (cur == s) break;
    }
    if (loop.size() >= 3) loops.push_back(std::move(loop));
  }
  return loops;
}

} // namespace gpx
