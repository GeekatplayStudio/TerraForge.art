// Geekatplay TerraForge - meshing a grown branch system as tubes.
//
// The segment mesher (plant_segment.cpp) dresses one axis with sections,
// profiles, flares and blades; a grown tree is thousands of short chains
// with nothing but a radius per internode, so it gets a mesher of its own
// that does one thing cheaply: a ring at every axial sample of every shoot,
// the rings of one shoot sharing their vertices along it, quads between
// them, a small cap at the tip. A lateral's first ring sits at its parent's
// internode, inside the parent's tube, so the joint needs no blending.
//
// Subdivisions are the manual's: axial samples per metre along the shoot,
// angular samples per metre of circumference, both scaled by 2^boost and
// thinned per LOD through subdiv(); a twig never has fewer than four sides
// or one sample per internode. UVs are Standard: u once round, v the
// distance along the wood in metres, so a bark picture tiles the same size
// on trunk and twig. Normals are parametric - the ring's radial tilted by
// the taper. Wind: one phase per shoot from a hash; the bend weight starts
// at the parent's weight where the shoot sits and grows along it with the
// same rule the segments use (distance over radius to the 3/2, scaled by
// the flexibility and wind parameters), the gravity parameter counted as
// extra flexibility since the static droop is not modelled here; flutter is
// 0 on wood. The height weight is left at -1 for the walker to fill.
#include "plant/plant_growth.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace plant {

namespace {

constexpr int MIN_SIDES = 4, MAX_SIDES = 32;
constexpr float BEND_K = 0.002f, BEND_MAX = 4.f;

// The bend weight of every internode of every shoot, parents first (a
// child's index is always greater than its parent's).
void bend_weights(const Instance &inst, const std::vector<Shoot> &shoots, const GrowthMeshParams &p,
                  std::vector<std::vector<float>> &bends) {
  const float flex = p.flexibility * p.wind * (1.f + 0.2f * std::max(0.f, p.gravity));
  bends.assign(shoots.size(), {});
  for (size_t i = 0; i < shoots.size(); ++i) {
    const Shoot &s = shoots[i];
    if (s.removed || s.nodes.empty()) continue;
    float base = inst.wind_bend;
    if (s.parent >= 0) {
      const std::vector<float> &pb = bends[(size_t)s.parent];
      if (!pb.empty()) base = pb[(size_t)std::min<int>(s.parent_node, (int)pb.size() - 1)];
    }
    const float r0 = std::max(s.radius, 0.01f);
    std::vector<float> &b = bends[i];
    b.resize(s.nodes.size());
    float dist = 0.f;
    V3 prev = s.start;
    for (size_t k = 0; k < s.nodes.size(); ++k) {
      dist += length(s.nodes[k] - prev);
      prev = s.nodes[k];
      b[k] = std::min(BEND_MAX, base + flex * std::pow(dist / r0, 1.5f) * BEND_K);
    }
  }
}

struct Ring {
  std::vector<uint32_t> v; // sides + 1 vertices, the seam duplicated
};

} // namespace

