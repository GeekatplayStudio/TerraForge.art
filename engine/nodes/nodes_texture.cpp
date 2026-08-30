// Geekatplay Studio — texturing nodes: procedural terrain albedo,
// gradient colorize, normal map.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"

namespace gpx {

static void eval_gradient(const std::vector<GradientStop> &stops, float t, float *rgba) {
  if (stops.empty()) {
    rgba[0] = rgba[1] = rgba[2] = t;
    rgba[3] = 1;
    return;
  }
  if (t <= stops.front().t) {
    const auto &s = stops.front();
    rgba[0] = s.r; rgba[1] = s.g; rgba[2] = s.b; rgba[3] = s.a;
    return;
  }
  for (size_t i = 0; i + 1 < stops.size(); ++i) {
    const auto &a = stops[i], &b = stops[i + 1];
    if (t <= b.t) {
      float f = (t - a.t) / std::max(b.t - a.t, 1e-6f);
      rgba[0] = a.r + (b.r - a.r) * f;
      rgba[1] = a.g + (b.g - a.g) * f;
      rgba[2] = a.b + (b.b - a.b) * f;
      rgba[3] = a.a + (b.a - a.a) * f;
      return;
    }
  }
  const auto &s = stops.back();
  rgba[0] = s.r; rgba[1] = s.g; rgba[2] = s.b; rgba[3] = s.a;
}

REGISTER_NODE(
    ColorizeGradient, "Texture", "Map height to a color gradient",
    [](Node &n) {
      n.add_in("input");
      n.add_out("texture", DataType::Texture);
      add_gradient(n.attrs, "gradient", "Gradient",
                   {{0.0f, 0.05f, 0.15f, 0.30f, 1},
                    {0.35f, 0.62f, 0.55f, 0.35f, 1},
                    {0.55f, 0.30f, 0.38f, 0.15f, 1},
                    {0.75f, 0.45f, 0.42f, 0.40f, 1},
                    {1.0f, 0.95f, 0.95f, 0.98f, 1}});
      add_bool(n.attrs, "hillshade", "Multiply hillshade", true);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      TextureRGBA &tex = n.out_tex("texture");
      Heightmap norm = *in;
      norm.remap(0.f, 1.f);
      const Attribute *ga = n.attrs.find("gradient");
      bool hs = n.attrs.get_b("hillshade", true);
      parallel_rows(tex.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < tex.w; ++x) {
            float rgba[4];
            eval_gradient(ga->stops, norm.at(x, y), rgba);
            if (hs) {
              float dx, dy;
              in->gradient_at(x, y, dx, dy);
              float scale = in->w * 0.5f;
              float lx = -0.5f, ly = -0.5f, lz = 0.7071f;
              float nx = -dx * scale, ny = -dy * scale, nz = 1.f;
              float len = std::sqrt(nx * nx + ny * ny + nz * nz);
              float shade = std::clamp((nx * lx + ny * ly + nz * lz) / len, 0.f, 1.f);
              shade = 0.35f + 0.65f * shade;
              rgba[0] *= shade; rgba[1] *= shade; rgba[2] *= shade;
            }
            float *px = tex.px(x, y);
            px[0] = rgba[0]; px[1] = rgba[1]; px[2] = rgba[2]; px[3] = rgba[3];
          }
      });
    })

