// Geekatplay TerraForge - bark pictures made from rules.
//
// A trunk wraps its picture round and round and up and up, so a bark tile
// must meet itself on every edge. Rather than blend four copies of an open
// noise (which flattens the contrast right where the eye looks), the
// noise here is periodic by construction: the gradient lattice and the
// Worley cell lattice are wrapped modulo an integer period, so the value
// at u = 1 is the value at u = 0 exactly, and every octave doubles the
// period so it wraps as well. gpx::noise lends its hashes and gradients.
//
// Each kind is a height field first - plates and fissures, scales and
// fibres are all relief before they are colour - and the colour follows
// the height: the base colour on the high ground, the crack colour down in
// the grooves, a fine grain on top scaled by `roughness`. The normal map
// is the height's Sobel gradient in tangent space, OpenGL convention (green
// up the trunk), so a renderer that reads it lights the ridges from the
// same side the colour suggests. `scale` multiplies the feature frequency;
// `seed` picks another patch of the same bark.
#include "gpx/noise_core.hpp"
#include "gpx/plant.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace gpx {

namespace {


float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float smoothstep(float a, float b, float x) {
  float t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
int wrap(int i, int n) { i %= n; return i < 0 ? i + n : i; }

// Perlin noise on a lattice that repeats every px by py cells.
float pperlin(float x, float y, int px, int py, uint32_t seed) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float xf = x - (float)xi, yf = y - (float)yi;
  float u = noise::fade(xf), v = noise::fade(yf);
  auto g = [&](int cx, int cy, float dx, float dy) {
    float gx, gy;
    noise::grad(wrap(cx, px), wrap(cy, py), seed, gx, gy);
    return gx * dx + gy * dy;
  };
  float d00 = g(xi, yi, xf, yf), d10 = g(xi + 1, yi, xf - 1, yf);
  float d01 = g(xi, yi + 1, xf, yf - 1), d11 = g(xi + 1, yi + 1, xf - 1, yf - 1);
  return 1.4142f * noise::lerp(noise::lerp(d00, d10, u), noise::lerp(d01, d11, u), v);
}

// Periodic fBm on the unit square: `fx`, `fy` cells across the tile.
float pfbm(float u, float v, int fx, int fy, uint32_t seed, int octaves, float gain = 0.5f) {
  float sum = 0, amp = 1, norm = 0;
  for (int o = 0; o < octaves; ++o) {
    sum += amp * pperlin(u * (float)fx, v * (float)fy, fx, fy, seed + (uint32_t)o * 1013u);
    norm += amp;
    amp *= gain;
    fx *= 2;
    fy *= 2;
  }
  return sum / norm;
}

float pridged(float u, float v, int fx, int fy, uint32_t seed, int octaves) {
  float sum = 0, amp = 0.5f, norm = 0, w = 1.f;
  for (int o = 0; o < octaves; ++o) {
    float n = pperlin(u * (float)fx, v * (float)fy, fx, fy, seed + (uint32_t)o * 1013u);
    n = 1.f - std::fabs(n);
    n *= n * w;
    w = clamp01(n * 0.8f);
    sum += n * amp;
    norm += amp;
    amp *= 0.5f;
    fx *= 2;
    fy *= 2;
  }
  return sum / norm; // 0..1, 1 on a ridge
}

// Periodic Worley: F1, F2, the winning cell's hash and the offset from its
// centre (in cells), on a lattice of nx by ny cells.
struct Cell {
  float f1 = 1e9f, f2 = 1e9f, dx = 0, dy = 0;
  uint32_t id = 0;
};
// `stagger` shifts every other row half a cell (ny must be even to wrap).
Cell pworley(float x, float y, int nx, int ny, uint32_t seed, float jitter = 1.f, bool stagger = false) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y);
  Cell c;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      int cx = xi + dx, cy = yi + dy;
      int wx = wrap(cx, nx), wy = wrap(cy, ny);
      float px = (float)cx + 0.5f + jitter * (noise::hash01(wx, wy, seed) - 0.5f) + (stagger && (wy & 1) ? 0.5f : 0.f);
      float py = (float)cy + 0.5f + jitter * (noise::hash01(wx, wy, seed ^ 0xA341u) - 0.5f);
      float ddx = x - px, ddy = y - py;
      float d = std::sqrt(ddx * ddx + ddy * ddy);
      if (d < c.f1) {
        c.f2 = c.f1;
        c.f1 = d;
        c.dx = ddx;
        c.dy = ddy;
        c.id = noise::hash2(wx, wy, seed ^ 0x7F1Du);
      } else if (d < c.f2) {
        c.f2 = d;
      }
    }
  return c;
}
float cell_unit(uint32_t id, int k) { return (float)((noise::hash_u32(id + (uint32_t)k * 0x9E3779B9u)) & 0xffffu) / 65535.f; }