void growth_mesh(const BuildCtx &ctx, const Instance &inst, const std::vector<Shoot> &shoots,
                 const GrowthMeshParams &p, MeshOut &out) {
  std::vector<std::vector<float>> bends;
  bend_weights(inst, shoots, p, bends);
  const float boost = std::exp2(p.mesh_boost);
  const float tint[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};

  out.begin(inst, PlantPartKind::Body, p.material, false, "growth");
  for (size_t i = 0; i < shoots.size(); ++i) {
    const Shoot &s = shoots[i];
    if (s.removed || s.nodes.empty() || s.radii.size() != s.nodes.size()) continue;
    const int sides = std::min(MAX_SIDES, std::max(MIN_SIDES, subdiv(ctx, p.angular_per_m * TAU * s.radius * boost, MIN_SIDES)));
    const float phase = hash_unit(hash_u32(inst.id, (uint32_t)i, hash_str("phase")));
    const std::vector<float> &b = bends[i];

    // the path: the start then every internode, with the radius, the bend
    // weight and the distance along the wood at each point
    const size_t m = s.nodes.size();
    std::vector<V3> pts(m + 1);
    std::vector<float> rad(m + 1), bend(m + 1), dist(m + 1);
    pts[0] = s.start;
    rad[0] = s.radii[0];
    bend[0] = s.parent >= 0 && !bends[(size_t)s.parent].empty()
                  ? bends[(size_t)s.parent][(size_t)std::min<int>(s.parent_node, (int)bends[(size_t)s.parent].size() - 1)]
                  : inst.wind_bend;
    dist[0] = s.dist_base;
    for (size_t k = 0; k < m; ++k) {
      pts[k + 1] = s.nodes[k];
      rad[k + 1] = s.radii[k];
      bend[k + 1] = b[k];
      dist[k + 1] = dist[k] + length(s.nodes[k] - pts[k]);
    }
    auto tangent = [&](size_t k) {
      const V3 a = k > 0 ? pts[k - 1] : pts[k];
      const V3 c = k + 1 <= m ? pts[k + 1] : pts[k];
      return normalize(c - a, s.dir);
    };

    // rings: at every path point and at the axial subdivisions between
    Frame frame = Frame::along(pts[0], tangent(0), nullptr);
    Ring prev;
    prev.v.resize((size_t)sides + 1);
    bool have_prev = false;
    V3 last_t = frame.z;
    float last_r = rad[0];
    for (size_t k = 0; k <= m; ++k) {
      const int segs = k == 0 ? 1 : std::max(1, subdiv(ctx, p.axial_per_m * (dist[k] - dist[k - 1]) * boost, 1));
      for (int j = (k == 0 ? 0 : 1); j <= (k == 0 ? 0 : segs); ++j) {
        const float t = k == 0 ? 0.f : (float)j / (float)segs;
        const V3 pos = k == 0 ? pts[0] : lerp(pts[k - 1], pts[k], t);
        const V3 tan = k == 0 ? tangent(0) : normalize(lerp(tangent(k - 1), tangent(k), t), tangent(k));
        const float r = k == 0 ? rad[0] : rad[k - 1] + (rad[k] - rad[k - 1]) * t;
        const float w = k == 0 ? bend[0] : bend[k - 1] + (bend[k] - bend[k - 1]) * t;
        const float d = k == 0 ? dist[0] : dist[k - 1] + (dist[k] - dist[k - 1]) * t;
        frame = Frame::along(pos, tan, &frame);
        // the taper tilts the normal: dr/ds along the tangent
        const float ds = k == 0 ? 1.f : std::max(1e-4f, (dist[k] - dist[k - 1]) / (float)segs);
        const float drds = k == 0 ? 0.f : (rad[k] - rad[k - 1]) / (float)segs / ds;
        Ring ring;
        ring.v.resize((size_t)sides + 1);
        const float wind4[4] = {phase, w, 0.f, -1.f};
        for (int c = 0; c <= sides; ++c) {
          const float a = TAU * (float)c / (float)sides;
          const V3 radial = frame.x * std::cos(a) + frame.y * std::sin(a);
          const V3 n = normalize(radial - tan * drds, radial);
          ring.v[(size_t)c] = out.vertex(pos + radial * r, n, (float)c / (float)sides, d, wind4, tint);
        }
        if (have_prev)
          for (int c = 0; c < sides; ++c)
            out.quad(prev.v[(size_t)c], prev.v[(size_t)c + 1], ring.v[(size_t)c + 1], ring.v[(size_t)c]);
        prev = ring;
        have_prev = true;
        last_t = tan;
        last_r = r;
      }
    }
    // the tip: a small dome closed by a fan
    {
      const float wind4[4] = {phase, bend[m], 0.f, -1.f};
      const uint32_t tip = out.vertex(pts[m] + last_t * (last_r * 0.5f), last_t, 0.5f, dist[m] + last_r * 0.5f, wind4, tint);
      for (int c = 0; c < sides; ++c) out.tri(prev.v[(size_t)c], prev.v[(size_t)c + 1], tip);
    }
  }
  out.end();
}

} // namespace plant
} // namespace gpx
