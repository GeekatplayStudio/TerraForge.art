// Geekatplay TerraForge — per-patch visibility for the terrain surface.
#include "terrain_cull.hpp"
#include "gpx/heightmap.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

namespace {

// row r, column c of a column-major GL matrix
inline float at(const float m[16], int r, int c) { return m[c * 4 + r]; }

void set_plane(float p[4], float a, float b, float c, float d) {
  float len = std::sqrt(a * a + b * b + c * c);
  if (len < 1e-20f) len = 1.f;
  p[0] = a / len;
  p[1] = b / len;
  p[2] = c / len;
  p[3] = d / len;
}

} // namespace

Frustum frustum_from_mvp(const float m[16]) {
  Frustum f;
  // rows of the matrix; clip = M * p, so row3 is the w component
  const float r0[4] = {at(m, 0, 0), at(m, 0, 1), at(m, 0, 2), at(m, 0, 3)};
  const float r1[4] = {at(m, 1, 0), at(m, 1, 1), at(m, 1, 2), at(m, 1, 3)};
  const float r2[4] = {at(m, 2, 0), at(m, 2, 1), at(m, 2, 2), at(m, 2, 3)};
  const float r3[4] = {at(m, 3, 0), at(m, 3, 1), at(m, 3, 2), at(m, 3, 3)};
  auto add = [&](int i, const float a[4], int sign) {
    set_plane(f.p[i], r3[0] + sign * a[0], r3[1] + sign * a[1],
              r3[2] + sign * a[2], r3[3] + sign * a[3]);
  };
  add(0, r0, +1); // left:   w + x >= 0
  add(1, r0, -1); // right:  w - x >= 0
  add(2, r1, +1); // bottom
  add(3, r1, -1); // top
  add(4, r2, +1); // near
  add(5, r2, -1); // far
  return f;
}

bool aabb_visible(const Frustum &f, const float lo[3], const float hi[3]) {
  for (int i = 0; i < 6; ++i) {
    const float *p = f.p[i];
    // the corner furthest along the plane normal: if even that one is on the
    // outside, every corner is, and the box cannot intersect the volume
    float x = p[0] >= 0.f ? hi[0] : lo[0];
    float y = p[1] >= 0.f ? hi[1] : lo[1];
    float z = p[2] >= 0.f ? hi[2] : lo[2];
    if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.f) return false;
  }
  return true;
}

std::vector<float> patch_height_bounds(const gpx::Heightmap &h, int patches) {
  std::vector<float> out;
  if (patches <= 0) return out;
  out.assign((size_t)patches * patches * 2, 0.f);
  if (h.w <= 0 || h.h <= 0 || h.v.empty()) return out;

  for (int py = 0; py < patches; ++py) {
    // Texel span of this patch, widened by one on every side. Bilinear
    // filtering inside the patch can reach the neighbouring texel, and a bound
    // that does not cover what the shader can sample is a bound that lies.
    int y0 = (int)std::floor((float)py / patches * h.h) - 1;
    int y1 = (int)std::ceil((float)(py + 1) / patches * h.h) + 1;
    y0 = std::max(y0, 0);
    y1 = std::min(y1, h.h - 1);
    for (int px = 0; px < patches; ++px) {
      int x0 = (int)std::floor((float)px / patches * h.w) - 1;
      int x1 = (int)std::ceil((float)(px + 1) / patches * h.w) + 1;
      x0 = std::max(x0, 0);
      x1 = std::min(x1, h.w - 1);
      float lo = h.v[(size_t)y0 * h.w + x0], hi = lo;
      for (int y = y0; y <= y1; ++y) {
        const float *row = &h.v[(size_t)y * h.w];
        for (int x = x0; x <= x1; ++x) {
          lo = std::min(lo, row[x]);
          hi = std::max(hi, row[x]);
        }
      }
      size_t i = ((size_t)py * patches + px) * 2;
      out[i] = lo;
      out[i + 1] = hi;
    }
  }
  return out;
}

float cull_pad(float disp_strength, bool has_disp, float fractal_amount,
               float field_strength) {
  float pad = 0.f;
  if (has_disp) pad += std::fabs(disp_strength);
  // gp_detail returns 0..1 and the shader subtracts 0.5 before scaling
  pad += 0.5f * std::fabs(fractal_amount);
  // A field graph's output range is unbounded in principle. Four times the
  // strength covers noise well past its nominal -1..1 and still leaves the
  // bound far tighter than no culling at all.
  pad += 4.f * std::fabs(field_strength);
  return pad;
}

int patches_visible(const Frustum &f, const std::vector<float> &bounds,
                    int patches, float hscale, float pad, const float cam[3],
                    float planet_radius) {
  if (patches <= 0 || (int)bounds.size() < patches * patches * 2) return 0;
  // a small planet wraps the tile round itself: no box bound holds, so the
  // shader draws every patch and this reports the same
  if (planet_radius > 0.f && planet_radius < 4.f) return patches * patches;
  int visible = 0;
  const float inv = 1.f / (float)patches;
  for (int py = 0; py < patches; ++py)
    for (int px = 0; px < patches; ++px) {
      float x0 = px * inv, x1 = (px + 1) * inv;
      float z0 = py * inv, z1 = (py + 1) * inv;
      size_t i = ((size_t)py * patches + px) * 2;
      float ylo = bounds[i] * hscale - pad;
      float yhi = bounds[i + 1] * hscale + pad;
      if (planet_radius > 0.f) {
        // The surface curves away from the tile's centre (the sphere sits
        // under it): p.y -= |p.xz - 0.5|^2 / (2R). Over the patch's footprint
        // that term is smallest at the point nearest the centre and largest
        // at the furthest corner, so the near distance lowers the top of the
        // box and the far distance lowers the bottom.
        (void)cam;
        const float c = 0.5f;
        float dx = std::max({x0 - c, 0.f, c - x1});
        float dz = std::max({z0 - c, 0.f, c - z1});
        float near2 = dx * dx + dz * dz;
        float fx = std::max(std::fabs(x0 - c), std::fabs(x1 - c));
        float fz = std::max(std::fabs(z0 - c), std::fabs(z1 - c));
        float far2 = fx * fx + fz * fz;
        yhi -= near2 / (2.f * planet_radius);
        ylo -= far2 / (2.f * planet_radius);
      }
      const float lo[3] = {x0, ylo, z0};
      const float hi[3] = {x1, yhi, z1};
      if (aabb_visible(f, lo, hi)) ++visible;
    }
  return visible;
}

} // namespace studio
