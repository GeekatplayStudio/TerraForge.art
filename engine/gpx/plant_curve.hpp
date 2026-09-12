// Geekatplay TerraForge - a curve parameter: a hand-drawn function of one
// number, the way a plant tool's "filter" is (a radius that thins toward
// the tip, a density that gathers branches near the crown, a season that
// turns the leaves).
//
// Keys are sorted by x. Between keys the curve is either straight (linear)
// or a cubic Hermite with a slope on each side of every key (smooth; a
// broken joint has two slopes). Outside the first and last key it holds
// their values. The domain and range are recorded so an editor can draw the
// axes, but eval() clamps nothing: a curve used as a multiplier may go above
// 1 on purpose.
//
// A curve attribute may carry several alternatives with weights: one is
// chosen per plant seed (a "multicurve"), so two individuals of a species
// can differ in kind, not only in degree. `curves[0]` is the one an editor
// shows; `pick()` is what an evaluator calls.
//
// Engine-only, no GL, no JSON here (serialization.cpp writes it).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpx {

struct CurveKey {
  float x = 0.f, y = 0.f;
  float sl = 0.f, sr = 0.f; // slopes on the left and right of the key (smooth)
};

struct Curve {
  std::vector<CurveKey> keys;
  int interp = 1; // 0 linear, 1 smooth (Hermite with per-key slopes)
  float xmin = 0.f, xmax = 1.f, ymin = 0.f, ymax = 1.f;

  // A constant curve: two keys at the ends, both `y`.
  static Curve constant(float y, float x0 = 0.f, float x1 = 1.f);
  // A straight line from (x0,y0) to (x1,y1).
  static Curve line(float y0, float y1, float x0 = 0.f, float x1 = 1.f);
  // A smooth curve through the points given as x,y pairs; slopes chosen so
  // the curve does not overshoot (Fritsch-Carlson).
  static Curve through(std::initializer_list<float> xy);

  float eval(float x) const;
  bool empty() const { return keys.empty(); }
  // Keep keys sorted by x and unique in x (later key wins).
  void normalise();
  // Choose slopes automatically for every key (monotone-preserving).
  void auto_slopes();
  bool operator==(const Curve &o) const;
};

// A curve or a weighted set of alternatives (one chosen per plant).
struct CurveSet {
  std::vector<Curve> curves;   // never empty once used; [0] is the primary
  std::vector<float> weights;  // same length as curves; missing = 1
  const Curve &primary() const;
  // The alternative a plant with this seed draws, by weight. `salt` lets two
  // parameters of one node pick independently.
  const Curve &pick(uint32_t seed, uint32_t salt = 0) const;
  float eval(float x, uint32_t seed = 0, uint32_t salt = 0) const {
    return pick(seed, salt).eval(x);
  }
  bool empty() const { return curves.empty() || curves[0].empty(); }
};

// Text form: "i;x,y,sl,sr;x,y,sl,sr;..." with the domain first
// ("d:xmin,xmax,ymin,ymax|i;..."). Used by serialization, by the AI ops
// (a curve typed as text) and by the tests.
std::string curve_to_string(const Curve &c);
bool curve_from_string(const std::string &s, Curve &out);

} // namespace gpx
