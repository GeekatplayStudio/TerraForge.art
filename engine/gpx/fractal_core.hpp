// Geekatplay TerraForge — the Vue-class fractal engine.
//
// Vue's fractal nodes (Reference Manual p850-884) share one generic body:
// a base noise repeated over harmonics, with a wavelength and per-axis
// stretch (damped up the harmonics), a scale ratio and amplitude ratio
// between iterations, roughness and gain, nine ways to combine the
// iterations, distortion of the input coordinates, a variable-roughness
// mode keyed on the altitude of earlier iterations, a filter profile with
// creep-in, and a second output that reports local roughness. The terrain
// flavours add a landscape type (plain / ridges / billows / mixes), ridge
// smoothness and bump surge. That body lives here, once, and the nodes in
// nodes_fractals*.cpp are parameter sets over it - the same design as the
// pl_fbm/planet kernels: one implementation, several fronts.
//
// Everything is a pure function of (x, y, seed, params): deterministic,
// trivially parallel, bit-identical under any thread count.
#pragma once
#include "gpx/noise_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gpx::fractal {

enum Base { PERLIN = 0, VALUE, CELL_F1, CELL_EDGES, GRAINY };
enum Combine { ADD = 0, BLEND, VAR_ROUGH, VAR_ROUGH_ABS, MAX, MAX_ABS, MIN,
               MIN_ABS, MULTIPLY };
enum Landscape { PLAIN = 0, RIDGES, BILLOWS, RIDGE_MIX, BILLOW_RIDGE_MIX };
enum Profile { P_NONE = 0, P_TERRACE, P_SOFT_CLIP, P_S_CURVE, P_PLATEAU,
               P_VALLEYS };

struct Params {
  // base noise
  int base = PERLIN;
  bool rotate = true;        // rotate each harmonic (hides lattice direction)
  bool double_noise = false; // a second, offset noise multiplied in
  // scale
  float wavelength = 0.25f;  // in tile units (coords come in 0..1)
  float stretch_x = 1.f, stretch_y = 1.f;
  float stretch_damping = 0.5f; // 0: every harmonic stretched, 1: only the first
  // fractal
  int octaves = 8;
  float scale_ratio = 0.5f;  // wavelength ratio between iterations (Vue: Scale)
  float amp_ratio = 0.5f;    // amplitude ratio between iterations
  float roughness = 1.f;     // 0..2, scales amp_ratio: more = more detail
  float gain = 1.f;          // contrast on the result
  int combine = ADD;
  // variable roughness (Vue: Variable Roughness Fractal)
  float smooth_level = 0.f;  // altitude of least roughness, -1..1
  float influence = 0.f;     // 0: plain fractal
  float local_influence = 0.f; // 0: keyed on the first iteration, 1: the last
  // distortion
  float distortion = 0.f;    // domain warp amount, tile units
  float distortion_scale = 1.f;
  float filter_steepness = 1.f; // contrast of the base noise itself
  // grainy variation (Vue: Noise variation)
  float variation_strength = 0.f;
  float variation_roughness = 0.5f;
  float smooth_altitude = 0.f;
  // landscape (terrain fractals)
  int landscape = PLAIN;
  float blend = 0.5f;        // mix weight for the *_MIX types
  float ridge_smooth = 0.2f; // rounding of ridges / billows
  float bump_surge = 0.f;    // bumps rise (+) or sink (-)
  // filter profile (Vue: Filter, Creep-in, Min, Max)
  int profile = P_NONE;
  float profile_steps = 6.f;
  float creep_in = 0.f;
  float filter_min = 0.f, filter_max = 1.f; // fraction of the range it acts on
  // output
  float amplitude = 1.f, offset = 0.f;
  float rough_ref = 0.f;     // ref. feature size (tile units) for rough areas
};

// base noise, -1..1
inline float base_noise(int base, float x, float y, uint32_t seed) {
  switch (base) {
    case VALUE: return noise::value_noise(x, y, seed) * 2.f - 1.f;
    case CELL_F1: {
      float f1, f2;
      noise::worley(x, y, seed, f1, f2, 1.f);
      return std::clamp(f1 * 1.6f, 0.f, 1.f) * 2.f - 1.f;
    }
    case CELL_EDGES: {
      float f1, f2;
      noise::worley(x, y, seed, f1, f2, 1.f);
      return std::clamp((f2 - f1) * 2.2f, 0.f, 1.f) * 2.f - 1.f;
    }
    case GRAINY: // perlin sharpened by value noise: detail at every frequency
      return std::clamp(noise::perlin(x, y, seed) *
                            (0.6f + 0.8f * noise::value_noise(x * 1.7f, y * 1.7f,
                                                              seed ^ 0x5bd1e995u)),
                        -1.f, 1.f);
    default: return std::clamp(noise::perlin(x, y, seed), -1.f, 1.f);
  }
}

