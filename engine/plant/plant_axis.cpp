// Geekatplay TerraForge - a segment's centre line (plant_internal.hpp:
// build_axis).
//
// The axis is walked, not solved: the tip advances one step at a time in
// the direction it is currently growing, and everything that bends a plant
// - its own shape, gravity and tropism, the biases the species carries, the
// kinks its children put in it - turns that direction a little at each step.
// That is how a real shoot grows and it is why the forces compose sensibly:
// a branch leaning under gravity and pulled by an attractor ends up doing
// both, without anyone having to write the combination down.
//
// Two rules keep the result usable. The turn allowed in one step is limited
// by the radius (`prevent_backfolds`): a thick trunk cannot turn inside its
// own skin, which is exactly the fold the manual warns about. And the
// finished curve is re-parametrised by arc length, so a heavily perturbed
// axis is still the length it was asked for - the manual is explicit that
// perturbation moves the axis about without lengthening it.
//
// Biases are the manual's seven kinds, global (from the species root) and
// local (the segment's own), read here in one place: Direction leans toward
// a vector, Conic toward (or, as a repeller, away from) the nearest
// direction on a cone, Attractor toward a point, Axis repeller away from a
// line, Swirl waves about an axis, Curl rolls the tip into loops, and Twist
// spins the section rather than moving the axis at all, so it is returned
// as a number of turns for the mesher to use.
#include "plant/plant_internal.hpp"
#include "gpx/noise_core.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

// Turn `d` toward `target` by `amount` (0 none, 1 all the way).
V3 toward(V3 d, V3 target, float amount) {
  target = normalize(target, d);
  const float c = clampf(dot(d, target), -1.f, 1.f);
  const float ang = std::acos(c) * clampf(amount, -4.f, 4.f);
  V3 axis = cross(d, target);
  if (length(axis) < 1e-7f || std::fabs(ang) < 1e-7f) return d;
  return normalize(rotate(d, normalize(axis), ang), d);
}

// The direction on the cone (apex direction `dir`, half angle `half`)
// closest to `d`.
V3 on_cone(V3 d, V3 dir, float half) {
  dir = normalize(dir, V3(0, 1, 0));
  V3 side = d - dir * dot(d, dir);
  if (length(side) < 1e-6f) side = perpendicular(dir);
  side = normalize(side);
  return normalize(dir * std::cos(half) + side * std::sin(half), dir);
}

float noise1(float x, uint32_t seed) { return noise::perlin(x, 0.37f, seed) * 2.f; }

} // namespace

