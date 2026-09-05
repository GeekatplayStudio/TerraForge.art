// Geekatplay TerraForge - Vue's Natural Grain colouring mode (manual p712):
// one or two colours varied by a noise, with the handful of controls an
// artist actually reaches for on ground and rock - scale, roughness,
// contrast, balance, distortion - instead of a full fractal editor.
//
// A node rather than a mode on FractalColor so the studio can show it as
// Vue does: pick the colours, drag five sliders, done. The alpha grain out
// is the same pattern as a mask, for a layer that wants its presence to
// follow its colour.
#include <algorithm>
#include "gpx/noise_core.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {
// a plain fBm on the engine's Perlin, 0..1
float fbm2(float x, float y, int octaves, float gain, uint32_t seed) {
  float sum = 0.f, amp = 0.5f, norm = 0.f;
  for (int o = 0; o < octaves; ++o) {
    sum += (noise::perlin(x, y, seed + (uint32_t)o * 7919u) * 0.5f + 0.5f) * amp;
    norm += amp;
    amp *= gain;
    x *= 2.03f;
    y *= 2.03f;
  }
  return norm > 0.f ? sum / norm : 0.5f;
}
} // namespace

REGISTER_NODE(
    NaturalGrain, "Material",
    "Natural grain: one or two colours varied by a noise, for ground and rock",
    [](Node &n) {
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("texture", DataType::Texture);
      n.add_out("grain", DataType::Heightmap);
      add_color(n.attrs, "color1", "Base color", 0.42f, 0.36f, 0.28f, 1.f, "Colors");
      add_bool(n.attrs, "use_color2", "Mix with a second color", true, "Colors");
      add_color(n.attrs, "color2", "Second color", 0.55f, 0.5f, 0.42f, 1.f, "Colors");
      add_float(n.attrs, "scale", "Scale", 0.2f, 0.005f, 4.f, "Grain", true)
          .tooltip = "The overall size of the grain. Keep it large for a "
                     "terrain, small for a pebble.";
      add_float(n.attrs, "roughness", "Roughness", 0.6f, 0.f, 1.f, "Grain")
          .tooltip = "How much fine detail rides on the large variation.";
      add_float(n.attrs, "contrast", "Contrast", 0.5f, 0.f, 1.f, "Grain");
      add_float(n.attrs, "balance", "Balance", 0.5f, 0.f, 1.f, "Grain")
          .tooltip = "Which of the two colours dominates.";
      add_float(n.attrs, "distortion", "Distortion", 0.f, 0.f, 1.f, "Grain")
          .tooltip = "Warps the grain so it stops looking like a noise.";
      add_seed(n.attrs, "seed", "Seed", 0, "Grain");
    },
    [](Node &n) {
      TextureRGBA &out = n.out_tex("texture");
      Heightmap &grain = n.out_hmap("grain");
      const Heightmap *mask = n.in_hmap("mask");
      if (mask && mask->empty()) mask = nullptr;
      const Attribute *c1 = n.attrs.find("color1");
      const Attribute *c2 = n.attrs.find("color2");
      const bool two = n.attrs.get_b("use_color2", true) && c2;
      const float scale = std::max(n.attrs.get_f("scale", 0.2f), 1e-3f);
      const float rough = n.attrs.get_f("roughness", 0.6f);
      const float contrast = n.attrs.get_f("contrast", 0.5f);
      const float balance = n.attrs.get_f("balance", 0.5f);
      const float distortion = n.attrs.get_f("distortion", 0.f);
      const uint32_t seed = n.attrs.get_seed("seed");
      const int octaves = 2 + (int)std::lround(rough * 6.f);
      const float gain = 0.35f + rough * 0.3f;
      // contrast steepens the curve through the balance point; balance moves
      // the midpoint, so 0.5 is an even mix and 1 is nearly all the second
      const float k = 1.f + contrast * 7.f;
      const float mid = 1.f - balance;
      grain = Heightmap(out.w, out.h);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = (x + 0.5f) / out.w / scale, v = (y + 0.5f) / out.h / scale;
            if (distortion > 0.f) {
              float wx = fbm2(u * 0.5f + 13.1f, v * 0.5f + 7.7f, 3, 0.5f, seed + 1u);
              float wy = fbm2(u * 0.5f + 3.3f, v * 0.5f + 21.9f, 3, 0.5f, seed + 2u);
              u += (wx - 0.5f) * distortion * 2.f;
              v += (wy - 0.5f) * distortion * 2.f;
            }
            float g = fbm2(u, v, octaves, gain, seed);
            // a logistic through the balance point, so contrast and balance
            // read independently
            float t = 1.f / (1.f + std::exp(-(g - mid) * k));
            if (mask) t *= std::clamp(mask->sample((x + 0.5f) / out.w, (y + 0.5f) / out.h), 0.f, 1.f);
            grain.at(x, y) = t;
            float *p = out.px(x, y);
            for (int ch = 0; ch < 3; ++ch) {
              float a = c1 ? c1->col[ch] : 0.5f;
              float b2 = two ? c2->col[ch] : a;
              p[ch] = a + (b2 - a) * t;
            }
            p[3] = 1.f;
          }
      });
    })

} // namespace gpx
