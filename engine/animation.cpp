#include "gpx/animation.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace gpx {

static const float KEY_EPS = 1e-4f; // two keys closer than this are the same key

float Track::sample(float t) const {
  if (keys.empty()) return 0.f;
  if (keys.size() == 1) return keys[0].value;
  // hold at both ends rather than extrapolating: an animation should not run
  // away before its first key or after its last
  if (t <= keys.front().time) return keys.front().value;
  if (t >= keys.back().time) return keys.back().value;

  // keys are kept sorted, so binary search
  auto it = std::lower_bound(keys.begin(), keys.end(), Key{t, 0.f});
  const Key &b = *it;
  const Key &a = *(it - 1);
  float span = b.time - a.time;
  if (span <= KEY_EPS) return b.value;
  float u = (t - a.time) / span;
  switch (interp) {
    case Interp::Constant: return a.value;
    case Interp::Smooth: u = u * u * (3.f - 2.f * u); break;
    case Interp::Linear: break;
  }
  return a.value + (b.value - a.value) * u;
}

bool Track::has_key_at(float t) const {
  for (const Key &k : keys)
    if (std::fabs(k.time - t) <= KEY_EPS) return true;
  return false;
}

bool Track::set_key(float t, float value) {
  for (Key &k : keys)
    if (std::fabs(k.time - t) <= KEY_EPS) {
      k.value = value; // re-keying the same frame replaces, never duplicates
      return false;
    }
  keys.push_back({t, value});
  std::sort(keys.begin(), keys.end());
  return true;
}

bool Track::remove_key(float t) {
  size_t before = keys.size();
  keys.erase(std::remove_if(keys.begin(), keys.end(),
                            [&](const Key &k) {
                              return std::fabs(k.time - t) <= KEY_EPS;
                            }),
             keys.end());
  return keys.size() != before;
}

// "interp;t,v;t,v;..." — compact, human-readable in a project file, and cheap
// to parse. Tracks are small by nature so a text form costs nothing.
std::string track_to_string(const Track &t) {
  if (t.keys.empty()) return "";
  std::ostringstream o;
  o.precision(9);
  o << (int)t.interp;
  for (const Key &k : t.keys) o << ';' << k.time << ',' << k.value;
  return o.str();
}

bool track_from_string(Track &t, const std::string &s) {
  t.keys.clear();
  if (s.empty()) return true;
  std::istringstream in(s);
  std::string tok;
  if (!std::getline(in, tok, ';')) return false;
  int mode = std::atoi(tok.c_str());
  t.interp = (Interp)std::clamp(mode, 0, 2);
  while (std::getline(in, tok, ';')) {
    size_t comma = tok.find(',');
    if (comma == std::string::npos) continue;
    Key k;
    k.time = (float)std::atof(tok.substr(0, comma).c_str());
    k.value = (float)std::atof(tok.substr(comma + 1).c_str());
    t.keys.push_back(k);
  }
  std::sort(t.keys.begin(), t.keys.end());
  return true;
}

} // namespace gpx