enum class Kind { Fissured, Plated, Smooth, Birch, Ringed, Peeling, Scaly, Fibrous };
Kind kind_of(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  if (s == "plated") return Kind::Plated;
  if (s == "smooth") return Kind::Smooth;
  if (s == "birch") return Kind::Birch;
  if (s == "ringed") return Kind::Ringed;
  if (s == "peeling") return Kind::Peeling;
  if (s == "scaly") return Kind::Scaly;
  if (s == "fibrous") return Kind::Fibrous;
  return Kind::Fissured;
}

// The height 0..1 of one kind at (u, v); `n` is the integer feature scale.
float bark_height(Kind k, float u, float v, int n, uint32_t seed) {
  switch (k) {
  case Kind::Fissured: {
    // The furrows are the ridge lines of a noise stretched six to one up
    // the trunk, so they run long and vertical and join into a network;
    // narrowing them with a steep threshold leaves broad plates between,
    // and a second, coarser network cuts the plates into lengths the way
    // an oak's do. The plate tops are domed and grained, never flat.
    float f = pridged(u, v, n * 5, n, seed, 3);
    float crack = smoothstep(0.40f, 0.80f, f);
    float cross = smoothstep(0.70f, 0.97f, pridged(u, v, n, n * 3, seed ^ 0x9Bu, 3));
    float plate = clamp01(1.f - std::max(crack, cross * 0.85f));
    float dome = 0.42f + 0.58f * (0.5f + 0.5f * pfbm(u, v, n * 3, n * 6, seed ^ 0x33u, 3));
    return clamp01(0.05f + 0.95f * plate * dome);
  }
  case Kind::Plated: {
    Cell c = pworley(u * (float)(n * 3), v * (float)(n * 4), n * 3, n * 4, seed, 0.9f);
    float groove = smoothstep(0.f, 0.22f, c.f2 - c.f1);
    float plate = 0.6f + 0.4f * cell_unit(c.id, 1);
    float grain = 0.08f * pfbm(u, v, n * 12, n * 12, seed ^ 0x44u, 3);
    return clamp01(groove * plate + grain);
  }
  case Kind::Smooth:
    return 0.55f + 0.18f * pfbm(u, v, n * 2, n * 3, seed, 4) + 0.04f * pfbm(u, v, n * 16, n * 16, seed ^ 0x9u, 2);
  case Kind::Birch: {
    // paper-smooth, with dark horizontal lenticels scattered as stretched cells
    float base = 0.7f + 0.08f * pfbm(u, v, n * 3, n * 3, seed, 4);
    Cell c = pworley(u * (float)(n * 2), v * (float)(n * 12), n * 2, n * 12, seed ^ 0x77u, 0.8f);
    float present = cell_unit(c.id, 2) < 0.45f ? 1.f : 0.f;
    float len = 0.25f + 0.3f * cell_unit(c.id, 3);
    float streak = smoothstep(len, len * 0.6f, std::fabs(c.dx)) * smoothstep(0.12f, 0.05f, std::fabs(c.dy));
    return base - 0.45f * present * streak;
  }
  case Kind::Ringed: {
    // leaf-scar bands up a palm: a band every 1/(3n), its edge wandering
    float wob = 0.06f * pfbm(u, v, n * 2, n, seed, 3);
    float ph = (v + wob) * (float)(n * 3);
    float tri = std::fabs(2.f * (ph - std::floor(ph)) - 1.f); // 1 at the band edge
    float band = smoothstep(0.55f, 0.95f, tri);
    return 0.3f + 0.5f * band + 0.1f * pfbm(u, v, n * 10, n * 10, seed ^ 0x21u, 3);
  }
  case Kind::Peeling: {
    // plates whose lower edge lifts away: height climbs down the plate
    // and drops sharply into the groove below it
    Cell c = pworley(u * (float)(n * 3), v * (float)(n * 3), n * 3, n * 3, seed, 0.8f);
    float groove = smoothstep(0.f, 0.12f, c.f2 - c.f1);
    float lift = 0.35f + 0.35f * cell_unit(c.id, 1);
    float flap = clamp01(0.5f + c.dy * 0.9f); // higher toward the plate's lower edge
    float h = groove * (0.35f + lift * flap);
    return clamp01(h + 0.05f * pfbm(u, v, n * 10, n * 10, seed ^ 0x55u, 3));
  }
  case Kind::Scaly: {
    // A pine's plates: irregular slabs, each lying a little over the one
    // below it. The cells are jittered hard so no honeycomb shows, the
    // groove between them is cut by F2 - F1, every plate sits at its own
    // height, and its lower edge lifts into a lip.
    Cell c = pworley(u * (float)(n * 5), v * (float)(n * 7), n * 5, n * 7, seed, 0.6f, true);
    float dome = clamp01(1.f - c.f1 * 1.35f);       // the scale's own curve
    float groove = smoothstep(0.f, 0.10f, c.f2 - c.f1);
    float lip = clamp01(0.5f - c.dy * 1.2f);        // thick at the scale's foot
    float own = 0.30f + 0.45f * cell_unit(c.id, 1); // this scale's own height
    float h = groove * (own * (0.45f + 0.55f * dome) + 0.25f * lip);
    return clamp01(h + 0.06f * pfbm(u, v, n * 12, n * 12, seed ^ 0x66u, 3));
  }
  case Kind::Fibrous:
  default: {
    // long fibres running up the trunk: noise stretched twelve to one
    float f = pfbm(u, v, n * 12, n, seed, 4, 0.6f);
    float fine = pridged(u, v, n * 24, n * 2, seed ^ 0x88u, 3);
    return clamp01(0.5f + 0.35f * f + 0.15f * (fine - 0.5f));
  }
  }
}

} // namespace

