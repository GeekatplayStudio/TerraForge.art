// Geekatplay TerraForge - the shape of a piece of ground, and how its edge
// gives way.
//
// Two nodes need the same thing and had neither: TerrainShape, which decides
// what the terrain tile *is* - an island, a plateau, a rectangle of ground -
// and Lake, which is a body of water with a shore. Both want a region with a
// nameable outline, an edge that is not a perfect curve, and a blend that
// gives way over a stated distance rather than a fixed one.
//
// What was there before was `post_zero_edges`: one slider, always the whole
// rectangle, always the same smoothstep. It could make a square island and
// nothing else, and "how far in does it reach" and "how hard does it pull"
// were the same number. Here they are three:
//
//   extent    - how far in from the rim the blend reaches, as a fraction of
//               the shape's own radius, so it means the same thing on a big
//               island and a small one;
//   gradient  - the curve across that band. Below 1 the ground stays high and
//               drops away near the rim, which is a plateau with a cliff;
//               above 1 it starts falling from well inside, which is a beach.
//               This is the "from the centre to the sides" control;
//   intensity - how far down it goes by the time it reaches the rim. 1 is all
//               the way to the base, less leaves the edge standing proud.
//
// Every length is a fraction of the tile, so a shape survives a change of
// resolution, and nothing here touches a buffer: it is a function of a point,
// which is what lets the same code serve a raster node and a preview.
#pragma once
#include "gpx/noise_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpx::shape {

// The outlines worth naming. Mask is the escape hatch: the caller supplies
// its own region and only the edge treatment comes from here.
enum Kind {
  Rectangle = 0,
  RoundedRect,
  Ellipse,
  Diamond,
  Mask,
  KIND_COUNT
};

struct Params {
  int kind = Ellipse;
  float cx = 0.5f, cy = 0.5f; // centre, as a fraction of the tile
  float rx = 0.42f, ry = 0.42f; // half-width and half-height, likewise
  float corner = 0.3f;          // RoundedRect: how much of the half-size rounds
  float rotation = 0.f;         // degrees
  // The edge, which in nature is never the curve the maths gives you.
  float edge_amount = 0.f; // how far the rim wanders, as a fraction of radius
  float edge_scale = 5.f;  // how many wanderings around the rim
  int edge_octaves = 4;
  uint32_t seed = 0;
  // The blend. See the header comment - these are three separate questions.
  float extent = 0.3f;
  float gradient = 1.f;
  float intensity = 1.f;
};

// How far inside the shape a point is, in units of the shape's own radius:
// 1 at the centre, 0 on the rim, negative outside. Scaling by the radius is
// what lets `extent` be a fraction and still mean a real distance.
inline float inside(const Params &p, float u, float v) {
  float dx = u - p.cx, dy = v - p.cy;
  if (p.rotation != 0.f) {
    const float a = p.rotation * 0.017453292519943295f;
    const float c = std::cos(a), s = std::sin(a);
    const float tx = dx * c + dy * s;
    dy = -dx * s + dy * c;
    dx = tx;
  }
  const float rx = std::max(p.rx, 1e-6f), ry = std::max(p.ry, 1e-6f);
  float nx = dx / rx, ny = dy / ry; // 1 on the rim of the base shape
  float d;                          // distance from the centre, rim at 1
  switch (p.kind) {
    case Rectangle:
      d = std::max(std::fabs(nx), std::fabs(ny));
      break;
    case RoundedRect: {
      // the usual rounded box: push in by the corner radius, measure the
      // distance to that smaller box, and add the radius back
      const float k = std::clamp(p.corner, 0.f, 1.f);
      const float bx = std::max(std::fabs(nx) - (1.f - k), 0.f);
      const float by = std::max(std::fabs(ny) - (1.f - k), 0.f);
      const float outside = std::sqrt(bx * bx + by * by);
      const float in = std::min(std::max(std::fabs(nx), std::fabs(ny)) -
                                    (1.f - k),
                                0.f);
      d = (1.f - k) + outside + in;
      break;
    }
    case Diamond:
      d = std::fabs(nx) + std::fabs(ny);
      break;
    case Ellipse:
    default:
      d = std::sqrt(nx * nx + ny * ny);
      break;
  }
  // A wandering rim. Perturbing the *distance* by a field of the point
  // rather than by a function of the angle is what keeps it seamless: an
  // angle wraps at half a turn and leaves a crease down one side.
  //
  // The wander only ever eats *into* the shape, never out of it. That is
  // what makes the radius a maximum in the sense Terragen's Max radius
  // means it - the rim moves inside the circle you named and never past it
  // - and it is the only version that lets a caller reason about where the
  // shape can possibly reach. A symmetric wobble would push the rim out as
  // often as in, and a lake would climb its own bank.
  if (p.edge_amount > 1e-6f) {
    noise::FbmParams f;
    f.octaves = std::clamp(p.edge_octaves, 1, 10);
    const float s = std::max(p.edge_scale, 0.01f);
    const float n = noise::fbm(u * s, v * s, p.seed ^ 0x9e37u, f); // about -1..1
    d *= 1.f + p.edge_amount * std::clamp(0.5f + 0.5f * n, 0.f, 1.f);
  }
  return 1.f - d;
}

// The blend curve across the edge band: 0 outside, 1 once fully inside.
inline float falloff(const Params &p, float in) {
  const float ext = std::max(p.extent, 1e-6f);
  float t = std::clamp(in / ext, 0.f, 1.f);
  const float g = std::clamp(p.gradient, 0.05f, 8.f);
  // g == 1 is the plain smoothstep, which is what the old single slider did,
  // so a shape left at the defaults looks like the thing it replaced.
  t = t * t * (3.f - 2.f * t);
  if (g != 1.f) t = std::pow(t, g);
  return t;
}

// Presence at a point: 1 where the shape is solid, 0 outside, and the blend
// between. `intensity` decides how far down the rim actually goes.
inline float presence(const Params &p, float u, float v) {
  const float t = falloff(p, inside(p, u, v));
  return 1.f - std::clamp(p.intensity, 0.f, 1.f) * (1.f - t);
}

} // namespace gpx::shape
