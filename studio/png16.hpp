// Geekatplay TerraForge - writing a 16-bit greyscale PNG.
//
// stb_image_write only emits 8 bits, and 8 bits of grey is 256 steps of
// terrain height: scale that up over a few hundred metres and the terraces
// are plainly visible. A height map saved for editing elsewhere and brought
// back has to survive the trip, so the painter writes 16.
//
// PNG itself is small enough to emit directly - signature, IHDR, one IDAT of
// zlib-compressed filtered scanlines, IEND - and miniz is already vendored
// for the project format, so this costs a dependency nobody has to add.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace studio {

// `bytes` is w*h*2, big-endian samples, row-major - the order PNG stores.
// Returns false if the file could not be written.
bool png_write_gray16(const std::string &path, int w, int h,
                      const std::vector<uint8_t> &bytes);

} // namespace studio
