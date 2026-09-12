// Geekatplay TerraForge — plants and rocks, built rather than loaded.
//
// A landscape is mostly what grows on it, and the studio had nothing to grow:
// every tree had to be found as a model, licensed, imported and scaled. These
// are built from their kind the way the cube is (scene_primitives.cpp), so a
// scene that uses them needs no file on disk, they come the right size for
// the world, and a population of them is as cheap as any other mesh. Each is
// a small triangle soup in the unit box primitives share - standing on y 0,
// a unit tall - split into a bark part and a foliage part, each with its own
// colour, so the object's material and tint still apply to the whole.
//
// The shapes follow what makes each plant read at a distance rather than its
// botany: a conifer is tiers of drooping, ragged skirts; a juniper or pinyon
// is a short twisted trunk under a lumpy dark crown; a palm is a leaning
// ringed trunk under arching fronds of paired leaflets; a fern is a rosette
// of arching fronds; a grass tuft is blades bending out from one root; a bush
// is a cluster of lumps; a boulder a fractured, flattened stone. Sheets of
// leaf are one-sided - the mesh shader lights whichever side is seen.
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace studio {

namespace {

constexpr float TAU = 6.2831853f;

struct V3 {
  float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) {
  const float l = std::sqrt(dot(a, a));
  return l > 1e-12f ? a * (1.f / l) : V3{0, 1, 0};
}

// a deterministic stream of numbers per plant, the same on every run
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed * 747796405u + 2891336453u) {}
  float next() {
    s = s * 1664525u + 1013904223u;
    uint32_t x = s ^ (s >> 15);
    x *= 0x2c1b3c6du;
    x ^= x >> 12;
    return float(x & 0xffffff) / float(0x1000000);
  }
  float range(float a, float b) { return a + (b - a) * next(); }
};

// smooth lattice noise on a direction, for lumps and fractures
float hash3(int x, int y, int z, uint32_t seed) {
  uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(z) * 2147483647u + seed * 144665u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return float((h ^ (h >> 16)) & 0xffffff) / float(0xffffff);
}
float vnoise(V3 p, uint32_t seed) {
  const int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y), iz = (int)std::floor(p.z);
  float fx = p.x - ix, fy = p.y - iy, fz = p.z - iz;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  fz = fz * fz * (3 - 2 * fz);
  auto L = [](float a, float b, float t) { return a + (b - a) * t; };
  return L(L(L(hash3(ix, iy, iz, seed), hash3(ix + 1, iy, iz, seed), fx),
             L(hash3(ix, iy + 1, iz, seed), hash3(ix + 1, iy + 1, iz, seed), fx), fy),
           L(L(hash3(ix, iy, iz + 1, seed), hash3(ix + 1, iy, iz + 1, seed), fx),
             L(hash3(ix, iy + 1, iz + 1, seed), hash3(ix + 1, iy + 1, iz + 1, seed), fx), fy),
           fz);
}
float fbm(V3 p, uint32_t seed, int oct) {
  float s = 0, a = 0.5f, n = 0;
  for (int i = 0; i < oct; ++i) {
    s += vnoise(p, seed + uint32_t(i) * 101u) * a;
    n += a;
    a *= 0.5f;
    p = p * 2.03f;
  }
  return s / n;
}

// The mesh being built: pos + normal per vertex, a uv (unused, but a part
// needs one per vertex), and the runs that are bark and foliage.
struct Build {
  std::vector<float> v, uv;
  void tri(V3 a, V3 b, V3 c, V3 na, V3 nb, V3 nc) {
    for (auto [p, n] : {std::pair{a, na}, std::pair{b, nb}, std::pair{c, nc}}) {
      v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
      uv.insert(uv.end(), {0.f, 0.f});
    }
  }
  void flat(V3 a, V3 b, V3 c) {
    const V3 n = norm(cross(b - a, c - a));
    tri(a, b, c, n, n, n);
  }
  int count() const { return (int)(v.size() / 6); }
};

