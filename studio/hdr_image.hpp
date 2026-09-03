// Geekatplay TerraForge — HDR image files in and out.
//
// The backdrop dome and the render passes both need true floating-point
// images: a dome that clips at 1.0 has no sun in it, and a depth pass in
// 8 bits is a poster. Radiance .hdr comes from stb_image; OpenEXR is read and
// written here directly (scanline images, NONE / RLE / ZIPS / ZIP
// compression, HALF / FLOAT / UINT channels) because the full library is
// larger than this whole application and the subset every HDRI site ships is
// small. Anything else (.png, .jpg) loads through stb_image as linear light.
#pragma once
#include <string>
#include <vector>

namespace studio {

struct HdrImage {
  int w = 0, h = 0;
  std::vector<float> rgb; // linear, row 0 = top of the picture
  bool empty() const { return rgb.empty(); }
};

// Loads .exr / .hdr / .pic and any LDR format stb_image knows (converted to
// linear). Returns false and fills `err` on failure.
bool hdr_image_load(const std::string &path, HdrImage &img, std::string &err);

// Writes a scanline OpenEXR file with FLOAT channels, uncompressed. `channels`
// is 1 (Y), 3 (RGB) or 4 (RGBA); `data` is interleaved, row 0 = top.
bool exr_write(const std::string &path, int w, int h, int channels,
               const float *data, std::string &err);

// Writes a Radiance .hdr (RGB only; a fourth channel is dropped).
bool hdr_write(const std::string &path, int w, int h, int channels,
               const float *data, std::string &err);

} // namespace studio
