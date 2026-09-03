// Geekatplay TerraForge — HDR image files in and out. See hdr_image.hpp.
//
// The OpenEXR reader follows the file layout in the OpenEXR technical
// introduction: magic, version, attribute list, offset table, chunks. Each
// chunk is one or more scanlines, every channel of a line stored contiguously
// in the order the header lists them (alphabetical). ZIP/RLE payloads are
// followed by the two-step reconstruction the format specifies: undo the
// delta predictor, then de-interleave the two halves.
#include "hdr_image.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <miniz/miniz.h>
#include "stb_image.h"
#include "stb_image_write.h"

namespace studio {

namespace {

std::string lower_ext(const std::string &path) {
  size_t dot = path.find_last_of('.');
  std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
  for (char &c : e) c = (char)std::tolower((unsigned char)c);
  return e;
}

float half_to_float(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff, f;
  if (e == 0) {
    if (m == 0) f = s << 31;
    else { // subnormal
      e = 1;
      while (!(m & 0x400)) { m <<= 1; --e; }
      m &= 0x3ff;
      f = (s << 31) | ((e + 112) << 23) | (m << 13);
    }
  } else if (e == 31) {
    f = (s << 31) | 0x7f800000 | (m << 13);
  } else {
    f = (s << 31) | ((e + 112) << 23) | (m << 13);
  }
  float out;
  std::memcpy(&out, &f, 4);
  return out;
}

struct Reader {
  const uint8_t *p, *end;
  bool ok = true;
  template <class T> T rd() {
    T v{};
    if (p + sizeof(T) > end) { ok = false; return v; }
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
  }
  std::string str() {
    std::string s;
    while (p < end && *p) s.push_back((char)*p++);
    if (p < end) ++p;
    return s;
  }
};

struct Channel {
  std::string name;
  int type = 2; // 0 uint, 1 half, 2 float
  int bytes() const { return type == 1 ? 2 : 4; }
};

// EXR "reconstruct": delta predictor then de-interleave (the inverse of what
// the compressor did before zlib / RLE).
void exr_reconstruct(std::vector<uint8_t> &buf) {
  for (size_t i = 1; i < buf.size(); ++i) {
    int d = (int)buf[i - 1] + (int)buf[i] - 128;
    buf[i] = (uint8_t)d;
  }
  std::vector<uint8_t> out(buf.size());
  const uint8_t *t1 = buf.data();
  const uint8_t *t2 = buf.data() + (buf.size() + 1) / 2;
  for (size_t i = 0; i < buf.size(); ++i) out[i] = (i & 1) ? *t2++ : *t1++;
  buf.swap(out);
}

bool exr_rle_decode(const uint8_t *src, size_t n, std::vector<uint8_t> &out) {
  size_t i = 0;
  while (i < n) {
    int count = (int8_t)src[i++];
    if (count < 0) {
      count = -count;
      if (i + count > n) return false;
      out.insert(out.end(), src + i, src + i + count);
      i += count;
    } else {
      if (i >= n) return false;
      out.insert(out.end(), (size_t)count + 1, src[i++]);
    }
  }
  return true;
}

bool load_exr(const std::string &path, HdrImage &img, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { err = "cannot open " + path; return false; }
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
  Reader r{file.data(), file.data() + file.size()};
  if (r.rd<uint32_t>() != 0x01312f76u) { err = "not an OpenEXR file"; return false; }
  uint32_t version = r.rd<uint32_t>();
  if (version & (0x200 | 0x800 | 0x1000)) {
    err = "tiled, deep or multi-part EXR is not supported (scanline only)";
    return false;
  }
  std::vector<Channel> channels;
  int compression = 0, xmin = 0, ymin = 0, xmax = -1, ymax = -1, line_order = 0;
  for (;;) {
    std::string name = r.str();
    if (name.empty() || !r.ok) break;
    std::string type = r.str();
    int32_t size = r.rd<int32_t>();
    if (!r.ok || size < 0 || r.p + size > r.end) { err = "corrupt header"; return false; }
    Reader a{r.p, r.p + size};
    if (name == "channels" && type == "chlist") {
      for (;;) {
        std::string cn = a.str();
        if (cn.empty() || !a.ok) break;
        Channel c;
        c.name = cn;
        c.type = a.rd<int32_t>();
        a.rd<uint8_t>(); a.rd<uint8_t>(); a.rd<uint8_t>(); a.rd<uint8_t>();
        a.rd<int32_t>(); a.rd<int32_t>(); // sampling
        channels.push_back(c);
      }
    } else if (name == "compression") {
      compression = a.rd<uint8_t>();
    } else if (name == "dataWindow") {
      xmin = a.rd<int32_t>(); ymin = a.rd<int32_t>();
      xmax = a.rd<int32_t>(); ymax = a.rd<int32_t>();
    } else if (name == "lineOrder") {
      line_order = a.rd<uint8_t>();
    }
    r.p += size;
  }
  if (compression > 3) {
    err = "EXR compression " + std::to_string(compression) +
          " (PIZ/PXR24/B44/DWA) is not supported; save as ZIP or uncompressed";
    return false;
  }
  int w = xmax - xmin + 1, h = ymax - ymin + 1;
  if (w <= 0 || h <= 0 || w > 32768 || h > 32768) { err = "bad data window"; return false; }
  if (channels.empty()) { err = "no channels"; return false; }
  const int lines_per_block = compression == 3 ? 16 : 1;
  const int blocks = (h + lines_per_block - 1) / lines_per_block;
  std::vector<uint64_t> offsets(blocks);
  for (int i = 0; i < blocks; ++i) offsets[i] = r.rd<uint64_t>();
  if (!r.ok) { err = "truncated offset table"; return false; }

  size_t line_bytes = 0;
  for (const Channel &c : channels) line_bytes += (size_t)w * c.bytes();
  // which channels feed R, G, B (Y alone is greyscale)
  int ci_r = -1, ci_g = -1, ci_b = -1, ci_y = -1;
  for (int i = 0; i < (int)channels.size(); ++i) {
    const std::string &n = channels[i].name;
    if (n == "R") ci_r = i; else if (n == "G") ci_g = i; else if (n == "B") ci_b = i;
    else if (n == "Y") ci_y = i;
  }
  if (ci_r < 0 && ci_y < 0) { err = "no R/G/B or Y channels"; return false; }

  img.w = w; img.h = h;
  img.rgb.assign((size_t)w * h * 3, 0.f);
  std::vector<uint8_t> raw;
  for (int b = 0; b < blocks; ++b) {
    if (offsets[b] + 8 > file.size()) { err = "chunk offset out of file"; return false; }
    Reader c{file.data() + offsets[b], file.data() + file.size()};
    int32_t y = c.rd<int32_t>();
    int32_t packed = c.rd<int32_t>();
    if (!c.ok || packed < 0 || c.p + packed > c.end) { err = "corrupt chunk"; return false; }
    int lines = std::min(lines_per_block, ymax - y + 1);
    size_t want = line_bytes * lines;
    raw.clear();
    if (compression == 0 || (size_t)packed == want) {
      raw.assign(c.p, c.p + packed);
    } else if (compression == 1) {
      if (!exr_rle_decode(c.p, packed, raw)) { err = "RLE decode failed"; return false; }
      exr_reconstruct(raw);
    } else {
      raw.resize(want);
      mz_ulong out_len = (mz_ulong)want;
      if (mz_uncompress(raw.data(), &out_len, c.p, packed) != MZ_OK) {
        err = "zlib decode failed";
        return false;
      }
      raw.resize(out_len);
      exr_reconstruct(raw);
    }
    if (raw.size() < want) { err = "short scanline block"; return false; }
    for (int li = 0; li < lines; ++li) {
      int row = (y - ymin) + li;
      if (line_order == 1) row = h - 1 - row;
      const uint8_t *line = raw.data() + line_bytes * li;
      size_t off = 0;
      for (int ch = 0; ch < (int)channels.size(); ++ch) {
        const Channel &C = channels[ch];
        int dst = ch == ci_r ? 0 : ch == ci_g ? 1 : ch == ci_b ? 2 : ch == ci_y ? 3 : -1;
        if (dst >= 0)
          for (int x = 0; x < w; ++x) {
            float v;
            const uint8_t *s = line + off + (size_t)x * C.bytes();
            if (C.type == 1) { uint16_t hv; std::memcpy(&hv, s, 2); v = half_to_float(hv); }
            else if (C.type == 2) std::memcpy(&v, s, 4);
            else { uint32_t u; std::memcpy(&u, s, 4); v = (float)u; }
            float *px = &img.rgb[((size_t)row * w + x) * 3];
            if (dst == 3) px[0] = px[1] = px[2] = v;
            else px[dst] = v;
          }
        off += (size_t)w * C.bytes();
      }
    }
  }
  return true;
}

} // namespace

bool hdr_image_load(const std::string &path, HdrImage &img, std::string &err) {
  img = HdrImage();
  std::string ext = lower_ext(path);
  if (ext == "exr") return load_exr(path, img, err);
  int w = 0, h = 0, comp = 0;
  // stb converts LDR formats to linear with its default 2.2 gamma; .hdr/.pic
  // are read as the floats they store
  float *data = stbi_loadf(path.c_str(), &w, &h, &comp, 3);
  if (!data) {
    const char *why = stbi_failure_reason();
    err = std::string("cannot load ") + path + (why ? std::string(": ") + why : "");
    return false;
  }
  img.w = w; img.h = h;
  img.rgb.assign(data, data + (size_t)w * h * 3);
  stbi_image_free(data);
  for (float &v : img.rgb)
    if (!std::isfinite(v) || v < 0.f) v = 0.f;
  return true;
}

bool exr_write(const std::string &path, int w, int h, int channels,
               const float *data, std::string &err) {
  if (channels != 1 && channels != 3 && channels != 4) { err = "1, 3 or 4 channels"; return false; }
  std::ofstream f(path, std::ios::binary);
  if (!f) { err = "cannot write " + path; return false; }
  auto put32 = [&](int32_t v) { f.write((const char *)&v, 4); };
  auto put64 = [&](uint64_t v) { f.write((const char *)&v, 8); };
  auto putf = [&](float v) { f.write((const char *)&v, 4); };
  auto attr = [&](const char *name, const char *type, int32_t size) {
    f.write(name, std::strlen(name) + 1);
    f.write(type, std::strlen(type) + 1);
    put32(size);
  };
  // channel names in the file order the format requires: alphabetical
  std::vector<std::pair<std::string, int>> chans; // name, source index
  if (channels == 1) chans = {{"Y", 0}};
  else if (channels == 3) chans = {{"B", 2}, {"G", 1}, {"R", 0}};
  else chans = {{"A", 3}, {"B", 2}, {"G", 1}, {"R", 0}};

  put32(0x01312f76); put32(2);
  int32_t chlist_size = 0;
  for (auto &c : chans) chlist_size += (int32_t)c.first.size() + 1 + 16;
  chlist_size += 1;
  attr("channels", "chlist", chlist_size);
  for (auto &c : chans) {
    f.write(c.first.c_str(), c.first.size() + 1);
    put32(2); // FLOAT
    uint8_t lin[4] = {0, 0, 0, 0};
    f.write((const char *)lin, 4);
    put32(1); put32(1);
  }
  f.put(0);
  attr("compression", "compression", 1); f.put(0);
  attr("dataWindow", "box2i", 16); put32(0); put32(0); put32(w - 1); put32(h - 1);
  attr("displayWindow", "box2i", 16); put32(0); put32(0); put32(w - 1); put32(h - 1);
  attr("lineOrder", "lineOrder", 1); f.put(0);
  attr("pixelAspectRatio", "float", 4); putf(1.f);
  attr("screenWindowCenter", "v2f", 8); putf(0.f); putf(0.f);
  attr("screenWindowWidth", "float", 4); putf(1.f);
  f.put(0); // end of header
  const uint64_t table_pos = (uint64_t)f.tellp();
  const uint64_t line_size = 8 + (uint64_t)w * 4 * chans.size();
  const uint64_t data_start = table_pos + (uint64_t)h * 8;
  for (int y = 0; y < h; ++y) put64(data_start + (uint64_t)y * line_size);
  std::vector<float> row(w);
  for (int y = 0; y < h; ++y) {
    put32(y);
    put32((int32_t)((uint64_t)w * 4 * chans.size()));
    for (auto &c : chans) {
      for (int x = 0; x < w; ++x) row[x] = data[((size_t)y * w + x) * channels + c.second];
      f.write((const char *)row.data(), (size_t)w * 4);
    }
  }
  if (!f) { err = "write failed: " + path; return false; }
  return true;
}

bool hdr_write(const std::string &path, int w, int h, int channels,
               const float *data, std::string &err) {
  std::vector<float> rgb((size_t)w * h * 3);
  for (size_t i = 0; i < (size_t)w * h; ++i)
    for (int c = 0; c < 3; ++c)
      rgb[i * 3 + c] = channels == 1 ? data[i] : data[i * channels + std::min(c, channels - 1)];
  if (!stbi_write_hdr(path.c_str(), w, h, 3, rgb.data())) {
    err = "cannot write " + path;
    return false;
  }
  return true;
}

} // namespace studio
