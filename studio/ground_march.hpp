// Geekatplay TerraForge - a ray walked onto the ground the viewport draws.
//
// The pickers walked the heightmap in strides that start at fifteen metres
// and grow to two hundred, and interpolated the crossing between the last
// two. That suits the heightmap, whose picking copy is twenty metres a texel.
// The ground a view draws also carries the fractal micro-relief
// (terrain_relief.hpp): metres either way, in waves as short as the triangles
// drawing them. A stride steps straight over a crest, and the interpolation
// lands anywhere between its two ends - at a grazing angle, tens of metres
// behind the ridge under the pointer.
//
// So the strides are taken against the top of the band the relief can reach,
// the heightmap plus half its amount: above that nothing can be hit. From the
// last stride above the band the ray is walked finely, no step longer than
// the relief there allows, until it goes under the drawn ground or climbs out
// of the band again, and the crossing is closed in by bisection. A ray
// crosses the band in a few fine steps unless it grazes it, and the fine step
// grows with the distance as the octaves a view draws thin out.
//
// Pure: the caller's probe says what the ground is at a distance along the
// ray, so this is tested without a heightmap or a view
// (tests/cpp/test_ground_march.cpp).
#pragma once
#include <algorithm>

namespace studio {

// One point of the ray, as the caller's ground answers for it.
struct GroundProbe {
  int where = 0;     // 1 over the ground, 0 off it (walked past), -1 stop walking
  float top = 0.f;   // the point's height above the band the relief can reach
  float drawn = 0.f; // its height above the ground as drawn        - fine probes
  float step = 0.f;  // how far along the ray a fine step may go here - fine probes
};

struct GroundWalk {
  float t_max = 12.f;     // how far along the ray to look
  float stride = 0.003f;  // the first stride, grown by `growth` up to `stride_max`
  float growth = 1.02f;
  float stride_max = 0.04f;
  int fine_budget = 4096; // fine steps before the walk stops looking for detail
};

// Where the ray first goes from above the drawn ground to under it, as a
// distance along the ray. `probe(t, fine)` answers for the point at t; `fine`
// asks for `drawn` and `step` as well, which cost the relief. A ray starting
// under the ground has to come out of it before it can hit anything, as the
// pickers always had it.
template <class Probe>
bool ground_march(const GroundWalk &w, Probe &&probe, float &t_hit) {
  float t = 0.f, stride = w.stride, t_last = 0.f;
  bool have_last = false; // t_last was over the ground and above the band
  int budget = w.fine_budget;
  while (t < w.t_max) {
    const GroundProbe p = probe(t, false);
    if (p.where < 0) return false;
    if (p.where == 0) {
      have_last = false;
      t += stride;
      continue;
    }
    if (p.top > 0.f) {
      have_last = true;
      t_last = t;
      t += stride;
      stride = std::min(stride * w.growth, w.stride_max);
      continue;
    }
    // In the band or under it: walk finely from the last stride above it,
    // through this one, until the ray is under the drawn ground or out.
    float s = have_last ? t_last : t, s_prev = s, drawn_prev = 0.f;
    bool above = false; // the fine point before was above the drawn ground
    bool resume = false;
    while (s < w.t_max) {
      const GroundProbe q = probe(s, true);
      if (q.where < 0) return false;
      if (q.where == 0) { // off the ground again: strides from here
        have_last = false;
        t = s + stride;
        resume = true;
        break;
      }
      if (above && q.drawn <= 0.f) {
        float lo = s_prev, hi = s, d_lo = drawn_prev, d_hi = q.drawn;
        for (int i = 0; i < 40; ++i) {
          if (hi - lo <= std::max(1e-7f, hi * 1e-6f)) break;
          const float mid = 0.5f * (lo + hi);
          if (!(mid > lo && mid < hi)) break; // as fine as a float goes
          const GroundProbe m = probe(mid, true);
          if (m.where == 1 && m.drawn > 0.f) {
            lo = mid;
            d_lo = m.drawn;
          } else {
            hi = mid;
            d_hi = m.where == 1 ? m.drawn : 0.f;
          }
        }
        t_hit = lo + (hi - lo) * (d_lo / std::max(d_lo - d_hi, 1e-30f));
        return true;
      }
      if (s > t && q.top > 0.f) { // out of the band past the stride that found it
        have_last = true;
        t_last = s;
        t = s + stride;
        stride = std::min(stride * w.growth, w.stride_max);
        resume = true;
        break;
      }
      above = q.drawn > 0.f;
      s_prev = s;
      drawn_prev = q.drawn;
      // the relief's step while the budget lasts - then growing strides, the
      // walk the heightmap alone had - and never past the stride that found
      // the band before it has been looked at
      float want = q.step;
      if (--budget < 0) {
        want = stride;
        stride = std::min(stride * w.growth, w.stride_max);
      }
      const float least = std::max(1e-7f, s * 2e-6f); // a step a float still moves by
      float next = s + std::max(std::min(want, stride), least);
      if (s < t && next > t) next = t;
      s = next;
    }
    if (!resume) return false;
  }
  return false;
}

} // namespace studio
