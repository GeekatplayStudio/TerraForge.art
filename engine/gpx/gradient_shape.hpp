// Geekatplay TerraForge — gradient shapes, as numbers.
//
// The Gradient node (engine/nodes/nodes_gradient.cpp) evaluates exactly this
// per texel; the tests read the same function (tests/cpp/test_gradient.cpp).
// A gradient is a distance - along a direction, from a line, from a centre in
// some norm, or round it - turned into a 0..1 ramp between a start and an
// end, repeated, wrapped, bent through a profile and optionally cut into
// terraces. Pure: no nodes, no maps.
#pragma once
#include <algorithm>
#include <cmath>

namespace gpx::gradient {

enum Type { Linear = 0, Reflected, Circle, Ellipse, Square, Diamond, Angular, Spiral };
enum Wrap { Hold = 0, Repeat, Mirror };
enum Profile { PLinear = 0, Smooth, Smoother, EaseIn, EaseOut, Sine, Dome, Bell };

struct Params {
  int type = Circle;
  float cx = 0.5f, cy = 0.5f; // centre, tile fractions
  float angle_deg = 0.f;      // the direction the gradient faces
  float size = 0.5f;          // reach from the centre, tile fractions
  float aspect = 1.f;         // stretch along the direction (area kept)
  float start = 0.f, end = 1.f;
  int profile = Smooth;
  float repeat = 1.f;
  int wrap = Hold;
  int steps = 0;              // terraces; 0 continuous
  float turns = 1.5f;         // spiral windings over the size
  float roundness = 0.f;      // square / diamond corners toward round
};

// The shapes that are high in their middle and fall to their edge.
inline bool centred(int type) { return type >= Circle && type <= Diamond; }

// The curve from 0 at one end of the ramp to 1 at the other.
inline float profile(int kind, float s) {
  s = std::clamp(s, 0.f, 1.f);
  switch (kind) {
    case PLinear: return s;
    case Smooth: return s * s * (3.f - 2.f * s);
    case Smoother: return s * s * s * (s * (s * 6.f - 15.f) + 10.f);
    case EaseIn: return s * s;
    case EaseOut: return 1.f - (1.f - s) * (1.f - s);
    case Sine: return 0.5f - 0.5f * std::cos(s * 3.14159265f);
    case Dome: return std::sqrt(std::max(0.f, 1.f - (1.f - s) * (1.f - s)));
    default: { // bell: a gaussian, flat at the top, a long soft foot
      const float k = 4.5f, floor_v = std::exp(-k);
      const float r = 1.f - s;
      return (std::exp(-k * r * r) - floor_v) / (1.f - floor_v);
    }
  }
}

// A ramp value past its end: held at the end, started again, or run back.
inline float wrap_t(int wrap, float t) {
  switch (wrap) {
    case Repeat: {
      const float f = t - std::floor(t);
      return (t > 0.f && f == 0.f) ? 1.f : f;
    }
    case Mirror: {
      const float f = t - 2.f * std::floor(t * 0.5f);
      return f > 1.f ? 2.f - f : f;
    }
    default: return std::clamp(t, 0.f, 1.f);
  }
}

// The gradient at a point of the tile (u, v in tile fractions), 0..1.
inline float value(const Params &p, float u, float v) {
  const float PI = 3.14159265f;
  const float a = p.angle_deg * (PI / 180.f);
  const float ca = std::cos(a), sa = std::sin(a);
  const float dx = u - p.cx, dy = v - p.cy;
  const float x = dx * ca + dy * sa;  // along the direction
  const float y = -dx * sa + dy * ca; // across it
  const float size = std::max(p.size, 1e-4f);
  float d = 0.f;
  switch (p.type) {
    case Linear: d = x / size * 0.5f + 0.5f; break;
    case Reflected: d = std::fabs(x) / size; break;
    case Circle: d = std::sqrt(x * x + y * y) / size; break;
    case Ellipse:
    case Square:
    case Diamond: {
      // stretched along the direction, the area kept; an ellipse starts out
      // twice as long as it is wide, so it is not a circle by default
      const float s = std::sqrt(std::max(p.aspect * (p.type == Ellipse ? 2.f : 1.f), 1e-3f));
      const float ex = x / s, ey = y * s;
      const float round = std::sqrt(ex * ex + ey * ey);
      if (p.type == Ellipse) d = round;
      else if (p.type == Square)
        d = std::max(std::fabs(ex), std::fabs(ey)) * (1.f - p.roundness) + round * p.roundness;
      else
        d = (std::fabs(ex) + std::fabs(ey)) * (1.f - p.roundness) + round * p.roundness;
      d /= size;
      break;
    }
    case Angular: d = std::atan2(y, x) / (2.f * PI) + 0.5f; break;
    default: // spiral: the angle round, plus the windings outward
      d = std::atan2(y, x) / (2.f * PI) + 0.5f + std::sqrt(x * x + y * y) / size * p.turns;
      break;
  }
  const float span = p.end - p.start;
  float t = std::fabs(span) < 1e-4f ? (d >= p.start ? 1.f : 0.f) : (d - p.start) / span;
  t *= std::max(p.repeat, 1.f);
  // a spiral has no end to hold at: it runs back down between its arms
  const int wrap = (p.type == Spiral && p.wrap == Hold) ? Mirror : p.wrap;
  t = wrap_t(wrap, t);
  float h = profile(p.profile, centred(p.type) ? 1.f - t : t);
  if (p.steps > 0) h = std::min(std::floor(h * float(p.steps)) / float(p.steps), 1.f);
  return h;
}

} // namespace gpx::gradient
