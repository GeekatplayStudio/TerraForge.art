// Geekatplay TerraForge - the point cloud thumbnail.
//
// These nodes used to draw nothing, so "does the preview work" was answered
// by squinting at the node editor. It is a pure function of the cloud, so it
// can be answered here instead: does a clustered cloud actually look
// different from an even one, does a path draw as a connected line, and does
// a single point survive being drawn next to a crowd.
#include "gpx/points_thumb.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static std::string g_case;

static void check(bool ok, const std::string &what) {
  if (ok) return;
  std::printf("  FAIL [%s] %s\n", g_case.c_str(), what.c_str());
  ++failures;
}

static const int W = 112;

// The red channel carries the ramp; 26 is the background, 255 the brightest.
static int lum(const std::vector<uint8_t> &px, int x, int y) {
  return px[((size_t)y * W + x) * 4];
}
static int lit_pixels(const std::vector<uint8_t> &px, int above = 40) {
  int n = 0;
  for (size_t i = 0; i < px.size(); i += 4)
    if (px[i] > above) ++n;
  return n;
}

static uint32_t rng_state = 12345u;
static float rnd() {
  rng_state = rng_state * 1664525u + 1013904223u;
  return (float)(rng_state >> 8) / 16777216.f;
}

int main() {
  std::printf("Geekatplay TerraForge - point cloud thumbnail\n\n");

  // ---- an empty cloud is background, not a crash or a white square -----
  {
    g_case = "empty";
    gpx::PointCloud pc;
    auto px = gpx::points_thumbnail(pc, W, false);
    check(px.size() == (size_t)W * W * 4, "is the size asked for");
    check(lit_pixels(px) == 0, "draws nothing");
    check(px[3] == 255, "is opaque");
  }

  // ---- one point is visible -------------------------------------------
  //
  // The ramp normalises by the brightest pixel, so a lone point is the
  // brightest thing in its own picture. This is the case that would break if
  // somebody normalised by a fixed constant instead.
  {
    g_case = "single point";
    gpx::PointCloud pc;
    pc.add(0.5f, 0.5f, 1.f);
    auto px = gpx::points_thumbnail(pc, W, false);
    check(lit_pixels(px) > 0, "puts something on the canvas");
    check(lum(px, W / 2, W / 2) > 200, "and it is bright");
    check(lum(px, 4, 4) < 40, "leaving the rest dark");
  }

  // ---- clustered reads differently from even ---------------------------
  //
  // This is the whole point of the preview: the rocks that would not cluster
  // were diagnosed by eye, and this is the picture that would have shown it.
  {
    g_case = "clustered vs even";
    const int N = 2000;
    gpx::PointCloud even, clumped;
    for (int i = 0; i < N; ++i) even.add(rnd(), rnd(), 1.f);
    for (int i = 0; i < N; ++i) {
      // twenty tight knots instead of an even spread
      float cx = (float)(i % 20 % 5) * 0.2f + 0.1f;
      float cy = (float)(i % 20 / 5) * 0.25f + 0.12f;
      clumped.add(cx + (rnd() - 0.5f) * 0.02f, cy + (rnd() - 0.5f) * 0.02f, 1.f);
    }
    auto a = gpx::points_thumbnail(even, W, false);
    auto b = gpx::points_thumbnail(clumped, W, false);
    check(lit_pixels(b) * 3 < lit_pixels(a),
          "clustered covers far less of the canvas (" +
              std::to_string(lit_pixels(b)) + " lit vs " +
              std::to_string(lit_pixels(a)) + ")");
    check(lit_pixels(a) > W * W / 4,
          "an even scatter of 2000 covers the tile");
  }

  // ---- a path is a line, not a constellation ---------------------------
  {
    g_case = "path";
    gpx::PointCloud pc;
    pc.add(0.05f, 0.5f, 1.f);
    pc.add(0.95f, 0.5f, 1.f); // two points, a tile's width apart
    auto dots = gpx::points_thumbnail(pc, W, false);
    auto line = gpx::points_thumbnail(pc, W, true);
    check(lit_pixels(line) > lit_pixels(dots) * 10,
          "the segment between them is drawn (" +
              std::to_string(lit_pixels(line)) + " lit vs " +
              std::to_string(lit_pixels(dots)) + ")");
    // and it is continuous: every column between the ends is lit
    int gaps = 0;
    for (int x = 8; x < W - 8; ++x) {
      bool any = false;
      for (int y = 0; y < W; ++y)
        if (lum(line, x, y) > 40) any = true;
      if (!any) ++gaps;
    }
    check(gaps == 0, "with no gaps along it, found " + std::to_string(gaps));
  }

  // ---- out of range positions leave the picture, not smear the edge ----
  //
  // Clouds arrive from PointsTransform and from CSV files, and neither
  // promises 0..1. Clamping them to the border would paint a bright rim that
  // is not there - and a rim is exactly what you would see if a transform had
  // worked, so it would hide the bug it was meant to reveal.
  {
    g_case = "out of range";
    gpx::PointCloud pc;
    for (int i = 0; i < 500; ++i) pc.add(-5.f + rnd(), rnd(), 1.f);
    auto px = gpx::points_thumbnail(pc, W, false);
    check(px.size() == (size_t)W * W * 4, "survives and returns a picture");
    check(lit_pixels(px) == 0,
          "a cloud entirely off the tile draws nothing, found " +
              std::to_string(lit_pixels(px)) + " lit");

    // one point on the tile among a hundred off it is still the picture
    gpx::PointCloud mixed;
    mixed.add(0.5f, 0.5f, 1.f);
    for (int i = 0; i < 100; ++i) mixed.add(3.f + rnd(), rnd(), 1.f);
    auto m = gpx::points_thumbnail(mixed, W, false);
    check(lum(m, W / 2, W / 2) > 200, "the one point on the tile is drawn");
    check(lum(m, W - 1, W / 2) < 40, "and the edge is not smeared");
  }

  // ---- a path that runs far off the tile is clipped, not walked --------
  //
  // A segment a million pixels long stepped one pixel at a time is a hung
  // preview, and a bad CSV is all it takes.
  {
    g_case = "path far off the tile";
    gpx::PointCloud pc;
    pc.add(-1e6f, 0.5f, 1.f);
    pc.add(1e6f, 0.5f, 1.f);
    auto px = gpx::points_thumbnail(pc, W, true);
    int gaps = 0;
    for (int x = 0; x < W; ++x) {
      bool any = false;
      for (int y = 0; y < W; ++y)
        if (lum(px, x, y) > 40) any = true;
      if (!any) ++gaps;
    }
    check(gaps == 0, "the visible part is still drawn end to end, " +
                         std::to_string(gaps) + " columns empty");
  }

  // ---- a path with no part on the tile draws nothing -------------------
  //
  // And, just as importantly, does not walk the segment to find that out:
  // without the clip this is 200 million pixel steps for a picture of
  // nothing at all.
  {
    g_case = "path off the tile";
    gpx::PointCloud pc;
    pc.add(2.f, 2.f, 1.f);
    pc.add(1e7f, 3.f, 1.f);
    pc.add(-1e7f, 5.f, 1.f);
    auto t0 = std::chrono::steady_clock::now();
    auto px = gpx::points_thumbnail(pc, W, true);
    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
    check(lit_pixels(px) == 0, "nothing drawn, found " +
                                   std::to_string(lit_pixels(px)) + " lit");
    // The pixels alone cannot tell the difference: walking two billion steps
    // that all land off the canvas draws nothing too. The clip is the whole
    // reason this returns, so the clock is what tests it.
    check(ms < 50.0, "and it returns at once, took " +
                         std::to_string((int)ms) + " ms");
  }

  // ---- nothing here is undefined behaviour on a bad number -------------
  {
    g_case = "not a number";
    gpx::PointCloud pc;
    pc.add(std::nanf(""), 0.5f, 1.f);
    pc.add(0.5f, std::nanf(""), 1.f);
    pc.add(INFINITY, 0.5f, 1.f);
    pc.add(0.25f, 0.25f, 1.f); // one real point, so there is something to see
    auto px = gpx::points_thumbnail(pc, W, false);
    check(lum(px, W / 4, W / 4) > 200, "the real point survives the bad ones");
    auto path = gpx::points_thumbnail(pc, W, true);
    check(path.size() == (size_t)W * W * 4, "as a path too");
  }

  // ---- same cloud, same picture ----------------------------------------
  {
    g_case = "deterministic";
    gpx::PointCloud pc;
    for (int i = 0; i < 300; ++i) pc.add(rnd(), rnd(), 1.f);
    check(gpx::points_thumbnail(pc, W, false) ==
              gpx::points_thumbnail(pc, W, false),
          "drawn twice is drawn the same");
  }

  if (failures)
    std::printf("\n%d failure(s)\n", failures);
  else
    std::printf("\nall passed\n");
  return failures ? 1 : 0;
}
