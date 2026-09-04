// Geekatplay TerraForge - placing the terrain tile on its planet.
// See planet_place.hpp for what this does and why.
#include "planet_place.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>

namespace studio {

namespace {

// ---------------------------------------------------------------- cache
// The relief under the tile depends only on the layer stack and the map
// size, and costs ~10^8 noise evaluations at 512^2. Kept for the last few
// (stack, size) pairs so the interactive low-res pass and the full pass each
// hit their own entry.
struct ReliefEntry {
  std::vector<gpx::planet::Layer> layers;
  int w = 0, h = 0;
  std::vector<float> relief, smooth;
};
std::deque<ReliefEntry> g_cache;
std::mutex g_cache_mtx;

bool same_layers(const std::vector<gpx::planet::Layer> &a,
                 const std::vector<gpx::planet::Layer> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::memcmp(&a[i], &b[i], sizeof(gpx::planet::Layer)) != 0) return false;
  return true;
}

float smoothstep01(float e0, float e1, float x) {
  return gpx::planet::pl_smoothstep(e0, e1, x);
}

// Separable box blur of a map, radius r texels, clamped edges. Two passes of
// a box are a triangle filter, which is smooth enough for a footprint halo
// and cheap enough to run on every evaluation.
void box_blur(std::vector<float> &m, int w, int h, int r) {
  if (r <= 0) return;
  std::vector<float> tmp((size_t)w * h);
  const float inv = 1.f / (float)(2 * r + 1);
  gpx::parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      const float *row = &m[(size_t)y * w];
      float *out = &tmp[(size_t)y * w];
      double acc = 0.0;
      for (int x = -r; x <= r; ++x) acc += row[std::clamp(x, 0, w - 1)];
      for (int x = 0; x < w; ++x) {
        out[x] = (float)(acc * inv);
        acc += row[std::clamp(x + r + 1, 0, w - 1)] - row[std::clamp(x - r, 0, w - 1)];
      }
    }
  });
  // columns: strided, so band over x instead
  gpx::parallel_rows(w, [&](int x0, int x1) {
    for (int x = x0; x < x1; ++x) {
      double acc = 0.0;
      for (int y = -r; y <= r; ++y) acc += tmp[(size_t)std::clamp(y, 0, h - 1) * w + x];
      for (int y = 0; y < h; ++y) {
        m[(size_t)y * w + x] = (float)(acc * inv);
        acc += tmp[(size_t)std::clamp(y + r + 1, 0, h - 1) * w + x] -
               tmp[(size_t)std::clamp(y - r, 0, h - 1) * w + x];
      }
    }
  });
}

// The level the tile's terrain meets its own border at: the median of the
// border ring. A normalised mountain fades to its rim, a stamped feature sits
// on a flat pad, and a fully-random tile has no better ground than this.
float border_median(const gpx::Heightmap &t) {
  std::vector<float> ring;
  ring.reserve((size_t)(t.w + t.h) * 2);
  for (int x = 0; x < t.w; ++x) {
    ring.push_back(t.at(x, 0));
    ring.push_back(t.at(x, t.h - 1));
  }
  for (int y = 1; y + 1 < t.h; ++y) {
    ring.push_back(t.at(0, y));
    ring.push_back(t.at(t.w - 1, y));
  }
  if (ring.empty()) return 0.f;
  size_t mid = ring.size() / 2;
  std::nth_element(ring.begin(), ring.begin() + mid, ring.end());
  return ring[mid];
}

PlaceResult g_last;

} // namespace

