// Geekatplay TerraForge — planetary surface math.
//
// A planet's relief is a sum of infinite procedural layers evaluated in 3D
// space on the unit sphere direction (the Vue manual is explicit about why:
// only a 3D-domain function stays continuous over the whole planet — any 2D
// parameterization pinches at the poles). The same function runs here on the
// CPU (tests, picking, altitude queries) and as a GLSL mirror in the
// renderer, so what you click is what you see.
//
// Everything is derived from the layer parameters and a seed: a planet costs
// no memory beyond its ~100-byte description, which is what makes an
// unlimited number of planets (each with unlimited surface detail) possible.
#pragma once
#include <cmath>
#include <cstdint>

namespace gpx::planet {

// one infinite procedural layer on a planet (or on the home ground plane)
struct Layer {
  uint32_t seed = 1;
  int type = 1;          // 0 rolling hills, 1 ridged mountains, 2 billow dunes,
                         // 3 realistic terrain (see pl_terrain)
  float frequency = 3.f; // features per planet radius
  float amplitude = 1.f; // relative weight within the planet's relief budget
  int octaves = 6;       // detail depth for CPU evaluation (GPU picks its own)
  float coverage = 1.f;  // 0..1 fraction of the sphere the layer occupies
  float mask_scale = 1.5f; // how large the covered regions are
};

static const int MAX_LAYERS = 6;

// ---- 3D value noise (mirrored in GLSL as pl_hash/pl_vnoise/pl_fbm) --------
inline float pl_hash(float x, float y, float z, uint32_t seed) {
  // integer lattice hash; stable across platforms
  int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
  uint32_t h = (uint32_t)xi * 374761393u + (uint32_t)yi * 668265263u +
               (uint32_t)zi * 2147483647u + seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xffffff) / 16777215.f;
}

// The same lattice hash, before it is squeezed into 0..1. Cellular noise needs
// three independent numbers per cell and one hash carries enough entropy for
// all three, so it slices this rather than hashing three times.
inline uint32_t pl_hash_bits(int xi, int yi, int zi, uint32_t seed) {
  uint32_t h = (uint32_t)xi * 374761393u + (uint32_t)yi * 668265263u +
               (uint32_t)zi * 2147483647u + seed * 3266489917u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return h;
}

// ---- 3D cellular (Worley) noise -------------------------------------------
// Scatters one feature point per lattice cell and reports the distance to the
// nearest (f1) and second nearest (f2), plus a stable random value for the
// nearest cell (id). Those three cover what cellular noise is actually used
// for: f1 is bubbles and craters, f2-f1 draws the cell walls that read as
// cracked ground and basalt columns, and id paints flat plates.
//
// Mirrored in GLSL as gpxf_cell (engine/field_glsl.cpp). The two must agree —
// the CPU/GPU agreement check exists to keep them honest.
//
// metric: 0 Euclidean (round cells), 1 Manhattan (diamond), 2 Chebyshev (square).
inline void pl_cell(float x, float y, float z, uint32_t seed, float jitter,
                    int metric, float &f1, float &f2, float &id) {
  const int xi = (int)std::floor(x), yi = (int)std::floor(y),
            zi = (int)std::floor(z);
  const float fx = x - (float)xi, fy = y - (float)yi, fz = z - (float)zi;
  f1 = 1e9f;
  f2 = 1e9f;
  id = 0.f;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const uint32_t h = pl_hash_bits(xi + dx, yi + dy, zi + dz, seed);
        // three offsets out of one hash, from disjoint bit ranges
        const float ox = (float)(h & 0x3ffu) * (1.f / 1023.f);
        const float oy = (float)((h >> 10) & 0x3ffu) * (1.f / 1023.f);
        const float oz = (float)((h >> 20) & 0x3ffu) * (1.f / 1023.f);
        const float px = (float)dx + 0.5f + (ox - 0.5f) * jitter - fx;
        const float py = (float)dy + 0.5f + (oy - 0.5f) * jitter - fy;
        const float pz = (float)dz + 0.5f + (oz - 0.5f) * jitter - fz;
        float d;
        if (metric == 1)
          d = std::fabs(px) + std::fabs(py) + std::fabs(pz);
        else if (metric == 2)
          d = std::fmax(std::fabs(px), std::fmax(std::fabs(py), std::fabs(pz)));
        else
          d = std::sqrt(px * px + py * py + pz * pz);
        if (d < f1) {
          f2 = f1;
          f1 = d;
          id = (float)(h & 0xffffffu) * (1.f / 16777215.f);
        } else if (d < f2) {
          f2 = d;
        }
      }
}