// A tube along a path, `sides` round, its radius per path point: a trunk,
// a branch. Normals from the ring, so it shades round.
void tube(Build &b, const std::vector<V3> &path, const std::vector<float> &radius, int sides) {
  const int n = (int)path.size();
  if (n < 2) return;
  std::vector<V3> ring_prev, nrm_prev;
  V3 ref{1, 0, 0};
  for (int i = 0; i < n; ++i) {
    const V3 t = norm(i + 1 < n ? path[(size_t)i + 1] - path[(size_t)i] : path[(size_t)i] - path[(size_t)i - 1]);
    V3 u = cross(t, ref);
    if (dot(u, u) < 1e-6f) u = cross(t, V3{0, 0, 1});
    u = norm(u);
    const V3 w = cross(t, u);
    ref = w * -1.f; // carry the frame along so the ring does not twist
    std::vector<V3> ring, nrm;
    for (int s = 0; s <= sides; ++s) {
      const float a = TAU * s / sides;
      const V3 d = u * std::cos(a) + w * std::sin(a);
      ring.push_back(path[(size_t)i] + d * radius[(size_t)i]);
      nrm.push_back(d);
    }
    if (i > 0)
      for (int s = 0; s < sides; ++s) {
        b.tri(ring_prev[(size_t)s], ring[(size_t)s], ring[(size_t)s + 1], nrm_prev[(size_t)s], nrm[(size_t)s], nrm[(size_t)s + 1]);
        b.tri(ring_prev[(size_t)s], ring[(size_t)s + 1], ring_prev[(size_t)s + 1], nrm_prev[(size_t)s], nrm[(size_t)s + 1], nrm_prev[(size_t)s + 1]);
      }
    ring_prev = ring;
    nrm_prev = nrm;
  }
}

// A lump of foliage or a stone: a sphere pushed about by noise. `flatten`
// squashes its underside toward `floor_y`.
void lump(Build &b, V3 c, V3 r, int seg, uint32_t seed, float rough, float floor_y = -1e9f) {
  const int rings = std::max(4, seg / 2);
  auto at = [&](int i, int j, V3 &p, V3 &n) {
    const float th = 3.14159265f * i / rings, ph = TAU * j / seg;
    const V3 d{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
    const float k = 1.f + (fbm(d * 2.2f + c * 7.f, seed, 3) - 0.5f) * rough;
    p = c + V3{d.x * r.x * k, d.y * r.y * k, d.z * r.z * k};
    if (p.y < floor_y) p.y = floor_y + (p.y - floor_y) * 0.15f;
    n = norm(V3{d.x / r.x, d.y / r.y, d.z / r.z});
  };
  for (int i = 0; i < rings; ++i)
    for (int j = 0; j < seg; ++j) {
      V3 p00, p10, p01, p11, n00, n10, n01, n11;
      at(i, j, p00, n00);
      at(i + 1, j, p10, n10);
      at(i, j + 1, p01, n01);
      at(i + 1, j + 1, p11, n11);
      b.tri(p00, p10, p11, n00, n10, n11);
      b.tri(p00, p11, p01, n00, n11, n01);
    }
}

// A frond: a rib that leaves `base` along `dir`, arches up and droops under
// its own weight, with paired leaflets along it that shorten toward the tip.
void frond(Build &b, V3 base, V3 dir, float len, float lift, float droop, float leaf_len,
           float leaf_w, int steps, Rng &rng) {
  const V3 side = norm(cross(V3{0, 1, 0}, dir));
  std::vector<V3> rib;
  for (int i = 0; i <= steps; ++i) {
    const float t = float(i) / steps;
    rib.push_back(base + dir * (len * t) + V3{0, 1, 0} * (len * (lift * t - droop * t * t)));
  }
  for (int i = 1; i < steps; ++i) {
    const float t = float(i) / steps;
    const V3 p = rib[(size_t)i];
    const V3 along = norm(rib[(size_t)i + 1] - rib[(size_t)i - 1]);
    const float l = leaf_len * (0.35f + 0.65f * std::sin(3.14159265f * std::min(t * 1.15f, 1.f))) * rng.range(0.85f, 1.15f);
    for (int s : {-1, 1}) {
      // each leaflet sweeps forward along the rib and hangs a little more
      // toward the tip
      const V3 out = norm(side * float(s) * 0.9f + along * 0.35f + V3{0, -0.12f - 0.3f * t, 0});
      const V3 tip = p + out * l;
      const V3 w = along * (leaf_w * 0.5f);
      const V3 mid = p + out * (l * 0.45f);
      b.flat(p - w, mid + w * 0.6f, tip);
      b.flat(p - w, tip, mid - w * 0.6f);
    }
  }
  // the rib itself, a thin strip so it reads at an angle
  for (int i = 0; i < steps; ++i) {
    const V3 w = side * (leaf_w * 0.12f);
    b.flat(rib[(size_t)i] - w, rib[(size_t)i + 1] - w, rib[(size_t)i + 1] + w);
    b.flat(rib[(size_t)i] - w, rib[(size_t)i + 1] + w, rib[(size_t)i] + w);
  }
}

void add_part(SceneObject &o, int first, int count, float r, float g, float bl, const char *name) {
  if (count <= 0) return;
  SceneObject::Part p;
  p.first = first;
  p.count = count;
  p.color[0] = r;
  p.color[1] = g;
  p.color[2] = bl;
  p.name = name;
  o.parts.push_back(std::move(p));
}

} // namespace

