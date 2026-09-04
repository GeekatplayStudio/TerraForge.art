// Geekatplay TerraForge — colour conversions shared by the CPU field nodes and
// the tests. The GLSL twins are gpxf_rgb2hsv / gpxf_hsv2rgb in the transpiler
// prelude (engine/field_glsl.cpp); the two must stay identical, branch for
// branch, or a colour graph renders differently on the GPU for no visible
// reason (AGENTS.md, node framework rule 9).
#pragma once
#include "attribute.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gpx {

// A colour gradient sampled at t. Stops are assumed sorted by t (add_gradient
// sorts them); outside the first and last stop the end colour is held. The
// alpha channel travels with the colour, which is what lets one fractal decide
// both what a material layer looks like and where it is present at all.
inline void eval_gradient(const std::vector<GradientStop> &stops, float t,
                          float *rgba) {
  if (stops.empty()) {
    rgba[0] = rgba[1] = rgba[2] = t;
    rgba[3] = 1.f;
    return;
  }
  if (t <= stops.front().t) {
    const GradientStop &s = stops.front();
    rgba[0] = s.r; rgba[1] = s.g; rgba[2] = s.b; rgba[3] = s.a;
    return;
  }
  for (size_t i = 0; i + 1 < stops.size(); ++i) {
    const GradientStop &a = stops[i], &b = stops[i + 1];
    if (t <= b.t) {
      float f = (t - a.t) / std::max(b.t - a.t, 1e-6f);
      rgba[0] = a.r + (b.r - a.r) * f;
      rgba[1] = a.g + (b.g - a.g) * f;
      rgba[2] = a.b + (b.b - a.b) * f;
      rgba[3] = a.a + (b.a - a.a) * f;
      return;
    }
  }
  const GradientStop &s = stops.back();
  rgba[0] = s.r; rgba[1] = s.g; rgba[2] = s.b; rgba[3] = s.a;
}


inline float luminance_rgb(const float *c) {
  return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

// h, s, v all in 0..1. Achromatic colours report hue 0.
inline void rgb_to_hsv(const float *c, float *hsv) {
  float mx = std::max(c[0], std::max(c[1], c[2]));
  float mn = std::min(c[0], std::min(c[1], c[2]));
  float d = mx - mn, h = 0.f;
  if (d > 1e-9f) {
    if (mx == c[0]) h = std::fmod((c[1] - c[2]) / d, 6.f);
    else if (mx == c[1]) h = (c[2] - c[0]) / d + 2.f;
    else h = (c[0] - c[1]) / d + 4.f;
    h /= 6.f;
    if (h < 0.f) h += 1.f;
  }
  hsv[0] = h;
  hsv[1] = mx > 1e-9f ? d / mx : 0.f;
  hsv[2] = mx;
}

inline void hsv_to_rgb(const float *hsv, float *c) {
  float hf = hsv[0] - std::floor(hsv[0]);
  float h = hf * 6.f;
  float s = std::clamp(hsv[1], 0.f, 1.f), v = hsv[2];
  int i = (int)std::floor(h);
  float f = h - (float)i;
  float p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
  switch (i) {
    case 0: c[0] = v; c[1] = t; c[2] = p; break;
    case 1: c[0] = q; c[1] = v; c[2] = p; break;
    case 2: c[0] = p; c[1] = v; c[2] = t; break;
    case 3: c[0] = p; c[1] = q; c[2] = v; break;
    case 4: c[0] = t; c[1] = p; c[2] = v; break;
    default: c[0] = v; c[1] = p; c[2] = q; break;
  }
}

} // namespace gpx
