// Geekatplay TerraForge — renderer maths that needs no GL context.
//
// The culling test that matters is not "does it cull" but "does it ever cull
// something visible". A patch wrongly discarded is a hole in the terrain, so
// the safety direction is asserted directly: every point that projects inside
// the frustum must lie in a patch the culler kept.
#include "blue_noise.hpp"
#include "terrain_cull.hpp"
#include "gpx/heightmap.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_failures = 0;
static void check_fail(const char *msg, int line) {
  std::printf("  [FAIL] %s (line %d)\n", msg, line);
  ++g_failures;
}
static void check_fail(const std::string &msg, int line) {
  check_fail(msg.c_str(), line);
}
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) check_fail(msg, __LINE__);                                    \
  } while (0)

// ---------------------------------------------------------------- helpers
// Column-major, matching the renderer and glUniformMatrix4fv(transpose=FALSE).
static void mat_mul(float o[16], const float a[16], const float b[16]) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
}

// The same view/projection the renderer builds, so the test exercises the
// matrix convention the shipping code actually uses.
static void make_mvp(float mvp[16], const float eye[3], const float target[3],
                     float fovy, float aspect, float znear, float zfar) {
  float fz[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
  float fl = std::sqrt(fz[0] * fz[0] + fz[1] * fz[1] + fz[2] * fz[2]);
  for (float &v : fz) v /= fl;
  float up[3] = {0, 1, 0};
  if (std::fabs(fz[1]) > 0.999f) { up[0] = 1; up[1] = 0; }
  float sx[3] = {fz[1] * up[2] - fz[2] * up[1], fz[2] * up[0] - fz[0] * up[2],
                 fz[0] * up[1] - fz[1] * up[0]};
  float sl = std::sqrt(sx[0] * sx[0] + sx[1] * sx[1] + sx[2] * sx[2]);
  for (float &v : sx) v /= sl;
  float uy[3] = {sx[1] * fz[2] - sx[2] * fz[1], sx[2] * fz[0] - sx[0] * fz[2],
                 sx[0] * fz[1] - sx[1] * fz[0]};
  float view[16] = {sx[0], uy[0], -fz[0], 0, sx[1], uy[1], -fz[1], 0,
                    sx[2], uy[2], -fz[2], 0,
                    -(sx[0] * eye[0] + sx[1] * eye[1] + sx[2] * eye[2]),
                    -(uy[0] * eye[0] + uy[1] * eye[1] + uy[2] * eye[2]),
                    fz[0] * eye[0] + fz[1] * eye[1] + fz[2] * eye[2], 1};
  float f = 1.f / std::tan(fovy * 0.5f);
  float proj[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0,
                    0, 0, (zfar + znear) / (znear - zfar), -1,
                    0, 0, 2 * zfar * znear / (znear - zfar), 0};
  mat_mul(mvp, proj, view);
}

// Is this world point inside the clip volume? The ground truth the culler must
// never contradict.
static bool point_on_screen(const float mvp[16], float x, float y, float z) {
  float c[4];
  for (int r = 0; r < 4; ++r)
    c[r] = mvp[0 * 4 + r] * x + mvp[1 * 4 + r] * y + mvp[2 * 4 + r] * z +
           mvp[3 * 4 + r];
  if (c[3] <= 0.f) return false;
  return std::fabs(c[0]) <= c[3] && std::fabs(c[1]) <= c[3] &&
         std::fabs(c[2]) <= c[3];
}

static gpx::Heightmap ridged_map(int n) {
  gpx::Heightmap h(n, n);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float u = x / float(n - 1), v = y / float(n - 1);
      float a = std::sin(u * 11.f) * std::cos(v * 7.f);
      float b = std::sin((u + v) * 23.f) * 0.25f;
      h.v[(size_t)y * n + x] = 0.5f + 0.35f * a + b * 0.2f;
    }
  return h;
}

