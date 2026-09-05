// Geekatplay TerraForge — track evaluation, tangents, serialization and the
// timeline's time formatting. Curve editing lives in anim_curve.cpp and the
// expression language in anim_expr.cpp.
#include "gpx/animation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace gpx {

static const float KEY_EPS = 1e-4f; // two keys closer than this are the same key

// ------------------------------------------------------------- tangents
void Track::update_tangents() {
  const size_t n = keys.size();
  for (size_t i = 0; i < n; ++i) {
    Key &k = keys[i];
    if (k.tangent != TangentMode::Auto) continue;
    if (n == 1) { k.tan_in = k.tan_out = 0.f; continue; }
    const Key *p = i > 0 ? &keys[i - 1] : nullptr;
    const Key *q = i + 1 < n ? &keys[i + 1] : nullptr;
    float slope;
    if (p && q) {
      float span = q->time - p->time;
      slope = span > KEY_EPS ? (q->value - p->value) / span : 0.f;
      // clamped: a key that is a local extremum, or a key between two equal
      // values, gets a flat tangent so the curve never overshoots its keys
      float dl = k.value - p->value, dr = q->value - k.value;
      if (dl * dr <= 0.f) slope = 0.f;
      else {
        // limit the slope so the segment stays inside the neighbours' range
        float sl = std::fabs(dl) / std::max(k.time - p->time, KEY_EPS) * 3.f;
        float sr = std::fabs(dr) / std::max(q->time - k.time, KEY_EPS) * 3.f;
        float lim = std::min(sl, sr);
        slope = std::clamp(slope, -lim, lim);
      }
    } else if (q) {
      float span = q->time - k.time;
      slope = span > KEY_EPS ? (q->value - k.value) / span : 0.f;
    } else {
      float span = k.time - p->time;
      slope = span > KEY_EPS ? (k.value - p->value) / span : 0.f;
    }
    k.tan_in = k.tan_out = slope;
  }
}

// ------------------------------------------------------------- evaluation
static float segment(const Key &a, const Key &b, float t, Interp mode) {
  float span = b.time - a.time;
  if (span <= KEY_EPS) return b.value;
  float u = (t - a.time) / span;
  switch (mode) {
    case Interp::Constant: return a.value;
    case Interp::Linear: return a.value + (b.value - a.value) * u;
    case Interp::Smooth: u = u * u * (3.f - 2.f * u); return a.value + (b.value - a.value) * u;
    case Interp::Bezier: {
      // cubic Hermite with slopes scaled to the segment
      float m0 = a.tan_out * span, m1 = b.tan_in * span;
      float u2 = u * u, u3 = u2 * u;
      float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u;
      float h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
      return h00 * a.value + h10 * m0 + h01 * b.value + h11 * m1;
    }
    case Interp::Default: break;
  }
  return a.value;
}

float Track::curve(float t) const {
  if (keys.empty()) return 0.f;
  if (keys.size() == 1) return keys[0].value;
  const Key &first = keys.front(), &last = keys.back();
  const float range = last.time - first.time;
  if (t < first.time - KEY_EPS || t > last.time + KEY_EPS) {
    const bool before = t < first.time;
    const Extrapolate mode = before ? pre : post;
    switch (mode) {
      case Extrapolate::Constant: return before ? first.value : last.value;
      case Extrapolate::Linear: {
        if (before) return first.value + (t - first.time) * first.tan_out;
        return last.value + (t - last.time) * last.tan_in;
      }
      case Extrapolate::Cycle:
      case Extrapolate::CycleOffset:
      case Extrapolate::PingPong: {
        if (range <= KEY_EPS) return first.value;
        float rel = t - first.time;
        float cycles = std::floor(rel / range);
        float local = rel - cycles * range;
        if (mode == Extrapolate::PingPong && (std::fmod(std::fabs(cycles), 2.f) >= 1.f))
          local = range - local;
        float v = curve(first.time + local);
        if (mode == Extrapolate::CycleOffset) v += cycles * (last.value - first.value);
        return v;
      }
    }
  }
  if (t <= first.time) return first.value;
  if (t >= last.time) return last.value;
  auto it = std::lower_bound(keys.begin(), keys.end(), Key{t, 0.f});
  if (it != keys.end() && std::fabs(it->time - t) <= KEY_EPS) return it->value;
  return segment(*(it - 1), *it, t, effective(*(it - 1)));
}

