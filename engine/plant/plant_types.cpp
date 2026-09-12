// Geekatplay TerraForge - the plant builders' small maths and the mesh sink
// (plant_internal.hpp).
//
// Why a file of its own: every builder rotates vectors, carries a frame
// along a curve, draws hashed numbers and appends vertices. Those must be
// the same arithmetic everywhere, or two builders disagree about which way
// "up" is and a leaf grows into its twig. So the vector rotation, the
// parallel-transported frame, the hash the draws come from and the vertex
// sink live here once.
//
// The hash is lowbias32 (the same mixing engine/gpx/noise_core.hpp uses),
// combined by feeding each further word through the mixer with a golden
// ratio stride, so (a, b) and (b, a) differ and a zero word still mixes.
// The gaussian is Box-Muller from two unit draws, so a "gaussian" spread
// costs two hashes and nothing else - no state, no rejection loop.
//
// The mesh sink keeps the four vertex streams in step and records the
// part runs. The wind weight's height (w) is written as the vertex's
// height in metres when the builder passes a negative value; plant_build
// divides every vertex's w by the finished plant's height, because no
// builder knows the final height while it works.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gpx {
namespace plant {

// ----------------------------------------------------------------- leaning
// Turn a frame's z toward `target` by `amount` of the angle between them.
//
// The degenerate case is the one that matters: when z already points the
// opposite way - a flower standing straight up asked to droop, a leaf held
// vertical asked to fall - the cross product is zero and there is no axis to
// turn about. Every direction perpendicular to z is then equally correct, so
// the frame's own x is used. Skipping the turn instead (which is what the
// builders each did before this was one function) made gravitropism and the
// dry droop do nothing on exactly the parts that needed them most.
void lean(Frame &f, V3 target, float amount) {
  target = normalize(target);
  const float ang = std::acos(clampf(dot(f.z, target), -1.f, 1.f)) * clampf(amount, 0.f, 1.f);
  if (ang < 1e-6f) return;
  const V3 axis = cross(f.z, target);
  f = f.rotated(length(axis) > 1e-6f ? normalize(axis) : f.x, ang);
}

// Orientation tropism: vertical leans the part toward the zenith (> 0) or the
// nadir (< 0), horizontal toward the nearest level direction.
void tropism_block(const ParamReader &pr, Frame &f) {
  const float v = pr.random("orient_vertical"), h = pr.random("orient_horizontal");
  if (v != 0.f) lean(f, V3(0.f, v > 0.f ? 1.f : -1.f, 0.f), std::fabs(v));
  if (h != 0.f) {
    V3 level(f.z.x, 0.f, f.z.z);
    if (length(level) < 1e-4f) level = V3(f.x.x, 0.f, f.x.z);
    lean(f, normalize(level, V3(1, 0, 0)), std::fabs(h));
  }
}

// ----------------------------------------------------------------- vectors

V3 rotate(V3 v, V3 k, float angle) {
  const float c = std::cos(angle), s = std::sin(angle);
  return v * c + cross(k, v) * s + k * (dot(k, v) * (1.f - c));
}

V3 perpendicular(V3 d) {
  const V3 a = std::fabs(d.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0);
  return normalize(cross(a, d), V3(1, 0, 0));
}

Frame Frame::along(V3 origin, V3 axis, const Frame *ref) {
  Frame f;
  f.o = origin;
  f.z = normalize(axis);
  V3 x;
  if (ref) {
    x = ref->x - f.z * dot(ref->x, f.z);
    if (dot(x, x) < 1e-10f) x = ref->y - f.z * dot(ref->y, f.z);
  }
  if (!ref || dot(x, x) < 1e-10f) x = perpendicular(f.z);
  f.x = normalize(x, perpendicular(f.z));
  f.y = normalize(cross(f.z, f.x), perpendicular(f.z));
  return f;
}

Frame Frame::rotated(V3 axis, float angle) const {
  Frame f = *this;
  const V3 k = normalize(axis);
  f.x = rotate(x, k, angle);
  f.y = rotate(y, k, angle);
  f.z = rotate(z, k, angle);
  return f;
}

// ------------------------------------------------------------------ hashing

uint32_t hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
uint32_t hash_u32(uint32_t a, uint32_t b) { return hash_u32(hash_u32(a) + b * 0x9E3779B1u + 0x7F4A7C15u); }
uint32_t hash_u32(uint32_t a, uint32_t b, uint32_t c) { return hash_u32(hash_u32(a, b), c); }
uint32_t hash_u32(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return hash_u32(hash_u32(a, b, c), d); }

uint32_t hash_str(const std::string &s) {
  uint32_t h = 2166136261u; // FNV-1a
  for (unsigned char ch : s) {
    h ^= ch;
    h *= 16777619u;
  }
  return h;
}

float Draw::gaussian() {
  const float u1 = std::max(unit(), 1e-7f), u2 = unit();
  return std::sqrt(-2.f * std::log(u1)) * std::cos(TAU * u2);
}

// ---------------------------------------------------------------- mesh sink

void MeshOut::begin(const Instance &inst, PlantPartKind kind, int material, bool double_sided,
                    const std::string &name) {
  if (part_index >= 0) end();
  PlantPart p;
  // The name the person gave the node wins; the builder's own word for what
  // it made ("cap", "blade") is kept as a suffix so a segment's body, its cap
  // and its blades stay apart in an exported file. Only when the node has no
  // name at all does the builder's word stand alone.
  p.name = name;
  if (const Node *src = ctx.graph ? ctx.graph->find_node(inst.node) : nullptr) {
    const std::string given = src->attrs.get_s("name");
    if (!given.empty()) p.name = given == name ? given : given + " " + name;
  }
  p.node = inst.node;
  p.material = material;
  p.first_index = (uint32_t)m.idx.size();
  p.index_count = 0;
  p.double_sided = double_sided;
  p.kind = kind;
  m.parts.push_back(p);
  part_index = (int)m.parts.size() - 1;
  part_first_vertex = m.pos.size() / 3;
  part_first_index = p.first_index;
}

uint32_t MeshOut::vertex(V3 p, V3 n, float u, float v, const float wind4[4], const float tint4[4]) {
  const uint32_t id = (uint32_t)(m.pos.size() / 3);
  m.pos.push_back(p.x); m.pos.push_back(p.y); m.pos.push_back(p.z);
  m.nrm.push_back(n.x); m.nrm.push_back(n.y); m.nrm.push_back(n.z);
  m.uv.push_back(u); m.uv.push_back(v);
  m.wind.push_back(wind4[0]);
  m.wind.push_back(wind4[1]);
  m.wind.push_back(wind4[2]);
  m.wind.push_back(wind4[3] < 0.f ? p.y : wind4[3]);
  m.tint.push_back(tint4[0]); m.tint.push_back(tint4[1]);
  m.tint.push_back(tint4[2]); m.tint.push_back(tint4[3]);
  return id;
}

void MeshOut::tri(uint32_t a, uint32_t b, uint32_t c) {
  if (a == b || b == c || a == c) return;
  m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
}

void MeshOut::quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  tri(a, b, c);
  tri(a, c, d);
}

