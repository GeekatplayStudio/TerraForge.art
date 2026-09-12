// Geekatplay TerraForge - the outline of a picture's alpha, and pictures
// to disk.
//
// A cut-out leaf is a card trimmed to the shape in its picture, so the mesh
// needs that shape as a polygon. This reads it off the alpha: everything at
// or above half opacity is leaf, the largest connected patch of it is the
// leaf (a stray speck in a scanned picture must not become the outline),
// its boundary is followed pixel by pixel, and Douglas-Peucker keeps the
// corners that matter until the polygon is about `max_points` long. The
// same tracer serves the rule-made leaves and any picture a person drops
// in, so both kinds of leaf cut the same way.
//
// Convention, used by every consumer: u runs 0..1 left to right, v runs
// 0..1 from the TOP row down (the picture's own row order, as glTF reads
// texture coordinates); the polygon winds counter-clockwise when the
// picture is looked at the right way up (v pointing down on the page), and
// the first point is not repeated at the end.
//
// The PNG writer sits here rather than with the leaf maker because it is
// the one place in the plant engine that touches the file system for a
// picture; stb_image_write's implementation is compiled once in
// engine/nodes/nodes_export.cpp, so this includes the header alone.
#include "gpx/plant.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include "stb_image_write.h"

namespace gpx {

namespace {

// The largest 4-connected region of set pixels, as a mask of its own.
// Returns its pixel count, 0 when nothing is set.
size_t largest_region(const std::vector<uint8_t> &in, int w, int h, std::vector<uint8_t> &out) {
  std::vector<int> label((size_t)w * h, 0);
  std::vector<size_t> sizes(1, 0);
  std::vector<int> stack;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      size_t i = (size_t)y * w + x;
      if (!in[i] || label[i]) continue;
      int id = (int)sizes.size();
      sizes.push_back(0);
      stack.clear();
      stack.push_back((int)i);
      label[i] = id;
      while (!stack.empty()) {
        int p = stack.back();
        stack.pop_back();
        ++sizes[(size_t)id];
        int px = p % w, py = p / w;
        const int nx[4] = {px - 1, px + 1, px, px}, ny[4] = {py, py, py - 1, py + 1};
        for (int k = 0; k < 4; ++k) {
          if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
          size_t q = (size_t)ny[k] * w + nx[k];
          if (!in[q] || label[q]) continue;
          label[q] = id;
          stack.push_back((int)q);
        }
      }
    }
  if (sizes.size() < 2) return 0;
  int best = 1;
  for (int id = 2; id < (int)sizes.size(); ++id)
    if (sizes[(size_t)id] > sizes[(size_t)best]) best = id;
  out.assign((size_t)w * h, 0);
  for (size_t i = 0; i < label.size(); ++i) out[i] = label[i] == best ? 1 : 0;
  return sizes[(size_t)best];
}

// Moore neighbour tracing (Jacob's stopping criterion) round the outer
// boundary of the mask, clockwise in pixel rows. Pixel coordinates.
void follow_boundary(const std::vector<uint8_t> &m, int w, int h, std::vector<std::pair<int, int>> &out) {
  auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && m[(size_t)y * w + x]; };
  // the start: the first set pixel in row order (its left neighbour is clear)
  int sx = -1, sy = -1;
  for (int y = 0; y < h && sx < 0; ++y)
    for (int x = 0; x < w; ++x)
      if (at(x, y)) { sx = x; sy = y; break; }
  if (sx < 0) return;
  // 8 neighbours clockwise starting from the west
  static const int DX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
  static const int DY[8] = {0, -1, -1, -1, 0, 1, 1, 1};
  int cx = sx, cy = sy, dir = 4; // as if we had arrived heading east
  out.push_back({cx, cy});
  const size_t guard = (size_t)w * h * 4 + 8;
  for (size_t steps = 0; steps < guard; ++steps) {
    int found = -1;
    // Sweep clockwise from the neighbour just past the one we came from:
    // that is what keeps the walk on the boundary rather than letting it
    // cut a corner into the inside.
    for (int k = 0; k < 8; ++k) {
      int d = (dir + 5 + k) % 8;
      if (at(cx + DX[d], cy + DY[d])) { found = d; break; }
    }
    if (found < 0) return; // a single pixel
    cx += DX[found];
    cy += DY[found];
    dir = found;
    if (cx == sx && cy == sy) return;
    out.push_back({cx, cy});
  }
}

float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
  float vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
  float t = l2 > 0 ? std::clamp(((px - ax) * vx + (py - ay) * vy) / l2, 0.f, 1.f) : 0.f;
  float dx = px - (ax + t * vx), dy = py - (ay + t * vy);
  return std::sqrt(dx * dx + dy * dy);
}