inline float pl_fade(float t) { return t * t * (3.f - 2.f * t); }

inline float pl_vnoise(float x, float y, float z, uint32_t seed) {
  float fx = x - std::floor(x), fy = y - std::floor(y), fz = z - std::floor(z);
  float ux = pl_fade(fx), uy = pl_fade(fy), uz = pl_fade(fz);
  auto c = [&](int dx, int dy, int dz) {
    return pl_hash(std::floor(x) + dx, std::floor(y) + dy, std::floor(z) + dz,
                   seed);
  };
  float x00 = c(0, 0, 0) + (c(1, 0, 0) - c(0, 0, 0)) * ux;
  float x10 = c(0, 1, 0) + (c(1, 1, 0) - c(0, 1, 0)) * ux;
  float x01 = c(0, 0, 1) + (c(1, 0, 1) - c(0, 0, 1)) * ux;
  float x11 = c(0, 1, 1) + (c(1, 1, 1) - c(0, 1, 1)) * ux;
  float y0 = x00 + (x10 - x00) * uy;
  float y1 = x01 + (x11 - x01) * uy;
  return y0 + (y1 - y0) * uz;
}

// fBm in one of the three basic layer styles; returns roughly -0.5..0.5.
//
// `octf` is a FLOAT octave count, exactly as the GLSL mirror takes it: the
// top octave fades in with clamp(octf - i, 0, 1). An integer count gives
// weights of exactly 1 and 0, so the int overload below is bit-identical to
// what it always produced, while the renderer's continuous LOD and the CPU
// compositing of the terrain tile can both ask for fractional detail.
inline float pl_fbmf(float x, float y, float z, uint32_t seed, float octf,
                     int type) {
  float sum = 0.f, amp = 1.f, norm = 0.f;
  float fx = x, fy = y, fz = z;
  for (int i = 0; i < 12; ++i) {
    float w = octf - (float)i;
    w = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
    if (w <= 0.f) break;
    float n = pl_vnoise(fx, fy, fz, seed + (uint32_t)i * 101u);
    if (type == 1) n = 1.f - std::fabs(n * 2.f - 1.f);       // ridged
    else if (type == 2) n = std::fabs(n * 2.f - 1.f);        // billow
    sum += n * amp * w;
    norm += amp * w;
    amp *= 0.5f;
    fx *= 2.03f; fy *= 2.03f; fz *= 2.03f; // irrational-ish: avoids banding
  }
  float v = norm > 0.f ? sum / norm : 0.f;
  if (type == 1) v = v * v; // sharpen ridges
  return v - 0.5f;
}
inline float pl_fbm(float x, float y, float z, uint32_t seed, int octaves,
                    int type) {
  return pl_fbmf(x, y, z, seed, (float)(octaves < 12 ? octaves : 12), type);
}

inline float pl_clamp01(float t) { return t < 0.f ? 0.f : (t > 1.f ? 1.f : t); }
inline float pl_smoothstep(float e0, float e1, float x) {
  float t = pl_clamp01((x - e0) / (e1 - e0));
  return t * t * (3.f - 2.f * t);
}

