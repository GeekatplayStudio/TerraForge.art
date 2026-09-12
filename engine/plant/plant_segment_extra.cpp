// Geekatplay TerraForge - a segment's ends and its feet
// (plant_internal.hpp: segment_caps, segment_flare).
//
// The cap is what a segment shows where it stops: the flat top of a cut
// branch, the disc a daisy's florets sit on, the dome of a mushroom's stem.
// It is a fan about the last ring, its shape given by a profile curve (flat
// by default, which is the disc), its own material and its own polar
// mapping; a border keeps the outermost ring in the body's material so bark
// wraps over the edge rather than stopping dead. A pruned segment shows the
// secondary cap instead, which is how the manual separates "the top of the
// branch" from "where the branch was cut".
//
// The flares are the buttress roots at the foot of a big trunk: a few
// ridges round the base, swelling the radius and dying away with height.
// They are a displacement of the body rather than geometry of their own, so
// they cost nothing and the bark follows them.
#include "plant/plant_internal.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

// One end of the segment, as a fan about the ring already meshed.
void cap_end(BuildCtx &ctx, const Instance &inst, MeshOut &out, const SegmentShape &sh,
             const std::vector<V3> &ring, const std::vector<V3> &ring_nrm, const Instance::AxisSample &s,
             bool upward, const Curve &profile, float offset, bool smooth, int material, float uv_angle,
             float uv_scale, float border, float v_at_ring) {
  if (ring.size() < 3) return;
  const int cols = (int)ring.size() - 1;
  const V3 axis = upward ? s.t : -s.t;
  const float radius = std::max(s.radius, 1e-5f);
  // the middle of the cap, lifted by the profile's value at the centre
  const float wind4[4] = {sh.wind_phase, wind_bend_at(ctx, inst, s.primal, sh.wind_flex), 0.f, -1.f};
  const int rings = std::max(2, (int)std::lround(2.f + std::fabs(profile.eval(0.f)) * 6.f));
  const float ca = std::cos(uv_angle), sa = std::sin(uv_angle);
  const V3 nx = s.n, ny = normalize(cross(s.t, s.n), perpendicular(s.t));

  std::vector<uint32_t> prev, cur;
  for (int i = 0; i <= rings; ++i) {
    const float t = (float)i / (float)rings;      // 1 at the rim, 0 at the middle
    const float rr = 1.f - t;                      // how far in
    cur.clear();
    for (int j = 0; j <= cols; ++j) {
      const float a = (float)j / (float)cols;
      const V3 rim = ring[(size_t)j];
      const V3 dir = rim - s.p;
      const float lift = (profile.eval(rr) + offset) * radius;
      const V3 p = s.p + dir * t + axis * lift;
      V3 nrm = axis;
      if (smooth && i == rings) nrm = normalize(ring_nrm[(size_t)j] + axis, axis);
      // polar coordinates from the middle, turned and scaled as asked
      const float lx = dot(p - s.p, nx) / radius * uv_scale, ly = dot(p - s.p, ny) / radius * uv_scale;
      const float u = 0.5f + 0.5f * (lx * ca - ly * sa), v = 0.5f + 0.5f * (lx * sa + ly * ca);
      cur.push_back(out.vertex(p, nrm, border > 0.f && i == rings ? a * sh.u_rep : u,
                               border > 0.f && i == rings ? v_at_ring : v, wind4, sh.tint));
    }
    if (i > 0)
      for (int j = 0; j < cols; ++j) {
        if (upward) out.quad(prev[(size_t)j], prev[(size_t)j + 1], cur[(size_t)j + 1], cur[(size_t)j]);
        else out.quad(prev[(size_t)j], cur[(size_t)j], cur[(size_t)j + 1], prev[(size_t)j + 1]);
      }
    prev = cur;
  }
  (void)material;
}

} // namespace

float segment_flare(const FlareParams &f, float radius, float length, float angle, float primal) {
  if (f.number <= 0 || f.height <= 0.f) return 0.f;
  const float h = clampf(1.f - primal * length / std::max(f.height, 1e-4f), 0.f, 1.f);
  if (h <= 0.f) return 0.f;
  const float shape = f.shape ? std::max(f.shape->eval(1.f - h), 0.f) : h;
  // where the ridges are: `number` of them, spread evenly, jittered a little
  float best = 0.f;
  for (int i = 0; i < f.number; ++i) {
    const float jitter = (hash_unit(hash_u32(f.seed, (uint32_t)i)) - 0.5f) * f.randomness;
    float da = angle - ((float)i / (float)f.number + jitter);
    da -= std::floor(da + 0.5f);
    const float w = std::max(0.06f, f.width * 0.5f / (float)f.number);
    best = std::max(best, std::max(0.f, 1.f - std::fabs(da) / w));
  }
  const float bump = best * best * (3.f - 2.f * best);
  return bump * shape * (radius * f.swell + f.depth * h * 0.15f);
}

void segment_caps(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, const SegmentShape &sh) {
  const ParamReader pr(ctx, n, inst);
  if (sh.rings.size() < 2) return;

  // the top: its own cap, or the secondary one when this segment was cut
  if (pr.b("cap_enable", true) && !sh.top_ring.empty()) {
    const int mode = pr.choice("cap_mode");
    const bool secondary = inst.pruned && mode != 0;
    const Curve profile = pr.curve(secondary ? "cap2_profile" : "cap_profile");
    const float offset = pr.random(secondary ? "cap2_offset" : "cap_offset");
    const bool smooth = pr.b(secondary ? "cap2_smoothing" : "cap_smoothing", true);
    out.begin(inst, PlantPartKind::Cap, sh.cap_material, false, "cap");
    cap_end(ctx, inst, out, sh, sh.top_ring, sh.top_nrm, sh.rings.back(), true, profile, offset, smooth,
            sh.cap_material, pr.f("cap_mat_angle") * DEG, std::max(pr.f("cap_mat_scale", 1.f), 1e-3f),
            pr.f("cap_border"), sh.top_v);
    out.end();
  }
  // the bottom, for a part that stands on its own (a mushroom's stem, a
  // fallen log): off by default, because a branch's foot is inside its parent
  if (pr.b("bcap_enable") && !sh.bottom_ring.empty()) {
    out.begin(inst, PlantPartKind::Cap, sh.cap_material, false, "bottom cap");
    cap_end(ctx, inst, out, sh, sh.bottom_ring, sh.bottom_nrm, sh.rings.front(), false,
            pr.curve("bcap_profile"), pr.random("bcap_offset"), pr.b("bcap_smoothing", true), sh.cap_material,
            pr.f("cap_mat_angle") * DEG, std::max(pr.f("cap_mat_scale", 1.f), 1e-3f), 0.f, sh.bottom_v);
    out.end();
  }
}

} // namespace plant
} // namespace gpx
