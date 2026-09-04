// Geekatplay TerraForge - the mesh cleanup operations behind repair.
// See gpx/mesh.hpp. Ported from Meshwright's engine (MIT, Geekatplay Studio).
//
// Every function returns how much it actually changed, never whether it ran:
// the repair report says "merged 412 duplicate vertices", and it can only say
// that honestly if the count comes back from the operation itself.
#include "gpx/mesh.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace gpx {

namespace {

// A vertex position quantised to a grid of `tol`, as a hashable key. Welding
// on a grid rather than by nearest-neighbour search keeps this linear; the
// half-cell offsets are what stop two points either side of a cell boundary
// from being missed.
struct GridKey {
  int64_t x, y, z;
  bool operator==(const GridKey &o) const {
    return x == o.x && y == o.y && z == o.z;
  }
};
struct GridHash {
  size_t operator()(const GridKey &k) const {
    uint64_t h = 1469598103934665603ull;
    for (int64_t v : {k.x, k.y, k.z}) {
      h ^= (uint64_t)v;
      h *= 1099511628211ull;
    }
    return (size_t)h;
  }
};

inline GridKey grid_of(const float *p, float inv_tol) {
  return {(int64_t)std::llround(p[0] * inv_tol),
          (int64_t)std::llround(p[1] * inv_tol),
          (int64_t)std::llround(p[2] * inv_tol)};
}

double tri_area(const TriMesh &m, const uint32_t *fc) {
  const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
  double ux = (double)p1[0] - p0[0], uy = (double)p1[1] - p0[1],
         uz = (double)p1[2] - p0[2];
  double vx = (double)p2[0] - p0[0], vy = (double)p2[1] - p0[1],
         vz = (double)p2[2] - p0[2];
  double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
  return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

} // namespace

size_t mesh_weld(TriMesh &m, float tol) {
  if (m.v.empty() || tol <= 0.f) return 0;
  const float inv = 1.f / tol;
  std::unordered_map<GridKey, uint32_t, GridHash> first;
  first.reserve(m.vert_count());
  std::vector<uint32_t> remap(m.vert_count());
  std::vector<float> out;
  out.reserve(m.v.size());
  for (size_t i = 0; i < m.vert_count(); ++i) {
    GridKey k = grid_of(m.vert(i), inv);
    auto it = first.find(k);
    if (it == first.end()) {
      uint32_t idx = (uint32_t)(out.size() / 3);
      first.emplace(k, idx);
      remap[i] = idx;
      out.insert(out.end(), m.vert(i), m.vert(i) + 3);
    } else {
      remap[i] = it->second;
    }
  }
  size_t merged = m.vert_count() - out.size() / 3;
  if (!merged) return 0;
  for (uint32_t &idx : m.f) idx = remap[idx];
  m.v.swap(out);
  return merged;
}

size_t mesh_drop_degenerate(TriMesh &m) {
  std::vector<uint32_t> keep;
  keep.reserve(m.f.size());
  size_t dropped = 0;
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    bool repeated = fc[0] == fc[1] || fc[1] == fc[2] || fc[0] == fc[2];
    if (repeated || tri_area(m, fc) <= 0.0) {
      ++dropped;
      continue;
    }
    keep.insert(keep.end(), fc, fc + 3);
  }
  if (dropped) m.f.swap(keep);
  return dropped;
}

size_t mesh_drop_duplicate_faces(TriMesh &m) {
  // Two faces are the same when they use the same three vertices, whichever
  // order - a duplicate wound the other way is still a duplicate, and leaving
  // it makes the surface non-manifold along all three edges.
  struct Key {
    uint32_t a, b, c;
    bool operator==(const Key &o) const {
      return a == o.a && b == o.b && c == o.c;
    }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const {
      uint64_t h = ((uint64_t)k.a * 73856093ull) ^ ((uint64_t)k.b * 19349663ull) ^
                   ((uint64_t)k.c * 83492791ull);
      return (size_t)h;
    }
  };
  std::unordered_set<Key, KeyHash> seen;
  seen.reserve(m.face_count());
  std::vector<uint32_t> keep;
  keep.reserve(m.f.size());
  size_t dropped = 0;
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    uint32_t s[3] = {fc[0], fc[1], fc[2]};
    std::sort(s, s + 3);
    if (!seen.insert(Key{s[0], s[1], s[2]}).second) {
      ++dropped;
      continue;
    }
    keep.insert(keep.end(), fc, fc + 3);
  }
  if (dropped) m.f.swap(keep);
  return dropped;
}

size_t mesh_drop_unreferenced(TriMesh &m) {
  std::vector<uint32_t> remap(m.vert_count(), 0xffffffffu);
  for (uint32_t idx : m.f)
    if (idx < remap.size()) remap[idx] = 1;
  std::vector<float> out;
  out.reserve(m.v.size());
  uint32_t next = 0;
  for (size_t i = 0; i < m.vert_count(); ++i) {
    if (remap[i] == 0xffffffffu) continue;
    remap[i] = next++;
    out.insert(out.end(), m.vert(i), m.vert(i) + 3);
  }
  size_t dropped = m.vert_count() - next;
  if (!dropped) return 0;
  for (uint32_t &idx : m.f) idx = remap[idx];
  m.v.swap(out);
  return dropped;
}

