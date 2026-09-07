// Geekatplay TerraForge - the footprint of an object on the terrain. See the header.
#include "imprint_footprint.hpp"
#include "gpx/deform.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace studio {

std::vector<float> convex_hull_xz(std::vector<float> pts) {
  const size_t n = pts.size() / 2;
  if (n < 3) return pts;
  std::vector<size_t> idx(n);
  for (size_t i = 0; i < n; ++i) idx[i] = i;
  std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
    return pts[a * 2] < pts[b * 2] || (pts[a * 2] == pts[b * 2] && pts[a * 2 + 1] < pts[b * 2 + 1]);
  });
  auto cross = [&](size_t o, size_t a, size_t b) {
    return (pts[a * 2] - pts[o * 2]) * (pts[b * 2 + 1] - pts[o * 2 + 1]) -
           (pts[a * 2 + 1] - pts[o * 2 + 1]) * (pts[b * 2] - pts[o * 2]);
  };
  std::vector<size_t> h(2 * n);
  size_t k = 0;
  for (size_t i = 0; i < n; ++i) {
    while (k >= 2 && cross(h[k - 2], h[k - 1], idx[i]) <= 0) --k;
    h[k++] = idx[i];
  }
  for (size_t i = n - 1, t = k + 1; i > 0; --i) {
    while (k >= t && cross(h[k - 2], h[k - 1], idx[i - 1]) <= 0) --k;
    h[k++] = idx[i - 1];
  }
  h.resize(k > 0 ? k - 1 : 0);
  std::vector<float> out;
  out.reserve(h.size() * 2);
  for (size_t i : h) { out.push_back(pts[i * 2]); out.push_back(pts[i * 2 + 1]); }
  return out;
}