void planet_relief_under_tile(const std::vector<gpx::planet::Layer> &layers,
                              int w, int h, std::vector<float> &relief,
                              std::vector<float> &smooth) {
  relief.assign((size_t)w * h, 0.f);
  smooth.assign((size_t)w * h, 0.f);
  if (layers.empty() || w <= 0 || h <= 0) return;
  // the surround's own budget: one octave per doubling of the map, capped
  // where the shader caps; the broad shape stops after the big octaves
  const float octf = std::clamp(std::log2((float)std::max(w, h)), 4.f, 11.f);
  const float octs = 2.5f;
  const gpx::planet::Layer *L = layers.data();
  const int n = (int)std::min<size_t>(layers.size(), gpx::planet::MAX_LAYERS);
  gpx::parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      const float v = h > 1 ? (float)y / (float)(h - 1) : 0.f;
      for (int x = 0; x < w; ++x) {
        const float u = w > 1 ? (float)x / (float)(w - 1) : 0.f;
        // the plane the surround shader samples: (u, 0.37, v)
        const float d[3] = {u, 0.37f, v};
        const size_t i = (size_t)y * w + x;
        relief[i] = 1.2f * gpx::planet::heightf(d, L, n, octf);
        smooth[i] = 1.2f * gpx::planet::heightf(d, L, n, octs);
      }
    }
  });
}

gpx::Heightmap planet_place_tile(const gpx::Heightmap &tile,
                                 const std::vector<gpx::planet::Layer> &layers,
                                 const PlaceSettings &s, PlaceResult *out) {
  PlaceResult res;
  if (!s.enabled || layers.empty() || tile.empty()) {
    double sum = 0;
    for (float v : tile.v) sum += v;
    res.ground = tile.v.empty() ? 0.f : (float)(sum / (double)tile.v.size());
    res.tile_ground = res.ground;
    res.placed = false;
    if (out) *out = res;
    return tile;
  }
  const int w = tile.w, h = tile.h;
  // relief under the tile, cached
  std::vector<float> relief, smooth;
  {
    std::lock_guard<std::mutex> lk(g_cache_mtx);
    for (const ReliefEntry &e : g_cache)
      if (e.w == w && e.h == h && same_layers(e.layers, layers)) {
        relief = e.relief;
        smooth = e.smooth;
        break;
      }
  }
  if (relief.empty()) {
    planet_relief_under_tile(layers, w, h, relief, smooth);
    std::lock_guard<std::mutex> lk(g_cache_mtx);
    g_cache.push_front({layers, w, h, relief, smooth});
    while (g_cache.size() > 4) g_cache.pop_back();
  }

  const float tile_ground = border_median(tile);
  const float ground = s.ground;
  const float pw = std::max(s.presence, 1e-4f);
  // presence: how much of a feature each texel is, then a halo around it
  std::vector<float> pres((size_t)w * h);
  gpx::parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = (size_t)y * w + x;
        pres[i] = smoothstep01(0.f, pw, std::fabs(tile.v[i] - tile_ground));
      }
  });
  double cov = 0;
  for (float p : pres) cov += p;
  res.coverage = (float)(cov / (double)pres.size());
  const int r = std::max(1, (int)std::lround(s.edge * 0.5f * (float)std::max(w, h)));
  box_blur(pres, w, h, r);
  box_blur(pres, w, h, std::max(1, r / 2));

  gpx::Heightmap outm(w, h);
  const float edge = std::max(s.edge, 1e-4f);
  const float flat = std::clamp(s.flatten, 0.f, 1.f);
  gpx::parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      const float v = h > 1 ? (float)y / (float)(h - 1) : 0.f;
      for (int x = 0; x < w; ++x) {
        const float u = w > 1 ? (float)x / (float)(w - 1) : 0.f;
        const size_t i = (size_t)y * w + x;
        // a blurred blob is 0.5 at its own outline: 1 inside, a halo outside
        float wgt = smoothstep01(0.f, 0.5f, pres[i]);
        // the tile has nothing to say past its border
        float b = std::min(std::min(u, 1.f - u), std::min(v, 1.f - v));
        wgt *= smoothstep01(0.f, edge, b);
        const float pb = ground + relief[i];
        const float pbs = ground + smooth[i];
        const float seat = pb + (pbs - pb) * flat;
        const float feature = seat + (tile.v[i] - tile_ground);
        outm.v[i] = pb + (feature - pb) * wgt;
      }
    }
  });
  res.ground = ground;
  res.tile_ground = tile_ground;
  res.placed = true;
  if (out) *out = res;
  return outm;
}

const PlaceResult &planet_place_last() { return g_last; }
void planet_place_set_last(const PlaceResult &r) { g_last = r; }

} // namespace studio
