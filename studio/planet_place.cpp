// Geekatplay TerraForge - placing the terrain tile on its planet.
// See planet_place.hpp for what this does and why.
#include "planet_place.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
  float tx[6] = {0, 0, 0, 0, 1, 1}; // on, pos x, pos z, yaw, scl x, scl z
  std::vector<float> relief, smooth, wet;
};
void tx_key(const PlaceSettings &s, float out[6]) {
  out[0] = s.tx_on ? 1.f : 0.f;
  out[1] = s.tx_pos[0];
  out[2] = s.tx_pos[1];
  out[3] = s.tx_yaw;
  out[4] = s.tx_scl[0];
  out[5] = s.tx_scl[1];
}
std::deque<ReliefEntry> g_cache;
std::mutex g_cache_mtx;

bool same_layers(const std::vector<gpx::planet::Layer> &a,
                 const std::vector<gpx::planet::Layer> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::memcmp(&a[i], &b[i], sizeof(gpx::planet::Layer)) != 0) return false;
  return true;
}

// A little value noise for the tile's outline, so its border is a coastline
// rather than a shape. Two octaves is all it needs: the first decides which
// way the edge bulges, the second frays it.
float bn_hash(int x, int y) {
  uint32_t h = (uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h * (1.f / 4294967295.f);
}
float bn_value(float x, float y) {
  const int xi = (int)std::floor(x), yi = (int)std::floor(y);
  float tx = x - xi, ty = y - yi;
  tx = tx * tx * (3.f - 2.f * tx);
  ty = ty * ty * (3.f - 2.f * ty);
  const float a = bn_hash(xi, yi), b = bn_hash(xi + 1, yi);
  const float c = bn_hash(xi, yi + 1), d = bn_hash(xi + 1, yi + 1);
  return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}
float border_noise(float u, float v) {
  return bn_value(u * 5.f, v * 5.f) * 0.68f + bn_value(u * 13.f + 7.f, v * 13.f + 3.f) * 0.32f;
}

// The polynomial smooth maximum: exactly max() outside a band k wide, a
// parabola across it, and the slopes match where the two meet. k = 0 is
// std::max to the bit.
float place_smax(float a, float b, float k) {
  if (k <= 1e-9f) return a > b ? a : b;
  const float h = std::clamp(0.5f + 0.5f * (a - b) / k, 0.f, 1.f);
  return b + (a - b) * h + k * h * (1.f - h);
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

// The level the tile's terrain meets its own border at: a low percentile of
// the border ring. A normalised mountain fades to its rim, a stamped feature
// sits on a flat pad, and a fully-random tile has no better ground than
// this. It used to be the median, which put half of a noise tile's border
// below the planet's ground - a moat of water round every such tile once
// the feather let the planet in. The 15th percentile settles the tile so
// its valleys meet the ground and its relief stands above it; a hole dug
// on purpose is still far below.
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
  size_t k = ring.size() * 15 / 100;
  std::nth_element(ring.begin(), ring.begin() + k, ring.end());
  return ring[k];
}

PlaceResult g_last;

} // namespace