// ---- value noise with its analytic gradient (GLSL: pl_vnoise_d) ----------
// The gradient is what makes erosion cheap: a fractal that knows how steep
// it already is can put its fine detail on the flats and leave the slopes
// smooth, which is what water does to real mountains.
inline float pl_vnoise_d(float x, float y, float z, uint32_t seed, float g[3]) {
  float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
  float fx = x - ix, fy = y - iy, fz = z - iz;
  float ux = pl_fade(fx), uy = pl_fade(fy), uz = pl_fade(fz);
  float dx = 6.f * fx * (1.f - fx), dy = 6.f * fy * (1.f - fy),
        dz = 6.f * fz * (1.f - fz);
  auto c = [&](int a, int b, int d) { return pl_hash(ix + a, iy + b, iz + d, seed); };
  float c000 = c(0, 0, 0), c100 = c(1, 0, 0), c010 = c(0, 1, 0), c110 = c(1, 1, 0);
  float c001 = c(0, 0, 1), c101 = c(1, 0, 1), c011 = c(0, 1, 1), c111 = c(1, 1, 1);
  // trilinear as a polynomial in (ux, uy, uz): k0 + k1 ux + k2 uy + k3 uz
  //   + k4 ux uy + k5 uy uz + k6 uz ux + k7 ux uy uz
  float k0 = c000, k1 = c100 - c000, k2 = c010 - c000, k3 = c001 - c000;
  float k4 = c000 - c100 - c010 + c110;
  float k5 = c000 - c010 - c001 + c011;
  float k6 = c000 - c100 - c001 + c101;
  float k7 = -c000 + c100 + c010 - c110 + c001 - c101 - c011 + c111;
  g[0] = dx * (k1 + k4 * uy + k6 * uz + k7 * uy * uz);
  g[1] = dy * (k2 + k4 * ux + k5 * uz + k7 * ux * uz);
  g[2] = dz * (k3 + k5 * uy + k6 * ux + k7 * ux * uy);
  return k0 + k1 * ux + k2 * uy + k3 * uz + k4 * ux * uy + k5 * uy * uz +
         k6 * uz * ux + k7 * ux * uy * uz;
}

// ---- eroded fBm (GLSL: pl_fbm_eroded) --------------------------------------
// Each octave is damped by the gradient accumulated so far (after Quilez):
// where the surface is already steep the fine octaves are suppressed, so
// slopes come out smooth and valley floors and ridge tops stay busy. Ridged
// (1) gives eroded mountains in 0..1; plain (0) gives hills in 0..1.
inline float pl_fbm_eroded(float x, float y, float z, uint32_t seed, float octf,
                           int ridged) {
  float sum = 0.f, amp = 1.f, norm = 0.f;
  float dsx = 0.f, dsy = 0.f, dsz = 0.f;
  float fx = x, fy = y, fz = z, lac = 1.f;
  for (int i = 0; i < 12; ++i) {
    float w = octf - (float)i;
    w = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
    if (w <= 0.f) break;
    float g[3];
    float n = pl_vnoise_d(fx, fy, fz, seed + (uint32_t)i * 101u, g);
    float n2 = n * 2.f - 1.f;
    float gx = g[0] * 2.f * lac, gy = g[1] * 2.f * lac, gz = g[2] * 2.f * lac;
    float v;
    if (ridged != 0) {
      float r = 1.f - std::fabs(n2);
      float s = (n2 < 0.f ? 1.f : -1.f) * 2.f * r; // d(r*r)/dn2
      v = r * r;
      gx *= s; gy *= s; gz *= s;
    } else {
      v = n;
      gx *= 0.5f; gy *= 0.5f; gz *= 0.5f;
    }
    dsx += gx * amp; dsy += gy * amp; dsz += gz * amp;
    float e = 1.f / (1.f + (dsx * dsx + dsy * dsy + dsz * dsz) * 0.35f);
    sum += v * amp * w * e;
    norm += amp * w;
    amp *= 0.5f;
    fx *= 2.03f; fy *= 2.03f; fz *= 2.03f;
    lac *= 2.03f;
  }
  return norm > 0.f ? sum / norm : 0.f;
}

// smooth staircase: flat treads with a soft riser taking `soft` of each step
// (GLSL: pl_terrace)
inline float pl_terrace(float h, float step, float soft) {
  float k = h / step;
  float f = std::floor(k);
  float t = k - f;
  float s = pl_smoothstep(1.f - soft, 1.f, t);
  return (f + s) * step;
}