// one harmonic shaped by the landscape type, -1..1
inline float shape(float n, int landscape, float blend, float ridge_smooth) {
  auto ridge = [&](float v) {
    float r = 1.f - std::fabs(v);
    // rounding: pull the crease toward a parabola
    r = r * (1.f - ridge_smooth) + (1.f - v * v) * ridge_smooth;
    return r * 2.f - 1.f;
  };
  auto billow = [&](float v) {
    float b = std::fabs(v);
    b = b * (1.f - ridge_smooth) + v * v * ridge_smooth;
    return b * 2.f - 1.f;
  };
  switch (landscape) {
    case RIDGES: return ridge(n);
    case BILLOWS: return billow(n);
    case RIDGE_MIX: return ridge(n) * (1.f - blend) + ridge(n * 0.5f + 0.25f) * blend;
    case BILLOW_RIDGE_MIX: return billow(n) * (1.f - blend) + ridge(n) * blend;
    default: return n;
  }
}

// the profile filter, on a 0..1 value
inline float profile(int p, float v, float steps) {
  switch (p) {
    case P_TERRACE: return std::floor(v * steps + 0.5f) / steps;
    case P_SOFT_CLIP: return 0.5f + 0.5f * std::tanh((v - 0.5f) * 3.f);
    case P_S_CURVE: return v * v * (3.f - 2.f * v);
    case P_PLATEAU: return std::min(v, 0.7f) / 0.7f;
    case P_VALLEYS: return v * v;
    default: return v;
  }
}

