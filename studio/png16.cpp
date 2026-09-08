// Geekatplay TerraForge - writing a 16-bit greyscale PNG. See the header.
#include "png16.hpp"
#include <miniz/miniz.h>
#include <cstdio>
#include <cstring>

namespace studio {

namespace {

void be32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back((uint8_t)(v >> 24));
  out.push_back((uint8_t)(v >> 16));
  out.push_back((uint8_t)(v >> 8));
  out.push_back((uint8_t)v);
}

// A PNG chunk is length, type, data, then a CRC over the type and the data -
// the length is deliberately outside the CRC.
void chunk(std::vector<uint8_t> &out, const char type[4],
           const std::vector<uint8_t> &data) {
  be32(out, (uint32_t)data.size());
  const size_t crc_from = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const uint32_t crc = (uint32_t)mz_crc32(
      MZ_CRC32_INIT, out.data() + crc_from, out.size() - crc_from);
  be32(out, crc);
}

} // namespace

bool png_write_gray16(const std::string &path, int w, int h,
                      const std::vector<uint8_t> &bytes) {
  if (w <= 0 || h <= 0 || bytes.size() != (size_t)w * h * 2) return false;

  // Filter byte 0 (None) in front of every row. Height data is smooth, so
  // Up or Paeth would compress better, but None keeps this simple and the
  // files are a megabyte either way.
  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * ((size_t)w * 2 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    const uint8_t *row = bytes.data() + (size_t)y * w * 2;
    raw.insert(raw.end(), row, row + (size_t)w * 2);
  }

  mz_ulong bound = mz_compressBound((mz_ulong)raw.size());
  std::vector<uint8_t> z(bound);
  if (mz_compress2(z.data(), &bound, raw.data(), (mz_ulong)raw.size(),
                   MZ_DEFAULT_LEVEL) != MZ_OK)
    return false;
  z.resize(bound);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t)w);
  be32(ihdr, (uint32_t)h);
  ihdr.push_back(16); // bit depth
  ihdr.push_back(0);  // colour type 0: greyscale
  ihdr.push_back(0);  // deflate
  ihdr.push_back(0);  // adaptive filtering
  ihdr.push_back(0);  // no interlace
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});

  std::FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const size_t wrote = std::fwrite(png.data(), 1, png.size(), f);
  std::fclose(f);
  return wrote == png.size();
}

} // namespace studio
