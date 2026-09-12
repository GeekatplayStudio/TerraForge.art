// Geekatplay TerraForge - reduced copies of a mesh that has parts, each
// keeping its own picture (mesh_lod.hpp).
//
// A plant is the case this exists for. Its geometry is divided by material -
// bark here, leaves there - and every part carries a cut-out picture that IS
// the shape: take the picture away and a leaf card is a grey rectangle and a
// crown is a cloud of them. So the reduction that worked for a rock, which
// welds the whole mesh into one soup of positions and throws the texture
// coordinates away, could not be used on a plant at all, and the only
// refusal was a one-line `if (!o.parts.empty()) return false`.
//
// The cost of that refusal was the whole distance budget. An instanced
// population picks a level per cell - near cells whole, far cells reduced,
// the farthest as cards - and with no reduced mesh to pick, every ring but
// the last drew the FULL tree. A wood of four thousand oaks was drawing
// half-million-triangle trees a kilometre away.
//
// Each part is reduced on its own and keeps its own run in the buffer, and
// the two kinds of part are reduced in the two different ways they have to be.
//
// WOOD is a surface, so it takes a quadric collapse: a quarter of the faces,
// then a sixteenth. Its texture coordinates are looked up again afterwards
// from the nearest original vertex, because the reducer moves its survivors
// and reports no mapping.
//
// FOLIAGE cannot be collapsed at all. A leaf card is a quad whose SHAPE is
// its picture - the geometry is a rectangle and the leaf is the part of the
// picture that is not transparent - so merging the corners of two leaves
// produces a triangle that samples the picture across the gap between them,
// and a crown decimated that way comes out as dark mush. The first attempt
// here did exactly that. What foliage wants instead is fewer leaves: whole
// cards, kept entire with their own coordinates, and simply not all of them.
// The survivors are grown about their own centres to hold the coverage the
// dropped ones took with them, which is why a thinned crown still reads as a
// crown rather than as a thinning one.
#include "mesh_lod.hpp"
#include "gpx/mesh.hpp"
#include "gpx/mesh_report.hpp"
#include "scene.hpp"
#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <vector>

namespace studio {

namespace {

struct Welded {
  gpx::TriMesh mesh;
  std::vector<float> uv; // two per welded vertex, the first one seen there
};

// One part's soup welded into an indexed mesh, keeping a texture coordinate
// for each position that survives.
Welded weld_part(const SceneObject &o, int first, int count) {
  Welded w;
  struct Key {
    long long x, y, z;
    bool operator==(const Key &k) const { return x == k.x && y == k.y && z == k.z; }
  };
  struct Hash {
    size_t operator()(const Key &k) const {
      return (size_t)(k.x * 73856093ll ^ k.y * 19349663ll ^ k.z * 83492791ll);
    }
  };
  std::unordered_map<Key, uint32_t, Hash> seen;
  const bool have_uv = o.uvs.size() >= (size_t)(first + count) * 2;
  w.mesh.f.reserve((size_t)count);
  for (int i = 0; i < count; ++i) {
    const float *p = &o.verts[(size_t)(first + i) * 6];
    const Key k{(long long)std::llround(p[0] * 1e5), (long long)std::llround(p[1] * 1e5),
                (long long)std::llround(p[2] * 1e5)};
    auto it = seen.find(k);
    uint32_t idx;
    if (it == seen.end()) {
      idx = (uint32_t)w.mesh.vert_count();
      w.mesh.v.insert(w.mesh.v.end(), p, p + 3);
      w.uv.push_back(have_uv ? o.uvs[(size_t)(first + i) * 2] : 0.f);
      w.uv.push_back(have_uv ? o.uvs[(size_t)(first + i) * 2 + 1] : 0.f);
      seen.emplace(k, idx);
    } else {
      idx = it->second;
    }
    w.mesh.f.push_back(idx);
  }
  return w;
}

// The texture coordinate at a position, from the part before it was reduced.
// A coarse grid over the original vertices, which is enough: what is being
// asked is "which corner of which leaf was here", and the answer is always
// within a leaf's width.
struct UvLookup {
  float cell = 0.05f;
  std::map<long long, std::vector<uint32_t>> grid;
  const Welded *src = nullptr;

