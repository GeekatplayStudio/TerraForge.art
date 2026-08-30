// Geekatplay Studio — material system nodes: file textures with mapping
// modes, multilayer splat compositing, texture blending, albedo->PBR.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace gpx {

// sample a texture with tiling + mapping mode; uv in [0,1] over the terrain
static void sample_mapped(const TextureRGBA &t, float u, float v, int mapping,
                          float tiles, float *rgba) {
  switch (mapping) {
    case 0: break;                       // stretch: whole texture over terrain
    case 1: u *= tiles; v *= tiles; break; // tiled
    case 2: {                            // tiled + offset half per row (brick-ish break-up)
      u *= tiles;
      v *= tiles;
      if (((int)std::floor(v)) & 1) u += 0.5f;
    } break;
  }
  u -= std::floor(u);
  v -= std::floor(v);
  float fx = u * (t.w - 1), fy = v * (t.h - 1);
  int x0 = (int)fx, y0 = (int)fy;
  int x1 = (x0 + 1) % t.w, y1 = (y0 + 1) % t.h;
  float ax = fx - x0, ay = fy - y0;
  const float *p00 = t.px(x0, y0), *p10 = t.px(x1, y0);
  const float *p01 = t.px(x0, y1), *p11 = t.px(x1, y1);
  for (int k = 0; k < 4; ++k)
    rgba[k] = (p00[k] * (1 - ax) + p10[k] * ax) * (1 - ay) +
              (p01[k] * (1 - ax) + p11[k] * ax) * ay;
}

REGISTER_NODE(
    TextureFile, "Material", "Load an image texture (PNG/JPG/TGA/BMP) with mapping modes",
    [](Node &n) {
      n.add_out("texture", DataType::Texture);
      add_filename(n.attrs, "path", "Image file", "");
      add_choice(n.attrs, "mapping", "Mapping", {"Stretch", "Tile", "Tile offset"}, 1);
      add_float(n.attrs, "tiles", "Tiles across", 8.f, 1.f, 64.f);
      add_float(n.attrs, "brightness", "Brightness", 1.f, 0.2f, 3.f);
    },
    [](Node &n) {
      std::string path = n.attrs.get_s("path");
      TextureRGBA &out = n.out_tex("texture");
      if (path.empty()) {
        n.error = "no image file set";
        return;
      }
      int iw, ih, comp;
      unsigned char *data = stbi_load(path.c_str(), &iw, &ih, &comp, 4);
      if (!data) {
        n.error = "cannot load: " + path;
        return;
      }
      TextureRGBA src(iw, ih);
      for (size_t i = 0; i < src.v.size(); ++i) src.v[i] = data[i] / 255.f;
      stbi_image_free(data);
      int mapping = n.attrs.get_choice("mapping");
      float tiles = n.attrs.get_f("tiles", 8.f);
      float bright = n.attrs.get_f("brightness", 1.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float rgba[4];
            sample_mapped(src, x / float(out.w), y / float(out.h), mapping, tiles,
                          rgba);
            float *px = out.px(x, y);
            px[0] = std::clamp(rgba[0] * bright, 0.f, 1.f);
            px[1] = std::clamp(rgba[1] * bright, 0.f, 1.f);
            px[2] = std::clamp(rgba[2] * bright, 0.f, 1.f);
            px[3] = rgba[3];
          }
      });
    })

REGISTER_NODE(
    TextureBlend, "Material", "Blend two textures by mask / mode / opacity",
    [](Node &n) {
      n.add_in("texture A", DataType::Texture);
      n.add_in("texture B", DataType::Texture);
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("texture", DataType::Texture);
      add_choice(n.attrs, "mode", "Mode",
                 {"Normal", "Multiply", "Add", "Overlay", "Screen", "Height tint"}, 0);
      add_float(n.attrs, "opacity", "Opacity", 1.f, 0.f, 1.f);
    },
    [](Node &n) {
      const TextureRGBA *ta = n.in_tex("texture A");
      const TextureRGBA *tb = n.in_tex("texture B");
      if (!ta || ta->empty() || !tb || tb->empty()) {
        n.error = "both texture inputs required";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      const Heightmap *mask = n.in_hmap("mask");
      int mode = n.attrs.get_choice("mode");
      float op = n.attrs.get_f("opacity", 1.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float *pa = ta->px(x, y), *pb = tb->px(x, y);
            float *po = out.px(x, y);
            float m = op;
            if (mask && !mask->empty())
              m *= std::clamp(mask->v[(size_t)y * out.w + x], 0.f, 1.f);
            for (int k = 0; k < 3; ++k) {
              float a = pa[k], b = pb[k], r = b;
              switch (mode) {
                case 1: r = a * b; break;
                case 2: r = std::min(a + b, 1.f); break;
                case 3: r = a < 0.5f ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b); break;
                case 4: r = 1 - (1 - a) * (1 - b); break;
                case 5: r = a * (0.5f + b); break;
              }
              po[k] = a * (1 - m) + r * m;
            }
            po[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    Splatmap, "Material", "Pack up to 4 masks into RGBA splat weights (normalized)",
    [](Node &n) {
      n.add_in("mask R");
      n.add_in("mask G", DataType::Heightmap, true);
      n.add_in("mask B", DataType::Heightmap, true);
      n.add_in("mask A", DataType::Heightmap, true);
      n.add_out("splat", DataType::Texture);
      add_bool(n.attrs, "normalize", "Normalize weights", true);
    },
    [](Node &n) {
      const Heightmap *m[4] = {n.in_hmap("mask R"), n.in_hmap("mask G"),
                               n.in_hmap("mask B"), n.in_hmap("mask A")};
      if (!m[0] || m[0]->empty()) {
        n.error = "input 'mask R' not connected";
        return;
      }
      TextureRGBA &out = n.out_tex("splat");
      bool norm = n.attrs.get_b("normalize", true);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            size_t i = (size_t)y * out.w + x;
            float wgt[4] = {0, 0, 0, 0}, sum = 0;
            for (int k = 0; k < 4; ++k) {
              if (m[k] && !m[k]->empty()) wgt[k] = std::clamp(m[k]->v[i], 0.f, 1.f);
              sum += wgt[k];
            }
            if (norm && sum > 1e-6f)
              for (float &wv : wgt) wv /= sum;
            float *px = out.px(x, y);
            for (int k = 0; k < 4; ++k) px[k] = wgt[k];
          }
      });
    })

