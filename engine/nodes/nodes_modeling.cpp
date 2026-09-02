// Geekatplay TerraForge — terrain modeling nodes: import heightfield maps
// and stamp/sculpt them onto the terrain.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "stb_image.h" // impl in nodes_materials.cpp
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace gpx {

static bool load_heightfield(const std::string &path, Heightmap &out,
                             std::string &err) {
  int iw, ih, comp;
  // SRTM .hgt: raw big-endian int16, square (3601 or 1201 a side), metres
  // above sea level, -32768 = void. The filename is the format.
  if (path.size() > 4 &&
      (path.rfind(".hgt") == path.size() - 4 ||
       path.rfind(".HGT") == path.size() - 4)) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
      err = "cannot load: " + path;
      return false;
    }
    size_t bytes = (size_t)f.tellg();
    size_t samples = bytes / 2;
    int side = (int)std::lround(std::sqrt((double)samples));
    if ((size_t)side * side != samples || side < 2) {
      err = "not a square .hgt file: " + path;
      return false;
    }
    std::vector<unsigned char> raw(bytes);
    f.seekg(0);
    f.read((char *)raw.data(), (std::streamsize)bytes);
    Heightmap src(side, side);
    float lo = 1e30f, hi = -1e30f;
    for (size_t i = 0; i < samples; ++i) {
      int16_t v = (int16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]);
      float h = v == -32768 ? 0.f : (float)v; // voids read as sea level
      src.v[i] = h;
      lo = std::min(lo, h);
      hi = std::max(hi, h);
    }
    // normalized like every import; real elevation range restorable through
    // the height scale
    float span = hi - lo > 1e-6f ? hi - lo : 1.f;
    for (float &v : src.v) v = (v - lo) / span;
    out = src.resampled(out.w, out.h);
    return true;
  }
  // 16-bit aware load for PNG heightmaps
  if (stbi_is_16_bit(path.c_str())) {
    unsigned short *d16 = stbi_load_16(path.c_str(), &iw, &ih, &comp, 1);
    if (!d16) {
      err = "cannot load: " + path;
      return false;
    }
    Heightmap src(iw, ih);
    for (size_t i = 0; i < src.v.size(); ++i) src.v[i] = d16[i] / 65535.f;
    stbi_image_free(d16);
    out = src.resampled(out.w, out.h);
    return true;
  }
  unsigned char *data = stbi_load(path.c_str(), &iw, &ih, &comp, 1);
  if (!data) {
    err = "cannot load: " + path;
    return false;
  }
  Heightmap src(iw, ih);
  for (size_t i = 0; i < src.v.size(); ++i) src.v[i] = data[i] / 255.f;
  stbi_image_free(data);
  out = src.resampled(out.w, out.h);
  return true;
}

REGISTER_NODE(
    HeightmapFile, "Primitive",
    "Import a heightfield: 8/16-bit PNG, JPG, TGA, or SRTM .hgt real-world DEM",
    [](Node &n) {
      n.add_out("output");
      add_filename(n.attrs, "path", "Heightfield image", "");
      setup_post(n);
    },
    [](Node &n) {
      std::string path = n.attrs.get_s("path");
      if (path.empty()) {
        n.error = "set a heightfield image file";
        return;
      }
      Heightmap &out = n.out_hmap("output");
      std::string err;
      if (!load_heightfield(path, out, err)) {
        n.error = err;
        return;
      }
      apply_post(n, out);
    })

REGISTER_NODE(
    Stamp, "Primitive", "Terrain modeling: stamp a heightfield shape onto the terrain",
    [](Node &n) {
      n.add_in("input");
      n.add_in("shape", DataType::Heightmap, true);
      n.add_out("output");
      add_filename(n.attrs, "path", "Shape image (if no input)", "");
      add_vec2(n.attrs, "position", "Position", 0.5f, 0.5f, -0.5f, 1.5f);
      add_float(n.attrs, "size", "Size", 0.5f, 0.02f, 2.f);
      add_float(n.attrs, "rotation", "Rotation °", 0.f, -180.f, 180.f);
      add_float(n.attrs, "height", "Height", 0.5f, -2.f, 2.f);
      add_choice(n.attrs, "blend", "Blend",
                 {"Add", "Max (merge)", "Min (carve)", "Replace by mask"}, 1);
      add_float(n.attrs, "falloff", "Edge falloff", 0.15f, 0.f, 0.5f);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in;
      // shape source: input port, else file
      Heightmap shape;
      const Heightmap *sp = n.in_hmap("shape");
      if (sp && !sp->empty()) {
        shape = *sp;
      } else {
        std::string path = n.attrs.get_s("path");
        if (path.empty()) {
          n.error = "connect a 'shape' input or set a shape image";
          return;
        }
        shape = Heightmap(256, 256);
        std::string err;
        if (!load_heightfield(path, shape, err)) {
          n.error = err;
          return;
        }
      }
      shape.remap(0.f, 1.f);
      float cx, cy;
      n.attrs.get_vec2("position", cx, cy);
      float size = n.attrs.get_f("size", 0.5f);
      float rot = n.attrs.get_f("rotation") * 0.017453293f;
      float ca = std::cos(rot), sa = std::sin(rot);
      float hgt = n.attrs.get_f("height", 0.5f);
      int blend = n.attrs.get_choice("blend");
      float falloff = n.attrs.get_f("falloff", 0.15f);
      float mn, mx;
      in->minmax(mn, mx);
      float amp = (mx - mn) > 1e-9f ? mx - mn : 1.f;
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            // terrain uv -> stamp local uv (centered, rotated, scaled)
            float u = x / float(out.w) - cx, v = y / float(out.h) - cy;
            float lu = (u * ca - v * sa) / size + 0.5f;
            float lv = (u * sa + v * ca) / size + 0.5f;
            if (lu < 0 || lu > 1 || lv < 0 || lv > 1) continue;
            float s = shape.sample(lu, lv);
            // radial-ish falloff from stamp edges
            float b = 1.f;
            if (falloff > 1e-5f) {
              float e = std::min(std::min(lu, 1.f - lu), std::min(lv, 1.f - lv)) /
                        falloff;
              b = std::clamp(e, 0.f, 1.f);
              b = b * b * (3.f - 2.f * b);
            }
            float sv = mn + s * hgt * amp;
            float cur = out.at(x, y);
            float target = cur;
            switch (blend) {
              case 0: target = cur + (sv - mn) * b; break;
              case 1: target = std::max(cur, mn + (sv - mn) * b + cur * 0.f);
                      target = cur + std::max(sv - cur, 0.f) * b; break;
              case 2: target = cur - std::max(cur - sv, 0.f) * b; break;
              case 3: target = cur * (1 - b) + sv * b; break;
            }
            out.at(x, y) = target;
          }
      });
    })

} // namespace gpx