// ------------------------------------------------------------------- tests
static void test_frustum_extraction() {
  const float eye[3] = {0.5f, 0.6f, -1.2f}, tgt[3] = {0.5f, 0.f, 0.5f};
  float mvp[16];
  make_mvp(mvp, eye, tgt, 0.9f, 16.f / 9.f, 0.01f, 50.f);
  studio::Frustum f = studio::frustum_from_mvp(mvp);

  for (int i = 0; i < 6; ++i) {
    float len = std::sqrt(f.p[i][0] * f.p[i][0] + f.p[i][1] * f.p[i][1] +
                          f.p[i][2] * f.p[i][2]);
    CHECK(std::fabs(len - 1.f) < 1e-4f, "frustum plane is not normalised");
  }

  // A tiny box at the point the camera is aimed at must survive; one well
  // behind the camera must not.
  const float in_lo[3] = {tgt[0] - .01f, tgt[1] - .01f, tgt[2] - .01f};
  const float in_hi[3] = {tgt[0] + .01f, tgt[1] + .01f, tgt[2] + .01f};
  CHECK(studio::aabb_visible(f, in_lo, in_hi), "target box culled");
  const float bk_lo[3] = {0.4f, 0.5f, -4.1f}, bk_hi[3] = {0.6f, 0.7f, -4.0f};
  CHECK(!studio::aabb_visible(f, bk_lo, bk_hi), "box behind camera kept");
  const float side_lo[3] = {40.f, 0.f, 0.4f}, side_hi[3] = {41.f, 1.f, 0.6f};
  CHECK(!studio::aabb_visible(f, side_lo, side_hi), "box far to the side kept");
}

static void test_bounds_cover_every_texel() {
  const int N = 128, P = 16;
  gpx::Heightmap h = ridged_map(N);
  std::vector<float> b = studio::patch_height_bounds(h, P);
  CHECK(b.size() == (size_t)P * P * 2, "bounds are the wrong size");

  // Every texel a patch can reach must be inside that patch's range. Sampling
  // the patch interior is not enough: bilinear filtering reaches one texel
  // past the edge, which is the case the margin exists for.
  for (int py = 0; py < P; ++py)
    for (int px = 0; px < P; ++px) {
      float lo = b[((size_t)py * P + px) * 2];
      float hi = b[((size_t)py * P + px) * 2 + 1];
      CHECK(lo <= hi, "patch bound is inverted");
      int x0 = px * N / P, x1 = (px + 1) * N / P;
      int y0 = py * N / P, y1 = (py + 1) * N / P;
      for (int y = y0; y <= y1 && y < N; ++y)
        for (int x = x0; x <= x1 && x < N; ++x) {
          float v = h.v[(size_t)y * N + x];
          if (v < lo - 1e-6f || v > hi + 1e-6f) {
            check_fail("a texel inside the patch is outside its bound",
                       __LINE__);
            return;
          }
        }
    }
}

// A guard against the bound being computed from something too coarse. The
// picking copy is 256 across; a spike narrower than that would vanish from it.
static void test_bounds_catch_a_single_spike() {
  const int N = 256, P = 64;
  gpx::Heightmap h(N, N);
  for (float &v : h.v) v = 0.2f;
  h.v[(size_t)130 * N + 130] = 0.95f;
  std::vector<float> b = studio::patch_height_bounds(h, P);
  int px = 130 * P / N, py = 130 * P / N;
  CHECK(b[((size_t)py * P + px) * 2 + 1] > 0.9f,
        "a one-texel spike is missing from its patch bound");
}