REGISTER_NODE(
    SplatMaterial, "Material", "Compose albedo from a splatmap and up to 4 layer textures",
    [](Node &n) {
      n.add_in("splat", DataType::Texture);
      n.add_in("layer R", DataType::Texture);
      n.add_in("layer G", DataType::Texture, true);
      n.add_in("layer B", DataType::Texture, true);
      n.add_in("layer A", DataType::Texture, true);
      n.add_out("texture", DataType::Texture);
    },
    [](Node &n) {
      const TextureRGBA *splat = n.in_tex("splat");
      const TextureRGBA *L[4] = {n.in_tex("layer R"), n.in_tex("layer G"),
                                 n.in_tex("layer B"), n.in_tex("layer A")};
      if (!splat || splat->empty() || !L[0] || L[0]->empty()) {
        n.error = "'splat' and 'layer R' required";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float *sw = splat->px(x, y);
            float *po = out.px(x, y);
            po[0] = po[1] = po[2] = 0;
            po[3] = 1;
            float used = 0;
            for (int k = 0; k < 4; ++k) {
              if (!L[k] || L[k]->empty() || sw[k] <= 0) continue;
              const float *pl = L[k]->px(x, y);
              po[0] += pl[0] * sw[k];
              po[1] += pl[1] * sw[k];
              po[2] += pl[2] * sw[k];
              used += sw[k];
            }
            if (used < 1e-6f && L[0]) {
              const float *pl = L[0]->px(x, y);
              po[0] = pl[0];
              po[1] = pl[1];
              po[2] = pl[2];
            }
          }
      });
    })

REGISTER_NODE(
    AlbedoToPBR, "Material", "Derive normal + roughness maps from an albedo texture",
    [](Node &n) {
      n.add_in("albedo", DataType::Texture);
      n.add_out("normal", DataType::Texture);
      n.add_out("roughness", DataType::Texture);
      add_float(n.attrs, "normal_strength", "Normal strength", 2.f, 0.1f, 10.f);
      add_float(n.attrs, "rough_base", "Roughness base", 0.8f, 0.f, 1.f);
      add_float(n.attrs, "rough_variation", "Roughness variation", 0.3f, 0.f, 1.f);
      add_bool(n.attrs, "invert_rough", "Bright = smooth", true);
    },
    [](Node &n) {
      const TextureRGBA *alb = n.in_tex("albedo");
      if (!alb || alb->empty()) {
        n.error = "input 'albedo' not connected";
        return;
      }
      TextureRGBA &nrm = n.out_tex("normal");
      TextureRGBA &rgh = n.out_tex("roughness");
      float strength = n.attrs.get_f("normal_strength", 2.f);
      float rbase = n.attrs.get_f("rough_base", 0.8f);
      float rvar = n.attrs.get_f("rough_variation", 0.3f);
      bool inv = n.attrs.get_b("invert_rough", true);
      auto lum = [&](int x, int y) {
        x = std::clamp(x, 0, alb->w - 1);
        y = std::clamp(y, 0, alb->h - 1);
        const float *p = alb->px(x, y);
        return p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f;
      };
      parallel_rows(nrm.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < nrm.w; ++x) {
            // Sobel on luminance (heightfield-from-brightness assumption)
            float gx = (lum(x + 1, y - 1) + 2 * lum(x + 1, y) + lum(x + 1, y + 1)) -
                       (lum(x - 1, y - 1) + 2 * lum(x - 1, y) + lum(x - 1, y + 1));
            float gy = (lum(x - 1, y + 1) + 2 * lum(x, y + 1) + lum(x + 1, y + 1)) -
                       (lum(x - 1, y - 1) + 2 * lum(x, y - 1) + lum(x + 1, y - 1));
            float nx = -gx * strength, ny = -gy * strength, nz = 1.f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            float *pn = nrm.px(x, y);
            pn[0] = nx / len * 0.5f + 0.5f;
            pn[1] = ny / len * 0.5f + 0.5f;
            pn[2] = nz / len * 0.5f + 0.5f;
            pn[3] = 1;
            float l = lum(x, y);
            float r = rbase + rvar * ((inv ? 1.f - l : l) - 0.5f) * 2.f;
            r = std::clamp(r, 0.f, 1.f);
            float *pr = rgh.px(x, y);
            pr[0] = pr[1] = pr[2] = r;
            pr[3] = 1;
          }
      });
    })

