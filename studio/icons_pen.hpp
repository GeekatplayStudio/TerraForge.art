// Geekatplay TerraForge — the pen the icon glyphs are drawn with.
//
// Internal to icons.cpp / icons_glyphs*.cpp. Every glyph is authored in a
// [-1,1] box about the centre and the pen turns that into pixels, snapping
// each point to the pixel grid so a one-pixel stroke at 18 px lands on one
// row of pixels instead of smearing across two. The stroke width follows the
// Cinema 4D palette ladder: 1 px at 18, 2 px at 26, 3 px at 36.
#pragma once
#include "icons.hpp"
#include <cmath>
#include <initializer_list>

namespace studio {

inline constexpr float ICON_PI = 3.14159265f;
// The inset between the icon box and the glyph, in pixels, at every size.
inline constexpr float ICON_PAD = 2.f;

struct Pen {
  ImDrawList *dl;
  ImVec2 c;   // centre, pixels
  float r;    // half the drawable box (size/2 - padding), pixels
  ImU32 col;
  float w;    // stroke width, pixels

  // A whole-pixel stroke is crisp when its centre line sits on a half pixel;
  // an even one when it sits on a pixel boundary.
  float snap(float v) const {
    bool odd = (static_cast<int>(w) & 1) != 0;
    return odd ? std::floor(v) + 0.5f : std::round(v);
  }
  ImVec2 p(float x, float y) const {
    return ImVec2(snap(c.x + x * r), snap(c.y + y * r));
  }
  // Round joins: a dot the width of the stroke where two segments meet, so
  // corners read as one continuous line at 26 and 36 instead of notching.
  void join(ImVec2 at) const {
    if (w >= 2.f) dl->AddCircleFilled(at, w * 0.5f, col, 8);
  }
  void line(float x0, float y0, float x1, float y1) const {
    dl->AddLine(p(x0, y0), p(x1, y1), col, w);
  }
  // A polyline through the given (x,y) pairs, joins rounded.
  void poly(std::initializer_list<float> xy, bool closed = false) const {
    const float *v = xy.begin();
    int n = static_cast<int>(xy.size() / 2);
    if (n < 2) return;
    for (int i = 0; i + 1 < n; ++i)
      dl->AddLine(p(v[i * 2], v[i * 2 + 1]), p(v[i * 2 + 2], v[i * 2 + 3]),
                  col, w);
    if (closed) dl->AddLine(p(v[(n - 1) * 2], v[(n - 1) * 2 + 1]), p(v[0], v[1]), col, w);
    for (int i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i)
      join(p(v[i * 2], v[i * 2 + 1]));
  }
  void rect(float x0, float y0, float x1, float y1, bool filled = false) const {
    if (filled) dl->AddRectFilled(p(x0, y0), p(x1, y1), col);
    else dl->AddRect(p(x0, y0), p(x1, y1), col, 0.f, 0, w);
  }
  void circle(float x, float y, float rad, bool filled = false) const {
    if (filled) dl->AddCircleFilled(p(x, y), rad * r, col, 0);
    else dl->AddCircle(p(x, y), rad * r, col, 0, w);
  }
  void arc(float x, float y, float rad, float a0, float a1) const {
    dl->PathClear();
    dl->PathArcTo(p(x, y), rad * r, a0, a1, 24);
    dl->PathStroke(col, 0, w);
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2) const {
    dl->AddTriangleFilled(p(x0, y0), p(x1, y1), p(x2, y2), col);
  }
  // A small filled dot (a keyframe, a port, a visibility dot).
  void dot(float x, float y, float rad = 0.16f) const { circle(x, y, rad, true); }
  // A filled arrow head with its tip at (x,y), pointing along angle `a`
  // (radians, 0 = +x, y down), `len` long in unit space.
  void head(float x, float y, float a, float len = 0.32f) const {
    float bx = x - std::cos(a) * len, by = y - std::sin(a) * len;
    float nx = -std::sin(a) * len * 0.55f, ny = std::cos(a) * len * 0.55f;
    tri(x, y, bx + nx, by + ny, bx - nx, by - ny);
  }
  // A straight arrow from (x0,y0) to (x1,y1), shaft stopped short of the head.
  void arrow(float x0, float y0, float x1, float y1, float len = 0.32f) const {
    float a = std::atan2(y1 - y0, x1 - x0);
    float sx = x1 - std::cos(a) * len * 0.6f, sy = y1 - std::sin(a) * len * 0.6f;
    line(x0, y0, sx, sy);
    head(x1, y1, a, len);
  }
};

// The glyph painters, one module per family (icons_glyphs.cpp holds the
// tools, objects and viewport; icons_glyphs2.cpp the timeline, deformers and
// the rest). Each returns false for an id it does not own.
bool paint_glyphs_a(const Pen &k, Icon ic);
bool paint_glyphs_b(const Pen &k, Icon ic);

} // namespace studio
