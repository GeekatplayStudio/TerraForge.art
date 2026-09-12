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

namespace studio {

inline float relief_fract(float x) { return x - std::floor(x); }

// gp_hash
inline float relief_hash(float px, float py) {
  px = relief_fract(px * 123.34f);
  py = relief_fract(py * 456.21f);
  const float d = px * (px + 45.32f) + py * (py + 45.32f);
  px += d;
  py += d;
  return relief_fract(px * py);
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

// gp_detail: ridged fBm, 0..1
inline float relief_detail(float u, float v, float base_freq, int octaves, float gain) {
  float sum = 0.f, amp = 1.f, norm = 0.f, freq = base_freq;
  for (int i = 0; i < std::min(octaves, 12); ++i) {
    const float o = float(i) * 17.3f;
    float n = relief_vnoise(u * freq + o, v * freq + o);
    n = 1.f - std::fabs(n * 2.f - 1.f);
    sum += n * amp;
    norm += amp;
    amp *= gain;
    freq *= 2.03f;
  }
  return norm > 0.f ? sum / norm : 0.f;
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
  float sum = 0.f, amp = 1.f, norm = 0.f, freq = scale, whole = 0.f;
  for (int i = 0; i < n; ++i) {
    const float o = float(i) * 17.3f;
    float nz = relief_vnoise(u * freq + o, v * freq + o);
    nz = 1.f - std::fabs(nz * 2.f - 1.f);
    sum += nz * amp;
    norm += amp;
    amp *= g;
    freq *= 2.03f;
    if (i + 1 == o0) whole = sum / norm;
  }
  float f = o0 > 0 ? whole - 0.5f : 0.f;
  if (fading) f += (sum / norm - 0.5f - f) * ft;
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