static float mod_hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return (float)(x & 0xffffff) / (float)0xffffff;
}
static float mod_noise(float x, uint32_t seed) {
  // value noise, deterministic in (x, seed); smooth between integer lattice
  float i = std::floor(x), f = x - i;
  f = f * f * (3.f - 2.f * f);
  uint32_t a = (uint32_t)(int)i * 2654435761U ^ seed * 0x9e3779b9U;
  uint32_t b = ((uint32_t)(int)i + 1) * 2654435761U ^ seed * 0x9e3779b9U;
  return (mod_hash(a) + (mod_hash(b) - mod_hash(a)) * f) * 2.f - 1.f;
}

static float apply_modifiers(const Track &tr, float t, float v,
                             const std::function<float(float)> &base) {
  for (const Modifier &m : tr.modifiers) {
    if (!m.enabled) continue;
    switch (m.type) {
      case ModType::Noise: {
        float sum = 0.f, amp = 1.f, freq = m.b, norm = 0.f;
        for (int o = 0; o < std::max(1, m.octaves); ++o) {
          sum += mod_noise((t + m.c) * freq, m.seed + (uint32_t)o * 131U) * amp;
          norm += amp;
          amp *= 0.5f;
          freq *= 2.f;
        }
        v += m.a * (norm > 0.f ? sum / norm : 0.f);
      } break;
      case ModType::Oscillator: {
        float ph = (t + m.c) * m.b;
        float w = 0.f;
        if (m.shape == 1) w = 4.f * std::fabs(ph - std::floor(ph + 0.5f)) - 1.f;
        else if (m.shape == 2) w = std::fmod(ph, 1.f) < 0.5f ? 1.f : -1.f;
        else w = std::sin(ph * 6.28318530718f);
        v += m.a * w;
      } break;
      case ModType::Offset: v = base(t + m.b) + m.a; break;
      case ModType::Limit: v = std::clamp(v, std::min(m.a, m.b), std::max(m.a, m.b)); break;
      case ModType::Smooth: {
        const int N = 8;
        float w = std::max(m.a, 0.f);
        if (w <= 0.f) break;
        float acc = 0.f;
        for (int i = 0; i <= N; ++i) acc += base(t - w * 0.5f + w * (float)i / N);
        v = acc / (N + 1);
      } break;
    }
  }
  return v;
}

float Track::sample(float t) const {
  ExprContext ctx;
  ctx.t = t;
  return sample(t, ctx);
}

float Track::sample(float t, const ExprContext &ctx) const {
  auto base = [&](float tt) -> float {
    float v = curve(tt);
    if (!expr.empty()) {
      ExprContext c = ctx;
      c.t = tt;
      c.value = v;
      float e = v;
      if (expr_eval(expr, c, e)) v = e;
    }
    return v;
  };
  float v = base(t);
  if (modifiers.empty()) return v;
  return apply_modifiers(*this, t, v, base);
}

// ------------------------------------------------------------- keys
int Track::index_at(float t) const {
  for (size_t i = 0; i < keys.size(); ++i)
    if (std::fabs(keys[i].time - t) <= KEY_EPS) return (int)i;
  return -1;
}
bool Track::has_key_at(float t) const { return index_at(t) >= 0; }