bool plant_texture_bark(const PlantBarkTexture &t, int w, int h, std::vector<uint8_t> &rgba,
                        std::vector<uint8_t> &normal_rgba) {
  rgba.clear();
  normal_rgba.clear();
  if (w < 4 || h < 4) return false;
  const Kind kind = kind_of(t.kind);
  const int n = std::max(1, (int)std::lround(std::max(t.scale, 0.05f) * 2.f));
  const float rough = clamp01(t.roughness);
  std::vector<float> height((size_t)w * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float u = ((float)x + 0.5f) / (float)w, v = ((float)y + 0.5f) / (float)h;
      height[(size_t)y * w + x] = clamp01(bark_height(kind, u, v, n, t.seed));
    }
  rgba.resize((size_t)w * h * 4);
  normal_rgba.resize((size_t)w * h * 4);
  // the relief is deeper on rough bark, so the normals lean further
  const float slope = (1.5f + 6.f * rough) * (float)std::min(w, h) / 512.f;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      size_t i = (size_t)y * w + x;
      float u = ((float)x + 0.5f) / (float)w, v = ((float)y + 0.5f) / (float)h;
      float hh = height[i];
      float lows = smoothstep(0.12f, 0.65f, hh);
      float grain = 1.f + rough * 0.25f * pfbm(u, v, n * 32, n * 32, t.seed ^ 0x99u, 2);
      uint8_t *px = &rgba[i * 4];
      for (int k = 0; k < 3; ++k) {
        float c = t.crack_color[k] + (t.color[k] - t.crack_color[k]) * lows;
        c *= grain * (0.85f + 0.3f * hh);
        px[k] = (uint8_t)std::lround(clamp01(c) * 255.f);
      }
      px[3] = 255;
      // Sobel on the wrapped height; y grows downward in rows, the normal's
      // green points up the trunk
      auto H = [&](int xx, int yy) { return height[(size_t)wrap(yy, h) * w + wrap(xx, w)]; };
      float gx = (H(x + 1, y - 1) + 2 * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x - 1, y) + H(x - 1, y + 1));
      float gy_up = (H(x - 1, y - 1) + 2 * H(x, y - 1) + H(x + 1, y - 1)) - (H(x - 1, y + 1) + 2 * H(x, y + 1) + H(x + 1, y + 1));
      float nx = -gx * slope, ny = -gy_up * slope, nz = 1.f;
      float len = std::sqrt(nx * nx + ny * ny + nz * nz);
      uint8_t *np = &normal_rgba[i * 4];
      np[0] = (uint8_t)std::lround((nx / len * 0.5f + 0.5f) * 255.f);
      np[1] = (uint8_t)std::lround((ny / len * 0.5f + 0.5f) * 255.f);
      np[2] = (uint8_t)std::lround((nz / len * 0.5f + 0.5f) * 255.f);
      np[3] = 255;
    }
  return true;
}

} // namespace gpx