// ---- realistic terrain (layer type 3; GLSL: pl_terrain) --------------------
// One layer that reads as a landscape rather than as a noise: broad lowland
// basins and uplands, eroded ridged mountains where an upland mask says so,
// rolling hills elsewhere, terraced plateaus in their own regions, a network
// of carved valleys, and lowland lakes sunk below the surrounding ground so
// they fill from the same water level as the sea. Returns about -0.5..0.5;
// `wet` (0..1) says how much of a valley floor or lake bed this point is,
// for shading. `octf` is the octave budget - the masks cap themselves low
// because they are shapes, not detail.
inline float pl_terrain(float x, float y, float z, uint32_t seed, float octf,
                        float *wet) {
  const float m3 = octf < 3.f ? octf : 3.f;
  // broad regions: basins/uplands, mountain belts, plateau country. These
  // are shapes, so they stop at three octaves whatever the budget.
  float cont = pl_fbmf(x, y, z, seed ^ 0x51ed27u, m3, 0);
  float land = pl_smoothstep(-0.05f, 0.08f, cont);
  float mtnm = pl_smoothstep(0.0f, 0.20f,
                             pl_fbmf(x * 1.3f + 11.3f, y * 1.3f + 4.7f,
                                     z * 1.3f - 7.1f, seed ^ 0x7a3c19u, m3, 0)) *
               land;
  float platm = pl_smoothstep(0.08f, 0.20f,
                              pl_fbmf(x * 1.2f - 3.9f, y * 1.2f + 8.2f,
                                      z * 1.2f + 2.6f, seed ^ 0x2f8d5bu, m3, 0)) *
                land * (1.f - mtnm);
  // relief: eroded hills everywhere, eroded ridges in the belts
  float hills = pl_fbm_eroded(x * 2.6f, y * 2.6f, z * 2.6f, seed ^ 0x9d1u,
                              octf < 8.f ? octf : 8.f, 0) - 0.5f;
  float mtns = pl_fbm_eroded(x * 1.7f + 5.5f, y * 1.7f, z * 1.7f + 1.5f,
                             seed ^ 0x3b7u, octf, 1);
  // the ground sits a little above the water on average: seas are the
  // deepest basins, not every second dip
  float base = cont * 0.35f + 0.02f;
  // floodplains: the land near the water is the flattest land there is
  float low = 1.f - pl_smoothstep(0.0f, 0.25f, base);
  float h = base + land * (0.04f + hills * 0.08f * (1.f - 0.85f * low)) +
            mtnm * mtns * 0.5f + (1.f - land) * hills * 0.05f;
  // plateaus: terrace the height where the plateau mask says so
  float ht = pl_terrace(h, 0.05f, 0.4f);
  h += (ht - h) * platm;
  // valleys: a thin band around the zero of a broad noise, cut deeper in
  // the mountains where a river has more to work with
  float riv = pl_fbmf(x * 2.0f + 2.2f, y * 2.0f - 6.4f, z * 2.0f + 9.9f,
                      seed ^ 0x6e2a4cu, octf < 5.f ? octf : 5.f, 0);
  float vall = 1.f - pl_smoothstep(0.f, 0.035f, std::fabs(riv));
  h -= vall * land * (0.03f + 0.06f * mtnm);
  // lowland lakes: basins sunk below the local ground where the land is low
  float lakem = pl_smoothstep(0.19f, 0.25f,
                              pl_fbmf(x * 1.3f - 8.8f, y * 1.3f + 1.9f,
                                      z * 1.3f - 4.4f, seed ^ 0x1c9e73u, 2.f, 0)) *
                land * (1.f - mtnm) * pl_smoothstep(0.12f, 0.03f, base);
  h += ((base - 0.06f) - h) * lakem;
  // soft ceiling so a mountain on an upland never clips flat at the budget
  if (h > 0.42f) h = 0.42f + (h - 0.42f) * 0.3f;
  if (wet) *wet = vall * land > lakem ? vall * land : lakem;
  return h < -0.5f ? -0.5f : (h > 0.5f ? 0.5f : h);
}

// one layer's relief at a point already scaled by its frequency
inline float pl_layer(float x, float y, float z, const Layer &L, float octf,
                      float *wet) {
  if (L.type == 3) return pl_terrain(x, y, z, L.seed, octf, wet);
  if (wet) *wet = 0.f;
  return pl_fbmf(x, y, z, L.seed, octf, L.type);
}

// smoothstep coverage mask: which part of the sphere this layer occupies
inline float pl_mask(float x, float y, float z, const Layer &L) {
  if (L.coverage >= 0.999f) return 1.f;
  if (L.coverage <= 0.001f) return 0.f;
  float m = pl_fbm(x * L.mask_scale, y * L.mask_scale, z * L.mask_scale,
                   L.seed ^ 0x9e3779b9u, 3, 0) + 0.5f; // 0..1
  float edge = 1.f - L.coverage;
  float t = (m - (edge - 0.12f)) / 0.24f;
  t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
  return t * t * (3.f - 2.f * t);
}

