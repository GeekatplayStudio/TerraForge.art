// Geekatplay TerraForge — the HDR image files round-trip. The backdrop dome
// and every render pass go through hdr_image.cpp, so a float written must be
// the float read back, for EXR (our own reader and writer) and for Radiance
// .hdr (stb, 8-bit mantissa: approximate). Linked into render_tests.
#include "hdr_image.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace hdr_tests {

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                   \
      ++g_fail;                                                                \
    }                                                                          \
  } while (0)

static std::vector<float> pattern(int w, int h, int ch) {
  std::vector<float> v((size_t)w * h * ch);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < ch; ++c) {
        // values well outside 0..1, negative too: a depth pass and a normal
        // pass need both
        float f = (float)x * 3.7f - (float)y * 11.25f + c * 1000.f;
        if (c == 3) f = 1.f;
        v[((size_t)y * w + x) * ch + c] = f;
      }
  return v;
}

static void test_exr_roundtrip() {
  std::printf("EXR write -> read round-trip...\n");
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "gpx_hdr_test";
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::string err;
  for (int ch : {1, 3, 4}) {
    const int w = 37, h = 23; // odd sizes: no accidental alignment
    std::vector<float> src = pattern(w, h, ch);
    std::string p = (dir / ("rt_" + std::to_string(ch) + ".exr")).string();
    CHECK(studio::exr_write(p, w, h, ch, src.data(), err), "exr_write succeeds");
    studio::HdrImage img;
    CHECK(studio::hdr_image_load(p, img, err), ("exr loads: " + err).c_str());
    CHECK(img.w == w && img.h == h, "size survives");
    if (img.w != w || img.h != h) continue;
    bool exact = true;
    for (int y = 0; y < h && exact; ++y)
      for (int x = 0; x < w && exact; ++x)
        for (int c = 0; c < 3; ++c) {
          float want = ch == 1 ? src[(size_t)y * w + x] : src[((size_t)y * w + x) * ch + c];
          if (img.rgb[((size_t)y * w + x) * 3 + c] != want) exact = false;
        }
    CHECK(exact, "every float comes back bit-exact (row order, channel order)");
  }
  // a file that is not EXR is refused with a reason, never read as garbage
  {
    std::string p = (dir / "not.exr").string();
    FILE *f = std::fopen(p.c_str(), "wb");
    if (f) { std::fputs("hello", f); std::fclose(f); }
    studio::HdrImage img;
    CHECK(!studio::hdr_image_load(p, img, err) && !err.empty(), "a bad EXR is refused with a message");
  }
}

static void test_hdr_roundtrip() {
  std::printf("Radiance .hdr write -> read round-trip...\n");
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "gpx_hdr_test";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const int w = 64, h = 16;
  std::vector<float> src((size_t)w * h * 3);
  for (size_t i = 0; i < src.size(); ++i) src[i] = 0.01f + (float)(i % 97) * 0.9f; // 0.01 .. 87
  std::string p = (dir / "rt.hdr").string(), err;
  CHECK(studio::hdr_write(p, w, h, 3, src.data(), err), "hdr_write succeeds");
  studio::HdrImage img;
  CHECK(studio::hdr_image_load(p, img, err), ("hdr loads: " + err).c_str());
  if (img.w == w && img.h == h) {
    // RGBE stores an 8-bit mantissa per channel against the *largest* channel's
    // exponent, so the error of any channel is a fraction of the pixel's
    // brightest channel (1/256 of it, plus stb's rounding), not of itself.
    float worst = 0.f;
    for (size_t i = 0; i < src.size(); i += 3) {
      float mx = std::max(src[i], std::max(src[i + 1], src[i + 2]));
      for (int c = 0; c < 3; ++c)
        worst = std::max(worst, std::fabs(img.rgb[i + c] - src[i + c]) / std::max(mx, 1e-3f));
    }
    CHECK(worst < 0.01f, "RGBE keeps every channel within 1 % of the pixel's brightest");
  } else {
    CHECK(false, "hdr size survives");
  }
}

int run_all() {
  test_exr_roundtrip();
  test_hdr_roundtrip();
  std::printf("  hdr image io: %d checks, %d failures\n", g_checks, g_fail);
  return g_fail;
}

} // namespace hdr_tests

int test_render_hdr_run() { return hdr_tests::run_all(); }
