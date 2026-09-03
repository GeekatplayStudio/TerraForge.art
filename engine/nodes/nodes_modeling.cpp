// Geekatplay TerraForge — terrain modeling nodes: import heightfield maps
// and stamp/sculpt them onto the terrain.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "stb_image.h" // impl in nodes_materials.cpp
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace gpx {

// Minimal uncompressed GeoTIFF reader: single-band int16/uint16/float32 DEM
// tiles, strip-organized, either endianness. Compressed TIFFs are refused
// with a message that says so rather than half-read.
static bool load_geotiff(const std::string &path, Heightmap &out,
                         std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot load: " + path;
    return false;
  }
  std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
  if (d.size() < 8) {
    err = "not a TIFF: " + path;
    return false;
  }
  bool le = d[0] == 'I';
  auto u16 = [&](size_t o) -> uint32_t {
    return le ? d[o] | (d[o + 1] << 8) : (d[o] << 8) | d[o + 1];
  };
  auto u32 = [&](size_t o) -> uint32_t {
    return le ? d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)
              : (d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3];
  };
  if (u16(2) != 42) {
    err = "not a classic TIFF: " + path;
    return false;
  }
  size_t ifd = u32(4);
  if (ifd + 2 > d.size()) { err = "truncated TIFF"; return false; }
  uint32_t n = u16(ifd);
  uint32_t width = 0, height = 0, bits = 16, compression = 1, fmt = 1,
           rows_per_strip = 0xffffffffu;
  std::vector<uint32_t> strip_off, strip_cnt;
  auto read_vals = [&](size_t entry, uint32_t count, uint32_t type,
                       std::vector<uint32_t> &vals) {
    uint32_t sz = type == 3 ? 2 : 4;
    size_t src = count * sz <= 4 ? entry + 8 : u32(entry + 8);
    for (uint32_t i = 0; i < count; ++i)
      vals.push_back(type == 3 ? u16(src + i * 2) : u32(src + i * 4));
  };
  for (uint32_t i = 0; i < n; ++i) {
    size_t e = ifd + 2 + (size_t)i * 12;
    if (e + 12 > d.size()) break;
    uint32_t tag = u16(e), type = u16(e + 2), count = u32(e + 4);
    std::vector<uint32_t> vals;
    switch (tag) {
      case 256: read_vals(e, 1, type, vals); width = vals[0]; break;
      case 257: read_vals(e, 1, type, vals); height = vals[0]; break;
      case 258: read_vals(e, 1, type, vals); bits = vals[0]; break;
      case 259: read_vals(e, 1, type, vals); compression = vals[0]; break;
      case 273: read_vals(e, count, type, strip_off); break;
      case 278: read_vals(e, 1, type, vals); rows_per_strip = vals[0]; break;
      case 279: read_vals(e, count, type, strip_cnt); break;
      case 339: read_vals(e, 1, type, vals); fmt = vals[0]; break;
      default: break;
    }
  }
  if (compression != 1) {
    err = "compressed GeoTIFF (compression " + std::to_string(compression) +
          ") - re-export uncompressed (gdal_translate -co COMPRESS=NONE)";
    return false;
  }
  if (!width || !height || strip_off.empty()) {
    err = "unsupported TIFF layout: " + path;
    return false;
  }
  size_t bpp = bits / 8;
  Heightmap src((int)width, (int)height);
  size_t row = 0;
  for (size_t s = 0; s < strip_off.size() && row < height; ++s) {
    size_t off = strip_off[s];
    size_t rows = std::min<size_t>(rows_per_strip, height - row);
    for (size_t r = 0; r < rows; ++r, ++row) {
      for (size_t x = 0; x < width; ++x) {
        size_t o = off + (r * width + x) * bpp;
        if (o + bpp > d.size()) { err = "truncated TIFF data"; return false; }
        float v = 0;
        if (bits == 32 && fmt == 3) {
          uint32_t u = u32(o);
          std::memcpy(&v, &u, 4);
        } else if (bits == 16) {
          uint32_t u = u16(o);
          v = fmt == 2 ? (float)(int16_t)u : (float)u;
        } else if (bits == 8) {
          v = (float)d[o];
        } else {
          err = "unsupported TIFF sample: " + std::to_string(bits) + " bit";
          return false;
        }
        src.v[row * width + x] = v;
      }
    }
  }
  float lo = 1e30f, hi = -1e30f;
  for (float v : src.v) {
    if (!std::isfinite(v) || v < -12000.f) continue; // nodata sentinels
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  float span = hi - lo > 1e-6f ? hi - lo : 1.f;
  for (float &v : src.v)
    v = (!std::isfinite(v) || v < -12000.f) ? 0.f : (v - lo) / span;
  out = src.resampled(out.w, out.h);
  return true;
}

static bool load_heightfield(const std::string &path, Heightmap &out,
                             std::string &err) {
  int iw, ih, comp;
  auto ext_is = [&](const char *e) {
    size_t n2 = std::strlen(e);
    if (path.size() < n2) return false;
    for (size_t i = 0; i < n2; ++i)
      if (std::tolower((unsigned char)path[path.size() - n2 + i]) != e[i])
        return false;
    return true;
  };
  if (ext_is(".tif") || ext_is(".tiff")) return load_geotiff(path, out, err);
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
