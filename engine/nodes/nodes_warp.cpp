// Geekatplay Studio — transform / warp nodes
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"

namespace gpx {

REGISTER_NODE(
    WarpNoise, "Transform", "Domain warp by internal fBm noise",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_seed(n.attrs);
      add_float(n.attrs, "amplitude", "Amplitude", 0.08f, 0.f, 0.5f);
      add_vec2(n.attrs, "kw", "Warp frequency", 3.f, 3.f, 0.2f, 32.f);
      add_int(n.attrs, "octaves", "Octaves", 4, 1, 10);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float amp = n.attrs.get_f("amplitude", 0.08f);
      float kx, ky;
      n.attrs.get_vec2("kw", kx, ky);
      uint32_t seed = n.attrs.get_seed("seed");
      noise::FbmParams p;
      p.octaves = n.attrs.get_i("octaves", 4);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float wx = noise::fbm(u * kx, v * ky, seed, p);
            float wy = noise::fbm(u * kx + 31.7f, v * ky + 47.3f, seed, p);
            out.at(x, y) = in->sample(std::clamp(u + wx * amp, 0.f, 1.f),
                                      std::clamp(v + wy * amp, 0.f, 1.f));
          }
      });
    })

REGISTER_NODE(
    WarpDirectional, "Transform", "Warp along gradient — wind-swept shapes",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "angle", "Direction °", 30.f, -180.f, 180.f);
      add_float(n.attrs, "amplitude", "Amplitude", 0.02f, 0.f, 0.2f);
      add_bool(n.attrs, "by_height", "Scale by height", true);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float a = n.attrs.get_f("angle", 30.f) * 0.017453293f;
      float dx = std::cos(a), dy = std::sin(a);
      float amp = n.attrs.get_f("amplitude", 0.02f);
      bool byh = n.attrs.get_b("by_height", true);
      float mn, mx;
      in->minmax(mn, mx);
      float d = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w), v = y / float(out.h);
            float s = byh ? (in->at(x, y) - mn) / d : 1.f;
            out.at(x, y) = in->sample(std::clamp(u - dx * amp * s, 0.f, 1.f),
                                      std::clamp(v - dy * amp * s, 0.f, 1.f));
          }
      });
    })

REGISTER_NODE(
    Transform, "Transform", "Translate / scale / rotate",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_vec2(n.attrs, "translate", "Translate", 0.f, 0.f, -1.f, 1.f);
      add_vec2(n.attrs, "scale", "Scale", 1.f, 1.f, 0.1f, 8.f);
      add_float(n.attrs, "angle", "Rotate °", 0.f, -180.f, 180.f);
      add_choice(n.attrs, "extend", "Outside area", {"Clamp", "Mirror", "Tile"}, 0);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      float tx, ty, sx, sy;
      n.attrs.get_vec2("translate", tx, ty);
      n.attrs.get_vec2("scale", sx, sy);
      float a = n.attrs.get_f("angle") * 0.017453293f;
      float ca = std::cos(a), sa = std::sin(a);
      int extend = n.attrs.get_choice("extend");
      auto wrap = [extend](float t) {
        if (extend == 0) return std::clamp(t, 0.f, 1.f);
        if (extend == 2) return t - std::floor(t); // tile
        // mirror
        t = std::fabs(t);
        float f = std::fmod(t, 2.f);
        return f > 1.f ? 2.f - f : f;
      };
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w) - 0.5f, v = y / float(out.h) - 0.5f;
            float ru = (u * ca - v * sa) / sx + 0.5f - tx;
            float rv = (u * sa + v * ca) / sy + 0.5f - ty;
            out.at(x, y) = in->sample(wrap(ru), wrap(rv));
          }
      });
    })

} // namespace gpx