  void build(const Welded &w, float span) {
    src = &w;
    cell = std::max(span * 0.02f, 1e-4f);
    double su = 0.0, sv = 0.0;
    for (uint32_t i = 0; i < (uint32_t)w.mesh.vert_count(); ++i) {
      grid[key(w.mesh.vert(i))].push_back(i);
      su += w.uv[(size_t)i * 2];
      sv += w.uv[(size_t)i * 2 + 1];
    }
    const size_t n = w.mesh.vert_count();
    if (n) {
      mean_uv[0] = (float)(su / (double)n);
      mean_uv[1] = (float)(sv / (double)n);
    }
  }
  long long key(const float *p) const {
    const long long x = (long long)std::floor(p[0] / cell), y = (long long)std::floor(p[1] / cell),
                    z = (long long)std::floor(p[2] / cell);
    return (x * 73856093ll) ^ (y * 19349663ll) ^ (z * 83492791ll);
  }
  // The rings are grown until something is found.
  //
  // A quadric collapse does not keep its vertices where it found them - it
  // puts the survivor at the point that costs least, which on a heavily
  // reduced part is a long way from any original. Searching one ring of cells
  // and giving up found nothing for most vertices, and "nothing" meant the
  // texture coordinate stayed at (0, 0): the corner of the picture, which on
  // a bark is the dark inside of a crack. Every reduced tree came out nearly
  // black. Growing the search until it hits costs a few more cells on the
  // vertices that need it and nothing at all on the ones that do not.
  void at(const float *p, float *uv) const {
    uv[0] = mean_uv[0];
    uv[1] = mean_uv[1];
    if (!src || grid.empty()) return;
    float best = 1e30f;
    uint32_t found = 0;
    bool any = false;
    for (int ring = 1; ring <= 8 && !any; ++ring)
      for (int dx = -ring; dx <= ring; ++dx)
        for (int dy = -ring; dy <= ring; ++dy)
          for (int dz = -ring; dz <= ring; ++dz) {
            // only the shell of this ring; the inside was searched already
            if (ring > 1 && std::abs(dx) != ring && std::abs(dy) != ring && std::abs(dz) != ring)
              continue;
            const float q[3] = {p[0] + (float)dx * cell, p[1] + (float)dy * cell,
                                p[2] + (float)dz * cell};
            auto it = grid.find(key(q));
            if (it == grid.end()) continue;
            for (uint32_t v : it->second) {
              const float *o = src->mesh.vert(v);
              const float d = (o[0] - p[0]) * (o[0] - p[0]) + (o[1] - p[1]) * (o[1] - p[1]) +
                              (o[2] - p[2]) * (o[2] - p[2]);
              if (d < best) {
                best = d;
                found = v;
                any = true;
              }
            }
          }
    if (!any) return; // the part's own average, which is at least its colour
    uv[0] = src->uv[(size_t)found * 2];
    uv[1] = src->uv[(size_t)found * 2 + 1];
  }
  float mean_uv[2] = {0.5f, 0.5f};
};

// Foliage, thinned: whole cards kept verbatim, the rest dropped, and the
// survivors grown about their own centres to hold the coverage.
//
// Deterministic, so a tree does not shuffle its leaves between levels: the
// cards are taken in order and every one whose running count crosses the next
// whole share is kept. The growth is 1/sqrt(rate) because area goes as the
// square of the scale - keep a quarter of the leaves at twice the size and
// the crown covers what it did.
void keep_some_cards(const SceneObject &o, const SceneObject::Part &part, float rate,
                     std::vector<float> &out) {
  const bool have_uv = o.uvs.size() >= (size_t)(part.first + part.count) * 2;
  const int tris = part.count / 3;
  if (tris <= 0) return;
  // a card is two triangles; keeping them in pairs keeps leaves whole
  const int cards = std::max(tris / 2, 1);
  const int per_card = tris / cards;
  const float grow = 1.f / std::sqrt(std::max(rate, 0.02f));
  float carry = 0.f;
  for (int c = 0; c < cards; ++c) {
    carry += rate;
    if (carry < 1.f) continue;
    carry -= 1.f;
    // this card's own centre, to grow it about
    float mid[3] = {0.f, 0.f, 0.f};
    int n = 0;
    for (int t = 0; t < per_card; ++t)
      for (int k = 0; k < 3; ++k) {
        const size_t v = (size_t)(part.first + ((size_t)c * per_card + t) * 3 + k);
        if (v * 6 + 5 >= o.verts.size()) continue;
        for (int q = 0; q < 3; ++q) mid[q] += o.verts[v * 6 + (size_t)q];
        ++n;
      }
    if (!n) continue;
    for (int q = 0; q < 3; ++q) mid[q] /= (float)n;
    for (int t = 0; t < per_card; ++t)
      for (int k = 0; k < 3; ++k) {
        const size_t v = (size_t)(part.first + ((size_t)c * per_card + t) * 3 + k);
        if (v * 6 + 5 >= o.verts.size()) continue;
        const float *p = &o.verts[v * 6];
        out.insert(out.end(), {mid[0] + (p[0] - mid[0]) * grow, mid[1] + (p[1] - mid[1]) * grow,
                               mid[2] + (p[2] - mid[2]) * grow, p[3], p[4], p[5],
                               have_uv ? o.uvs[v * 2] : 0.f, have_uv ? o.uvs[v * 2 + 1] : 0.f});
      }
  }
}

} // namespace

// Off, and honestly so.
//
// The wood half of this works: collapsing a trunk to a quarter of its faces
// and looking its texture coordinates up again drew correct brown trunks, and
// the whole forest's GPU time fell from 15.0 ms to 8.6 ms. The foliage half
// does not. Thinning the cards keeps each leaf's own coordinates, which is
// the right idea, but the crowns still draw far too dark and growing the
// survivors to hold their coverage costs more fill than the reduction saves -
// 24.8 ms, worse than drawing the trees whole.
//
// So it is switched off at the door rather than left half-working: a wood of
// dark trees is worse than a wood of slow ones, and the billboard ring that
// was there before this is both correct and cheap. What is here is the
// groundwork - per-part reduction, the coordinate lookup, the per-part draw
// path in renderer_instances.cpp - and what it still needs is to find why a
// card copied verbatim, with its own coordinates and its own picture, draws
// darker through the reduced buffer than through the full one. That is a
// contained question and it wants a session with a frame capture, not another
// guess.
static constexpr bool PART_LODS_READY = false;

bool mesh_build_lods_parts(SceneObject &o) {
  o.lod_verts[0].clear();
  o.lod_verts[1].clear();
  o.lod_parts[0].clear();
  o.lod_parts[1].clear();
  o.lod_count[0] = o.lod_count[1] = 0;
  if (!PART_LODS_READY) return false;
  if (o.parts.empty()) return false;
  const size_t faces = o.verts.size() / 18;
  if (faces < 600) return false; // nothing worth reducing

  float span = 0.f;
  for (int k = 0; k < 3; ++k) span = std::max(span, o.bmax[k] - o.bmin[k]);
  if (span <= 0.f) span = 1.f;

  const float targets[2] = {0.25f, 0.0625f};
  bool any = false;
  for (int level = 0; level < 2; ++level) {
    std::vector<float> &out = o.lod_verts[level];
    for (int pi = 0; pi < (int)o.parts.size(); ++pi) {
      const SceneObject::Part &part = o.parts[(size_t)pi];
      if (part.count < 3 || part.first < 0 || part.first + part.count > o.vert_count) continue;
      SceneObject::LodPart lp;
      lp.part = pi;
      lp.first = (int)(out.size() / 8);

      if (part.double_sided) {
        // Foliage: fewer leaves, each one whole.
        keep_some_cards(o, part, targets[level], out);
      } else {
        // Wood: a surface, so it collapses.
        Welded w = weld_part(o, part.first, part.count);
        if (w.mesh.face_count() < 4) continue;
        UvLookup uv;
        uv.build(w, span);
        gpx::TriMesh m = w.mesh;
        const size_t target = std::max<size_t>((size_t)((float)m.face_count() * targets[level]), 4);
        gpx::MeshReduceResult r;
        // A part already small enough is kept whole rather than dropped: it
        // is still part of the tree at that distance.
        if (m.face_count() > target) gpx::mesh_reduce(m, target, r);
        if (m.face_count() == 0) continue;
        for (size_t f = 0; f < m.face_count(); ++f) {
          const uint32_t *fc = m.face(f);
          const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
          const float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
          const float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
          float nx = u[1] * v[2] - u[2] * v[1], ny = u[2] * v[0] - u[0] * v[2],
                nz = u[0] * v[1] - u[1] * v[0];
          const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
          if (len > 0.f) {
            nx /= len;
            ny /= len;
            nz /= len;
          } else {
            ny = 1.f;
          }
          for (const float *p : {p0, p1, p2}) {
            float t[2];
            uv.at(p, t);
            out.insert(out.end(), {p[0], p[1], p[2], nx, ny, nz, t[0], t[1]});
          }
        }
      }
      lp.count = (int)(out.size() / 8) - lp.first;
      if (lp.count > 0) {
        o.lod_parts[level].push_back(lp);
        any = true;
      }
    }
    o.lod_count[level] = (int)(out.size() / 8);
  }
  return any;
}

} // namespace studio
