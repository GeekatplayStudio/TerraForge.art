#include "gpx/attribute.hpp"
#include <algorithm>

namespace gpx {

Attribute &AttrSet::add(Attribute a) {
  items.push_back(std::move(a));
  return items.back();
}

Attribute *AttrSet::find(const std::string &key) {
  for (auto &a : items)
    if (a.key == key) return &a;
  return nullptr;
}

const Attribute *AttrSet::find(const std::string &key) const {
  for (auto &a : items)
    if (a.key == key) return &a;
  return nullptr;
}

float AttrSet::get_f(const std::string &k, float def) const {
  auto *a = find(k);
  return a ? a->f : def;
}
int AttrSet::get_i(const std::string &k, int def) const {
  auto *a = find(k);
  return a ? a->i : def;
}
bool AttrSet::get_b(const std::string &k, bool def) const {
  auto *a = find(k);
  return a ? a->b : def;
}
uint32_t AttrSet::get_seed(const std::string &k) const {
  auto *a = find(k);
  return a ? a->seed : 0u;
}
std::string AttrSet::get_s(const std::string &k) const {
  auto *a = find(k);
  return a ? a->s : std::string{};
}
int AttrSet::get_choice(const std::string &k) const {
  auto *a = find(k);
  return a ? a->i : 0;
}
void AttrSet::get_range(const std::string &k, float &lo, float &hi) const {
  auto *a = find(k);
  lo = a ? a->v2[0] : 0.f;
  hi = a ? a->v2[1] : 1.f;
}
void AttrSet::get_vec2(const std::string &k, float &x, float &y) const {
  auto *a = find(k);
  x = a ? a->v2[0] : 0.f;
  y = a ? a->v2[1] : 0.f;
}

static Attribute base(AttrType t, const std::string &key, const std::string &label,
                      const std::string &group) {
  Attribute a;
  a.type = t;
  a.key = key;
  a.label = label;
  a.group = group;
  return a;
}

Attribute &add_float(AttrSet &s, const std::string &key, const std::string &label,
                     float def, float mn, float mx, const std::string &group,
                     bool log_scale) {
  Attribute a = base(AttrType::Float, key, label, group);
  a.f = a.fdefault = def;
  a.fmin = mn;
  a.fmax = mx;
  a.log_scale = log_scale;
  return s.add(a);
}

Attribute &add_int(AttrSet &s, const std::string &key, const std::string &label,
                   int def, int mn, int mx, const std::string &group) {
  Attribute a = base(AttrType::Int, key, label, group);
  a.i = a.idefault = def;
  a.imin = mn;
  a.imax = mx;
  return s.add(a);
}

Attribute &add_bool(AttrSet &s, const std::string &key, const std::string &label,
                    bool def, const std::string &group) {
  Attribute a = base(AttrType::Bool, key, label, group);
  a.b = a.bdefault = def;
  return s.add(a);
}

Attribute &add_seed(AttrSet &s, const std::string &key, const std::string &label,
                    uint32_t def, const std::string &group) {
  Attribute a = base(AttrType::Seed, key, label, group);
  a.seed = def;
  return s.add(a);
}

Attribute &add_choice(AttrSet &s, const std::string &key, const std::string &label,
                      std::vector<std::string> labels, int def,
                      const std::string &group) {
  Attribute a = base(AttrType::Choice, key, label, group);
  a.labels = std::move(labels);
  a.i = a.idefault = def;
  return s.add(a);
}

Attribute &add_range(AttrSet &s, const std::string &key, const std::string &label,
                     float lo, float hi, float mn, float mx, const std::string &group) {
  Attribute a = base(AttrType::Range, key, label, group);
  a.v2[0] = a.v2default[0] = lo;
  a.v2[1] = a.v2default[1] = hi;
  a.v2min = mn;
  a.v2max = mx;
  return s.add(a);
}

Attribute &add_vec2(AttrSet &s, const std::string &key, const std::string &label,
                    float x, float y, float mn, float mx, const std::string &group) {
  Attribute a = base(AttrType::Vec2, key, label, group);
  a.v2[0] = a.v2default[0] = x;
  a.v2[1] = a.v2default[1] = y;
  a.v2min = mn;
  a.v2max = mx;
  return s.add(a);
}

Attribute &add_gradient(AttrSet &s, const std::string &key, const std::string &label,
                        std::vector<GradientStop> stops, const std::string &group) {
  Attribute a = base(AttrType::Gradient, key, label, group);
  a.stops = std::move(stops);
  std::sort(a.stops.begin(), a.stops.end(),
            [](const GradientStop &x, const GradientStop &y) { return x.t < y.t; });
  return s.add(a);
}

Attribute &add_filename(AttrSet &s, const std::string &key, const std::string &label,
                        const std::string &def, const std::string &group) {
  Attribute a = base(AttrType::Filename, key, label, group);
  a.s = def;
  return s.add(a);
}

Attribute &add_text(AttrSet &s, const std::string &key, const std::string &label,
                    const std::string &def, const std::string &group) {
  Attribute a = base(AttrType::Text, key, label, group);
  a.s = def;
  return s.add(a);
}

Attribute &add_field(AttrSet &s, const std::string &key, const std::string &label,
                     int w, int h, float mn, float mx,
                     const std::string &group) {
  Attribute a = base(AttrType::Field, key, label, group);
  a.fw = w > 0 ? w : 0;
  a.fh = h > 0 ? h : 0;
  a.fmin = mn;
  a.fmax = mx;
  // left empty: an unpainted field costs nothing to carry or store
  return s.add(a);
}

} // namespace gpx