size_t mesh_fix_winding(TriMesh &m) {
  const size_t nf = m.face_count();
  if (!nf) return 0;
  // Walk the surface face by face. A neighbour that shares an edge in the
  // *same* direction is wound the opposite way round, so flip it and carry on.
  std::unordered_map<uint64_t, std::vector<uint32_t>> at;
  at.reserve(nf * 3);
  auto key = [](uint32_t a, uint32_t b) {
    return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
  };
  for (size_t i = 0; i < nf; ++i) {
    const uint32_t *fc = m.face(i);
    for (int e = 0; e < 3; ++e)
      at[key(fc[e], fc[(e + 1) % 3])].push_back((uint32_t)i);
  }
  std::vector<uint8_t> done(nf, 0);
  std::vector<uint32_t> stack;
  size_t flipped = 0;
  for (size_t seed = 0; seed < nf; ++seed) {
    if (done[seed]) continue;
    done[seed] = 1;
    stack.push_back((uint32_t)seed);
    while (!stack.empty()) {
      uint32_t fi = stack.back();
      stack.pop_back();
      for (int e = 0; e < 3; ++e) {
        uint32_t a = m.f[fi * 3 + e], b = m.f[fi * 3 + (e + 1) % 3];
        auto it = at.find(key(a, b));
        if (it == at.end()) continue;
        for (uint32_t nb : it->second) {
          if (nb == fi || done[nb]) continue;
          bool same_dir = false;
          for (int k = 0; k < 3; ++k)
            if (m.f[nb * 3 + k] == a && m.f[nb * 3 + (k + 1) % 3] == b)
              same_dir = true;
          if (same_dir) {
            std::swap(m.f[nb * 3 + 1], m.f[nb * 3 + 2]);
            ++flipped;
          }
          done[nb] = 1;
          stack.push_back(nb);
        }
      }
    }
  }
  return flipped;
}

size_t mesh_fix_inversion(TriMesh &m) {
  if (mesh_volume(m) >= 0.0) return 0;
  for (size_t i = 0; i < m.face_count(); ++i)
    std::swap(m.f[i * 3 + 1], m.f[i * 3 + 2]);
  return 1;
}

size_t mesh_fill_holes(TriMesh &m, size_t max_edges) {
  std::vector<std::vector<uint32_t>> loops = mesh_boundary_loops(m);
  size_t closed = 0;
  for (const std::vector<uint32_t> &loop : loops) {
    if (loop.size() < 3 || loop.size() > max_edges) continue;
    // Fan from the first vertex. The loop is walked in the direction the
    // surrounding faces left open, so the fan comes out facing the same way
    // as its neighbours without a normal ever being computed.
    for (size_t i = 1; i + 1 < loop.size(); ++i) {
      m.f.push_back(loop[0]);
      m.f.push_back(loop[i + 1]);
      m.f.push_back(loop[i]);
    }
    ++closed;
  }
  return closed;
}

size_t mesh_drop_small_shells(TriMesh &m, size_t min_faces,
                              float min_extent_frac) {
  std::vector<int> label;
  int n = mesh_components(m, label);
  if (n <= 1) return 0;
  float lo[3], hi[3];
  if (!mesh_bounds(m, lo, hi)) return 0;
  float whole = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  std::vector<size_t> faces((size_t)n, 0);
  std::vector<float> clo((size_t)n * 3, 1e30f), chi((size_t)n * 3, -1e30f);
  for (size_t i = 0; i < m.face_count(); ++i) {
    int c = label[i];
    ++faces[(size_t)c];
    for (int e = 0; e < 3; ++e) {
      const float *p = m.vert(m.f[i * 3 + e]);
      for (int k = 0; k < 3; ++k) {
        clo[(size_t)c * 3 + k] = std::min(clo[(size_t)c * 3 + k], p[k]);
        chi[(size_t)c * 3 + k] = std::max(chi[(size_t)c * 3 + k], p[k]);
      }
    }
  }
  std::vector<uint8_t> drop((size_t)n, 0);
  size_t dropped = 0;
  for (int c = 0; c < n; ++c) {
    float ext = std::max({chi[(size_t)c * 3] - clo[(size_t)c * 3],
                          chi[(size_t)c * 3 + 1] - clo[(size_t)c * 3 + 1],
                          chi[(size_t)c * 3 + 2] - clo[(size_t)c * 3 + 2]});
    bool tiny = faces[(size_t)c] < min_faces ||
                (whole > 0.f && ext < whole * min_extent_frac);
    if (tiny) {
      drop[(size_t)c] = 1;
      ++dropped;
    }
  }
  if (dropped == (size_t)n) return 0; // never delete the whole model
  if (!dropped) return 0;
  std::vector<uint32_t> keep;
  keep.reserve(m.f.size());
  for (size_t i = 0; i < m.face_count(); ++i)
    if (!drop[(size_t)label[i]])
      keep.insert(keep.end(), m.face(i), m.face(i) + 3);
  m.f.swap(keep);
  mesh_drop_unreferenced(m);
  return dropped;
}

} // namespace gpx
