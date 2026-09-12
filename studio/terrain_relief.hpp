// Geekatplay TerraForge - the terrain's fractal micro-relief, on the CPU.
//
// The viewport lays a ridged fractal over the heightmap as it draws it
// (FRACTAL_FN and terrain_place in shaders_terrain.cpp, the surround in
// planet_shaders.cpp): RenderSettings::fractal_detail world units of it, up
// to half that either way - twelve metres from trough to crest on the default
// five-kilometre tile. Nothing on the CPU knew about it. A plant stood on the
// heightmap was drawn buried under a crest or hanging over a trough, and the
// terrain the offline renderers were given had none of the grit the viewport
// showed.
//
// This is the same function in the same float arithmetic, so a point placed
// with it stands where the viewport draws the ground. `octaves` is continuous
// the way the shaders' is: an octave fades in rather than popping.
//
// How many octaves is the query's to say. What stands on the ground, and
// anything that must not follow the camera (a plant's footing, a grounded
// object's seat, a script's probe), reads every octave: RELIEF_NEAR_OCTAVES.
// What a view shows at a point (a click, the sculpt brush, the orbit pivot)
// reads the count terrain_place draws there: relief_view_octaves, with the
// view's own u_tri_k (studio/ground_march.hpp walks a ray onto it).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace studio {

inline float relief_fract(float x) { return x - std::floor(x); }

