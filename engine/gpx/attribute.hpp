// Geekatplay Studio — declarative node parameters.
// A node declares attributes in its setup fn; the properties panel renders
// them automatically and serialization walks the same list.
#pragma once
#include "gpx/animation.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gpx {

enum class AttrType {
  Float,   // slider, min..max
  Int,     // slider
  Bool,    // checkbox
  Choice,  // combo from labels
  Seed,    // int + dice button
  Range,   // vec2 dual slider
  Vec2,    // x/y pair
  Color,   // rgba
  Gradient,// color gradient stops
  Filename,// path + browse
  Text,    // string
  Field    // painted 2D scalar buffer (sculpt strokes, hand-drawn masks)
};

struct GradientStop {
  float t = 0.f;
  float r = 0, g = 0, b = 0, a = 1;
};

struct Attribute {
  AttrType type = AttrType::Float;
  std::string key, label, group; // group -> collapsible section
  std::string tooltip;

  float f = 0, fmin = 0, fmax = 1, fdefault = 0;
  int i = 0, imin = 0, imax = 10, idefault = 0;
  bool b = false, bdefault = false;
  uint32_t seed = 0;
  float v2[2] = {0, 1}, v2default[2] = {0, 1};
  float v2min = 0, v2max = 1;
  float col[4] = {1, 1, 1, 1};
  std::vector<GradientStop> stops;
  std::vector<std::string> labels; // Choice
  std::string s;                   // Filename / Text / Choice value
  bool log_scale = false;
  // Field: a buffer the user paints into rather than types. Kept as plain
  // floats in memory and quantized + compressed on the way to disk.
  std::vector<float> field;
  int fw = 0, fh = 0;
  // Animation. Every parameter can carry a track; an un-animated one costs an
  // empty vector. The hook lives here rather than in a side table so that
  // copying, serializing, undoing and publishing a parameter all carry its
  // animation with it automatically.
  Track anim;
  bool animated() const { return !anim.empty(); }
};

// Ordered attribute container; preserves declaration order for UI.
class AttrSet {
public:
  Attribute &add(Attribute a);
  Attribute *find(const std::string &key);
  const Attribute *find(const std::string &key) const;
  std::vector<Attribute> items;

  // typed getters (assert-free, tolerant)
  float get_f(const std::string &k, float def = 0) const;
  int get_i(const std::string &k, int def = 0) const;
  bool get_b(const std::string &k, bool def = false) const;
  uint32_t get_seed(const std::string &k) const;
  std::string get_s(const std::string &k) const;
  int get_choice(const std::string &k) const; // index into labels
  void get_range(const std::string &k, float &lo, float &hi) const;
  void get_vec2(const std::string &k, float &x, float &y) const;
};

// one-line builders used by node setup functions -----------------------------
Attribute &add_float(AttrSet &s, const std::string &key, const std::string &label,
                     float def, float mn, float mx, const std::string &group = "",
                     bool log_scale = false);
Attribute &add_int(AttrSet &s, const std::string &key, const std::string &label,
                   int def, int mn, int mx, const std::string &group = "");
Attribute &add_bool(AttrSet &s, const std::string &key, const std::string &label,
                    bool def, const std::string &group = "");
Attribute &add_seed(AttrSet &s, const std::string &key = "seed",
                    const std::string &label = "Seed", uint32_t def = 0,
                    const std::string &group = "");
Attribute &add_choice(AttrSet &s, const std::string &key, const std::string &label,
                      std::vector<std::string> labels, int def = 0,
                      const std::string &group = "");
Attribute &add_range(AttrSet &s, const std::string &key, const std::string &label,
                     float lo, float hi, float mn, float mx,
                     const std::string &group = "");
Attribute &add_vec2(AttrSet &s, const std::string &key, const std::string &label,
                    float x, float y, float mn, float mx,
                    const std::string &group = "");
Attribute &add_color(AttrSet &s, const std::string &key, const std::string &label,
                     float r, float g, float b, float a = 1.f,
                     const std::string &group = "");
Attribute &add_gradient(AttrSet &s, const std::string &key, const std::string &label,
                        std::vector<GradientStop> stops,
                        const std::string &group = "");
Attribute &add_filename(AttrSet &s, const std::string &key, const std::string &label,
                        const std::string &def, const std::string &group = "");
Attribute &add_text(AttrSet &s, const std::string &key, const std::string &label,
                    const std::string &def, const std::string &group = "");
// A painted buffer. Starts empty (all zero) and is filled in by brush strokes;
// `mn`/`mx` bound what a stroke may write.
Attribute &add_field(AttrSet &s, const std::string &key, const std::string &label,
                     int w, int h, float mn, float mx,
                     const std::string &group = "");

} // namespace gpx