// Signed relief at a unit direction, as a fraction of the planet's relief
// budget (the renderer multiplies by radius * relief). `dir` need not be
// perfectly normalized. `octf` is the float octave budget every layer is
// evaluated to (a layer's own `octaves` caps it); `wet` receives the
// wetness of the realistic layers, weighted like their relief.
inline float heightf(const float dir[3], const Layer *layers, int count,
                     float octf, float *wet = nullptr) {
  float total = 0.f, wsum = 0.f, wett = 0.f;
  for (int i = 0; i < count && i < MAX_LAYERS; ++i) {
    const Layer &L = layers[i];
    if (L.amplitude <= 0.f) continue;
    float m = pl_mask(dir[0], dir[1], dir[2], L);
    wsum += L.amplitude;
    if (m <= 0.f) continue;
    float oct = (float)L.octaves < octf ? (float)L.octaves : octf;
    float w = 0.f;
    float h = pl_layer(dir[0] * L.frequency, dir[1] * L.frequency,
                       dir[2] * L.frequency, L, oct, &w);
    total += h * L.amplitude * m;
    wett += w * L.amplitude * m;
  }
  if (wet) *wet = wsum > 0.f ? wett / wsum : 0.f;
  return wsum > 0.f ? total / wsum : 0.f;
}
inline float height(const float dir[3], const Layer *layers, int count,
                    int octave_cap = 12) {
  return heightf(dir, layers, count, (float)octave_cap);
}

// ---- the tile on its planet (GLSL: gpx_sphere_place) -----------------------
// The home terrain tile lies on a sphere whose centre is R below the tile's
// centre; a tile point s, t from that centre travels through angles s/R and
// t/R. When the tile is wider than the circumference the angles are clamped
// so it wraps the globe exactly once, equirectangular, and its heights
// shrink with it (a 1 m planet made from a 5 km heightmap is 1 m of terrain,
// not 5 km of spikes on a marble).
//
// Written so no term ever subtracts R from something of size R: the drop is
// R(1 - cos a cos b) = 2R sin^2(a/2) + 2R cos a sin^2(b/2), and the lateral
// reach is s * sin(a)/a. `c + d*(R+h)` lost the height entirely past a few
// thousand tile radii and jittered the surface long before that.
inline float pl_sinc(float a) {
  return std::fabs(a) < 1e-3f ? 1.f - a * a * (1.f / 6.f) : std::sin(a) / a;
}
// How much a wrapped tile's heights shrink: the square of the wrap ratio,
// so relief shrinks faster than the globe and a 1 m planet made from a 5 km
// heightmap carries Earth-like proportions rather than spikes taller than
// its own radius. Continuous with the unwrapped case at the wrap point.
inline float sphere_height_scale(float R) {
  if (R <= 0.f) return 1.f;
  float c = R * 6.2831853f;
  return c < 1.f ? c * c : 1.f;
}
inline void sphere_place(float u, float v, float h, float R, float out[3]) {
  if (R <= 0.f) {
    out[0] = u; out[1] = h; out[2] = v;
    return;
  }
  float k = 1.f / R, kl = 1.f / R;
  if (k > 6.2831853f) k = 6.2831853f;
  if (kl > 3.14159265f) kl = 3.14159265f;
  float ax = (u - 0.5f) * k, ay = (v - 0.5f) * kl;
  if (ax > 3.14159265f) ax = 3.14159265f;
  if (ax < -3.14159265f) ax = -3.14159265f;
  if (ay > 1.5707963f) ay = 1.5707963f;
  if (ay < -1.5707963f) ay = -1.5707963f;
  h *= sphere_height_scale(R);
  float sx = std::sin(ax), cx = std::cos(ax), sy = std::sin(ay), cl = std::cos(ay);
  float hx = std::sin(ax * 0.5f), hy = std::sin(ay * 0.5f);
  float drop = 2.f * R * hx * hx + 2.f * R * cx * hy * hy;
  // R sin(ax) == (ax R) sinc(ax) == (u - 0.5) k R sinc(ax); k R is 1 unless
  // the tile wraps, in which case the reach is the globe's own
  float reach_x = (u - 0.5f) * (k * R) * pl_sinc(ax);
  float reach_z = (v - 0.5f) * (kl * R) * pl_sinc(ay);
  out[0] = 0.5f + (reach_x + h * sx) * cl;
  out[1] = -drop + h * cx * cl;
  out[2] = 0.5f + reach_z + h * sy;
}

} // namespace gpx::planet