void MeshOut::end() {
  if (part_index < 0) return;
  PlantPart &p = m.parts[(size_t)part_index];
  p.index_count = (uint32_t)m.idx.size() - p.first_index;
  if (p.index_count == 0) m.parts.pop_back();
  part_index = -1;
}

void MeshOut::geometric_normals() {
  if (part_index < 0) return;
  const size_t v0 = part_first_vertex, nv = m.pos.size() / 3 - v0;
  if (nv == 0) return;
  std::vector<float> acc(nv * 3, 0.f);
  for (size_t i = part_first_index; i + 2 < m.idx.size(); i += 3) {
    const uint32_t a = m.idx[i], b = m.idx[i + 1], c = m.idx[i + 2];
    if (a < v0 || b < v0 || c < v0) continue;
    const V3 pa(&m.pos[a * 3]), pb(&m.pos[b * 3]), pc(&m.pos[c * 3]);
    const V3 fn = cross(pb - pa, pc - pa); // area-weighted
    for (uint32_t k : {a, b, c}) {
      float *o = &acc[(k - v0) * 3];
      o[0] += fn.x; o[1] += fn.y; o[2] += fn.z;
    }
  }
  for (size_t i = 0; i < nv; ++i) {
    const V3 n = normalize(V3(&acc[i * 3]), V3(&m.nrm[(v0 + i) * 3]));
    n.to(&m.nrm[(v0 + i) * 3]);
  }
}

// ------------------------------------------------------------ subdivision

int subdiv(const BuildCtx &ctx, float number, int mn, int lod_shift) {
  const float f = number * std::pow(2.f, ctx.detail + (float)lod_shift - (float)ctx.lod);
  const int n = (int)std::lround(f);
  return std::max(mn, n);
}

} // namespace plant
} // namespace gpx
