// Geekatplay TerraForge — curve editing operations on a Track: what the
// Timeline's and the Curve editor's tools do to a selection of keys. Every
// operation leaves the track sorted with Auto tangents recomputed, so the
// UI never has to remember to.
#include "gpx/animation.hpp"
#include <algorithm>
#include <cmath>

namespace gpx::anim {

static const float KEY_EPS = 1e-4f;

static void finish(Track &t) {
  std::sort(t.keys.begin(), t.keys.end());
  // merge keys that landed on the same time: the later one in the list wins
  for (size_t i = 1; i < t.keys.size();) {
    if (std::fabs(t.keys[i].time - t.keys[i - 1].time) <= KEY_EPS) t.keys.erase(t.keys.begin() + (long)(i - 1));
    else ++i;
  }
  t.update_tangents();
}

static bool valid(const Track &t, int i) { return i >= 0 && i < (int)t.keys.size(); }

void set_interp(Track &t, const std::vector<int> &sel, Interp i) {
  for (int k : sel) if (valid(t, k)) t.keys[(size_t)k].interp = i;
  t.update_tangents();
}

void set_tangent_mode(Track &t, const std::vector<int> &sel, TangentMode m) {
  for (int k : sel) if (valid(t, k)) t.keys[(size_t)k].tangent = m;
  t.update_tangents();
}

void set_ease(Track &t, const std::vector<int> &sel, Ease e) {
  for (int k : sel) {
    if (!valid(t, k)) continue;
    Key &key = t.keys[(size_t)k];
    switch (e) {
      case Ease::Linear: key.interp = Interp::Linear; key.tangent = TangentMode::Auto; break;
      case Ease::Hold: key.interp = Interp::Constant; break;
      case Ease::Easy: // flat both sides: After Effects' F9
        key.interp = Interp::Bezier; key.tangent = TangentMode::User; key.tan_in = key.tan_out = 0.f; break;
      case Ease::In: // arriving slowly: flat in-tangent
        key.interp = Interp::Bezier; key.tangent = TangentMode::Broken; key.tan_in = 0.f; break;
      case Ease::Out: // leaving slowly: flat out-tangent
        key.interp = Interp::Bezier; key.tangent = TangentMode::Broken; key.tan_out = 0.f; break;
      case Ease::InOut:
        key.interp = Interp::Bezier; key.tangent = TangentMode::User; key.tan_in = key.tan_out = 0.f; break;
    }
  }
  t.update_tangents();
}

static std::vector<int> transform(Track &t, std::vector<int> &sel,
                                  const std::function<void(Key &)> &fn) {
  std::vector<Key> moved;
  std::vector<bool> take(t.keys.size(), false);
  for (int k : sel) if (valid(t, k)) take[(size_t)k] = true;
  std::vector<Key> rest;
  for (size_t i = 0; i < t.keys.size(); ++i) {
    if (take[i]) { Key k = t.keys[i]; fn(k); moved.push_back(k); }
    else rest.push_back(t.keys[i]);
  }
  // moved keys replace any unselected key at the same time
  for (const Key &m : moved)
    rest.erase(std::remove_if(rest.begin(), rest.end(),
                              [&](const Key &r) { return std::fabs(r.time - m.time) <= KEY_EPS; }),
               rest.end());
  t.keys = rest;
  t.keys.insert(t.keys.end(), moved.begin(), moved.end());
  std::sort(t.keys.begin(), t.keys.end(), [](const Key &a, const Key &b) { return a.time < b.time; });
  // pointers are invalid after the sort; re-find by (time, value) instead
  std::vector<int> out;
  for (const Key &m : moved) {
    for (size_t i = 0; i < t.keys.size(); ++i)
      if (std::fabs(t.keys[i].time - m.time) <= KEY_EPS && t.keys[i].value == m.value) { out.push_back((int)i); break; }
  }
  t.update_tangents();
  return out;
}

void move_keys(Track &t, std::vector<int> &sel, float dt, float dv) {
  sel = transform(t, sel, [&](Key &k) { k.time += dt; k.value += dv; });
}

void retime(Track &t, std::vector<int> &sel, float pivot, float factor) {
  if (factor <= 0.f) return;
  sel = transform(t, sel, [&](Key &k) { k.time = pivot + (k.time - pivot) * factor; });
}

void mirror(Track &t, std::vector<int> &sel) {
  float lo = 1e30f, hi = -1e30f;
  for (int k : sel) if (valid(t, k)) { lo = std::min(lo, t.keys[(size_t)k].time); hi = std::max(hi, t.keys[(size_t)k].time); }
  if (lo > hi) return;
  float c = (lo + hi) * 0.5f;
  sel = transform(t, sel, [&](Key &k) {
    k.time = 2.f * c - k.time;
    std::swap(k.tan_in, k.tan_out);
    k.tan_in = -k.tan_in; k.tan_out = -k.tan_out;
  });
}

void smooth(Track &t, const std::vector<int> &sel) {
  for (int k : sel) {
    if (!valid(t, k)) continue;
    Key &key = t.keys[(size_t)k];
    float s = 0.f; int n = 0;
    if (k > 0) { const Key &p = t.keys[(size_t)k - 1]; float sp = key.time - p.time; if (sp > KEY_EPS) { s += (key.value - p.value) / sp; ++n; } }
    if (k + 1 < (int)t.keys.size()) { const Key &q = t.keys[(size_t)k + 1]; float sp = q.time - key.time; if (sp > KEY_EPS) { s += (q.value - key.value) / sp; ++n; } }
    key.interp = Interp::Bezier;
    key.tangent = TangentMode::User;
    key.tan_in = key.tan_out = n ? s / n : 0.f;
  }
  t.update_tangents();
}

void flatten(Track &t, const std::vector<int> &sel) {
  for (int k : sel) {
    if (!valid(t, k)) continue;
    Key &key = t.keys[(size_t)k];
    key.interp = Interp::Bezier;
    key.tangent = TangentMode::User;
    key.tan_in = key.tan_out = 0.f;
  }
  t.update_tangents();
}

void bake(Track &t, float fps) {
  if (t.keys.empty() || fps <= 0.f) return;
  float a = t.first_time(), b = t.last_time();
  std::vector<Key> out;
  int n = (int)std::lround((b - a) * fps);
  for (int i = 0; i <= std::max(n, 0); ++i) {
    Key k;
    k.time = a + (float)i / fps;
    k.value = t.sample(k.time);
    k.interp = Interp::Linear;
    out.push_back(k);
  }
  t.keys = out;
  t.modifiers.clear();
  t.expr.clear();
  finish(t);
}

// Ramer–Douglas–Peucker over the sampled curve: keep the keys that matter.
static void rdp(const std::vector<Key> &pts, size_t lo, size_t hi, float tol, std::vector<bool> &keep) {
  if (hi <= lo + 1) return;
  const Key &a = pts[lo], &b = pts[hi];
  float span = b.time - a.time;
  float best = -1.f; size_t bi = lo;
  for (size_t i = lo + 1; i < hi; ++i) {
    float u = span > KEY_EPS ? (pts[i].time - a.time) / span : 0.f;
    float lin = a.value + (b.value - a.value) * u;
    float d = std::fabs(pts[i].value - lin);
    if (d > best) { best = d; bi = i; }
  }
  if (best > tol) {
    keep[bi] = true;
    rdp(pts, lo, bi, tol, keep);
    rdp(pts, bi, hi, tol, keep);
  }
}

void simplify(Track &t, float tolerance, float fps) {
  if (t.keys.size() < 3 || fps <= 0.f) return;
  Track baked = t;
  bake(baked, fps);
  std::vector<bool> keep(baked.keys.size(), false);
  keep.front() = keep.back() = true;
  rdp(baked.keys, 0, baked.keys.size() - 1, tolerance, keep);
  std::vector<Key> out;
  for (size_t i = 0; i < baked.keys.size(); ++i)
    if (keep[i]) { Key k = baked.keys[i]; k.interp = Interp::Bezier; k.tangent = TangentMode::Auto; out.push_back(k); }
  t.keys = out;
  t.modifiers.clear();
  t.expr.clear();
  finish(t);
}

void snap_to_frames(Track &t, float fps) {
  if (fps <= 0.f) return;
  for (Key &k : t.keys) k.time = std::round(k.time * fps) / fps;
  finish(t);
}

std::vector<Key> copy_keys(const Track &t, const std::vector<int> &sel) {
  std::vector<Key> out;
  float lo = 1e30f;
  for (int k : sel) if (valid(t, k)) { out.push_back(t.keys[(size_t)k]); lo = std::min(lo, t.keys[(size_t)k].time); }
  std::sort(out.begin(), out.end());
  for (Key &k : out) k.time -= lo;
  return out;
}

void paste_keys(Track &t, const std::vector<Key> &keys, float at) {
  for (const Key &src : keys) {
    Key k = src;
    k.time += at;
    t.keys.erase(std::remove_if(t.keys.begin(), t.keys.end(),
                                [&](const Key &r) { return std::fabs(r.time - k.time) <= KEY_EPS; }),
                 t.keys.end());
    t.keys.push_back(k);
  }
  finish(t);
}

} // namespace gpx::anim