REGISTER_NODE(
    ColorAdjust, "Material", "Color correction: brightness, contrast, saturation, hue, tint",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "brightness", "Brightness", 1.f, 0.2f, 3.f);
      add_float(n.attrs, "contrast", "Contrast", 1.f, 0.2f, 3.f);
      add_float(n.attrs, "saturation", "Saturation", 1.f, 0.f, 3.f);
      add_float(n.attrs, "hue_shift", "Hue shift °", 0.f, -180.f, 180.f);
      add_float(n.attrs, "tint_r", "Tint R", 1.f, 0.f, 2.f, "Tint");
      add_float(n.attrs, "tint_g", "Tint G", 1.f, 0.f, 2.f, "Tint");
      add_float(n.attrs, "tint_b", "Tint B", 1.f, 0.f, 2.f, "Tint");
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      float bright = n.attrs.get_f("brightness", 1.f);
      float contrast = n.attrs.get_f("contrast", 1.f);
      float sat = n.attrs.get_f("saturation", 1.f);
      float hue = n.attrs.get_f("hue_shift", 0.f) * 0.017453293f;
      float tr = n.attrs.get_f("tint_r", 1.f);
      float tg = n.attrs.get_f("tint_g", 1.f);
      float tb = n.attrs.get_f("tint_b", 1.f);
      float ch = std::cos(hue), sh = std::sin(hue);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float *pi = in->px(x, y);
            float *po = out.px(x, y);
            float r = pi[0] * bright * tr, g = pi[1] * bright * tg,
                  b = pi[2] * bright * tb;
            // contrast around mid-gray
            r = (r - 0.5f) * contrast + 0.5f;
            g = (g - 0.5f) * contrast + 0.5f;
            b = (b - 0.5f) * contrast + 0.5f;
            // saturation via luminance
            float lum = r * 0.299f + g * 0.587f + b * 0.114f;
            r = lum + (r - lum) * sat;
            g = lum + (g - lum) * sat;
            b = lum + (b - lum) * sat;
            // hue rotation (YIQ approximation)
            if (hue != 0.f) {
              float yy = r * 0.299f + g * 0.587f + b * 0.114f;
              float ii = r * 0.596f - g * 0.274f - b * 0.322f;
              float qq = r * 0.211f - g * 0.523f + b * 0.312f;
              float i2 = ii * ch - qq * sh, q2 = ii * sh + qq * ch;
              r = yy + 0.956f * i2 + 0.621f * q2;
              g = yy - 0.272f * i2 - 0.647f * q2;
              b = yy - 1.106f * i2 + 1.703f * q2;
            }
            po[0] = std::clamp(r, 0.f, 1.f);
            po[1] = std::clamp(g, 0.f, 1.f);
            po[2] = std::clamp(b, 0.f, 1.f);
            po[3] = pi[3];
          }
      });
    })

REGISTER_NODE(
    FlatColor, "Material", "Solid color material (procedural function + color)",
    [](Node &n) {
      n.add_in("mask", DataType::Heightmap, true);
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "r", "Red", 0.5f, 0.f, 1.f);
      add_float(n.attrs, "g", "Green", 0.45f, 0.f, 1.f);
      add_float(n.attrs, "b", "Blue", 0.4f, 0.f, 1.f);
    },
    [](Node &n) {
      TextureRGBA &out = n.out_tex("texture");
      float r = n.attrs.get_f("r", 0.5f), g = n.attrs.get_f("g", 0.45f),
            b = n.attrs.get_f("b", 0.4f);
      const Heightmap *mask = n.in_hmap("mask");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float *p = out.px(x, y);
            float m = mask && !mask->empty()
                          ? std::clamp(mask->v[(size_t)y * out.w + x], 0.f, 1.f)
                          : 1.f;
            p[0] = r * m;
            p[1] = g * m;
            p[2] = b * m;
            p[3] = 1.f;
          }
      });
    })

} // namespace gpx