Footprint imprint_footprint(const SceneObject &o, float height_scale, float band) {
  Footprint f;
  const float hs = std::max(height_scale, 1e-6f);
  float m16[16];
  scene_object_matrix(o, height_scale, m16, nullptr);
  auto place = [&](const float *local, float *out) {
    float p[3] = {local[0], local[1], local[2]};
    if (!o.deform.identity()) gpx::deform_point(o.deform, o.bmin, o.bmax, p);
    for (int r = 0; r < 3; ++r)
      out[r] = m16[r] * p[0] + m16[4 + r] * p[1] + m16[8 + r] * p[2] + m16[12 + r];
  };
  std::vector<float> pts;
  float base_y = 1e30f;
  const size_t nv = o.verts.size() / 6;
  if (nv >= 3) {
    // the base band, in the object's own space
    float lo = 1e30f, hi = -1e30f;
    for (size_t i = 0; i < nv; ++i) { lo = std::min(lo, o.verts[i * 6 + 1]); hi = std::max(hi, o.verts[i * 6 + 1]); }
    const float cut = lo + std::max(hi - lo, 1e-9f) * std::clamp(band, 0.f, 1.f);
    pts.reserve(nv * 2);
    for (size_t i = 0; i < nv; ++i) {
      const float *v = &o.verts[i * 6];
      float w[3];
      place(v, w);
      base_y = std::min(base_y, w[1]);
      if (v[1] <= cut + 1e-7f) { pts.push_back(w[0]); pts.push_back(w[2]); }
    }
  } else {
    // no geometry: the bounding box's eight corners
    for (int c = 0; c < 8; ++c) {
      float local[3] = {c & 1 ? o.bmax[0] : o.bmin[0], c & 2 ? o.bmax[1] : o.bmin[1], c & 4 ? o.bmax[2] : o.bmin[2]};
      float w[3];
      place(local, w);
      base_y = std::min(base_y, w[1]);
      if (!(c & 2)) { pts.push_back(w[0]); pts.push_back(w[2]); }
    }
  }
  // a very flat base (a plane) still needs three points that are not collinear:
  // widen a degenerate hull by a hair so it has an area
  f.xz = convex_hull_xz(pts);
  if (f.xz.size() < 6 && pts.size() >= 4) {
    // collinear: make a thin strip along the line
    float x0 = pts[0], z0 = pts[1], x1 = pts[0], z1 = pts[1];
    for (size_t i = 0; i < pts.size(); i += 2) {
      if (pts[i] < x0 || (pts[i] == x0 && pts[i + 1] < z0)) { x0 = pts[i]; z0 = pts[i + 1]; }
      if (pts[i] > x1 || (pts[i] == x1 && pts[i + 1] > z1)) { x1 = pts[i]; z1 = pts[i + 1]; }
    }
    float dx = x1 - x0, dz = z1 - z0, len = std::sqrt(dx * dx + dz * dz);
    float nx = len > 1e-9f ? -dz / len : 1.f, nz = len > 1e-9f ? dx / len : 0.f, e = 1e-4f;
    f.xz = {x0 + nx * e, z0 + nz * e, x1 + nx * e, z1 + nz * e, x1 - nx * e, z1 - nz * e, x0 - nx * e, z0 - nz * e};
  }
  // keep the hull small: the node walks every edge per pixel
  const size_t MAXP = 40;
  if (f.xz.size() / 2 > MAXP) {
    std::vector<float> thin;
    const size_t n = f.xz.size() / 2;
    for (size_t i = 0; i < MAXP; ++i) {
      size_t j = i * n / MAXP;
      thin.push_back(f.xz[j * 2]);
      thin.push_back(f.xz[j * 2 + 1]);
    }
    f.xz = thin;
  }
  // centroid and area of the polygon
  if (f.valid()) {
    double A = 0, cx = 0, cz = 0;
    const size_t n = f.xz.size() / 2;
    for (size_t i = 0; i < n; ++i) {
      size_t j = (i + 1) % n;
      double x0 = f.xz[i * 2], z0 = f.xz[i * 2 + 1], x1 = f.xz[j * 2], z1 = f.xz[j * 2 + 1];
      double c = x0 * z1 - x1 * z0;
      A += c;
      cx += (x0 + x1) * c;
      cz += (z0 + z1) * c;
    }
    A *= 0.5;
    if (std::fabs(A) > 1e-14) {
      f.cx = (float)(cx / (6 * A));
      f.cz = (float)(cz / (6 * A));
    } else {
      for (size_t i = 0; i < n; ++i) { f.cx += f.xz[i * 2] / n; f.cz += f.xz[i * 2 + 1] / n; }
    }
    f.radius = (float)std::sqrt(std::fabs(A) / 3.14159265358979);
  }
  f.base = base_y < 1e29f ? base_y / hs : o.pos[1];
  return f;
}

GroundLockStep ground_lock_step(float pos_y, float last_y, bool seen,
                                float rest, float offset) {
  // Anything that moved the object since the last pass is the user choosing
  // an altitude, and it becomes the offset. Otherwise the object is carried
  // along at the offset it already has.
  if (seen && std::fabs(pos_y - last_y) > 1e-6f) offset = pos_y - rest;
  return {offset, rest + offset};
}

std::string imprint_footprint_line(const Footprint &f, float sink, float margin,
                                   float blend, float lift, float dig) {
  std::string s;
  char buf[96];
  std::snprintf(buf, sizeof buf, "%.6f %.6f %.6f %.6f %d", f.base, sink, margin, blend, (int)(f.xz.size() / 2));
  s += buf;
  for (size_t i = 0; i + 1 < f.xz.size(); i += 2) {
    std::snprintf(buf, sizeof buf, " %.6f %.6f", f.xz[i], f.xz[i + 1]);
    s += buf;
  }
  // Appended after the hull rather than inserted among the leading fields,
  // so a project saved before these existed still parses: the reader's
  // trailing read simply fails and its defaults stand.
  std::snprintf(buf, sizeof buf, " %.6f %.6f", lift, dig);
  s += buf;
  s += '\n';
  return s;
}

} // namespace studio