// The one that matters. Every point the camera can see must belong to a patch
// the culler kept.
static void test_culling_never_hides_visible_ground() {
  const int N = 256, P = 64;
  gpx::Heightmap h = ridged_map(N);
  std::vector<float> bounds = studio::patch_height_bounds(h, P);
  const float hscale = 0.22f;
  const float pad = studio::cull_pad(0.05f, true, 0.0025f, 0.05f);

  struct View { float eye[3], tgt[3]; float radius; };
  const View views[] = {
      {{0.5f, 0.6f, -1.0f}, {0.5f, 0.05f, 0.5f}, 0.f},
      {{0.5f, 0.6f, -1.0f}, {0.5f, 0.05f, 0.5f}, 1275.f},
      {{0.1f, 0.25f, 0.1f}, {0.9f, 0.10f, 0.9f}, 1275.f},
      {{0.5f, 2.5f, 0.5f}, {0.5f, 0.00f, 0.55f}, 1275.f},  // looking down
      {{0.5f, 0.15f, 0.5f}, {1.4f, 0.15f, 0.5f}, 1275.f},  // inside the tile
      {{-0.4f, 0.5f, -0.4f}, {0.3f, 0.0f, 0.3f}, 0.f},
  };

  for (const View &vw : views) {
    float mvp[16];
    make_mvp(mvp, vw.eye, vw.tgt, 0.9f, 16.f / 9.f, 0.005f, 60.f);
    studio::Frustum f = studio::frustum_from_mvp(mvp);

    // Which patches would the shader keep?
    std::vector<char> kept((size_t)P * P, 0);
    const float inv = 1.f / P;
    for (int py = 0; py < P; ++py)
      for (int px = 0; px < P; ++px) {
        float x0 = px * inv, x1 = (px + 1) * inv;
        float z0 = py * inv, z1 = (py + 1) * inv;
        size_t i = ((size_t)py * P + px) * 2;
        float ylo = bounds[i] * hscale - pad, yhi = bounds[i + 1] * hscale + pad;
        if (vw.radius > 0.f) {
          float dx = std::max({x0 - vw.eye[0], 0.f, vw.eye[0] - x1});
          float dz = std::max({z0 - vw.eye[2], 0.f, vw.eye[2] - z1});
          float fx = std::max(std::fabs(x0 - vw.eye[0]), std::fabs(x1 - vw.eye[0]));
          float fz = std::max(std::fabs(z0 - vw.eye[2]), std::fabs(z1 - vw.eye[2]));
          yhi -= (dx * dx + dz * dz) / (2.f * vw.radius);
          ylo -= (fx * fx + fz * fz) / (2.f * vw.radius);
        }
        const float lo[3] = {x0, ylo, z0}, hi[3] = {x1, yhi, z1};
        kept[(size_t)py * P + px] = studio::aabb_visible(f, lo, hi) ? 1 : 0;
      }

    // Now place real surface points, exactly as the shader places them, and
    // demand that any one on screen lives in a kept patch.
    int checked = 0, on_screen = 0;
    const int S = 5; // samples per patch edge
    for (int py = 0; py < P; ++py)
      for (int px = 0; px < P; ++px)
        for (int sy = 0; sy <= S; ++sy)
          for (int sx = 0; sx <= S; ++sx) {
            float u = (px + sx / float(S)) * inv;
            float v = (py + sy / float(S)) * inv;
            int hx = std::min((int)(u * (N - 1)), N - 1);
            int hy = std::min((int)(v * (N - 1)), N - 1);
            float y = h.v[(size_t)hy * N + hx] * hscale;
            if (vw.radius > 0.f) {
              float dx = u - vw.eye[0], dz = v - vw.eye[2];
              y -= (dx * dx + dz * dz) / (2.f * vw.radius);
            }
            ++checked;
            if (!point_on_screen(mvp, u, y, v)) continue;
            ++on_screen;
            if (!kept[(size_t)py * P + px]) {
              check_fail("culled a patch containing a point that is on screen",
                         __LINE__);
              return;
            }
          }
    CHECK(checked > 0, "no sample points tested");
    CHECK(on_screen > 0, "no sample point was on screen — the view is vacuous");
  }
}

// The mirror of the above: the feature has to actually do something, or the
// safety test passes trivially and we ship a no-op.
//
// It is worth being precise about *when* it does something, because the answer
// is not flattering everywhere. A camera pulled back far enough to frame the
// whole tile sees every patch, and culling correctly removes none of them; the
// win is entirely in the views where most of the terrain is off screen, which
// is what standing on the ground looks like. Both cases are asserted so
// neither can quietly change.
static void test_culling_actually_culls() {
  const int N = 256, P = 64;
  gpx::Heightmap h = ridged_map(N);
  std::vector<float> bounds = studio::patch_height_bounds(h, P);
  const float pad = studio::cull_pad(0.f, false, 0.0025f, 0.f);
  auto visible_from = [&](const float eye[3], const float tgt[3], float fovy) {
    float mvp[16];
    make_mvp(mvp, eye, tgt, fovy, 16.f / 9.f, 0.005f, 60.f);
    return studio::patches_visible(studio::frustum_from_mvp(mvp), bounds, P,
                                   0.22f, pad, eye, 1275.f);
  };

  // Standing on the terrain looking across it: most of the tile is behind or
  // beside the camera. This is the case the feature exists for.
  const float ground[3] = {0.5f, 0.15f, 0.5f}, across[3] = {1.4f, 0.15f, 0.5f};
  int on_ground = visible_from(ground, across, 0.9f);
  CHECK(on_ground > 0, "everything was culled at ground level");
  CHECK(on_ground < P * P / 2,
        "a ground-level view kept more than half the grid");

  // Pulled back to frame the whole tile, nothing may be culled — every patch
  // really is on screen, and removing any of them would be the hole this
  // whole test file exists to prevent.
  const float back[3] = {0.5f, 0.6f, -1.0f}, centre[3] = {0.5f, 0.05f, 0.5f};
  CHECK(visible_from(back, centre, 0.9f) == P * P,
        "a view framing the whole tile culled part of it");

  // Facing away from the terrain entirely, nothing should survive.
  const float away[3] = {0.5f, 0.5f, -2.f}, far_off[3] = {0.5f, 0.5f, -20.f};
  CHECK(visible_from(away, far_off, 0.9f) == 0,
        "patches survived with the camera pointed away from them");
}

