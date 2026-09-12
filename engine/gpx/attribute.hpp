// Geekatplay Studio — declarative node parameters.
// A node declares attributes in its setup fn; the properties panel renders
// them automatically and serialization walks the same list.
#pragma once
#include "gpx/animation.hpp"
#include "gpx/plant_curve.hpp"
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
  Field,   // painted 2D scalar buffer (sculpt strokes, hand-drawn masks)
  // A hand-drawn function of one number (gpx/plant_curve.hpp): a radius
  // along a branch, a density along a stem, a colour over the seasons.
  // `curves` holds it, and may hold weighted alternatives one of which is
  // chosen per plant seed.
  Curve,
  // A number with a random spread: `f` is the value, `spread` how far a draw
  // may stray (absolute, a fraction, or a gaussian deviation per
  // `spread_mode`), `scope` when a new draw is made, and two curves that
  // scale the result along the primitive (`curve_along`, x = 0 at its base
  // and 1 at its tip) and by where the primitive sits on its parent
  // (`curve_hier`, x = 0 at the parent's base and 1 at its tip). Every
  // number a plant part is described by is one of these.
  Random
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
  // Text only: this holds the name of a scene object, so the studio offers
  // the scene's objects instead of a field to spell one into. A hint about
  // presentation, the way log_scale is - the engine still has no idea what a
  // scene is, and the value is a string either way.
  bool object_ref = false;
  // Curve: the function, with any alternatives and their weights.
  CurveSet curves;
  // Random: see AttrType::Random. Defaults are a plain number.
  float spread = 0.f;
  int spread_mode = 0;  // 0 absolute, 1 relative (fraction of the value), 2 gaussian
  int scope = 1;        // 0 each time, 1 per primitive, 2 per plant, 3 per ancestor
  int hier_level = 1;   // curve_hier: how many levels up the parent it reads (1 = parent)
  bool hier_cascade = false; // curve_hier: multiply across every level up to hier_level
  CurveSet curve_along, curve_hier;
  // Published: shown on the species' preset sheet under this name and group
  // (plant nodes); `external` lets a scene's population dial it.
  bool published = false;
  bool external = false;
  std::string pub_name, pub_group;
  // Text only: this holds the id of another node in the same graph, decimal.
  // Loading a project renumbers every node, so a reference written as a bare
  // id would land on whatever node inherited that number - which is not an
  // error the reader could ever notice, because the wrong node is a perfectly
  // valid one. Flagged here, graph_from_json remaps it with the links.
  bool node_ref = false;
  // Field: a buffer the user paints into rather than types. Kept as plain
  // floats in memory and quantized + compressed on the way to disk.
  std::vector<float> field;
  int fw = 0, fh = 0;
  // Animation. Every parameter can carry a track; an un-animated one costs an
  // empty vector. The hook lives here rather than in a side table so that
  // copying, serializing, undoing and publishing a parameter all carry its
  // animation with it automatically.
  Track anim;
  // Vec2 / Range / Color: one track per component, allocated on first key
  // (index 0..3). Scalars keep using `anim`.
  std::vector<Track> anim_v;
  bool animated() const {
    if (!anim.empty()) return true;
    for (const Track &t : anim_v) if (!t.empty()) return true;
    return false;
  }
  Track &anim_comp(int c) { if ((int)anim_v.size() <= c) anim_v.resize((size_t)c + 1); return anim_v[(size_t)c]; }
};

// Ordered attribute container; preserves declaration order for UI.
class AttrSet {
public:
  Attribute &add(Attribute a);
  Attribute *find(const std::string &key);
  const Attribute *find(const std::string &key) const;
  // Drop one a shared setup helper added that this node supersedes. Rare and
  // deliberate: showing somebody two controls for the same thing, one of
  // which does nothing, is worse than showing them one.
  bool remove(const std::string &key);
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
  // Curve: the primary curve, or a constant 1 when the key is unknown.
  const Curve &get_curve(const std::string &k) const;
  float eval_curve(const std::string &k, float x, uint32_t seed = 0) const;
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
// A curve: `c` is its default shape (Curve::constant(1) for a multiplier).
Attribute &add_curve(AttrSet &s, const std::string &key, const std::string &label,
                     const Curve &c, const std::string &group = "");
// A random number: value `def` within `mn..mx`, straying `spread` (absolute
// units by default; see Attribute::spread_mode). The two shaping curves start
// as constant 1.
Attribute &add_random(AttrSet &s, const std::string &key, const std::string &label,
                      float def, float mn, float mx, float spread = 0.f,
                      const std::string &group = "", bool log_scale = false);
// A painted buffer. Starts empty (all zero) and is filled in by brush strokes;
// `mn`/`mx` bound what a stroke may write.
Attribute &add_field(AttrSet &s, const std::string &key, const std::string &label,
                     int w, int h, float mn, float mx,
                     const std::string &group = "");

} // namespace gpx