// "pine" or "pine#12": the kind, and the seed another tree of it grows from.
// The seed rides in the pseudo-path ("primitive:pine#12"), so a saved scene
// rebuilds the same tree and a seed of 0 is the tree as it always was.
std::string scene_plant_kind(const std::string &spec, uint32_t *seed) {
  const size_t h = spec.find('#');
  if (seed) *seed = h == std::string::npos ? 0u : (uint32_t)std::strtoul(spec.c_str() + h + 1, nullptr, 10);
  return h == std::string::npos ? spec : spec.substr(0, h);
}

bool scene_is_plant_kind(const std::string &spec) {
  const std::string kind = scene_plant_kind(spec, nullptr);
  return kind == "pine" || kind == "juniper" || kind == "palm" || kind == "fern" ||
         kind == "grass" || kind == "bush" || kind == "boulder";
}

// How big each one is when it is added, in metres: the unit box's size.
float scene_plant_size_m(const std::string &spec) {
  const std::string kind = scene_plant_kind(spec, nullptr);
  if (kind == "pine") return 22.f;
  if (kind == "juniper") return 6.f;
  if (kind == "palm") return 16.f;
  if (kind == "fern") return 1.6f;
  if (kind == "grass") return 0.8f;
  if (kind == "bush") return 2.5f;
  if (kind == "boulder") return 3.f;
  return 400.f;
}