void dp_rec(const std::vector<std::pair<int, int>> &p, size_t a, size_t b, float eps, std::vector<uint8_t> &keep) {
  if (b <= a + 1) return;
  float best = -1;
  size_t bi = a;
  for (size_t i = a + 1; i < b; ++i) {
    float d = seg_dist((float)p[i].first, (float)p[i].second, (float)p[a].first, (float)p[a].second,
                       (float)p[b].first, (float)p[b].second);
    if (d > best) { best = d; bi = i; }
  }
  if (best > eps) {
    keep[bi] = 1;
    dp_rec(p, a, bi, eps, keep);
    dp_rec(p, bi, b, eps, keep);
  }
}

// Douglas-Peucker on a closed ring: split at the point farthest from the
// first, simplify both arcs.
std::vector<std::pair<int, int>> simplify(const std::vector<std::pair<int, int>> &p, float eps) {
  if (p.size() < 4) return p;
  size_t far = 0;
  float best = -1;
  for (size_t i = 1; i < p.size(); ++i) {
    float dx = (float)(p[i].first - p[0].first), dy = (float)(p[i].second - p[0].second);
    if (dx * dx + dy * dy > best) { best = dx * dx + dy * dy; far = i; }
  }
  std::vector<uint8_t> keep(p.size(), 0);
  keep[0] = keep[far] = 1;
  dp_rec(p, 0, far, eps, keep);
  std::vector<std::pair<int, int>> closed(p.begin() + (long)far, p.end());
  closed.push_back(p[0]);
  std::vector<uint8_t> keep2(closed.size(), 0);
  dp_rec(closed, 0, closed.size() - 1, eps, keep2);
  for (size_t i = 1; i + 1 < closed.size(); ++i)
    if (keep2[i]) keep[far + i] = 1;
  std::vector<std::pair<int, int>> out;
  for (size_t i = 0; i < p.size(); ++i)
    if (keep[i]) out.push_back(p[i]);
  return out;
}

} // namespace

bool plant_trace_cutout(const uint8_t *rgba, int w, int h, int max_points, std::vector<float> &cutout_uv) {
  cutout_uv.clear();
  if (!rgba || w < 2 || h < 2) return false;
  if (max_points < 3) max_points = 3;
  std::vector<uint8_t> mask((size_t)w * h);
  bool any = false;
  for (size_t i = 0; i < mask.size(); ++i) {
    mask[i] = rgba[i * 4 + 3] >= 128 ? 1 : 0;
    any = any || mask[i];
  }
  if (!any) return false;
  std::vector<uint8_t> region;
  if (!largest_region(mask, w, h, region)) return false;
  std::vector<std::pair<int, int>> ring;
  follow_boundary(region, w, h, ring);
  if (ring.size() < 3) {
    // a patch too small to have a ring: its bounding box
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (region[(size_t)y * w + x]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    ring = {{x0, y0}, {x0, y1 + 1}, {x1 + 1, y1 + 1}, {x1 + 1, y0}};
  }
  // simplify until the ring is short enough; the tolerance grows gently so
  // the result is the finest polygon that fits the budget
  std::vector<std::pair<int, int>> poly = ring;
  float eps = 0.5f;
  const float eps_max = (float)std::max(w, h);
  while ((int)poly.size() > max_points && eps < eps_max) {
    poly = simplify(ring, eps);
    eps *= 1.15f; // gently, so the polygon lands just inside the budget
  }
  if (poly.size() < 3) return false;
  // orientation: counter-clockwise with v pointing down on the page means a
  // negative shoelace sum in (u, v); flip when the follower went the other way
  double area = 0;
  for (size_t i = 0; i < poly.size(); ++i) {
    const auto &a = poly[i], &b = poly[(i + 1) % poly.size()];
    area += (double)a.first * b.second - (double)b.first * a.second;
  }
  if (area > 0) std::reverse(poly.begin(), poly.end());
  cutout_uv.reserve(poly.size() * 2);
  for (const auto &p : poly) {
    cutout_uv.push_back(((float)p.first + 0.5f) / (float)w);
    cutout_uv.push_back(((float)p.second + 0.5f) / (float)h);
  }
  return true;
}

bool plant_texture_write_png(const std::string &path, const std::vector<uint8_t> &rgba, int w, int h) {
  if (path.empty() || w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * 4) return false;
  try {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
      std::error_code ec;
      std::filesystem::create_directories(p.parent_path(), ec);
    }
  } catch (...) {
    return false;
  }
  return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

} // namespace gpx
