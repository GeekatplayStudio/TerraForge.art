// Geekatplay TerraForge - a thumbnail of a mesh, rasterised on the CPU.
//
// The ImportObject node shows what it imported. A three-quarter view from
// above, flat-lit, with the model's own pictures where it has them: enough
// to tell a house from a dragon at 112 pixels, and it needs no GL context,
// so it can be made right after a load on whatever thread did the loading.
#include "mesh_thumbnail.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace studio {

std::vector<uint8_t> mesh_raster(const SceneObject &o, int size,
                                 const MeshRasterOptions &opt) {
  const int W = size, H = size;
  std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
  std::vector<float> depth((size_t)W * H, 1e30f);
  // the canvas: a dark plate, so a thumbnail reads as a picture. A card that
  // will stand in for the mesh in the world keeps it transparent instead.
  if (opt.plate)
    for (int i = 0; i < W * H; ++i) {
      rgba[(size_t)i * 4 + 0] = 34;
      rgba[(size_t)i * 4 + 1] = 34;
      rgba[(size_t)i * 4 + 2] = 36;
      rgba[(size_t)i * 4 + 3] = 255;
    }
  const int n = o.vert_count;
  if (n < 3 || o.verts.size() < (size_t)n * 6) return rgba;

  // rotate about Y, then tilt, then an orthographic fit of the bounds
  const float ay = opt.yaw_deg * 0.017453f, ax = opt.pitch_deg * 0.017453f;
  const float cy = std::cos(ay), sy = std::sin(ay), cx = std::cos(ax), sx = std::sin(ax);
  auto view = [&](const float *p, float *out) {
    float x = p[0] * cy + p[2] * sy, z = -p[0] * sy + p[2] * cy, y = p[1];
    out[0] = x;
    out[1] = y * cx - z * sx;
    out[2] = y * sx + z * cx;
  };
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  std::vector<float> vp((size_t)n * 3);
  for (int i = 0; i < n; ++i) {
    view(&o.verts[(size_t)i * 6], &vp[(size_t)i * 3]);
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], vp[(size_t)i * 3 + k]);
      hi[k] = std::max(hi[k], vp[(size_t)i * 3 + k]);
    }
  }
  const float span = std::max(hi[0] - lo[0], hi[1] - lo[1]);
  if (span < 1e-9f) return rgba;
  const float scale = (W - 10) / span;
  const float ox = (W - (hi[0] - lo[0]) * scale) * 0.5f, oy = (H - (hi[1] - lo[1]) * scale) * 0.5f;

  // light from the upper left, in view space
  const float L[3] = {-0.45f, 0.75f, 0.5f};
  const bool have_uv = o.uvs.size() == (size_t)n * 2;
  // which part a vertex belongs to, for its picture and colour
  auto part_of = [&](int v) -> const SceneObject::Part * {
    for (const SceneObject::Part &p : o.parts)
      if (v >= p.first && v < p.first + p.count) return &p;
    return nullptr;
  };

  for (int t = 0; t + 2 < n; t += 3) {
    const float *a = &vp[(size_t)t * 3], *b = &vp[(size_t)(t + 1) * 3], *c = &vp[(size_t)(t + 2) * 3];
    // the face normal in view space, from the raw normal rotated
    float nv[3];
    view(&o.verts[(size_t)t * 6 + 3], nv);
    float nl = std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
    if (nl < 1e-9f) continue;
    float ndl = (nv[0] * L[0] + nv[1] * L[1] + nv[2] * L[2]) / nl;
    float shade = 0.35f + 0.65f * std::max(0.f, ndl);
    const SceneObject::Part *part = part_of(t);
    float base[3] = {o.color[0], o.color[1], o.color[2]};
    if (part) for (int k = 0; k < 3; ++k) base[k] = part->color[k];
    const bool tex = part && part->w > 0 && part->h > 0 && !part->rgba.empty() && have_uv;

    // screen triangle, y down
    float X[3] = {ox + (a[0] - lo[0]) * scale, ox + (b[0] - lo[0]) * scale, ox + (c[0] - lo[0]) * scale};
    float Y[3] = {H - (oy + (a[1] - lo[1]) * scale), H - (oy + (b[1] - lo[1]) * scale), H - (oy + (c[1] - lo[1]) * scale)};
    float Z[3] = {-a[2], -b[2], -c[2]};
    int x0 = std::max(0, (int)std::floor(std::min({X[0], X[1], X[2]})));
    int x1 = std::min(W - 1, (int)std::ceil(std::max({X[0], X[1], X[2]})));
    int y0 = std::max(0, (int)std::floor(std::min({Y[0], Y[1], Y[2]})));
    int y1 = std::min(H - 1, (int)std::ceil(std::max({Y[0], Y[1], Y[2]})));
    float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (std::fabs(area) < 1e-6f) continue;
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) / area;
        float w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) / area;
        float w2 = 1.f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float z = w0 * Z[0] + w1 * Z[1] + w2 * Z[2];
        float &d = depth[(size_t)y * W + x];
        if (z >= d) continue;
        d = z;
        float col[3] = {base[0], base[1], base[2]};
        if (tex) {
          const float *ua = &o.uvs[(size_t)t * 2], *ub = &o.uvs[(size_t)(t + 1) * 2], *uc = &o.uvs[(size_t)(t + 2) * 2];
          float u = w0 * ua[0] + w1 * ub[0] + w2 * uc[0];
          float v = w0 * ua[1] + w1 * ub[1] + w2 * uc[1];
          u -= std::floor(u);
          v -= std::floor(v);
          int tx = std::min(part->w - 1, (int)(u * part->w)), ty = std::min(part->h - 1, (int)(v * part->h));
          const uint8_t *s = &part->rgba[((size_t)ty * part->w + tx) * 4];
          if (opt.alpha_cutout && s[3] < 128) continue; // a leaf's cut-out
          for (int k = 0; k < 3; ++k) col[k] *= s[k] / 255.f;
        }
        uint8_t *dst = &rgba[((size_t)y * W + x) * 4];
        for (int k = 0; k < 3; ++k) dst[k] = (uint8_t)std::clamp(col[k] * shade * 255.f, 0.f, 255.f);
        dst[3] = 255;
      }
  }
  return rgba;
}

std::vector<uint8_t> mesh_thumbnail(const SceneObject &o, int size) {
  return mesh_raster(o, size, MeshRasterOptions{});
}

} // namespace studio
