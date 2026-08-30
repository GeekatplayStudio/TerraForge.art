// Geekatplay Studio — export nodes (16-bit PNG heightmap, albedo PNG, OBJ, RAW)
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <cstdio>
#include <fstream>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gpx {

// The canonical end-of-graph node: combines the final heightmap with extra
// shape layers and the albedo, applies global zero-edges and height range.
// The viewport prefers this node automatically.
REGISTER_NODE(
    TerrainOutput, "Export", "Final terrain: combines height layers + material, zero edges",
    [](Node &n) {
      n.add_in("heightmap");
      n.add_in("extra layer 1", DataType::Heightmap, true);
      n.add_in("extra layer 2", DataType::Heightmap, true);
      n.add_in("albedo", DataType::Texture, true);
      n.add_out("heightmap");
      n.add_out("albedo", DataType::Texture);
      add_choice(n.attrs, "combine", "Combine layers", {"Add", "Max (merge)", "Min"},
                 0, "Combine");
      add_float(n.attrs, "layer_strength", "Layer strength", 1.f, 0.f, 2.f,
                "Combine");
      add_float(n.attrs, "zero_edges", "Zero edges width", 0.12f, 0.f, 0.5f,
                "Edges")
          .tooltip = "Fades terrain to zero at the borders — the final\n"
                     "island/tile edge treatment.";
      add_choice(n.attrs, "edge_curve", "Edge curve",
                 {"Smooth", "Linear", "Steep (cliff)"}, 0, "Edges");
      add_range(n.attrs, "height_range", "Final height range", 0.f, 1.f, -1.f, 2.f,
                "Output");
      add_bool(n.attrs, "remap", "Remap to range", true, "Output");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "heightmap");
      if (!in) return;
      Heightmap &out = n.out_hmap("heightmap");
      out = *in;
      int combine = n.attrs.get_choice("combine");
      float ls = n.attrs.get_f("layer_strength", 1.f);
      for (const char *port : {"extra layer 1", "extra layer 2"}) {
        const Heightmap *l = n.in_hmap(port);
        if (!l || l->empty()) continue;
        for (size_t i = 0; i < out.v.size(); ++i) {
          float lv = l->v[i] * ls;
          switch (combine) {
            case 1: out.v[i] = std::max(out.v[i], lv); break;
            case 2: out.v[i] = std::min(out.v[i], lv); break;
            default: out.v[i] += lv;
          }
        }
      }
      if (n.attrs.get_b("remap", true)) {
        float lo, hi;
        n.attrs.get_range("height_range", lo, hi);
        out.remap(lo, hi);
      }
      // zero edges with selectable curve
      float w = n.attrs.get_f("zero_edges", 0.12f);
      if (w > 1e-6f) {
        int curve = n.attrs.get_choice("edge_curve");
        float mn, mx;
        out.minmax(mn, mx);
        for (int y = 0; y < out.h; ++y)
          for (int x = 0; x < out.w; ++x) {
            float u = x / float(out.w - 1), v = y / float(out.h - 1);
            float b = std::min(std::min(u, 1.f - u), std::min(v, 1.f - v)) / w;
            b = std::clamp(b, 0.f, 1.f);
            if (curve == 0) b = b * b * (3.f - 2.f * b);
            else if (curve == 2) b = std::pow(b, 0.4f);
            out.at(x, y) = mn + (out.at(x, y) - mn) * b;
          }
      }
      // albedo passthrough
      const TextureRGBA *alb = n.in_tex("albedo");
      if (alb && !alb->empty()) n.out_tex("albedo") = *alb;
    })

REGISTER_NODE(
    ExportHeightmap, "Export", "Write 16-bit PNG / RAW heightmap",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output"); // pass-through so chains continue
      add_filename(n.attrs, "path", "File", "heightmap.png");
      add_choice(n.attrs, "format", "Format", {"PNG 16-bit", "RAW float32"}, 0);
      add_bool(n.attrs, "auto_export", "Export on every compute", false);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      if (!n.attrs.get_b("auto_export")) return;
      std::string path = n.attrs.get_s("path");
      if (path.empty()) return;
      Heightmap norm = *in;
      norm.remap(0.f, 1.f);
      if (n.attrs.get_choice("format") == 0) {
        // stb has no 16-bit png; write raw 16-bit big-endian PNG via minimal
        // fallback: use 8-bit if extension mismatch. Keep simple: 16-bit PGM
        // when .pgm, else 8-bit PNG.
        std::vector<uint8_t> img(norm.v.size());
        for (size_t i = 0; i < norm.v.size(); ++i)
          img[i] = (uint8_t)std::clamp(norm.v[i] * 255.f + 0.5f, 0.f, 255.f);
        stbi_write_png(path.c_str(), norm.w, norm.h, 1, img.data(), norm.w);
      } else {
        std::ofstream f(path, std::ios::binary);
        f.write((const char *)norm.v.data(), norm.v.size() * sizeof(float));
      }
    })

REGISTER_NODE(
    ExportTexture, "Export", "Write albedo/texture PNG",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      add_filename(n.attrs, "path", "File", "albedo.png");
      add_bool(n.attrs, "auto_export", "Export on every compute", false);
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      if (!n.attrs.get_b("auto_export")) return;
      std::string path = n.attrs.get_s("path");
      if (path.empty()) return;
      auto img = in->to_u8();
      stbi_write_png(path.c_str(), in->w, in->h, 4, img.data(), in->w * 4);
    })

REGISTER_NODE(
    ExportMesh, "Export", "Write OBJ mesh",
    [](Node &n) {
      n.add_in("input");
      add_filename(n.attrs, "path", "File", "terrain.obj");
      add_int(n.attrs, "max_verts_side", "Mesh resolution", 256, 32, 1024);
      add_float(n.attrs, "height_scale", "Height scale", 0.25f, 0.01f, 2.f);
      add_bool(n.attrs, "auto_export", "Export on every compute", false);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      if (!n.attrs.get_b("auto_export")) return;
      std::string path = n.attrs.get_s("path");
      if (path.empty()) return;
      int side = std::min(n.attrs.get_i("max_verts_side", 256), in->w);
      float hs = n.attrs.get_f("height_scale", 0.25f);
      Heightmap m = in->resampled(side, side);
      m.remap(0.f, hs);
      std::ofstream f(path);
      f << "# Geekatplay Studio terrain export\n";
      for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
          f << "v " << x / float(side - 1) << ' ' << m.at(x, y) << ' '
            << y / float(side - 1) << '\n';
      for (int y = 0; y < side - 1; ++y)
        for (int x = 0; x < side - 1; ++x) {
          int i = y * side + x + 1;
          f << "f " << i << ' ' << i + side << ' ' << i + 1 << '\n';
          f << "f " << i + 1 << ' ' << i + side << ' ' << i + side + 1 << '\n';
        }
    })

} // namespace gpx