// -------------------------------------------------- procedural terrain albedo
// Layered material texturing: bedrock / talus / soil-grass / sand / snow,
// weighted by altitude, slope, cavity, plus per-material noise breakup.
REGISTER_NODE(
    TerrainTexture, "Texture", "Physically-inspired layered terrain albedo",
    [](Node &n) {
      n.add_in("input");
      n.add_in("flow", DataType::Heightmap, true);
      n.add_out("texture", DataType::Texture);
      add_seed(n.attrs);
      add_float(n.attrs, "snow_line", "Snow line", 0.75f, 0.f, 1.f, "Layers");
      add_float(n.attrs, "grass_line", "Vegetation ceiling", 0.55f, 0.f, 1.f, "Layers");
      add_float(n.attrs, "rock_slope", "Rock slope threshold", 0.45f, 0.05f, 1.f, "Layers");
      add_float(n.attrs, "snow_slope", "Snow max slope", 0.55f, 0.05f, 1.f, "Layers");
      add_float(n.attrs, "beach_level", "Sand level", 0.06f, 0.f, 0.4f, "Layers");
      add_float(n.attrs, "breakup", "Noise breakup", 0.5f, 0.f, 1.f, "Detail");
      add_float(n.attrs, "detail_scale", "Detail scale", 24.f, 2.f, 96.f, "Detail");
      add_float(n.attrs, "wetness", "Flow darkening", 0.4f, 0.f, 1.f, "Detail");
      // off by default: the 3D renderer lights and shadows the terrain itself
      add_bool(n.attrs, "hillshade", "Multiply hillshade", false, "Detail");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      TextureRGBA &tex = n.out_tex("texture");
      const Heightmap *flow = n.in_hmap("flow");
      Heightmap norm = *in;
      norm.remap(0.f, 1.f);
      float snow_line = n.attrs.get_f("snow_line", 0.75f);
      float grass_line = n.attrs.get_f("grass_line", 0.55f);
      float rock_slope = n.attrs.get_f("rock_slope", 0.45f);
      float snow_slope = n.attrs.get_f("snow_slope", 0.55f);
      float beach = n.attrs.get_f("beach_level", 0.06f);
      float breakup = n.attrs.get_f("breakup", 0.5f);
      float dscale = n.attrs.get_f("detail_scale", 24.f);
      float wetness = n.attrs.get_f("wetness", 0.4f);
      bool hs = n.attrs.get_b("hillshade", false);
      uint32_t seed = n.attrs.get_seed("seed");

      // slope map normalized
      Heightmap slope(in->w, in->h);
      float mn, mx;
      in->minmax(mn, mx);
      float amp = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            float dx, dy;
            in->gradient_at(x, y, dx, dy);
            slope.at(x, y) =
                std::atan(std::sqrt(dx * dx + dy * dy) * in->w / amp) * 0.63662f;
          }
      });

      struct Mat { float r, g, b, rough; };
      const Mat rock  {0.36f, 0.33f, 0.31f, 0};
      const Mat rock2 {0.28f, 0.24f, 0.22f, 0};
      const Mat talus {0.46f, 0.42f, 0.38f, 0};
      const Mat grass {0.20f, 0.31f, 0.12f, 0};
      const Mat grass2{0.32f, 0.36f, 0.14f, 0};
      const Mat sand  {0.72f, 0.65f, 0.50f, 0};
      const Mat snow  {0.93f, 0.94f, 0.97f, 0};
      const Mat dirt  {0.38f, 0.30f, 0.22f, 0};

      noise::FbmParams fp;
      fp.octaves = 5;
      parallel_rows(tex.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < tex.w; ++x) {
            float u = x / float(tex.w), v = y / float(tex.h);
            float alt = norm.at(x, y);
            float sl = slope.at(x, y);
            float bnoise =
                noise::fbm(u * dscale, v * dscale, seed, fp) * 0.5f * breakup;
            float bnoise2 =
                noise::fbm(u * dscale * 2.7f + 13.f, v * dscale * 2.7f, seed ^ 7u, fp);

            // layer weights
            float w_snow = std::clamp((alt - snow_line - bnoise * 0.4f) * 8.f, 0.f, 1.f) *
                           std::clamp((snow_slope - sl) * 6.f, 0.f, 1.f);
            float w_rock = std::clamp((sl - rock_slope + bnoise * 0.3f) * 7.f, 0.f, 1.f);
            float w_sand = std::clamp((beach - alt + bnoise * 0.1f) * 14.f, 0.f, 1.f);
            float w_grass = std::clamp((grass_line - alt + bnoise * 0.5f) * 5.f, 0.f, 1.f) *
                            (1.f - w_rock) * (1.f - w_sand);
            // grass dies on high slopes
            w_grass *= std::clamp((0.6f - sl) * 4.f, 0.f, 1.f);

            // base: dirt/talus mix by altitude
            float r = dirt.r, g = dirt.g, b = dirt.b;
            float tal = std::clamp(alt * 1.5f, 0.f, 1.f);
            r = r * (1 - tal) + talus.r * tal;
            g = g * (1 - tal) + talus.g * tal;
            b = b * (1 - tal) + talus.b * tal;

            auto blend = [&](const Mat &m, const Mat &m2, float w, float varsel) {
              if (w <= 0) return;
              float mr = m.r + (m2.r - m.r) * varsel;
              float mg = m.g + (m2.g - m.g) * varsel;
              float mb = m.b + (m2.b - m.b) * varsel;
              r = r * (1 - w) + mr * w;
              g = g * (1 - w) + mg * w;
              b = b * (1 - w) + mb * w;
            };
            float var = std::clamp(bnoise2 * 0.5f + 0.5f, 0.f, 1.f);
            blend(grass, grass2, w_grass, var);
            blend(rock, rock2, w_rock, var);
            blend(sand, sand, w_sand, 0);
            blend(snow, snow, w_snow, 0);

            // flow wetness darkening (river beds)
            if (flow && !flow->empty() && wetness > 0) {
              float f = std::clamp(flow->v[(size_t)y * tex.w + x], 0.f, 1.f);
              float dark = 1.f - wetness * f * 0.7f;
              r *= dark; g *= dark;
              b *= dark * 1.05f;
            }
            // micro AO from detail noise
            float micro = 1.f - std::fabs(bnoise2) * 0.12f;
            r *= micro; g *= micro; b *= micro;

            if (hs) {
              float dx, dy;
              in->gradient_at(x, y, dx, dy);
              float scale = in->w * 0.5f / amp;
              float nx = -dx * scale, ny = -dy * scale, nz = 1.f;
              float len = std::sqrt(nx * nx + ny * ny + nz * nz);
              float shade =
                  std::clamp((nx * -0.5f + ny * -0.5f + nz * 0.7071f) / len, 0.f, 1.f);
              shade = 0.4f + 0.6f * shade;
              r *= shade; g *= shade; b *= shade;
            }
            float *px = tex.px(x, y);
            px[0] = std::clamp(r, 0.f, 1.f);
            px[1] = std::clamp(g, 0.f, 1.f);
            px[2] = std::clamp(b, 0.f, 1.f);
            px[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    NormalMap, "Texture", "Tangent-space normal map from height",
    [](Node &n) {
      n.add_in("input");
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "strength", "Strength", 1.f, 0.05f, 8.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      TextureRGBA &tex = n.out_tex("texture");
      float strength = n.attrs.get_f("strength", 1.f);
      float mn, mx;
      in->minmax(mn, mx);
      float amp = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      float scale = strength * in->w / amp * 0.02f;
      parallel_rows(tex.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < tex.w; ++x) {
            float dx, dy;
            in->gradient_at(x, y, dx, dy);
            float nx = -dx * scale, ny = -dy * scale, nz = 1.f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            float *px = tex.px(x, y);
            px[0] = nx / len * 0.5f + 0.5f;
            px[1] = ny / len * 0.5f + 0.5f;
            px[2] = nz / len * 0.5f + 0.5f;
            px[3] = 1.f;
          }
      });
    })

} // namespace gpx
