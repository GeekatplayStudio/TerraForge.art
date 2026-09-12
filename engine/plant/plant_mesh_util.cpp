// Geekatplay TerraForge - the two surfaces every plant part is made of
// (plant_internal.hpp: lathe, grid).
//
// Almost every part of a plant is one of two shapes. A lathe is a profile
// turned about the part's growth axis, its radius shaped by a section
// function of the angle: a trunk, a berry, a seed head, a flower's bell.
// A grid is a rectangle bent into a surface: a leaf card, a blade, a petal.
// Writing them once means every part shades, maps and winds the same way,
// and a builder is left with only the shape it actually cares about.
//
// Both append into the part the caller has already begun (MeshOut::begin),
// never start one of their own, so a builder can put several surfaces in
// one part. Normals come from two finite differences on the surface itself
// rather than from the triangles, which keeps a coarse trunk shading round;
// a builder that wants the facets can call MeshOut::geometric_normals.
// Winding is counter-clockwise seen from the front (the normal's side), the
// convention the whole engine and both exporters use; `invert` flips both
// the normal and the winding, and `double_sided` appends the same surface
// again the other way round for a part that must be seen from behind
// without a two-sided material.
//
// The seam: a lathe emits column 0 twice, once at u = 0 and once at u = 1,
// so the texture meets itself rather than smearing backwards across the
// last column.
#include "plant/plant_internal.hpp"
#include <algorithm>
#include <vector>

namespace gpx {
namespace plant {

void lathe(MeshOut &out, const Frame &f, const std::function<void(float, float &, float &)> &profile,
           const std::function<float(float, float)> &section, const LatheOpts &o, const float wind4[4],
           const float tint4[4]) {
  const int rings = std::max(o.rings, 1), cols = std::max(o.columns, 3);
  auto point = [&](float a, float t) {
    float r = 0.f, h = 0.f;
    profile(clampf(t, 0.f, 1.f), r, h);
    const float k = section ? section(a - std::floor(a), clampf(t, 0.f, 1.f)) : 1.f;
    const float ang = a * TAU;
    return V3(std::cos(ang) * r * k, std::sin(ang) * r * k, h);
  };
  // the widest radius, for the projected UV mode and the caps' mapping
  float rmax = 1e-6f;
  for (int i = 0; i <= rings; ++i) {
    float r = 0.f, h = 0.f;
    profile((float)i / (float)rings, r, h);
    rmax = std::max(rmax, std::fabs(r));
  }
  const float da = 0.25f / (float)cols, dt = 0.25f / (float)rings;
  auto emit = [&](float a, float t, bool flip) {
    const V3 p = point(a, t);
    const V3 pa = point(a + da, t) - point(a - da, t);
    const V3 pt = point(a, std::min(t + dt, 1.f)) - point(a, std::max(t - dt, 0.f));
    V3 n = cross(pa, pt);
    if (dot(n, n) < 1e-24f) n = V3(std::cos(a * TAU), std::sin(a * TAU), 0.f);
    n = normalize(n);
    if (flip) n = -n;
    float u = a * o.u_tile + o.u_offset, v = t * o.v_tile + o.v_offset;
    if (o.uv_mode == 1) { // a disc seen from above: the surface's own x, y
      u = (0.5f + 0.5f * p.x / rmax) * o.u_tile + o.u_offset;
      v = (0.5f + 0.5f * p.y / rmax) * o.v_tile + o.v_offset;
    }
    return out.vertex(f.world(p), f.dir(n), u, v, wind4, tint4);
  };
  auto build = [&](bool flip) {
    const size_t stride = (size_t)cols + 1;
    std::vector<uint32_t> ids((size_t)(rings + 1) * stride);
    for (int i = 0; i <= rings; ++i)
      for (int j = 0; j <= cols; ++j)
        ids[(size_t)i * stride + (size_t)j] = emit((float)j / (float)cols, (float)i / (float)rings, flip);
    for (int i = 0; i < rings; ++i)
      for (int j = 0; j < cols; ++j) {
        const uint32_t a = ids[(size_t)i * stride + (size_t)j];
        const uint32_t b = ids[(size_t)i * stride + (size_t)j + 1];
        const uint32_t c = ids[(size_t)(i + 1) * stride + (size_t)j + 1];
        const uint32_t d = ids[(size_t)(i + 1) * stride + (size_t)j];
        if (flip) out.quad(a, d, c, b);
        else out.quad(a, b, c, d);
      }
    // the ends, as fans about the axis
    auto cap = [&](float t, bool upward) {
      float r = 0.f, h = 0.f;
      profile(t, r, h);
      const V3 axis(0.f, 0.f, upward ? 1.f : -1.f);
      const V3 n = f.dir(flip ? -axis : axis);
      const uint32_t centre = out.vertex(f.world(V3(0.f, 0.f, h)), n, 0.5f, 0.5f, wind4, tint4);
      std::vector<uint32_t> ring((size_t)cols + 1);
      for (int j = 0; j <= cols; ++j) {
        const float a = (float)j / (float)cols;
        const V3 p = point(a, t);
        ring[(size_t)j] = out.vertex(f.world(p), n, 0.5f + 0.5f * p.x / rmax, 0.5f + 0.5f * p.y / rmax, wind4,
                                     tint4);
      }
      for (int j = 0; j < cols; ++j) {
        const bool ccw = upward != flip;
        if (ccw) out.tri(centre, ring[(size_t)j], ring[(size_t)j + 1]);
        else out.tri(centre, ring[(size_t)j + 1], ring[(size_t)j]);
      }
    };
    if (o.cap_bottom) cap(0.f, false);
    if (o.cap_top) cap(1.f, true);
  };
  build(o.invert);
  if (o.double_sided) build(!o.invert);
}

void grid(MeshOut &out, int w, int h, const std::function<void(float, float, V3 &, V3 &)> &at,
          bool double_sided, const std::function<void(float, float, float wind4[4])> &wind,
          const float tint4[4], float u0, float u1, float v0, float v1) {
  const int nw = std::max(w, 1), nh = std::max(h, 1);
  const size_t stride = (size_t)nw + 1;
  auto build = [&](bool flip) {
    std::vector<uint32_t> ids((size_t)(nh + 1) * stride);
    for (int j = 0; j <= nh; ++j)
      for (int i = 0; i <= nw; ++i) {
        const float u = (float)i / (float)nw, v = (float)j / (float)nh;
        V3 p, n;
        at(u, v, p, n);
        if (flip) n = -n;
        float w4[4] = {0.f, 0.f, 0.f, -1.f};
        if (wind) wind(u, v, w4);
        ids[(size_t)j * stride + (size_t)i] =
            out.vertex(p, n, u0 + (u1 - u0) * u, v0 + (v1 - v0) * v, w4, tint4);
      }
    for (int j = 0; j < nh; ++j)
      for (int i = 0; i < nw; ++i) {
        const uint32_t a = ids[(size_t)j * stride + (size_t)i];
        const uint32_t b = ids[(size_t)j * stride + (size_t)i + 1];
        const uint32_t c = ids[(size_t)(j + 1) * stride + (size_t)i + 1];
        const uint32_t d = ids[(size_t)(j + 1) * stride + (size_t)i];
        if (flip) out.quad(a, d, c, b);
        else out.quad(a, b, c, d);
      }
  };
  build(false);
  if (double_sided) build(true);
}

} // namespace plant
} // namespace gpx
