// Geekatplay TerraForge - building one rock. See gpx/rock.hpp for why it is
// built out of fracture, abrasion and weathering rather than sculpted.
//
// The order matters and is the order the processes happened in:
//
//   1. a sphere, subdivided from an icosahedron so the triangles are even
//   2. the axis scales - bedding squashes it, a column stretches it
//   3. FRACTURE: cut with planes. A vertex outside a plane is pushed onto it,
//      which is exactly what a break does to a lump of rock
//   4. ABRASION: corners retreat. The measure of a corner is how far the
//      vertex stands out from the average of its neighbours, so this wears
//      the edges and leaves the faces, which is what a river does
//   5. WEATHERING: three scales of noise and, for the pitted kinds, discrete
//      hollows
//   6. normals, re-derived from the geometry, and split where the surface
//      creases - a fracture face has an edge, and smoothing across it is the
//      single thing that makes a procedural rock look like a potato
#include "gpx/rock.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace gpx {

namespace {

// Wellons' lowbias32, as the terrain relief and the cloud volumes use.
inline uint32_t rk_mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
// A number in 0..1 from a seed and a counter.
inline float rk_unit(uint32_t seed, uint32_t i) {
  return (float)rk_mix(seed ^ (i * 0x9E3779B9u)) * (1.f / 4294967296.f);
}
inline float rk_range(uint32_t seed, uint32_t i, float lo, float hi) {
  return lo + (hi - lo) * rk_unit(seed, i);
}

struct V3 {
  float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) {
  const float l = len(a);
  return l > 1e-20f ? a * (1.f / l) : V3{0, 1, 0};
}

// A direction spread evenly over the sphere, from two units. The z is uniform
// and the angle is free, which is the only way to get an even spread - taking
// two angles uniformly crowds the poles.
V3 rk_dir(uint32_t seed, uint32_t i) {
  const float z = rk_range(seed, i * 2u, -1.f, 1.f);
  const float a = rk_unit(seed, i * 2u + 1u) * 6.28318530718f;
  const float r = std::sqrt(std::max(0.f, 1.f - z * z));
  return {r * std::cos(a), z, r * std::sin(a)};
}

// ---------------------------------------------------------------- the sphere
// An icosahedron, subdivided `level` times, every vertex on the unit sphere.
// Even triangles matter here: a UV sphere's poles would crowd the fracture
// cuts and the abrasion pass would wear them differently from the equator.
void icosphere(int level, std::vector<V3> &pos, std::vector<uint32_t> &idx) {
  const float t = (1.f + std::sqrt(5.f)) * 0.5f;
  pos = {{-1, t, 0}, {1, t, 0},  {-1, -t, 0}, {1, -t, 0},
         {0, -1, t}, {0, 1, t},  {0, -1, -t}, {0, 1, -t},
         {t, 0, -1}, {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
  for (V3 &v : pos) v = norm(v);
  idx = {0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
         1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
         3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
         4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1};
  for (int l = 0; l < level; ++l) {
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) {
      const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      pos.push_back(norm(pos[a] + pos[b]));
      const uint32_t n = (uint32_t)pos.size() - 1;
      mid.emplace(key, n);
      return n;
    };
    std::vector<uint32_t> out;
    out.reserve(idx.size() * 4);
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
      const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
      const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
      out.insert(out.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
    }
    idx.swap(out);
  }
}

// ------------------------------------------------------------------- noise
// The same construction the terrain relief uses, and for the same reason:
// bands that are only scaled and never turned leave a visible grid, and on a
// rock that grid reads as a woven texture rather than stone.
float rk_vnoise(V3 p, uint32_t seed) {
  const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
  float tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
  tx = tx * tx * (3.f - 2.f * tx);
  ty = ty * ty * (3.f - 2.f * ty);
  tz = tz * tz * (3.f - 2.f * tz);
  auto h = [&](int dx, int dy, int dz) {
    const uint32_t hx = (uint32_t)(int32_t)(fx + dx);
    const uint32_t hy = (uint32_t)(int32_t)(fy + dy);
    const uint32_t hz = (uint32_t)(int32_t)(fz + dz);
    return (float)rk_mix(seed ^ rk_mix(hx * 0x9E3779B9u ^ rk_mix(hy * 0x85EBCA77u) ^
                                       rk_mix(hz * 0xC2B2AE3Du))) *
           (1.f / 4294967296.f);
  };
  const float c00 = h(0,0,0) + (h(1,0,0) - h(0,0,0)) * tx;
  const float c10 = h(0,1,0) + (h(1,1,0) - h(0,1,0)) * tx;
  const float c01 = h(0,0,1) + (h(1,0,1) - h(0,0,1)) * tx;
  const float c11 = h(0,1,1) + (h(1,1,1) - h(0,1,1)) * tx;
  const float c0 = c00 + (c10 - c00) * ty;
  const float c1 = c01 + (c11 - c01) * ty;
  return c0 + (c1 - c0) * tz;
}

// Three turned bands, 0..1 about a half.
float rk_grain(V3 p, float freq, uint32_t seed) {
  // a fixed turn per band, written out - the same discipline as the relief's
  const V3 a{p.x * 0.80f - p.z * 0.60f, p.y, p.x * 0.60f + p.z * 0.80f};
  const V3 b{a.x, a.y * 0.28f - a.z * 0.96f, a.y * 0.96f + a.z * 0.28f};
  float s = rk_vnoise(p * freq, seed) * 0.55f;
  s += rk_vnoise(a * (freq * 2.07f), seed ^ 0x51u) * 0.30f;
  s += rk_vnoise(b * (freq * 4.31f), seed ^ 0xA3u) * 0.15f;
  return s;
}

} // namespace

void rock_build(const RockParams &p, RockMesh &out) {
  out = RockMesh();
  RockRecipe r = rock_recipe(p.type);
  // the dials, each multiplying what the type asks for
  const float k_fr = std::max(p.fracture, 0.f);
  const float k_rd = std::clamp(p.roundness, 0.f, 3.f);
  const float k_wx = std::max(p.weathering, 0.f);
  r.cuts = std::clamp((int)std::lround(r.cuts * std::min(k_fr, 2.f)), 0, 24);
  r.cut_flat = std::clamp(r.cut_flat * std::min(k_fr, 1.4f), 0.f, 1.f);
  r.round = std::clamp(r.round * k_rd, 0.f, 1.f);
  r.bump *= k_wx;
  r.pit *= k_wx;
  r.flat = std::clamp(r.flat * std::max(p.flatness, 0.05f), 0.02f, 4.f);
  r.elong = std::clamp(r.elong * std::max(p.elongation, 0.05f), 0.05f, 8.f);
  const int level = std::clamp(p.detail > 0 ? p.detail : r.detail, 0, 5);
  const uint32_t seed = p.seed ? p.seed : 1u;

  std::vector<V3> v;
  std::vector<uint32_t> idx;
  icosphere(level, v, idx);

  // ---- 2. fracture ---------------------------------------------------------
  // Before the axis scales, so a cut set 0.7 of the way out means that on
  // every type. After them, a cut on a shape already squashed to a sixth of
  // its width never reached the surface at all, and the slab came out a
  // smooth lens instead of a broken flag.
  // Each cut is a plane at a random heading, set a little way in from the
  // surface. A vertex beyond it is moved back onto it, so the face is as flat
  // as `cut_flat` says and the rest of the stone is untouched.
  for (int c = 0; c < r.cuts; ++c) {
    const V3 n = rk_dir(seed ^ 0x1234u, (uint32_t)c + 1u);
    // how deep the cut bites: shallow cuts leave a face, deep ones take a
    // corner off. Never past the middle, or the rock loses its volume.
    const float d = rk_range(seed ^ 0x5678u, (uint32_t)c + 1u, 0.55f, 0.95f);
    for (V3 &q : v) {
      const float s = dot(q, n);
      if (s <= d) continue;
      const V3 onto = q - n * (s - d);
      q = onto + (q - onto) * (1.f - r.cut_flat);
    }
  }

  // Bedding: two parallel faces, the rock parted along the layers it was laid
  // down in. This is what makes a flagstone a flagstone - two flat faces and
  // broken edges between them. Squashing the whole shape instead gives a
  // smooth lens, which is what the slab was before this.
  if (r.bed > 0.f) {
    // the bedding is near horizontal but not dead level: strata are tilted
    const V3 n = norm(V3{rk_range(seed ^ 0xBEDu, 1u, -0.18f, 0.18f), 1.f,
                         rk_range(seed ^ 0xBEDu, 2u, -0.18f, 0.18f)});
    const float half = std::max(r.bed, 0.01f);
    for (V3 &q : v) {
      const float sgn = dot(q, n);
      if (sgn > half) q = q - n * (sgn - half);
      else if (sgn < -half) q = q - n * (sgn + half);
    }
  }

  // ---- 3. the axis scales, and a column's sides -----------------------------
  // A column is not a scaled sphere: it has a fixed number of flat sides, so
  // it is made by pulling every vertex in to the prism that many sides define.
  if (r.sides >= 3) {
    const float turn = rk_unit(seed, 991u) * 6.28318530718f;
    for (V3 &q : v) {
      const float a = std::atan2(q.z, q.x) - turn;
      const float step = 6.28318530718f / (float)r.sides;
      // the distance to the flat face at this angle, for a prism of radius 1
      const float wedge = a - step * std::floor(a / step + 0.5f);
      const float face = std::cos(step * 0.5f) / std::max(std::cos(wedge), 1e-3f);
      const float rad = std::sqrt(q.x * q.x + q.z * q.z);
      if (rad > 1e-6f) {
        const float want = std::min(rad, face * 0.98f);
        q.x *= want / rad;
        q.z *= want / rad;
      }
    }
  }
  for (V3 &q : v) {
    q.y *= r.flat;
    q.x *= r.elong;
  }

  // ---- 4. abrasion ---------------------------------------------------------
  // A corner is a vertex that stands out from its neighbours. Worn stone is
  // worn THERE, not everywhere - which is why a river cobble keeps faint
  // facets while its edges are gone. One pass of neighbour averaging weighted
  // by how much each vertex protrudes.
  if (r.round > 0.f) {
    std::vector<V3> sum(v.size(), V3{0, 0, 0});
    std::vector<int> cnt(v.size(), 0);
    for (size_t i = 0; i + 2 < idx.size(); i += 3)
      for (int k = 0; k < 3; ++k) {
        const uint32_t a = idx[i + k], b = idx[i + (k + 1) % 3];
        sum[a] = sum[a] + v[b];
        ++cnt[a];
        sum[b] = sum[b] + v[a];
        ++cnt[b];
      }
    // two passes: rounding is cumulative, and one pass cannot take a corner
    // back far enough to read as tumbled
    const int passes = r.round > 0.7f ? 3 : (r.round > 0.35f ? 2 : 1);
    for (int pass = 0; pass < passes; ++pass) {
      std::vector<V3> next = v;
      for (size_t i = 0; i < v.size(); ++i) {
        if (cnt[i] == 0) continue;
        const V3 avg = sum[i] * (1.f / (float)cnt[i]);
        // how far it stands out, as a share of the radius: that is the
        // measure of "corner"
        const float out_by = std::max(len(v[i]) - len(avg), 0.f);
        const float w = std::clamp(out_by * 3.f, 0.f, 1.f) * r.round;
        next[i] = v[i] + (avg - v[i]) * w;
      }
      v.swap(next);
      if (pass + 1 < passes) {
        std::fill(sum.begin(), sum.end(), V3{0, 0, 0});
        std::fill(cnt.begin(), cnt.end(), 0);
        for (size_t i = 0; i + 2 < idx.size(); i += 3)
          for (int k = 0; k < 3; ++k) {
            const uint32_t a = idx[i + k], b = idx[i + (k + 1) % 3];
            sum[a] = sum[a] + v[b]; ++cnt[a];
            sum[b] = sum[b] + v[a]; ++cnt[b];
          }
      }
    }
  }

  // ---- 5. weathering -------------------------------------------------------
  if (r.bump > 0.f)
    for (V3 &q : v) {
      const V3 d = norm(q);
      const float g = rk_grain(q, r.bump_freq, seed ^ 0xBEEFu) - 0.5f;
      q = q + d * (g * 2.f * r.bump);
    }
  // Pits: discrete hollows, not noise. A vesicle has an edge and a floor.
  if (r.pit > 0.f && r.pit_count > 0) {
    struct Pit { V3 c; float rad; };
    std::vector<Pit> pits;
    pits.reserve((size_t)r.pit_count);
    for (int i = 0; i < r.pit_count; ++i)
      pits.push_back({rk_dir(seed ^ 0x9A11u, (uint32_t)i + 1u),
                      rk_range(seed ^ 0x77u, (uint32_t)i + 1u, 0.45f, 1.f)});
    for (V3 &q : v) {
      const V3 d = norm(q);
      float deepest = 0.f;
      for (const Pit &pit : pits) {
        // how far this point is from the pit's centre, over the sphere
        const float c = dot(d, pit.c);
        if (c <= 0.f) continue;
        const float ang = std::acos(std::min(c, 1.f));
        const float w = pit.rad * 0.32f;
        if (ang >= w) continue;
        const float t = 1.f - ang / w;
        deepest = std::max(deepest, t * t * (3.f - 2.f * t));
      }
      if (deepest > 0.f) q = q - d * (deepest * r.pit);
    }
  }

  // ---- flat bottom ---------------------------------------------------------
  // A slab lies on the ground and an outcrop comes out of it: both have an
  // underside nobody sees and neither is rounded there.
  if (r.flat_bottom) {
    float lo = 1e30f;
    for (const V3 &q : v) lo = std::min(lo, q.y);
    const float cut = lo + (0.f - lo) * 0.35f; // a third of the way up
    for (V3 &q : v)
      if (q.y < cut) q.y = cut;
  }
  // A column broke off something at both ends. Without this the sphere's
  // poles survive the prism (their radius is small, so the faces never reach
  // them) and it reads as a rounded pillar rather than a snapped column.
  if (r.sides >= 3) {
    float lo = 1e30f, hi = -1e30f;
    for (const V3 &q : v) { lo = std::min(lo, q.y); hi = std::max(hi, q.y); }
    const float span = std::max(hi - lo, 1e-6f);
    const float top = hi - span * 0.10f, bot = lo + span * 0.10f;
    for (V3 &q : v) {
      if (q.y > top) q.y = top;
      if (q.y < bot) q.y = bot;
    }
  }

  // ---- 6. out --------------------------------------------------------------
  // Normals from the faces, and the surface split where it creases: a
  // fracture face has an edge, and a rock smoothed across its edges is a
  // potato. The threshold is 40 degrees - shallower than that is the same
  // face, steeper is a break.
  out.pos.reserve(idx.size() * 3);
  out.nrm.reserve(idx.size() * 3);
  out.uv.reserve(idx.size() * 2);
  out.idx.reserve(idx.size());
  // face normals first
  const size_t faces = idx.size() / 3;
  std::vector<V3> fn(faces);
  for (size_t f = 0; f < faces; ++f)
    fn[f] = norm(cross(v[idx[f * 3 + 1]] - v[idx[f * 3]], v[idx[f * 3 + 2]] - v[idx[f * 3]]));
  // each vertex of each face takes the average of the faces around it that
  // are within the crease angle
  std::unordered_map<uint32_t, std::vector<uint32_t>> around;
  around.reserve(v.size() * 2);
  for (size_t f = 0; f < faces; ++f)
    for (int k = 0; k < 3; ++k) around[idx[f * 3 + k]].push_back((uint32_t)f);
  const float crease = std::cos(40.f * 0.01745329251994f);
  for (size_t f = 0; f < faces; ++f)
    for (int k = 0; k < 3; ++k) {
      const uint32_t vi = idx[f * 3 + k];
      V3 n = fn[f];
      for (uint32_t g : around[vi])
        if (g != f && dot(fn[g], fn[f]) > crease) n = n + fn[g];
      n = norm(n);
      const V3 q = v[vi];
      out.pos.insert(out.pos.end(), {q.x, q.y, q.z});
      out.nrm.insert(out.nrm.end(), {n.x, n.y, n.z});
      // a spherical wrap: a rock has no seam anybody looks for, and this
      // keeps a texture the same size all over it
      const V3 d = norm(q);
      out.uv.insert(out.uv.end(), {std::atan2(d.z, d.x) * 0.15915494f + 0.5f,
                                   std::asin(std::clamp(d.y, -1.f, 1.f)) * 0.31830989f + 0.5f});
      out.idx.push_back((uint32_t)(out.pos.size() / 3 - 1));
    }

  // ---- size ---------------------------------------------------------------
  for (int k = 0; k < 3; ++k) { out.bmin[k] = 1e30f; out.bmax[k] = -1e30f; }
  for (size_t i = 0; i + 2 < out.pos.size(); i += 3)
    for (int k = 0; k < 3; ++k) {
      out.bmin[k] = std::min(out.bmin[k], out.pos[i + k]);
      out.bmax[k] = std::max(out.bmax[k], out.pos[i + k]);
    }
  const float span = std::max(std::max(out.bmax[0] - out.bmin[0], out.bmax[1] - out.bmin[1]),
                              out.bmax[2] - out.bmin[2]);
  const float want = std::max(p.size_m, 1e-4f);
  const float s = span > 1e-9f ? want / span : 1.f;
  // scaled to the asked-for longest axis, and sat on y = 0 so a rock placed
  // at a point on the ground is ON the ground rather than half in it
  for (size_t i = 0; i + 2 < out.pos.size(); i += 3) {
    out.pos[i] = (out.pos[i] - (out.bmin[0] + out.bmax[0]) * 0.5f) * s;
    out.pos[i + 1] = (out.pos[i + 1] - out.bmin[1]) * s;
    out.pos[i + 2] = (out.pos[i + 2] - (out.bmin[2] + out.bmax[2]) * 0.5f) * s;
  }
  for (int k = 0; k < 3; ++k) { out.bmin[k] = 1e30f; out.bmax[k] = -1e30f; }
  for (size_t i = 0; i + 2 < out.pos.size(); i += 3)
    for (int k = 0; k < 3; ++k) {
      out.bmin[k] = std::min(out.bmin[k], out.pos[i + k]);
      out.bmax[k] = std::max(out.bmax[k], out.pos[i + k]);
    }
  out.height_m = out.bmax[1] - out.bmin[1];
}

} // namespace gpx