void planet_relief_under_tile(const std::vector<gpx::planet::Layer> &layers,
                              int w, int h, std::vector<float> &relief,
                              std::vector<float> &smooth,
                              const PlaceSettings *where,
                              std::vector<float> *wet) {
  relief.assign((size_t)w * h, 0.f);
  smooth.assign((size_t)w * h, 0.f);
  if (wet) wet->assign((size_t)w * h, 0.f);
  if (layers.empty() || w <= 0 || h <= 0) return;
  // the tile's place on the planet: a texel (u, v) of the tile lies at
  // world (X, Z) through the tile's offset, heading and scale, and the
  // relief there is what the tile blends to - the same mapping the
  // vertex stage applies (terrain_xform.hpp), so the join is exact
  const bool tx = where && where->tx_on;
  const float ch = tx ? std::cos(where->tx_yaw * 3.14159265f / 180.f) : 1.f;
  const float sh = tx ? std::sin(where->tx_yaw * 3.14159265f / 180.f) : 0.f;
  const float sx = tx ? where->tx_scl[0] : 1.f, sz = tx ? where->tx_scl[1] : 1.f;
  const float px = tx ? where->tx_pos[0] : 0.f, pz = tx ? where->tx_pos[1] : 0.f;
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
        // the plane the surround shader samples: (X, 0.37, Z)
        float X = u, Z = v;
        if (tx) {
          const float qx = (u - 0.5f) * sx, qz = (v - 0.5f) * sz;
          X = ch * qx + sh * qz + 0.5f + px;   // H(yaw) as the shader composes it
          Z = -sh * qx + ch * qz + 0.5f + pz;
        }
        const float d[3] = {X, 0.37f, Z};
        const size_t i = (size_t)y * w + x;
        float wv = 0.f;
        relief[i] = 1.2f * gpx::planet::heightf(d, L, n, octf, wet ? &wv : nullptr);
        smooth[i] = 1.2f * gpx::planet::heightf(d, L, n, octs);
        if (wet) (*wet)[i] = wv;
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
  std::vector<float> relief, smooth, wet;
  float key[6];
  tx_key(s, key);
  {
    std::lock_guard<std::mutex> lk(g_cache_mtx);
    for (const ReliefEntry &e : g_cache)
      if (e.w == w && e.h == h && same_layers(e.layers, layers) &&
          std::equal(key, key + 6, e.tx)) {
        relief = e.relief;
        smooth = e.smooth;
        wet = e.wet;
        break;
      }
  }
  if (relief.empty()) {
    planet_relief_under_tile(layers, w, h, relief, smooth, &s, &wet);
    std::lock_guard<std::mutex> lk(g_cache_mtx);
    ReliefEntry e{layers, w, h, {}, relief, smooth, wet};
    std::copy(key, key + 6, e.tx);
    g_cache.push_front(std::move(e));
    while (g_cache.size() > 6) g_cache.pop_back();
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
  res.weight = gpx::Heightmap(w, h);
  // the planet's wetness travels with the weight: the tile's shader
  // paints the same wet soil along the same valley floors as the ground
  // outside it (planet_place.hpp)
  res.wet = gpx::Heightmap(w, h);
  if (wet.size() == res.wet.v.size()) res.wet.v = wet;
  const float edge = std::max(s.edge, 1e-4f);
  const float flat = std::clamp(s.flatten, 0.f, 1.f);
  const float grad = std::clamp(s.gradient, 0.05f, 8.f);
  const bool whole = s.mode >= 1;
  const bool zero_edge = s.mode == 2;
  // Clipping: the tile stands only where it is higher (mode 3, +1) or lower
  // (mode 4, -1) than the planet's own ground; the other way the planet
  // shows. A plain max of the two surfaces is continuous but its slope is
  // not - the join is a knife crease along wherever they cross, and no
  // landscape has one. It is a smooth maximum instead, rounded over the
  // same `presence` band the material weight fades across, so the shape
  // and the texture give way together.
  const int clip = s.mode == 3 ? 1 : (s.mode == 4 ? -1 : 0);
  const gpx::Heightmap *mask = s.mask && !s.mask->empty() ? s.mask.get() : nullptr;
  gpx::parallel_rows(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      const float v = h > 1 ? (float)y / (float)(h - 1) : 0.f;
      for (int x = 0; x < w; ++x) {
        const float u = w > 1 ? (float)x / (float)(w - 1) : 0.f;
        const size_t i = (size_t)y * w + x;
        // a blurred blob is 0.5 at its own outline: 1 inside, a halo outside;
        // the whole-tile mode has no blob - the tile is the tile
        float wgt = whole ? 1.f : smoothstep01(0.f, 0.5f, pres[i]);
        // the tile has nothing to say past its outline: the border of the
        // square, the rim of a disc, or the sides of a centred rectangle
        float b;
        if (s.shape == 1) {
          const float du = u - 0.5f, dv = v - 0.5f;
          b = 0.5f - std::sqrt(du * du + dv * dv);
        } else if (s.shape == 2) {
          const float a = std::max(s.aspect, 0.05f);
          const float hw = a >= 1.f ? 0.5f / a : 0.5f;   // half width
          const float hd = a >= 1.f ? 0.5f : 0.5f * a;   // half depth
          b = std::min(std::min(hw - std::fabs(u - 0.5f), hd - std::fabs(v - 0.5f)),
                       std::min(std::min(u, 1.f - u), std::min(v, 1.f - v)));
        } else {
          // Not the plain minimum of the four edge distances. That is the
          // Chebyshev distance to the square: its contours are squares,
          // with a crease running out to each corner where the argmin
          // swaps - so the feather, the material weight and everything
          // downstream of them draw a square frame on the ground. That
          // frame is the square people report seeing.
          //
          // A p-norm rounds it away: a large p is the square again, p = 2
          // the inscribed circle, and in between the corners come off while
          // the sides stay where they are. It is smooth everywhere, so
          // there is no crease left to catch the light.
          const float du = std::fabs(u - 0.5f), dv = std::fabs(v - 0.5f);
          const float rr = std::clamp(s.round, 0.f, 1.f);
          if (rr <= 0.001f) {
            b = 0.5f - std::max(du, dv);
          } else {
            // p = 2 is the circle inscribed in the tile and p = 8 already
            // reads as a square again, so the whole useful range lives in
            // between - a 30 there left the default indistinguishable from
            // the square it was meant to replace.
            const float pn = 2.f + (1.f - rr) * 6.f;
            b = 0.5f - std::pow(std::pow(du, pn) + std::pow(dv, pn), 1.f / pn);
          }
        }
        // And the outline wanders, so the eye finds no outline at all.
        // Inward only: the tile has no data past its own square, so a
        // border pushed outward would show the tile where there is nothing
        // to show and put back the hard edge this is here to remove.
        if (s.wander > 0.f && s.shape != 2)
          b -= border_noise(u, v) * edge * std::clamp(s.wander, 0.f, 1.f) * 1.3f;
        // the border feather over `edge`, bent by the gradient: above 1 the
        // tile gives way from further in, below 1 it holds until the rim
        {
          float tb = std::pow(std::clamp(b / edge, 0.f, 1.f), grad);
          // zero edge: the S-curve applied twice is flat at both ends, so
          // neither the tile's rim nor the planet's shows a crease
          float f = smoothstep01(0.f, 1.f, tb);
          if (zero_edge) f = smoothstep01(0.f, 1.f, f);
          wgt *= f;
        }
        if (mask) {
          const int mx = std::clamp((int)(u * (float)(mask->w - 1) + 0.5f), 0, mask->w - 1);
          const int my = std::clamp((int)(v * (float)(mask->h - 1) + 0.5f), 0, mask->h - 1);
          wgt *= std::clamp(mask->at(mx, my), 0.f, 1.f);
        }
        const float pb = ground + relief[i];
        const float pbs = ground + smooth[i];
        const float seat = pb + (pbs - pb) * flat;
        const float feature = seat + (tile.v[i] - tile_ground);
        if (clip != 0) {
          // where the tile loses, the planet is what stands there
          const float kept = clip > 0 ? place_smax(feature, pb, pw * 0.5f)
                                      : -place_smax(-feature, -pb, pw * 0.5f);
          outm.v[i] = pb + (kept - pb) * wgt;
          wgt *= smoothstep01(0.f, pw, (feature - pb) * (float)clip);
        } else {
          outm.v[i] = pb + (feature - pb) * wgt;
        }
        res.weight.v[i] = wgt;
      }
    }
  });
  res.ground = ground;
  res.tile_ground = tile_ground;
  res.placed = true;
  if (out) *out = res;
  return outm;
}

std::vector<float> planet_place_rg(const PlaceResult &r, int w, int h) {
  const size_t n = (size_t)w * (size_t)h;
  if (!r.placed || r.weight.w != w || r.weight.h != h || r.weight.v.size() != n)
    return {};
  const bool has_wet = r.wet.w == w && r.wet.h == h && r.wet.v.size() == n;
  std::vector<float> rg(n * 2);
  for (size_t i = 0; i < n; ++i) {
    rg[i * 2] = r.weight.v[i];
    rg[i * 2 + 1] = has_wet ? r.wet.v[i] : 0.f;
  }
  return rg;
}

const PlaceResult &planet_place_last() { return g_last; }
void planet_place_set_last(const PlaceResult &r) { g_last = r; }

} // namespace studio