void build_axis(const BuildCtx &ctx, const AxisParams &p, const Frame &start, int samples,
                std::vector<Instance::AxisSample> &out, float &twist_total) {
  const int n = std::max(samples, 2);
  const float axis_len = std::max(p.length, 1e-5f);
  const float ds = axis_len / (float)(n - 1);
  twist_total = 0.f;

  // the starting direction, after the Transform group's orientation tropism
  Frame f = start;
  if (p.orient_vertical != 0.f)
    f.z = toward(f.z, V3(0.f, p.orient_vertical > 0.f ? 1.f : -1.f, 0.f), std::fabs(p.orient_vertical));
  if (p.orient_horizontal != 0.f) {
    V3 level(f.z.x, 0.f, f.z.z);
    if (length(level) < 1e-4f) level = V3(f.x.x, 0.f, f.x.z);
    f.z = toward(f.z, normalize(level, V3(1, 0, 0)), std::fabs(p.orient_horizontal));
  }
  f = Frame::along(f.o, f.z, &start);

  // every bias in one list: the species' own first, then the segment's
  std::vector<AxisParams::Bias> biases = p.global_biases;
  for (AxisParams::Bias &b : biases) b.strength *= p.global_bias_strength;
  if (p.bias_type > 0 && p.bias_strength != 0.f) {
    AxisParams::Bias b;
    b.type = p.bias_type - 1; // the segment's list has a None in front
    b.strength = p.bias_strength;
    b.dir = p.bias_dir;
    b.origin = p.bias_origin;
    b.local = p.bias_local;
    b.relative = !p.bias_length_agnostic;
    b.repeller = p.bias_repeller;
    b.cone_angle = p.bias_cone_angle;
    b.base_length = p.bias_base_length;
    b.twist_planar = p.twist_planar;
    b.twist_symmetry = p.twist_symmetry;
    b.twist_target = p.twist_target;
    biases.push_back(b);
  }

  std::vector<V3> pts;
  pts.reserve((size_t)n);
  V3 pos = f.o, d = f.z;
  const V3 up(0.f, 1.f, 0.f);
  pts.push_back(pos);
  const float max_turn = p.prevent_backfolds ? ds / std::max(p.radius, 1e-4f) * 0.9f : 6.f;
  for (int i = 1; i < n; ++i) {
    const float t0 = (float)(i - 1) / (float)(n - 1);
    const V3 before = d;
    // the shape the segment was asked for
    const float bend = p.bend_deg * DEG / (float)(n - 1);
    V3 side = cross(d, up);
    if (length(side) < 1e-5f) side = f.x;
    side = normalize(side);
    switch (p.mode) {
      case 1: d = rotate(d, side, bend); break;                       // curve up
      case 2: d = rotate(d, side, -bend); break;                      // curve down
      case 3: d = rotate(d, side, t0 < 0.5f ? bend * 2.f : -bend * 2.f); break; // an S
      case 4: d = rotate(d, normalize(rotate(side, before, t0 * TAU)), bend); break; // a spiral
      case 5:
        if (p.custom) {
          // the curve is a sideways offset in fractions of the length
          const float a = p.custom->eval(t0), b = p.custom->eval(t0 + 1.f / (float)(n - 1));
          d = normalize(d + f.x * ((b - a) * axis_len / std::max(ds, 1e-6f)) * 0.02f, d);
        }
        break;
      default: break;
    }
    // gravity and tropism: the segment's own pull toward (or away from) the sky
    if (p.tropism != 0.f) {
      const float amount = p.tropism * p.gravity * ds / axis_len;
      d = toward(d, V3(0.f, amount > 0.f ? 1.f : -1.f, 0.f), std::fabs(amount));
    }
    // the biases
    for (const AxisParams::Bias &b : biases) {
      if (b.strength == 0.f) continue;
      const float step = b.relative ? ds / axis_len : ds;
      const float s = b.strength * step;
      const V3 dir = b.local ? f.dir(b.dir) : b.dir;
      switch (b.type) {
        case 0: d = toward(d, dir, s); break;                          // direction
        case 1: {                                                      // conic
          const V3 target = on_cone(d, dir, b.cone_angle * DEG);
          if (!b.repeller) d = toward(d, target, s);
          else {
            const float gap = std::acos(clampf(dot(d, target), -1.f, 1.f));
            const float near = clampf(1.f - gap / std::max(b.base_length, 1e-3f), 0.f, 1.f);
            if (near > 0.f) d = toward(d, target, -s * near);
          }
        } break;
        case 2: d = toward(d, b.origin - pos, s); break;                // attractor
        case 3: {                                                       // axis repeller
          const V3 along = normalize(dir, up);
          const V3 rel = pos - b.origin;
          V3 away = rel - along * dot(rel, along);
          if (length(away) < 1e-5f) away = perpendicular(along);
          d = toward(d, away, s);
        } break;
        case 4: d = rotate(d, normalize(dir, up), s * std::sin(t0 * TAU)); break; // swirl
        case 5: d = rotate(d, side, s * TAU); break;                    // curl
        case 6: {                                                       // twist
          float turns = b.strength * (b.relative ? 1.f : axis_len);
          if (b.twist_planar) {
            const float sym = (float)std::max(b.twist_symmetry, 1);
            turns = std::min(turns, 0.5f / sym) + b.twist_target * DEG / TAU;
          }
          twist_total = turns;
        } break;
        default: break;
      }
    }
    // the kinks children ask for, alternating sides (the manual's zig-zag)
    for (size_t k = 0; k < p.kinks.size(); ++k) {
      const AxisParams::Kink &kk = p.kinks[k];
      const float w = std::max(p.kink_smooth, 1e-3f) * 0.5f;
      const float d0 = std::fabs(t0 - kk.primal);
      if (d0 > w) continue;
      const float fall = 1.f - d0 / w;
      d = rotate(d, side, kk.angle * fall * ((k % 2) ? -1.f : 1.f) / std::max(1.f, w * (float)n));
    }
    // no turn tighter than the body can bend
    const float turned = std::acos(clampf(dot(before, d), -1.f, 1.f));
    if (turned > max_turn && turned > 1e-6f) d = toward(before, d, max_turn / turned);
    d = normalize(d, before);
    pos += d * ds;
    pts.push_back(pos);
  }

  // the perturbation: the axis wandering sideways without getting longer
  if (p.perturb_strength > 0.f) {
    const uint32_t seed = p.noise_seed;
    const float amp = p.perturb_strength * axis_len * 0.15f;
    const float planar = clampf(p.perturb_planar, -1.f, 1.f);
    const float fx = std::min(1.f, 1.f - planar), fy = std::min(1.f, 1.f + planar);
    for (int i = 1; i < n; ++i) {
      const float t = (float)i / (float)(n - 1);
      float w = 1.f;
      if (p.perturb_smooth_start) w *= smoothstep(0.f, 0.25f, t);
      if (p.perturb_keep_tip) w *= 1.f - smoothstep(0.75f, 1.f, t);
      const float a = noise1(t * p.perturb_frequency, seed) * amp * w * fx;
      const float b = noise1(t * p.perturb_frequency + 11.3f, seed ^ 0x9e3779b9u) * amp * w * fy;
      pts[(size_t)i] += f.x * a + f.y * b;
    }
  }
  // the smoothing: a box filter over the points, ends held
  if (p.smoothing > 0.f) {
    const int passes = std::max(1, (int)std::lround(p.smoothing * 3.f));
    for (int k = 0; k < passes; ++k) {
      std::vector<V3> next = pts;
      for (size_t i = 1; i + 1 < pts.size(); ++i)
        next[i] = pts[i - 1] * 0.25f + pts[i] * 0.5f + pts[i + 1] * 0.25f;
      pts.swap(next);
    }
  }

  // re-parametrised by arc length, so the segment is the length it was asked
  // for however much it was bent about
  std::vector<float> dist(pts.size(), 0.f);
  for (size_t i = 1; i < pts.size(); ++i) dist[i] = dist[i - 1] + length(pts[i] - pts[i - 1]);
  const float total = std::max(dist.back(), 1e-6f);
  out.clear();
  out.reserve((size_t)n);
  Frame carry = f;
  for (int i = 0; i < n; ++i) {
    const float want = (float)i / (float)(n - 1) * total;
    size_t k = 0;
    while (k + 2 < dist.size() && dist[k + 1] < want) ++k;
    const float span = std::max(dist[k + 1] - dist[k], 1e-9f);
    const float u = clampf((want - dist[k]) / span, 0.f, 1.f);
    Instance::AxisSample s;
    s.p = lerp(pts[k], pts[k + 1], u);
    s.t = normalize(pts[k + 1] - pts[k], f.z);
    carry = Frame::along(s.p, s.t, &carry);
    s.n = carry.x;
    s.primal = (float)i / (float)(n - 1);
    s.dist = s.primal * axis_len;
    s.radius = 0.f; // the segment fills this from its profile
    out.push_back(s);
  }
  (void)ctx;
}

} // namespace plant
} // namespace gpx
