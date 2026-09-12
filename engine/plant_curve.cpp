// Geekatplay TerraForge - the curve parameter (gpx/plant_curve.hpp).
#include "gpx/plant_curve.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace gpx {

Curve Curve::constant(float y, float x0, float x1) {
  Curve c;
  c.keys = {{x0, y, 0.f, 0.f}, {x1, y, 0.f, 0.f}};
  c.xmin = x0;
  c.xmax = x1;
  c.ymin = std::min(0.f, y);
  c.ymax = std::max(1.f, y);
  return c;
}

Curve Curve::line(float y0, float y1, float x0, float x1) {
  Curve c;
  const float s = (x1 - x0) != 0.f ? (y1 - y0) / (x1 - x0) : 0.f;
  c.keys = {{x0, y0, s, s}, {x1, y1, s, s}};
  c.xmin = x0;
  c.xmax = x1;
  c.ymin = std::min({0.f, y0, y1});
  c.ymax = std::max({1.f, y0, y1});
  return c;
}

Curve Curve::through(std::initializer_list<float> xy) {
  Curve c;
  const float *p = xy.begin();
  for (size_t i = 0; i + 1 < xy.size(); i += 2) c.keys.push_back({p[i], p[i + 1], 0.f, 0.f});
  c.normalise();
  c.auto_slopes();
  if (!c.keys.empty()) {
    c.xmin = c.keys.front().x;
    c.xmax = c.keys.back().x;
    c.ymin = 0.f;
    c.ymax = 1.f;
    for (const CurveKey &k : c.keys) {
      c.ymin = std::min(c.ymin, k.y);
      c.ymax = std::max(c.ymax, k.y);
    }
  }
  return c;
}

void Curve::normalise() {
  std::stable_sort(keys.begin(), keys.end(),
                   [](const CurveKey &a, const CurveKey &b) { return a.x < b.x; });
  std::vector<CurveKey> out;
  for (const CurveKey &k : keys) {
    if (!out.empty() && std::fabs(out.back().x - k.x) < 1e-7f) out.back() = k;
    else out.push_back(k);
  }
  keys.swap(out);
}

void Curve::auto_slopes() {
  const size_t n = keys.size();
  if (n == 0) return;
  if (n == 1) {
    keys[0].sl = keys[0].sr = 0.f;
    return;
  }
  std::vector<float> d(n - 1);
  for (size_t i = 0; i + 1 < n; ++i) {
    const float dx = keys[i + 1].x - keys[i].x;
    d[i] = dx > 1e-9f ? (keys[i + 1].y - keys[i].y) / dx : 0.f;
  }
  for (size_t i = 0; i < n; ++i) {
    float m;
    if (i == 0) m = d[0];
    else if (i + 1 == n) m = d[n - 2];
    else if (d[i - 1] * d[i] <= 0.f) m = 0.f;
    else m = 0.5f * (d[i - 1] + d[i]);
    keys[i].sl = keys[i].sr = m;
  }
  // Fritsch-Carlson: keep the interpolant monotone between keys
  for (size_t i = 0; i + 1 < n; ++i) {
    if (std::fabs(d[i]) < 1e-12f) {
      keys[i].sr = 0.f;
      keys[i + 1].sl = 0.f;
      continue;
    }
    const float a = keys[i].sr / d[i], b = keys[i + 1].sl / d[i];
    const float s = a * a + b * b;
    if (s > 9.f) {
      const float t = 3.f / std::sqrt(s);
      keys[i].sr = t * a * d[i];
      keys[i + 1].sl = t * b * d[i];
    }
  }
}

float Curve::eval(float x) const {
  const size_t n = keys.size();
  if (n == 0) return 0.f;
  if (n == 1 || x <= keys.front().x) return keys.front().y;
  if (x >= keys.back().x) return keys.back().y;
  size_t i = 0;
  while (i + 2 < n && keys[i + 1].x <= x) ++i;
  const CurveKey &a = keys[i], &b = keys[i + 1];
  const float h = b.x - a.x;
  if (h <= 1e-9f) return b.y;
  const float t = (x - a.x) / h;
  if (interp == 0) return a.y + (b.y - a.y) * t;
  const float t2 = t * t, t3 = t2 * t;
  const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
  const float h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
  return h00 * a.y + h10 * h * a.sr + h01 * b.y + h11 * h * b.sl;
}

bool Curve::operator==(const Curve &o) const {
  if (interp != o.interp || keys.size() != o.keys.size()) return false;
  for (size_t i = 0; i < keys.size(); ++i)
    if (keys[i].x != o.keys[i].x || keys[i].y != o.keys[i].y || keys[i].sl != o.keys[i].sl ||
        keys[i].sr != o.keys[i].sr)
      return false;
  return true;
}

// ---------------------------------------------------------------- CurveSet

const Curve &CurveSet::primary() const {
  static const Curve none = Curve::constant(1.f);
  return curves.empty() ? none : curves[0];
}

const Curve &CurveSet::pick(uint32_t seed, uint32_t salt) const {
  if (curves.size() <= 1) return primary();
  uint32_t h = seed * 747796405u + salt * 2891336453u + 0x9E3779B9u;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  float total = 0.f;
  for (size_t i = 0; i < curves.size(); ++i)
    total += i < weights.size() ? std::max(weights[i], 0.f) : 1.f;
  if (total <= 0.f) return curves[h % curves.size()];
  float r = (float)(h & 0xffffff) / (float)0x1000000 * total;
  for (size_t i = 0; i < curves.size(); ++i) {
    const float w = i < weights.size() ? std::max(weights[i], 0.f) : 1.f;
    if (r < w) return curves[i];
    r -= w;
  }
  return curves.back();
}

// ------------------------------------------------------------------- text

std::string curve_to_string(const Curve &c) {
  char buf[128];
  std::string s;
  std::snprintf(buf, sizeof buf, "d:%g,%g,%g,%g|%d", c.xmin, c.xmax, c.ymin, c.ymax, c.interp);
  s += buf;
  for (const CurveKey &k : c.keys) {
    std::snprintf(buf, sizeof buf, ";%g,%g,%g,%g", k.x, k.y, k.sl, k.sr);
    s += buf;
  }
  return s;
}

bool curve_from_string(const std::string &str, Curve &out) {
  Curve c;
  std::string s = str;
  if (s.rfind("d:", 0) == 0) {
    const size_t bar = s.find('|');
    if (bar == std::string::npos) return false;
    if (std::sscanf(s.c_str() + 2, "%f,%f,%f,%f", &c.xmin, &c.xmax, &c.ymin, &c.ymax) != 4) return false;
    s = s.substr(bar + 1);
  }
  std::stringstream ss(s);
  std::string part;
  bool first = true;
  while (std::getline(ss, part, ';')) {
    if (first) {
      first = false;
      c.interp = std::atoi(part.c_str());
      continue;
    }
    CurveKey k;
    const int got = std::sscanf(part.c_str(), "%f,%f,%f,%f", &k.x, &k.y, &k.sl, &k.sr);
    if (got < 2) return false;
    c.keys.push_back(k);
  }
  c.normalise();
  out = c;
  return true;
}

} // namespace gpx
