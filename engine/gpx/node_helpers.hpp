// Geekatplay Studio — shared node setup/compute helpers.
// setup_post(node) appends the standard output block (remap / invert /
// clamp) to a node; apply_post(node, h) applies it. One line per node,
// consistent everywhere.
#pragma once
#include "gpx/node_graph.hpp"
#include "gpx/parallel.hpp"

namespace gpx {

inline void setup_post(Node &n) {
  add_bool(n.attrs, "post_remap", "Remap to range", true, "Output");
  add_range(n.attrs, "post_range", "Output range", 0.f, 1.f, -1.f, 2.f, "Output");
  add_bool(n.attrs, "post_invert", "Invert", false, "Output");
  add_float(n.attrs, "post_gain", "Gain (gamma)", 1.f, 0.05f, 4.f, "Output");
  add_float(n.attrs, "post_zero_edges", "Zero edges width", 0.f, 0.f, 0.5f,
            "Output")
      .tooltip = "Fades the terrain to zero at the borders over this\n"
                 "fraction of the map — clean edges for islands/tiles.";
}

inline void apply_zero_edges(Heightmap &h, float width) {
  if (width <= 1e-6f) return;
  float mn, mx;
  h.minmax(mn, mx);
  parallel_rows(h.h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < h.w; ++x) {
        float u = x / float(h.w - 1), v = y / float(h.h - 1);
        float b = std::min(std::min(u, 1.f - u), std::min(v, 1.f - v)) / width;
        b = std::clamp(b, 0.f, 1.f);
        b = b * b * (3.f - 2.f * b);
        h.at(x, y) = mn + (h.at(x, y) - mn) * b;
      }
  });
}

inline void apply_post(Node &n, Heightmap &h) {
  if (n.attrs.get_b("post_remap", true)) {
    float lo, hi;
    n.attrs.get_range("post_range", lo, hi);
    h.remap(lo, hi);
  }
  bool inv = n.attrs.get_b("post_invert");
  float gain = n.attrs.get_f("post_gain", 1.f);
  if (inv || gain != 1.f) {
    float mn, mx;
    h.minmax(mn, mx);
    float d = (mx - mn) > 1e-12f ? (mx - mn) : 1.f;
    parallel_index(h.v.size(), [&](size_t i0, size_t i1) {
      for (size_t i = i0; i < i1; ++i) {
        float t = (h.v[i] - mn) / d;
        if (inv) t = 1.f - t;
        if (gain != 1.f) t = std::pow(std::max(t, 0.f), gain);
        h.v[i] = mn + t * d;
      }
    });
  }
  apply_zero_edges(h, n.attrs.get_f("post_zero_edges", 0.f));
}

// standard coordinate attrs for primitives
inline void setup_coords(Node &n) {
  add_vec2(n.attrs, "kw", "Wavenumber", 4.f, 4.f, 0.1f, 64.f, "Coordinates");
  add_vec2(n.attrs, "offset", "Offset", 0.f, 0.f, -16.f, 16.f, "Coordinates");
  add_float(n.attrs, "angle", "Rotation °", 0.f, -180.f, 180.f, "Coordinates");
}

struct CoordMap {
  float kx, ky, ox, oy, ca, sa;
  void from(const Node &n) {
    n.attrs.get_vec2("kw", kx, ky);
    n.attrs.get_vec2("offset", ox, oy);
    float a = n.attrs.get_f("angle") * 0.017453293f;
    ca = std::cos(a);
    sa = std::sin(a);
  }
  // texel -> noise-space coords
  inline void map(int x, int y, int w, int h, float &nx, float &ny) const {
    float u = x / float(w), v = y / float(h);
    float ru = u * ca - v * sa, rv = u * sa + v * ca;
    nx = ru * kx + ox;
    ny = rv * ky + oy;
  }
};

// optional envelope input: multiply output by a mask if connected
inline void apply_mask_blend(const Heightmap *mask, const Heightmap &original,
                             Heightmap &modified) {
  if (!mask || mask->empty()) return;
  parallel_index(modified.v.size(), [&](size_t i0, size_t i1) {
    for (size_t i = i0; i < i1; ++i) {
      float m = std::clamp(mask->v[i], 0.f, 1.f);
      modified.v[i] = original.v[i] * (1.f - m) + modified.v[i] * m;
    }
  });
}

// require a connected input or record an error; returns null on failure
inline const Heightmap *require_in(Node &n, const char *port) {
  const Heightmap *in = n.in_hmap(port);
  if (!in || in->empty()) n.error = std::string("input '") + port + "' not connected";
  return (in && !in->empty()) ? in : nullptr;
}

} // namespace gpx
