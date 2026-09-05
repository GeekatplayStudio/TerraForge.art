// Geekatplay TerraForge — material graph nodes.
// MaterialOutput is the material itself (Blender's Material Output / a
// Substance output set): every channel the renderer understands arrives on
// one node that can be assigned to a scene object. The rest are Substance
// Designer style channel operators.
#include "gpx/node_graph.hpp"
#include "gpx/material_params.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/noise_core.hpp"

namespace gpx {

static inline float luminance(const float *p) {
  return p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f;
}

// ------------------------------------------------------------- the material
REGISTER_NODE(
    MaterialOutput, "Material",
    "The material: base color, normal, roughness, metallic, height and AO channels",
    [](Node &n) {
      n.add_in("base color", DataType::Texture, true);
      n.add_in("normal", DataType::Texture, true);
      n.add_in("roughness", DataType::Texture, true);
      n.add_in("metallic", DataType::Texture, true);
      n.add_in("height", DataType::Texture, true);
      n.add_in("ambient occlusion", DataType::Texture, true);
      n.add_out("preview", DataType::Texture);
      add_text(n.attrs, "name", "Material name", "Material", "Identity");
      // every surface property, grouped by the Material Editor's tabs
      // (engine/material_params.cpp); the renderers read them through
      // material_params_from()
      material_params_declare(n.attrs);
    },
    [](Node &n) {
      // the preview output is simply the base color, so the node shows a
      // thumbnail in the graph like every other node
      TextureRGBA &out = n.out_tex("preview");
      const TextureRGBA *base = n.in_tex("base color");
      if (base && !base->empty()) {
        out = *base;
      } else {
        float r = 0.55f, g = 0.53f, b = 0.5f;
        parallel_rows(out.h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < out.w; ++x) {
              float *p = out.px(x, y);
              p[0] = r; p[1] = g; p[2] = b; p[3] = 1.f;
            }
        });
      }
    })

// ----------------------------------------------------------------- channels
REGISTER_NODE(
    Levels, "Material", "Levels: remap input black/white/gamma to output range",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "in_black", "Input black", 0.f, 0.f, 1.f, "Input");
      add_float(n.attrs, "in_white", "Input white", 1.f, 0.f, 1.f, "Input");
      add_float(n.attrs, "gamma", "Gamma", 1.f, 0.1f, 4.f, "Input");
      add_float(n.attrs, "out_black", "Output black", 0.f, 0.f, 1.f, "Output");
      add_float(n.attrs, "out_white", "Output white", 1.f, 0.f, 1.f, "Output");
      add_bool(n.attrs, "per_channel", "Per channel", false, "Output")
          .tooltip = "Off: operate on luminance and keep the hue.\n"
                     "On: apply the curve to R, G and B separately.";
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      float ib = n.attrs.get_f("in_black", 0.f), iw = n.attrs.get_f("in_white", 1.f);
      float g = n.attrs.get_f("gamma", 1.f);
      float ob = n.attrs.get_f("out_black", 0.f), ow = n.attrs.get_f("out_white", 1.f);
      bool per = n.attrs.get_b("per_channel");
      float span = std::max(iw - ib, 1e-5f);
      auto curve = [&](float v) {
        v = std::clamp((v - ib) / span, 0.f, 1.f);
        v = std::pow(v, 1.f / std::max(g, 1e-3f));
        return ob + v * (ow - ob);
      };
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float *pi = in->px(x, y);
            float *po = out.px(x, y);
            if (per) {
              for (int k = 0; k < 3; ++k) po[k] = curve(pi[k]);
            } else {
              float l = luminance(pi);
              float nl = curve(l);
              float s = l > 1e-4f ? nl / l : 0.f;
              for (int k = 0; k < 3; ++k) po[k] = std::clamp(pi[k] * s, 0.f, 1.f);
            }
            po[3] = pi[3];
          }
      });
    })