// gp_mix / gp_hash. Integer work, so the number here is the number the GPU
// computes rather than nearly it; the mixer is Wellons' lowbias32, as
// cloud_noise.cpp already uses. The old hash ended in fract(x * y), symmetric
// about the diagonal, and laid a weave over everything built on it.
inline uint32_t relief_mix(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
inline float relief_hash(float px, float py) {
  const uint32_t ix = (uint32_t)(int32_t)std::floor(px);
  const uint32_t iy = (uint32_t)(int32_t)std::floor(py);
  const uint32_t h = relief_mix(ix * 0x9E3779B9u ^ relief_mix(iy * 0x85EBCA77u));
  return (float)h * (1.f / 4294967296.f);
}

// gp_turn: turn and scale in one, `cs` being (cos, sin) already scaled. The
// constants are written out, not computed - a cos() evaluated at runtime need
// not give this the same bits as the shader.
struct ReliefTurn {
  float c, s;
};
inline constexpr ReliefTurn RELIEF_BASE{0.81964802f, 0.57286746f};   // 0.61 rad
inline constexpr ReliefTurn RELIEF_WARPA{-0.08788221f, 0.23404426f}; // 1.93, quarter
inline constexpr ReliefTurn RELIEF_WARPB{-0.21068984f, -0.13457263f};// 3.71, quarter
inline constexpr ReliefTurn RELIEF_ROUGH{0.02910383f, -0.04666869f}; // 5.27, a twentieth
inline constexpr ReliefTurn RELIEF_OCT{-1.49685882f, 1.37124530f};   // golden angle * 2.03
inline void relief_turn(float px, float py, const ReliefTurn &t, float &ox, float &oy) {
  ox = px * t.c - py * t.s;
  oy = px * t.s + py * t.c;
}

// gp_vnoise
inline float relief_vnoise(float px, float py) {
  const float ix = std::floor(px), iy = std::floor(py);
  float fx = px - ix, fy = py - iy;
  fx = fx * fx * (3.f - 2.f * fx);
  fy = fy * fy * (3.f - 2.f * fy);
  const float a = relief_hash(ix, iy), b = relief_hash(ix + 1.f, iy);
  const float c = relief_hash(ix, iy + 1.f), d = relief_hash(ix + 1.f, iy + 1.f);
  const float top = a + (b - a) * fx, bottom = c + (d - c) * fx;
  return top + (bottom - top) * fy;
}

// The base position every band is read from: turned off the raw grid once,
// so nothing downstream is axis aligned.
inline void relief_base(float u, float v, float base_freq, float &bx, float &by) {
  relief_turn(u * base_freq, v * base_freq, RELIEF_BASE, bx, by);
}

// The large band: how rough the ground is here, 0.25..1. Read from the turned
// base like everything else.
inline float relief_rough(float bx, float by) {
  float rx, ry;
  relief_turn(bx + 53.9f, by + 17.2f, RELIEF_ROUGH, rx, ry);
  return 0.25f + 0.75f * relief_vnoise(rx, ry);
}

// The medium band: the push on the sample position that actually breaks the
// repeat. Modulating amplitude alone only makes a modulated grid.
inline void relief_warped(float bx, float by, float &px, float &py) {
  float ax, ay, cx, cy;
  relief_turn(bx, by, RELIEF_WARPA, ax, ay);
  relief_turn(bx + 31.7f, by + 9.4f, RELIEF_WARPB, cx, cy);
  px = bx + (relief_vnoise(ax, ay) - 0.5f) * 2.2f;
  py = by + (relief_vnoise(cx, cy) - 0.5f) * 2.2f;
}

// gp_detail: ridged fBm at three scales, 0..1. See FRACTAL_FN in
// studio/shaders_terrain.cpp for why each octave is turned as well as scaled.
inline float relief_detail(float u, float v, float base_freq, int octaves, float gain) {
  float bx, by;
  relief_base(u, v, base_freq, bx, by);
  float px, py;
  relief_warped(bx, by, px, py);
  float sum = 0.f, amp = 1.f, norm = 0.f;
  for (int i = 0; i < std::min(octaves, 12); ++i) {
    float n = relief_vnoise(px, py);
    n = 1.f - std::fabs(n * 2.f - 1.f);
    sum += n * amp;
    norm += amp;
    amp *= gain;
    float rx, ry;
    relief_turn(px, py, RELIEF_OCT, rx, ry);
    px = rx;
    py = ry;
  }
  if (!(norm > 0.f)) return 0.f;
  return 0.5f + (sum / norm - 0.5f) * relief_rough(bx, by);
}

// gp_gain: the uploaded gain, or the 0.5 a program that never set it reads
inline float relief_gain(float g) { return g > 0.f ? std::clamp(g, 0.05f, 0.95f) : 0.5f; }

// The micro-relief at a point of the flat world, tile units in, world units
// out, at `octaves` (continuous, 0..9). `amount`, `scale` and `gain` are
// RenderSettings::fractal_detail, fractal_scale and fractal_gain, the amount
// already capped as the renderer caps it on a small planet.
//
// terrain_place sums gp_detail twice, the whole octaves and one more for the
// octave fading in. The first o0 octaves of the longer sum are the shorter
// sum operation for operation, so one pass reads both: bit for bit
// relief_detail(o0) and relief_detail(o0 + 1), at half the cost - which the
// pickers, sampling the ground many times a click, pay for.
inline float relief_at(float u, float v, float amount, float scale, float gain, float octaves) {
  if (amount <= 0.f) return 0.f;
  const float of = std::clamp(octaves, 0.f, 9.f);
  const int o0 = (int)std::floor(of);
  const float ft = of - float(o0);
  const float g = relief_gain(gain);
  const bool fading = ft > 0.001f && o0 < 9;
  const int n = o0 + (fading ? 1 : 0);
  float bx, by;
  relief_base(u, v, scale, bx, by);
  float px, py;
  relief_warped(bx, by, px, py);
  float sum = 0.f, amp = 1.f, norm = 0.f, whole = 0.f, whole_norm = 1.f;
  for (int i = 0; i < n; ++i) {
    float nz = relief_vnoise(px, py);
    nz = 1.f - std::fabs(nz * 2.f - 1.f);
    sum += nz * amp;
    norm += amp;
    amp *= g;
    float rx, ry;
    relief_turn(px, py, RELIEF_OCT, rx, ry);
    px = rx;
    py = ry;
    if (i + 1 == o0) {
      whole = sum;
      whole_norm = norm;
    }
  }
  // The two sums shaped exactly as relief_detail shapes them - the 0.5 added
  // and taken away again is not redundant. relief_detail returns
  // 0.5 + (mean - 0.5) * rough and terrain_place subtracts 0.5 from that;
  // going straight to (mean - 0.5) * rough rounds differently for small
  // values, and this has to agree with it to the bit or a bake would move
  // under the plants standing on it.
  const float rough = relief_rough(bx, by);
  const float d0 = o0 > 0 ? (0.5f + (whole / whole_norm - 0.5f) * rough) - 0.5f : 0.f;
  float f = d0;
  if (fading) {
    const float d1 = (0.5f + (sum / norm - 0.5f) * rough) - 0.5f;
    f += (d1 - f) * ft;
  }
  return f * amount;
}

// The three dials as the renderer uploads them (renderer_passes.cpp): the
// amount capped at a twentieth of a small planet's radius. A template so this
// header needs nothing but a type with those fields (RenderSettings).
struct ReliefDials {
  float amount = 0.f, scale = 90.f, gain = 0.5f;
};
template <class Settings> ReliefDials relief_dials(const Settings &rs) {
  ReliefDials d;
  d.amount = rs.planet_radius > 0.f ? std::min(rs.fractal_detail, rs.planet_radius * 0.05f) : rs.fractal_detail;
  d.scale = rs.fractal_scale;
  d.gain = rs.fractal_gain;
  return d;
}
inline float relief_at(float u, float v, const ReliefDials &d, float octaves) {
  return relief_at(u, v, d.amount, d.scale, d.gain, octaves);
}

// The octaves a surface close to the camera shows - every one the shaders
// allow, which is what a thing standing on the ground is looked at with.
// Also what a query that must not follow the camera reads: a grounded
// object's seat, a script's probe.
inline constexpr float RELIEF_NEAR_OCTAVES = 9.f;

// How many octaves a triangle `edge` tile units long carries without drawing
// them as a sawtooth: the shaders' own limit, no wave shorter than two edges.
inline float relief_octaves_for_edge(float edge, float scale) {
  return std::clamp(std::log2(1.f / (2.f * edge * std::max(scale, 1e-4f))) / std::log2(2.03f) + 1.f, 0.f, 9.f);
}

// The same limit for a grid `cells` across the tile.
inline float relief_octaves_for_grid(float cells, float scale) {
  return relief_octaves_for_edge(1.f / std::max(cells, 1.f), scale);
}

// gp_octavesf(dist, 9): how much relief is worth drawing `dist` from the eye.
inline float relief_octaves_at_distance(float dist) {
  return std::clamp(std::log2(1.f / std::max(dist, 1e-4f)) * 0.9f + 4.f, 0.f, 9.f);
}

// The count terrain_place draws at a vertex `dist` from the eye (the distance
// to the point before the relief is added): by distance, and no finer than the
// triangles there carry. `tri_k` is the view's u_tri_k, a triangle's edge per
// unit of distance; 0 - an orthographic view - has no triangle limit. What the
// pickers and the orbit pivot read, so they meet the surface a view draws.
// Change the shader's choice and change this.
inline float relief_view_octaves(float dist, float tri_k, float scale) {
  float of = relief_octaves_at_distance(dist);
  if (tri_k > 0.f) of = std::min(of, relief_octaves_for_edge(std::max(dist * tri_k, 1e-7f), scale));
  return std::max(of, 0.f);
}

// u_tri_k for a perspective view `h` pixels tall (renderer_passes.cpp): the
// tessellation's target edge in pixels, or the fixed grid's eight, over the
// pixels a unit of distance spans.
inline float relief_tri_k(float fovy_rad, int h, bool tessellation, float tess_pixels) {
  const float edge_px = tessellation ? std::max(tess_pixels, 1.f) : 8.f;
  return 2.f * std::tan(fovy_rad * 0.5f) / float(std::max(h, 1)) * edge_px;
}

// The wavelength of the finest octave `octaves` draws, tile units - the one
// fading in counts - or 0 when none is drawn. How finely a ray has to be
// walked not to step over it.
inline float relief_finest_wave(float octaves, float scale) {
  const int top = (int)std::ceil(std::clamp(octaves, 0.f, 9.f)) - 1;
  if (top < 0) return 0.f;
  return 1.f / (std::max(scale, 1e-4f) * std::pow(2.03f, float(top)));
}

} // namespace studio