static void test_cull_pad_covers_each_contribution() {
  CHECK(studio::cull_pad(0.f, false, 0.f, 0.f) == 0.f,
        "pad is non-zero with nothing displacing");
  CHECK(studio::cull_pad(0.3f, true, 0.f, 0.f) >= 0.3f,
        "pad ignores the displacement map");
  CHECK(studio::cull_pad(0.3f, false, 0.f, 0.f) == 0.f,
        "pad counts a displacement map that is not bound");
  CHECK(studio::cull_pad(0.f, false, 0.4f, 0.f) >= 0.2f,
        "pad ignores fractal relief");
  CHECK(studio::cull_pad(0.f, false, 0.f, 0.5f) >= 0.5f,
        "pad is smaller than the field strength it must cover");
  CHECK(studio::cull_pad(0.f, false, 0.f, -0.5f) > 0.f,
        "a negative field strength gives no pad");
}

// ------------------------------------------------------------- blue noise
// The whole point of blue noise is a property white noise does not have, so
// the test has to measure that property rather than just check the values are
// in range — otherwise `rand()` would pass.
//
// The property: no low-frequency content. Neighbouring pixels are pushed
// apart, so the mean absolute difference between adjacent samples is much
// higher than for white noise. For uniform white noise that mean is 1/3; a
// good blue-noise pattern sits well above it.
static void test_blue_noise() {
  const int N = 64;
  std::vector<float> p = studio::blue_noise_pattern(N, 0x9E3779B9u);
  CHECK(p.size() == (size_t)N * N, "blue noise is the wrong size");

  double sum = 0;
  float lo = 2.f, hi = -1.f;
  for (float v : p) {
    CHECK(v >= 0.f && v < 1.0001f, "blue noise value outside [0,1)");
    sum += v;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  double mean = sum / p.size();
  CHECK(std::fabs(mean - 0.5) < 0.02, "blue noise is not centred on 0.5");
  CHECK(lo < 0.01f && hi > 0.99f, "blue noise does not use its full range");

  // Every rank appears exactly once: void-and-cluster is a permutation, and a
  // duplicate would mean a pixel was assigned twice or never.
  std::vector<int> seen(N * N, 0);
  for (float v : p) {
    int r = (int)(v * N * N + 0.5f);
    if (r >= 0 && r < N * N) seen[r]++;
  }
  bool permutation = true;
  for (int c : seen)
    if (c != 1) { permutation = false; break; }
  CHECK(permutation, "blue noise is not a permutation of its ranks");

  auto neighbour_diff = [&](const std::vector<float> &a) {
    double d = 0;
    int cnt = 0;
    for (int y = 0; y < N; ++y)
      for (int x = 0; x < N; ++x) {
        float c = a[(size_t)y * N + x];
        d += std::fabs(c - a[(size_t)y * N + (x + 1) % N]);
        d += std::fabs(c - a[(size_t)((y + 1) % N) * N + x]);
        cnt += 2;
      }
    return d / cnt;
  };

  // White noise for comparison, from the same kind of hash the shader used
  // before this existed.
  std::vector<float> white((size_t)N * N);
  uint32_t s = 12345u;
  for (float &v : white) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    v = (float)(s % 100000u) / 100000.f;
  }

  // Measured: 0.427 for this pattern against 0.333 for white noise, so 1.15x
  // is a floor with room, not a threshold tuned to just pass. A regression in
  // the generator shows up here before it shows up on screen.
  double blue_d = neighbour_diff(p), white_d = neighbour_diff(white);
  CHECK(blue_d > white_d * 1.15,
        "the pattern is no more decorrelated at short range than white noise "
        "— it is not blue");

  // Deterministic: the same seed must give the same pattern on every machine,
  // or the dither differs between two people looking at the same scene.
  std::vector<float> again = studio::blue_noise_pattern(N, 0x9E3779B9u);
  CHECK(again == p, "blue noise is not deterministic for a fixed seed");
  std::vector<float> other = studio::blue_noise_pattern(N, 7u);
  CHECK(other != p, "the seed does not change the pattern");
}

int test_render_hdr_run(); // test_render_hdr.cpp

int main() {
  std::printf("renderer maths tests\n");
  g_failures += test_render_hdr_run();
  test_blue_noise();
  test_frustum_extraction();
  test_bounds_cover_every_texel();
  test_bounds_catch_a_single_spike();
  test_culling_never_hides_visible_ground();
  test_culling_actually_culls();
  test_cull_pad_covers_each_contribution();
  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all renderer maths tests passed\n");
  return 0;
}