bool Track::set_key(float t, float value) {
  int i = index_at(t);
  if (i >= 0) {
    keys[(size_t)i].value = value; // re-keying the same frame replaces
    update_tangents();
    return false;
  }
  Key k;
  k.time = t;
  k.value = value;
  keys.push_back(k);
  std::sort(keys.begin(), keys.end());
  update_tangents();
  return true;
}

bool Track::remove_key(float t) {
  size_t before = keys.size();
  keys.erase(std::remove_if(keys.begin(), keys.end(),
                            [&](const Key &k) { return std::fabs(k.time - t) <= KEY_EPS; }),
             keys.end());
  if (keys.size() != before) update_tangents();
  return keys.size() != before;
}

// ------------------------------------------------------------- text form
// v2: "K2;interp;pre;post|t,v,i,m,tin,tout;...|M:type,en,a,b,c,shape,oct,seed;...|E:expr"
// The expression is last and unescaped, so it may contain any character.
std::string track_to_string(const Track &t) {
  if (t.empty() && t.modifiers.empty()) return "";
  std::ostringstream o;
  o.precision(9);
  o << "K2;" << (int)t.interp << ';' << (int)t.pre << ';' << (int)t.post << '|';
  bool first = true;
  for (const Key &k : t.keys) {
    if (!first) o << ';';
    first = false;
    o << k.time << ',' << k.value << ',' << (int)k.interp << ',' << (int)k.tangent << ','
      << k.tan_in << ',' << k.tan_out;
  }
  o << "|M:";
  first = true;
  for (const Modifier &m : t.modifiers) {
    if (!first) o << ';';
    first = false;
    o << (int)m.type << ',' << (m.enabled ? 1 : 0) << ',' << m.a << ',' << m.b << ',' << m.c
      << ',' << m.shape << ',' << m.octaves << ',' << m.seed;
  }
  o << "|E:" << t.expr;
  return o.str();
}

static std::vector<std::string> split(const std::string &s, char c) {
  std::vector<std::string> out;
  std::string tok;
  std::istringstream in(s);
  while (std::getline(in, tok, c)) out.push_back(tok);
  return out;
}

static bool from_string_v1(Track &t, const std::string &s) {
  std::istringstream in(s);
  std::string tok;
  if (!std::getline(in, tok, ';')) return false;
  int mode = std::atoi(tok.c_str());
  t.interp = (Interp)std::clamp(mode, 0, 3);
  while (std::getline(in, tok, ';')) {
    size_t comma = tok.find(',');
    if (comma == std::string::npos) continue;
    Key k;
    k.time = (float)std::atof(tok.substr(0, comma).c_str());
    k.value = (float)std::atof(tok.substr(comma + 1).c_str());
    t.keys.push_back(k);
  }
  std::sort(t.keys.begin(), t.keys.end());
  t.update_tangents();
  return true;
}