// Evaluate at (x, y) in tile units. Returns the altitude (about -1..1 before
// amplitude/offset) and, through rough_out, the local roughness in 0..1.
inline float eval(float x, float y, uint32_t seed, const Params &P,
                  float *rough_out = nullptr) {
  // distortion: smear the input coordinates with a low-frequency noise
  if (P.distortion != 0.f) {
    float ds = 1.f / std::max(P.wavelength * P.distortion_scale, 1e-4f);
    float dx = noise::perlin(x * ds + 31.7f, y * ds + 11.3f, seed ^ 0x27d4eb2fu);
    float dy = noise::perlin(x * ds - 17.1f, y * ds + 47.9f, seed ^ 0x165667b1u);
    x += dx * P.distortion;
    y += dy * P.distortion;
  }
  const float persistence = std::clamp(P.amp_ratio * P.roughness, 0.02f, 0.98f);
  const float lacunarity = 1.f / std::clamp(P.scale_ratio, 0.05f, 0.95f);
  float freq = 1.f / std::max(P.wavelength, 1e-5f);
  float amp = 1.f;
  float sum = 0.f, norm = 0.f, rough = 0.f, rough_norm = 0.f;
  float first = 0.f, prev = 0.f;
  float acc_max = -1e9f, acc_min = 1e9f, acc_mul = 1.f;
  const int oct = std::clamp(P.octaves, 1, 16);
  for (int i = 0; i < oct; ++i) {
    // stretch, damped up the harmonics; rotation by the golden angle
    float damp = 1.f / (1.f + (float)i * P.stretch_damping * 2.f);
    float sx = std::pow(std::max(P.stretch_x, 1e-3f), damp);
    float sy = std::pow(std::max(P.stretch_y, 1e-3f), damp);
    float px = x / sx, py = y / sy;
    if (P.rotate && i > 0) {
      float a = (float)i * 2.39996323f;
      float c = std::cos(a), s = std::sin(a);
      float rx = px * c - py * s, ry = px * s + py * c;
      px = rx;
      py = ry;
    }
    uint32_t hs = seed + (uint32_t)i * 1013u;
    float n = base_noise(P.base, px * freq + (float)i * 7.31f, py * freq - (float)i * 3.17f, hs);
    if (P.double_noise)
      n *= 0.5f + 0.5f * base_noise(P.base, px * freq * 1.31f + 91.f, py * freq * 0.77f + 17.f,
                                    hs ^ 0x9e3779b9u) + 0.25f;
    if (P.filter_steepness != 1.f)
      n = std::copysign(std::pow(std::fabs(n), 1.f / std::max(P.filter_steepness, 0.05f)), n);
    n = shape(n, P.landscape, P.blend, P.ridge_smooth);
    // grainy variation: the harmonic's weight varies over the map
    if (P.variation_strength > 0.f) {
      float g = noise::perlin(px * freq * P.variation_roughness + 5.f,
                              py * freq * P.variation_roughness - 9.f, hs ^ 0x7f4a7c15u);
      float var = 1.f - P.variation_strength * std::clamp(0.5f + 0.5f * g - P.smooth_altitude, 0.f, 1.f);
      n *= var;
    }
    // variable roughness: harmonics past the first weaken near the smooth level
    float a_i = amp;
    if (i > 0 && P.influence > 0.f) {
      float ref = first * (1.f - P.local_influence) + prev * P.local_influence;
      float d = std::clamp(std::fabs(ref - P.smooth_level), 0.f, 1.f);
      a_i *= 1.f - P.influence * (1.f - d);
    }
    float contrib = n * a_i;
    switch (P.combine) {
      case BLEND: sum += contrib; norm += a_i; break;
      case VAR_ROUGH: {
        float w = i == 0 ? 1.f : std::clamp(0.5f - first * 0.5f, 0.f, 1.f);
        sum += contrib * w; norm += a_i * w; break;
      }
      case VAR_ROUGH_ABS: {
        float w = i == 0 ? 1.f : std::clamp(1.f - std::fabs(first), 0.f, 1.f);
        sum += contrib * w; norm += a_i * w; break;
      }
      case MAX: acc_max = std::max(acc_max, contrib); break;
      case MAX_ABS: if (std::fabs(contrib) > std::fabs(acc_max) || i == 0) acc_max = contrib; break;
      case MIN: acc_min = std::min(acc_min, contrib); break;
      case MIN_ABS: if (std::fabs(contrib) < std::fabs(acc_min) || i == 0) acc_min = contrib; break;
      case MULTIPLY: acc_mul *= 0.5f + 0.5f * n; break;
      default: sum += contrib; norm += a_i; break;
    }
    // roughness: energy of the harmonics finer than the reference size
    if (P.rough_ref <= 0.f || 1.f / freq < P.rough_ref) {
      rough += std::fabs(contrib);
      rough_norm += a_i;
    }
    if (i == 0) first = n;
    prev = n;
    amp *= persistence;
    freq *= lacunarity;
  }
  float v;
  switch (P.combine) {
    case BLEND: case VAR_ROUGH: case VAR_ROUGH_ABS: v = norm > 0.f ? sum / norm : 0.f; break;
    case MAX: case MAX_ABS: v = acc_max; break;
    case MIN: case MIN_ABS: v = acc_min; break;
    case MULTIPLY: v = acc_mul * 2.f - 1.f; break;
    default: v = norm > 0.f ? sum / norm : 0.f; break; // ADD, normalised
  }
  if (P.bump_surge != 0.f) v += P.bump_surge * v * std::fabs(v);
  if (P.gain != 1.f)
    v = std::copysign(std::pow(std::fabs(std::clamp(v, -1.f, 1.f)), 1.f / std::max(P.gain, 0.05f)), v);
  // profile filter on the 0..1 range, within [min, max] of it, with creep-in
  if (P.profile != P_NONE) {
    float u = std::clamp(v * 0.5f + 0.5f, 0.f, 1.f);
    float lo = std::min(P.filter_min, P.filter_max), hi = std::max(P.filter_max, lo + 1e-4f);
    float t = std::clamp((u - lo) / (hi - lo), 0.f, 1.f);
    float f = lo + profile(P.profile, t, std::max(P.profile_steps, 1.f)) * (hi - lo);
    if (u < lo || u > hi) f = u;
    u = f * (1.f - P.creep_in) + u * P.creep_in;
    v = u * 2.f - 1.f;
  }
  if (rough_out) *rough_out = rough_norm > 0.f ? std::clamp(rough / rough_norm, 0.f, 1.f) : 0.f;
  return v * P.amplitude + P.offset;
}

} // namespace gpx::fractal
