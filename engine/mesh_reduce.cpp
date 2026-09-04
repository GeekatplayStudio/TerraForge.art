// Geekatplay TerraForge - polygon reduction by quadric edge collapse, with
// the surface deviation measured rather than estimated.
//
// Meshwright reduces with QuadriFlow, MeshLab or fast-simplification; all
// three are GPL or bring a heavy toolchain, so this is our own quadric
// collapse (Garland & Heckbert): each vertex carries the sum of the squared
// distances to the planes of its faces, an edge costs what collapsing it adds
// to that sum, and the cheapest edge goes first.
//
// Vue calls the same operation Decimate (p306) and shows the resulting face
// count; we also report how far the result actually moved from the original.
#include "gpx/mesh_report.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace gpx {

namespace {

// A symmetric 4x4 quadric, upper triangle only.
struct Quadric {
  double q[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  void add_plane(double a, double b, double c, double d) {
    const double v[4] = {a, b, c, d};
    int k = 0;
    for (int i = 0; i < 4; ++i)
      for (int j = i; j < 4; ++j) q[k++] += v[i] * v[j];
  }
  void add(const Quadric &o) {
    for (int i = 0; i < 10; ++i) q[i] += o.q[i];
  }
  // v^T Q v for a point.
  double eval(const double p[3]) const {
    const double v[4] = {p[0], p[1], p[2], 1.0};
    double sum = 0;
    int k = 0;
    for (int i = 0; i < 4; ++i)
      for (int j = i; j < 4; ++j) sum += (i == j ? 1.0 : 2.0) * q[k++] * v[i] * v[j];
    return sum;
  }
};

struct Candidate {
  double cost;
  uint32_t a, b;
  uint64_t stamp; // the edge's version when the cost was computed
  // Ties break on the vertex pair so the result never depends on the heap's
  // internal order - the same mesh reduces to the same mesh, always.
  bool operator<(const Candidate &o) const {
    if (cost != o.cost) return cost > o.cost; // min-heap
    if (a != o.a) return a > o.a;
    return b > o.b;
  }
};

inline uint64_t pair_key(uint32_t a, uint32_t b) {
  return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
}

// A uniform grid over the reduced mesh's triangles, so measuring how far the
// original strayed does not become an all-pairs comparison.
struct TriGrid {
  float lo[3] = {0, 0, 0};
  float cell = 1.f;
  int n[3] = {1, 1, 1};
  std::unordered_map<uint64_t, std::vector<uint32_t>> bucket;

  uint64_t key(int x, int y, int z) const {
    return ((uint64_t)(uint32_t)x << 42) ^ ((uint64_t)(uint32_t)y << 21) ^
           (uint64_t)(uint32_t)z;
  }
  void build(const TriMesh &m) {
    float hi[3];
    if (!mesh_bounds(m, lo, hi)) return;
    double span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    size_t nf = std::max<size_t>(m.face_count(), 1);
    cell = (float)std::max(span / std::cbrt((double)nf) * 2.0, 1e-9);
    for (int k = 0; k < 3; ++k)
      n[k] = std::max(1, (int)((hi[k] - lo[k]) / cell) + 1);
    for (size_t i = 0; i < m.face_count(); ++i) {
      const uint32_t *fc = m.face(i);
      float tlo[3], thi[3];
      for (int k = 0; k < 3; ++k) {
        tlo[k] = std::min({m.vert(fc[0])[k], m.vert(fc[1])[k], m.vert(fc[2])[k]});
        thi[k] = std::max({m.vert(fc[0])[k], m.vert(fc[1])[k], m.vert(fc[2])[k]});
      }
      int a[3], b[3];
      for (int k = 0; k < 3; ++k) {
        a[k] = (int)((tlo[k] - lo[k]) / cell);
        b[k] = (int)((thi[k] - lo[k]) / cell);
      }
      for (int x = a[0]; x <= b[0]; ++x)
        for (int y = a[1]; y <= b[1]; ++y)
          for (int z = a[2]; z <= b[2]; ++z)
            bucket[key(x, y, z)].push_back((uint32_t)i);
    }
  }
};

double point_tri_dist2(const double p[3], const float *a, const float *b,
                       const float *c) {
  // Ericson, Real-Time Collision Detection: closest point on a triangle.
  double ab[3], ac[3], ap[3];
  for (int k = 0; k < 3; ++k) {
    ab[k] = b[k] - a[k];
    ac[k] = c[k] - a[k];
    ap[k] = p[k] - a[k];
  }
  double d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
  double d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
  double closest[3];
  auto set = [&](double u, double v) {
    for (int k = 0; k < 3; ++k) closest[k] = a[k] + u * ab[k] + v * ac[k];
  };
  if (d1 <= 0 && d2 <= 0) {
    set(0, 0);
  } else {
    double bp[3], cp[3];
    for (int k = 0; k < 3; ++k) {
      bp[k] = p[k] - b[k];
      cp[k] = p[k] - c[k];
    }
    double d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
    double d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
    double d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
    double d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
    double vc = d1 * d4 - d3 * d2;
    double vb = d5 * d2 - d1 * d6;
    double va = d3 * d6 - d5 * d4;
    if (d3 >= 0 && d4 <= d3) {
      set(1, 0);
    } else if (d6 >= 0 && d5 <= d6) {
      set(0, 1);
    } else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
      set(d1 / (d1 - d3), 0);
    } else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      set(0, d2 / (d2 - d6));
    } else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
      double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      for (int k = 0; k < 3; ++k) closest[k] = b[k] + w * (c[k] - b[k]);
    } else {
      double denom = 1.0 / (va + vb + vc);
      set(vb * denom, vc * denom);
    }
  }
  double d = 0;
  for (int k = 0; k < 3; ++k) {
    double e = p[k] - closest[k];
    d += e * e;
  }
  return d;
}

} // namespace