bool track_from_string(Track &t, const std::string &s) {
  t = Track{};
  if (s.empty()) return true;
  if (s.rfind("K2;", 0) != 0) return from_string_v1(t, s);
  // sections: header | keys | M:mods | E:expr (expr keeps every later '|')
  size_t p1 = s.find('|');
  size_t p2 = p1 == std::string::npos ? p1 : s.find('|', p1 + 1);
  size_t p3 = p2 == std::string::npos ? p2 : s.find('|', p2 + 1);
  if (p1 == std::string::npos) return false;
  std::vector<std::string> head = split(s.substr(3, p1 - 3), ';');
  if (head.size() >= 1) t.interp = (Interp)std::clamp(std::atoi(head[0].c_str()), 0, 3);
  if (head.size() >= 2) t.pre = (Extrapolate)std::clamp(std::atoi(head[1].c_str()), 0, 4);
  if (head.size() >= 3) t.post = (Extrapolate)std::clamp(std::atoi(head[2].c_str()), 0, 4);
  std::string keys = s.substr(p1 + 1, (p2 == std::string::npos ? s.size() : p2) - p1 - 1);
  for (const std::string &ks : split(keys, ';')) {
    std::vector<std::string> f = split(ks, ',');
    if (f.size() < 2) continue;
    Key k;
    k.time = (float)std::atof(f[0].c_str());
    k.value = (float)std::atof(f[1].c_str());
    if (f.size() > 2) { int iv = std::atoi(f[2].c_str()); k.interp = iv == 255 ? Interp::Default : (Interp)std::clamp(iv, 0, 3); }
    if (f.size() > 3) k.tangent = (TangentMode)std::clamp(std::atoi(f[3].c_str()), 0, 2);
    if (f.size() > 4) k.tan_in = (float)std::atof(f[4].c_str());
    if (f.size() > 5) k.tan_out = (float)std::atof(f[5].c_str());
    t.keys.push_back(k);
  }
  std::sort(t.keys.begin(), t.keys.end());
  if (p2 != std::string::npos) {
    std::string mods = s.substr(p2 + 1, (p3 == std::string::npos ? s.size() : p3) - p2 - 1);
    if (mods.rfind("M:", 0) == 0) mods = mods.substr(2);
    for (const std::string &ms : split(mods, ';')) {
      std::vector<std::string> f = split(ms, ',');
      if (f.size() < 8) continue;
      Modifier m;
      m.type = (ModType)std::clamp(std::atoi(f[0].c_str()), 0, 4);
      m.enabled = std::atoi(f[1].c_str()) != 0;
      m.a = (float)std::atof(f[2].c_str());
      m.b = (float)std::atof(f[3].c_str());
      m.c = (float)std::atof(f[4].c_str());
      m.shape = std::atoi(f[5].c_str());
      m.octaves = std::atoi(f[6].c_str());
      m.seed = (uint32_t)std::strtoul(f[7].c_str(), nullptr, 10);
      t.modifiers.push_back(m);
    }
  }
  if (p3 != std::string::npos) {
    std::string e = s.substr(p3 + 1);
    if (e.rfind("E:", 0) == 0) e = e.substr(2);
    t.expr = e;
  }
  t.update_tangents();
  return true;
}

// ------------------------------------------------------------- timeline
float Timeline::snap_time(float t) const {
  if (!snap || fps <= 0.f) return t;
  return std::round(t * fps) / fps;
}

std::string Timeline::format(float t) const {
  char buf[48];
  switch (display) {
    case TimeDisplay::Frames:
      snprintf(buf, sizeof buf, "%d", (int)std::lround(t * fps));
      break;
    case TimeDisplay::Timecode: {
      long frames = std::lround(t * fps);
      long f = (long)fps > 0 ? frames % (long)fps : 0;
      long total_s = (long)fps > 0 ? frames / (long)fps : 0;
      snprintf(buf, sizeof buf, "%s%02ld:%02ld:%02ld:%02ld", frames < 0 ? "-" : "",
               std::labs(total_s) / 3600, (std::labs(total_s) / 60) % 60, std::labs(total_s) % 60,
               std::labs(f));
    } break;
    case TimeDisplay::Seconds: snprintf(buf, sizeof buf, "%.3f s", t); break;
  }
  return buf;
}

bool Timeline::parse(const std::string &s, float &t) const {
  if (s.empty()) return false;
  if (s.find(':') != std::string::npos) {
    int h = 0, m = 0, sec = 0, f = 0;
    int n = sscanf(s.c_str(), "%d:%d:%d:%d", &h, &m, &sec, &f);
    if (n < 2) return false;
    if (n == 2) { f = m; m = 0; sec = h; h = 0; }
    else if (n == 3) { f = sec; sec = m; m = h; h = 0; }
    t = ((h * 3600 + m * 60 + sec) * fps + f) / fps;
    return true;
  }
  char *end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str()) return false;
  std::string rest = end;
  bool seconds = rest.find('s') != std::string::npos || display == TimeDisplay::Seconds;
  t = seconds ? (float)v : (float)v / fps;
  return true;
}

} // namespace gpx