bool scene_plant_build(const std::string &spec, int detail, SceneObject &o) {
  if (!scene_is_plant_kind(spec)) return false;
  uint32_t seed = 0;
  const std::string kind = scene_plant_kind(spec, &seed);
  const float q = std::clamp(detail / 24.f, 0.5f, 4.f); // how finely
  const int sides = std::max(5, (int)(8 * q));
  Build bark, leaf;
  Rng rng(uint32_t(kind.size() * 131u + (unsigned char)kind[0] * 7u + (unsigned char)kind[1]) +
          seed * 2654435761u);
  float bark_col[3] = {0.28f, 0.20f, 0.14f}, leaf_col[3] = {0.16f, 0.27f, 0.10f};

  if (kind == "pine") {
    std::vector<V3> path;
    std::vector<float> rad;
    for (int i = 0; i <= 8; ++i) {
      const float t = i / 8.f;
      path.push_back({0.01f * std::sin(t * 5.f), t * 0.97f, 0.008f * std::cos(t * 4.f)});
      rad.push_back(0.032f * (1.f - t) + 0.003f);
    }
    tube(bark, path, rad, sides);
    // Many thin skirts rather than a few thick cones, each branch tip its
    // own spike reaching out and hanging, the lower ones wider and longer:
    // a stack of even cones is what a toy tree looks like.
    const int tiers = (int)(18 * q);
    for (int k = 0; k < tiers; ++k) {
      const float t = float(k) / tiers;
      const float y = 0.14f + 0.83f * t + rng.range(-0.012f, 0.012f);
      const float R = (0.36f * std::pow(1.f - t, 0.9f) + 0.03f) * rng.range(0.85f, 1.08f);
      const float rise = 0.07f * (1.f - t) + 0.03f;
      const int m = std::max(10, (int)(22 * q));
      const float spin = rng.next() * TAU;
      V3 apex{0.f, y + rise, 0.f};
      std::vector<V3> rim(m + 1), nr(m + 1);
      for (int j = 0; j <= m; ++j) {
        const float a = spin + TAU * j / m;
        // ragged: branch tips reach out further than the gaps between them
        const bool tip = j % 2 == 0;
        const float rr = R * (tip ? rng.range(0.9f, 1.15f) : rng.range(0.45f, 0.65f));
        const float hang = R * (tip ? rng.range(0.3f, 0.5f) : rng.range(0.12f, 0.2f));
        rim[(size_t)j] = {std::cos(a) * rr, y - hang, std::sin(a) * rr};
        nr[(size_t)j] = norm(V3{std::cos(a), 0.9f, std::sin(a)});
      }
      rim[(size_t)m] = rim[0];
      nr[(size_t)m] = nr[0];
      const V3 up{0, 1, 0};
      for (int j = 0; j < m; ++j) {
        // rounded shading: the normal leans out from the trunk and up, so the
        // skirt reads as a mass of needles rather than a facet
        leaf.tri(apex, rim[(size_t)j + 1], rim[(size_t)j], up, nr[(size_t)j + 1], nr[(size_t)j]);
        const V3 c{0, y - R * 0.08f, 0};
        const V3 in0 = rim[(size_t)j] * 0.5f + c * 0.5f, in1 = rim[(size_t)j + 1] * 0.5f + c * 0.5f;
        leaf.tri(rim[(size_t)j], rim[(size_t)j + 1], in1, nr[(size_t)j], nr[(size_t)j + 1], nr[(size_t)j + 1] * -1.f);
        leaf.tri(rim[(size_t)j], in1, in0, nr[(size_t)j], nr[(size_t)j + 1] * -1.f, nr[(size_t)j] * -1.f);
      }
    }
    leaf_col[0] = 0.075f; leaf_col[1] = 0.15f; leaf_col[2] = 0.085f; // the blue-black green of spruce
  } else if (kind == "juniper") {
    // a short trunk that twists as it climbs, forking into two or three limbs
    std::vector<V3> path;
    std::vector<float> rad;
    for (int i = 0; i <= 6; ++i) {
      const float t = i / 6.f;
      path.push_back({0.06f * std::sin(t * 3.1f), t * 0.38f, 0.04f * std::sin(t * 2.3f + 1.f)});
      rad.push_back(0.055f * (1.f - t * 0.55f));
    }
    tube(bark, path, rad, sides);
    const int limbs = 3;
    for (int l = 0; l < limbs; ++l) {
      const float a = TAU * l / limbs + rng.next();
      std::vector<V3> lp;
      std::vector<float> lr;
      const V3 s = path.back();
      for (int i = 0; i <= 4; ++i) {
        const float t = i / 4.f;
        lp.push_back(s + V3{std::cos(a) * 0.2f * t, 0.18f * t + 0.05f * t * t, std::sin(a) * 0.2f * t});
        lr.push_back(0.03f * (1.f - t * 0.7f));
      }
      tube(bark, lp, lr, std::max(4, sides - 2));
    }
    // the crown: dark lumps, wider than tall, over and around the limbs
    const int lumps = (int)(9 * std::min(q, 2.f));
    for (int k = 0; k < lumps; ++k) {
      const float a = rng.next() * TAU, d = rng.range(0.0f, 0.28f);
      V3 c{std::cos(a) * d, rng.range(0.5f, 0.78f), std::sin(a) * d};
      const float r = rng.range(0.13f, 0.22f);
      lump(leaf, c, {r * 1.15f, r * 0.8f, r * 1.1f}, std::max(8, (int)(12 * q)), 11u + k + seed * 977u, 0.55f, 0.28f);
    }
    bark_col[0] = 0.33f; bark_col[1] = 0.27f; bark_col[2] = 0.21f;
    leaf_col[0] = 0.17f; leaf_col[1] = 0.23f; leaf_col[2] = 0.13f;
  } else if (kind == "palm") {
    // a trunk leaning and curving up, ringed where the old fronds fell
    std::vector<V3> path;
    std::vector<float> rad;
    const int segs = (int)(14 * q);
    for (int i = 0; i <= segs; ++i) {
      const float t = float(i) / segs;
      path.push_back({0.16f * t * t, t * 0.82f, 0.04f * t});
      const float ring = 1.f + 0.08f * std::pow(std::fabs(std::sin(t * 60.f)), 8.f);
      rad.push_back((0.034f * (1.f - t) + 0.022f * t) * ring);
    }
    tube(bark, path, rad, sides);
    const V3 top = path.back();
    const int fronds = 12;
    for (int f = 0; f < fronds; ++f) {
      const float a = TAU * f / fronds + rng.range(-0.15f, 0.15f);
      const V3 dir = norm(V3{std::cos(a), 0.f, std::sin(a)});
      // young fronds stand up, old ones arch out and down past level
      const float up = rng.range(0.35f, 1.3f);
      frond(leaf, top, dir, rng.range(0.36f, 0.46f), up, up + rng.range(0.2f, 0.7f),
            0.13f, 0.02f, std::max(8, (int)(12 * q)), rng);
    }
    bark_col[0] = 0.36f; bark_col[1] = 0.30f; bark_col[2] = 0.22f;
    leaf_col[0] = 0.18f; leaf_col[1] = 0.33f; leaf_col[2] = 0.11f;
  } else if (kind == "fern") {
    const int fronds = 13;
    for (int f = 0; f < fronds; ++f) {
      const float a = TAU * f / fronds + rng.range(-0.2f, 0.2f);
      const V3 dir = norm(V3{std::cos(a), 0.f, std::sin(a)});
      const float up = rng.range(1.2f, 1.9f);
      frond(leaf, V3{0.f, 0.02f, 0.f}, dir, rng.range(0.36f, 0.48f), up, up * rng.range(0.9f, 1.2f),
            0.075f, 0.018f, std::max(8, (int)(11 * q)), rng);
    }
    leaf_col[0] = 0.15f; leaf_col[1] = 0.34f; leaf_col[2] = 0.09f;
  } else if (kind == "grass") {
    const int blades = (int)(48 * q);
    for (int k = 0; k < blades; ++k) {
      const float a = rng.next() * TAU, d = rng.range(0.f, 0.09f);
      const V3 root{std::cos(a) * d, 0.f, std::sin(a) * d};
      const float h = rng.range(0.55f, 1.f);
      const float lean = rng.range(0.1f, 0.45f);
      const V3 out{std::cos(a), 0.f, std::sin(a)};
      const V3 side = norm(cross(V3{0, 1, 0}, out)) * rng.range(0.01f, 0.018f);
      const int segs = 4;
      for (int s = 0; s < segs; ++s) {
        const float t0 = float(s) / segs, t1 = float(s + 1) / segs;
        auto pt = [&](float t) { return root + out * (lean * t * t) + V3{0, 1, 0} * (h * (t - 0.25f * lean * t * t)); };
        const V3 p0 = pt(t0), p1 = pt(t1);
        const V3 w0 = side * (1.f - t0), w1 = side * (1.f - t1);
        leaf.flat(p0 - w0, p1 - w1, p1 + w1);
        leaf.flat(p0 - w0, p1 + w1, p0 + w0);
      }
    }
    leaf_col[0] = 0.55f; leaf_col[1] = 0.48f; leaf_col[2] = 0.30f; // dry, the desert's
  } else if (kind == "bush") {
    const int lumps = (int)(8 * std::min(q, 2.f));
    for (int k = 0; k < lumps; ++k) {
      const float a = rng.next() * TAU, d = rng.range(0.f, 0.22f);
      const float r = rng.range(0.16f, 0.26f);
      V3 c{std::cos(a) * d, r * 0.75f + rng.range(0.f, 0.35f), std::sin(a) * d};
      lump(leaf, c, {r, r * 0.85f, r}, std::max(8, (int)(12 * q)), 31u + k + seed * 977u, 0.5f, 0.f);
    }
    leaf_col[0] = 0.19f; leaf_col[1] = 0.29f; leaf_col[2] = 0.12f;
  } else { // boulder
    lump(bark, V3{0.f, 0.36f, 0.f}, V3{0.5f, 0.38f, 0.44f}, std::max(12, (int)(24 * q)), 7u + seed * 977u, 0.7f, 0.f);
    bark_col[0] = 0.60f; bark_col[1] = 0.39f; bark_col[2] = 0.27f; // sandstone
  }

  o.verts = bark.v;
  o.verts.insert(o.verts.end(), leaf.v.begin(), leaf.v.end());
  o.uvs = bark.uv;
  o.uvs.insert(o.uvs.end(), leaf.uv.begin(), leaf.uv.end());
  o.parts.clear();
  add_part(o, 0, bark.count(), bark_col[0], bark_col[1], bark_col[2], "bark");
  add_part(o, bark.count(), leaf.count(), leaf_col[0], leaf_col[1], leaf_col[2], "foliage");
  o.vert_count = (int)(o.verts.size() / 6);
  return true;
}

} // namespace studio