REGISTER_NODE(
    GradientMap, "Material", "Recolor a texture through a gradient by luminance",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      n.add_out("texture", DataType::Texture);
      add_gradient(n.attrs, "gradient", "Gradient",
                   {{0.0f, 0.06f, 0.05f, 0.04f, 1},
                    {0.45f, 0.38f, 0.31f, 0.24f, 1},
                    {1.0f, 0.88f, 0.86f, 0.82f, 1}});
      add_float(n.attrs, "mix", "Amount", 1.f, 0.f, 1.f);
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      const Attribute *ga = n.attrs.find("gradient");
      float mix = n.attrs.get_f("mix", 1.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float *pi = in->px(x, y);
            float t = std::clamp(luminance(pi), 0.f, 1.f);
            float rgba[4] = {t, t, t, 1.f};
            const auto &stops = ga->stops;
            if (!stops.empty()) {
              if (t <= stops.front().t) {
                rgba[0] = stops.front().r; rgba[1] = stops.front().g;
                rgba[2] = stops.front().b;
              } else if (t >= stops.back().t) {
                rgba[0] = stops.back().r; rgba[1] = stops.back().g;
                rgba[2] = stops.back().b;
              } else {
                for (size_t k = 0; k + 1 < stops.size(); ++k)
                  if (t <= stops[k + 1].t) {
                    float f = (t - stops[k].t) /
                              std::max(stops[k + 1].t - stops[k].t, 1e-6f);
                    rgba[0] = stops[k].r + (stops[k + 1].r - stops[k].r) * f;
                    rgba[1] = stops[k].g + (stops[k + 1].g - stops[k].g) * f;
                    rgba[2] = stops[k].b + (stops[k + 1].b - stops[k].b) * f;
                    break;
                  }
              }
            }
            float *po = out.px(x, y);
            for (int k = 0; k < 3; ++k) po[k] = pi[k] * (1 - mix) + rgba[k] * mix;
            po[3] = pi[3];
          }
      });
    })

REGISTER_NODE(
    NormalBlend, "Material", "Combine two normal maps (whiteout blend)",
    [](Node &n) {
      n.add_in("base", DataType::Texture);
      n.add_in("detail", DataType::Texture);
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "detail_strength", "Detail strength", 1.f, 0.f, 3.f);
    },
    [](Node &n) {
      const TextureRGBA *a = n.in_tex("base");
      const TextureRGBA *b = n.in_tex("detail");
      if (!a || a->empty() || !b || b->empty()) {
        n.error = "both normal inputs required";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      float s = n.attrs.get_f("detail_strength", 1.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float *pa = a->px(x, y);
            const float *pb = b->px(x, y);
            float n1[3] = {pa[0] * 2 - 1, pa[1] * 2 - 1, pa[2] * 2 - 1};
            float n2[3] = {(pb[0] * 2 - 1) * s, (pb[1] * 2 - 1) * s, pb[2] * 2 - 1};
            // whiteout blend
            float r[3] = {n1[0] + n2[0], n1[1] + n2[1], n1[2] * std::max(n2[2], 0.05f)};
            float len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (len < 1e-6f) len = 1.f;
            float *po = out.px(x, y);
            for (int k = 0; k < 3; ++k) po[k] = r[k] / len * 0.5f + 0.5f;
            po[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    TextureTransform, "Material", "Tile, scale, offset and rotate a texture",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      n.add_out("texture", DataType::Texture);
      add_vec2(n.attrs, "tiles", "Tiles", 1.f, 1.f, 0.1f, 64.f);
      add_vec2(n.attrs, "offset", "Offset", 0.f, 0.f, -2.f, 2.f);
      add_float(n.attrs, "rotation", "Rotation", 0.f, -180.f, 180.f);
      add_bool(n.attrs, "mirror", "Mirror repeat", false)
          .tooltip = "Flips alternate tiles so seams are less visible.";
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      float tx, ty, ox, oy;
      n.attrs.get_vec2("tiles", tx, ty);
      n.attrs.get_vec2("offset", ox, oy);
      float rot = n.attrs.get_f("rotation") * 0.017453293f;
      float ca = std::cos(rot), sa = std::sin(rot);
      bool mirror = n.attrs.get_b("mirror");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w) - 0.5f, v = y / float(out.h) - 0.5f;
            float ru = (u * ca - v * sa) * tx + 0.5f + ox;
            float rv = (u * sa + v * ca) * ty + 0.5f + oy;
            if (mirror) {
              auto mir = [](float t) {
                t = std::fabs(t);
                float f = std::fmod(t, 2.f);
                return f > 1.f ? 2.f - f : f;
              };
              ru = mir(ru);
              rv = mir(rv);
            } else {
              ru -= std::floor(ru);
              rv -= std::floor(rv);
            }
            int sx = std::min((int)(ru * in->w), in->w - 1);
            int sy = std::min((int)(rv * in->h), in->h - 1);
            const float *pi = in->px(std::max(sx, 0), std::max(sy, 0));
            float *po = out.px(x, y);
            for (int k = 0; k < 4; ++k) po[k] = pi[k];
          }
      });
    })