bool mesh_reduce(TriMesh &m, size_t target_faces, MeshReduceResult &out) {
  out = MeshReduceResult();
  out.faces_before = m.face_count();
  out.faces_after = out.faces_before;
  if (target_faces == 0 || m.face_count() <= target_faces) return false;

  const TriMesh original = m;

  // Per-vertex quadrics from the plane of every incident face.
  std::vector<Quadric> Q(m.vert_count());
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
    double u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    double v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    double nx = u[1] * v[2] - u[2] * v[1];
    double ny = u[2] * v[0] - u[0] * v[2];
    double nz = u[0] * v[1] - u[1] * v[0];
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 0) continue;
    nx /= len;
    ny /= len;
    nz /= len;
    double d = -(nx * p0[0] + ny * p0[1] + nz * p0[2]);
    Quadric qp;
    qp.add_plane(nx, ny, nz, d);
    for (int k = 0; k < 3; ++k) Q[fc[k]].add(qp);
  }

  // Adjacency that survives collapses: which faces touch a vertex.
  std::vector<std::vector<uint32_t>> vfaces(m.vert_count());
  for (size_t i = 0; i < m.face_count(); ++i)
    for (int k = 0; k < 3; ++k) vfaces[m.f[i * 3 + k]].push_back((uint32_t)i);

  std::vector<uint8_t> face_dead(m.face_count(), 0);
  std::vector<uint32_t> remap(m.vert_count());
  for (uint32_t i = 0; i < remap.size(); ++i) remap[i] = i;
  std::vector<uint64_t> vstamp(m.vert_count(), 0);
  std::function<uint32_t(uint32_t)> root = [&](uint32_t i) {
    while (remap[i] != i) i = remap[i] = remap[remap[i]];
    return i;
  };

  auto cost_of = [&](uint32_t a, uint32_t b, double mid[3]) {
    for (int k = 0; k < 3; ++k)
      mid[k] = 0.5 * ((double)m.v[a * 3 + k] + m.v[b * 3 + k]);
    Quadric q = Q[a];
    q.add(Q[b]);
    return std::max(0.0, q.eval(mid));
  };

  std::priority_queue<Candidate> heap;
  {
    const EdgeTable et = mesh_edges(m);
    for (size_t i = 0; i < et.size(); ++i) {
      double mid[3];
      double c = cost_of(et.a[i], et.b[i], mid);
      heap.push({c, et.a[i], et.b[i], 0});
    }
  }

  size_t live_faces = m.face_count();
  while (live_faces > target_faces && !heap.empty()) {
    Candidate cd = heap.top();
    heap.pop();
    uint32_t a = root(cd.a), b = root(cd.b);
    if (a == b) continue;
    if (vstamp[a] + vstamp[b] != cd.stamp) {
      // One end moved since this cost was computed: re-price and re-queue.
      double mid[3];
      double c = cost_of(a, b, mid);
      heap.push({c, a, b, vstamp[a] + vstamp[b]});
      continue;
    }
    // Collapse b into a at the midpoint.
    double mid[3];
    cost_of(a, b, mid);
    for (int k = 0; k < 3; ++k) m.v[a * 3 + k] = (float)mid[k];
    Q[a].add(Q[b]);
    remap[b] = a;
    ++vstamp[a];

    // Rewrite b's faces onto a; any face that now repeats a vertex is gone.
    std::vector<uint32_t> touched = vfaces[b];
    vfaces[b].clear();
    for (uint32_t fi : touched) {
      if (face_dead[fi]) continue;
      uint32_t *fc = &m.f[fi * 3];
      for (int k = 0; k < 3; ++k)
        if (root(fc[k]) == a || fc[k] == b) fc[k] = a;
      if (fc[0] == fc[1] || fc[1] == fc[2] || fc[0] == fc[2]) {
        face_dead[fi] = 1;
        --live_faces;
      } else {
        vfaces[a].push_back(fi);
      }
    }
    // Fresh candidates for the merged neighbourhood.
    std::unordered_set<uint32_t> nbrs;
    for (uint32_t fi : vfaces[a]) {
      if (face_dead[fi]) continue;
      for (int k = 0; k < 3; ++k) {
        uint32_t o = root(m.f[fi * 3 + k]);
        if (o != a) nbrs.insert(o);
      }
    }
    for (uint32_t o : nbrs) {
      double c = cost_of(a, o, mid);
      heap.push({c, std::min(a, o), std::max(a, o), vstamp[a] + vstamp[o]});
    }
  }

  // Compact.
  TriMesh reduced;
  reduced.v = m.v;
  for (size_t i = 0; i < m.face_count(); ++i) {
    if (face_dead[i]) continue;
    uint32_t fc[3] = {root(m.f[i * 3]), root(m.f[i * 3 + 1]),
                      root(m.f[i * 3 + 2])};
    if (fc[0] == fc[1] || fc[1] == fc[2] || fc[0] == fc[2]) continue;
    reduced.f.insert(reduced.f.end(), fc, fc + 3);
  }
  mesh_drop_duplicate_faces(reduced);
  mesh_drop_unreferenced(reduced);
  m = reduced;
  out.faces_after = m.face_count();

  // How far did it move? Every original vertex, to the nearest point on the
  // reduced surface - measured, not estimated.
  TriGrid grid;
  grid.build(m);
  double worst = 0.0;
  for (size_t i = 0; i < original.vert_count(); ++i) {
    const double p[3] = {original.v[i * 3], original.v[i * 3 + 1],
                         original.v[i * 3 + 2]};
    double best = 1e300;
    int c[3];
    for (int k = 0; k < 3; ++k)
      c[k] = (int)((p[k] - grid.lo[k]) / grid.cell);
    for (int r = 0; r <= 3 && best == 1e300; ++r) {
      for (int x = c[0] - r; x <= c[0] + r; ++x)
        for (int y = c[1] - r; y <= c[1] + r; ++y)
          for (int z = c[2] - r; z <= c[2] + r; ++z) {
            auto it = grid.bucket.find(grid.key(x, y, z));
            if (it == grid.bucket.end()) continue;
            for (uint32_t fi : it->second) {
              const uint32_t *fc = m.face(fi);
              best = std::min(best, point_tri_dist2(p, m.vert(fc[0]),
                                                    m.vert(fc[1]),
                                                    m.vert(fc[2])));
            }
          }
    }
    if (best < 1e300) worst = std::max(worst, best);
  }
  out.max_deviation = std::sqrt(worst);
  float lo[3], hi[3];
  if (mesh_bounds(original, lo, hi)) {
    double span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    out.deviation_frac = span > 0 ? out.max_deviation / span : 0.0;
  }
  return true;
}

} // namespace gpx