REGISTER_NODE(
    AOFromHeight, "Material", "Ambient occlusion baked from a height input",
    [](Node &n) {
      n.add_in("height");
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "radius", "Radius", 0.02f, 0.002f, 0.15f);
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 3.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "height");
      if (!in) return;
      TextureRGBA &out = n.out_tex("texture");
      int r = std::max(1, (int)(n.attrs.get_f("radius", 0.02f) * in->w));
      float strength = n.attrs.get_f("strength", 1.f);
      float mn, mx;
      in->minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            int sx = x * in->w / out.w, sy = y * in->h / out.h;
            float h = in->atc(sx, sy);
            float occ = 0;
            const int K = 8;
            for (int k = 0; k < K; ++k) {
              float a = k * 6.2831853f / K;
              float hs = in->atc(sx + (int)(std::cos(a) * r),
                                 sy + (int)(std::sin(a) * r));
              occ += std::max(hs - h, 0.f) / amp;
            }
            float ao = std::clamp(1.f - occ / K * 6.f * strength, 0.f, 1.f);
            float *po = out.px(x, y);
            po[0] = po[1] = po[2] = ao;
            po[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    CurvatureFromHeight, "Material", "Convex/concave curvature map from height",
    [](Node &n) {
      n.add_in("height");
      n.add_out("texture", DataType::Texture);
      add_float(n.attrs, "scale", "Feature scale", 0.01f, 0.002f, 0.1f);
      add_float(n.attrs, "contrast", "Contrast", 1.f, 0.1f, 6.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "height");
      if (!in) return;
      TextureRGBA &out = n.out_tex("texture");
      int r = std::max(1, (int)(n.attrs.get_f("scale", 0.01f) * in->w));
      float contrast = n.attrs.get_f("contrast", 1.f);
      float mn, mx;
      in->minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            int sx = x * in->w / out.w, sy = y * in->h / out.h;
            float lap = in->atc(sx - r, sy) + in->atc(sx + r, sy) +
                        in->atc(sx, sy - r) + in->atc(sx, sy + r) -
                        4.f * in->atc(sx, sy);
            float c = std::clamp(0.5f + lap / amp * 6.f * contrast, 0.f, 1.f);
            float *po = out.px(x, y);
            po[0] = po[1] = po[2] = c;
            po[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    ChannelMix, "Material", "Pack three grayscale inputs into one RGB texture",
    [](Node &n) {
      n.add_in("red");
      n.add_in("green", DataType::Heightmap, true);
      n.add_in("blue", DataType::Heightmap, true);
      n.add_out("texture", DataType::Texture);
      add_bool(n.attrs, "normalize", "Normalize inputs", true);
    },
    [](Node &n) {
      const Heightmap *ch[3] = {n.in_hmap("red"), n.in_hmap("green"),
                                n.in_hmap("blue")};
      if (!ch[0] || ch[0]->empty()) {
        n.error = "input 'red' not connected";
        return;
      }
      TextureRGBA &out = n.out_tex("texture");
      bool norm = n.attrs.get_b("normalize", true);
      Heightmap tmp[3];
      for (int k = 0; k < 3; ++k)
        if (ch[k] && !ch[k]->empty() && norm) {
          tmp[k] = *ch[k];
          tmp[k].remap(0.f, 1.f);
          ch[k] = &tmp[k];
        }
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float *po = out.px(x, y);
            for (int k = 0; k < 3; ++k) {
              float v = 0.f;
              if (ch[k] && !ch[k]->empty())
                v = ch[k]->sample(x / float(out.w), y / float(out.h));
              po[k] = std::clamp(v, 0.f, 1.f);
            }
            po[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    MaskToTexture, "Material", "Grayscale mask or heightmap as a texture channel",
    [](Node &n) {
      n.add_in("input");
      n.add_out("texture", DataType::Texture);
      add_bool(n.attrs, "normalize", "Normalize", true);
      add_float(n.attrs, "scale", "Scale", 1.f, 0.f, 2.f);
      add_float(n.attrs, "offset", "Offset", 0.f, -1.f, 1.f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      TextureRGBA &out = n.out_tex("texture");
      Heightmap src = *in;
      if (n.attrs.get_b("normalize", true)) src.remap(0.f, 1.f);
      float s = n.attrs.get_f("scale", 1.f), o = n.attrs.get_f("offset", 0.f);
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            float v = std::clamp(
                src.sample(x / float(out.w), y / float(out.h)) * s + o, 0.f, 1.f);
            float *po = out.px(x, y);
            po[0] = po[1] = po[2] = v;
            po[3] = 1.f;
          }
      });
    })

REGISTER_NODE(
    TextureToMask, "Material", "Texture luminance back into a mask/heightmap",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      n.add_out("mask");
      add_choice(n.attrs, "channel", "Channel",
                 {"Luminance", "Red", "Green", "Blue", "Alpha"}, 0);
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      Heightmap &out = n.out_hmap("mask");
      int ch = n.attrs.get_choice("channel");
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            int sx = std::min(x * in->w / out.w, in->w - 1);
            int sy = std::min(y * in->h / out.h, in->h - 1);
            const float *p = in->px(sx, sy);
            out.at(x, y) = ch == 0 ? luminance(p) : p[std::clamp(ch - 1, 0, 3)];
          }
      });
    })

} // namespace gpx
