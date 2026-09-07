// Geekatplay TerraForge — the fractal compute shader.
//
// A line-for-line mirror of gpx::fractal::eval and the gpx::noise functions it
// calls. Kept beside the accelerator rather than inside it so the two can be
// read against their originals without scrolling past dispatch code.
//
// The hashes port exactly: they are 32-bit integer arithmetic, which GLSL
// specifies precisely, so the lattice is bit-identical and only the float
// arithmetic between lattice points can differ. That difference is what
// tests/cpp/test_engine.cpp's agreement check bounds — and it is why the
// noise had to be hash-based with no lookup table for this to be possible at
// all.
//
// What is deliberately NOT here: the cellular bases (worley). The accelerator
// declines those, the CPU path runs, and nobody sees a different terrain.
#pragma once
#include <string>

namespace studio {

inline const std::string &fractal_compute_source() {
  static const std::string src = R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) writeonly buffer OutBuf { float out_h[]; };
layout(std430, binding = 1) writeonly buffer RoughBuf { float out_r[]; };

uniform ivec2 u_size;
uniform uint  u_seed;
// gpx::fractal::Params, in declaration order where it matters
uniform int   u_base, u_landscape, u_combine, u_octaves, u_profile;
uniform int   u_rotate, u_double_noise;
uniform float u_wavelength, u_stretch_x, u_stretch_y, u_stretch_damping;
uniform float u_scale_ratio, u_amp_ratio, u_roughness, u_gain;
uniform float u_smooth_level, u_influence, u_local_influence;
uniform float u_distortion, u_distortion_scale, u_filter_steepness;
uniform float u_variation_strength, u_variation_roughness, u_smooth_altitude;
uniform float u_blend, u_ridge_smooth, u_bump_surge;
uniform float u_profile_steps, u_creep_in, u_filter_min, u_filter_max;
uniform float u_amplitude, u_offset, u_rough_ref;

// ---- gpx::noise, exactly ------------------------------------------------
uint hash_u32(uint x){
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16; return x;
}
uint hash2(int x, int y, uint seed){
  return hash_u32(uint(x) * 0x9E3779B1u ^ uint(y) * 0x85EBCA77u ^ seed);
}
float hash01(int x, int y, uint seed){
  return float(hash2(x, y, seed)) * (1.0 / 4294967295.0);
}
float fade(float t){ return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
vec2 grad2(int x, int y, uint seed){
  uint h = hash2(x, y, seed);
  float a = float(h) * (6.2831853 / 4294967296.0);
  return vec2(cos(a), sin(a));
}
float perlin(float x, float y, uint seed){
  int xi = int(floor(x)), yi = int(floor(y));
  float xf = x - float(xi), yf = y - float(yi);
  float u = fade(xf), v = fade(yf);
  vec2 g00 = grad2(xi, yi, seed), g10 = grad2(xi + 1, yi, seed);
  vec2 g01 = grad2(xi, yi + 1, seed), g11 = grad2(xi + 1, yi + 1, seed);
  float d00 = g00.x * xf + g00.y * yf;
  float d10 = g10.x * (xf - 1.0) + g10.y * yf;
  float d01 = g01.x * xf + g01.y * (yf - 1.0);
  float d11 = g11.x * (xf - 1.0) + g11.y * (yf - 1.0);
  return 1.4142 * mix(mix(d00, d10, u), mix(d01, d11, u), v);
}
float value_noise(float x, float y, uint seed){
  int xi = int(floor(x)), yi = int(floor(y));
  float u = fade(x - float(xi)), v = fade(y - float(yi));
  float v00 = hash01(xi, yi, seed) * 2.0 - 1.0;
  float v10 = hash01(xi + 1, yi, seed) * 2.0 - 1.0;
  float v01 = hash01(xi, yi + 1, seed) * 2.0 - 1.0;
  float v11 = hash01(xi + 1, yi + 1, seed) * 2.0 - 1.0;
  return mix(mix(v00, v10, u), mix(v01, v11, u), v);
}

// ---- gpx::fractal, exactly ----------------------------------------------
// VALUE = 1, GRAINY = 4 in the Base enum; the cellular bases never reach here
float base_noise(int base, float x, float y, uint seed){
  if (base == 1) return value_noise(x, y, seed) * 2.0 - 1.0;
  if (base == 4)
    return clamp(perlin(x, y, seed) *
                 (0.6 + 0.8 * value_noise(x * 1.7, y * 1.7, seed ^ 0x5bd1e995u)),
                 -1.0, 1.0);
  return clamp(perlin(x, y, seed), -1.0, 1.0);
}
float ridge_of(float v){
  float r = 1.0 - abs(v);
  r = r * (1.0 - u_ridge_smooth) + (1.0 - v * v) * u_ridge_smooth;
  return r * 2.0 - 1.0;
}
float billow_of(float v){
  float b = abs(v);
  b = b * (1.0 - u_ridge_smooth) + v * v * u_ridge_smooth;
  return b * 2.0 - 1.0;
}
float shape(float n){
  if (u_landscape == 1) return ridge_of(n);                 // RIDGES
  if (u_landscape == 2) return billow_of(n);                // BILLOWS
  if (u_landscape == 3)                                      // RIDGE_MIX
    return ridge_of(n) * (1.0 - u_blend) + ridge_of(n * 0.5 + 0.25) * u_blend;
  if (u_landscape == 4)                                      // BILLOW_RIDGE_MIX
    return billow_of(n) * (1.0 - u_blend) + ridge_of(n) * u_blend;
  return n;
}
float profile_of(float v){
  if (u_profile == 1) return floor(v * u_profile_steps + 0.5) / u_profile_steps;
  if (u_profile == 2) return 0.5 + 0.5 * tanh((v - 0.5) * 3.0);
  if (u_profile == 3) return v * v * (3.0 - 2.0 * v);
  if (u_profile == 4) return min(v, 0.7) / 0.7;
  if (u_profile == 5) return v * v;
  return v;
}
float copysign_pow(float v, float e){
  return sign(v) * pow(abs(v), e);
}

void main(){
  ivec2 px = ivec2(gl_GlobalInvocationID.xy);
  if (px.x >= u_size.x || px.y >= u_size.y) return;
  float x = float(px.x) / float(u_size.x);
  float y = float(px.y) / float(u_size.y);

  if (u_distortion != 0.0){
    float ds = 1.0 / max(u_wavelength * u_distortion_scale, 1e-4);
    float dx = perlin(x * ds + 31.7, y * ds + 11.3, u_seed ^ 0x27d4eb2fu);
    float dy = perlin(x * ds - 17.1, y * ds + 47.9, u_seed ^ 0x165667b1u);
    x += dx * u_distortion;
    y += dy * u_distortion;
  }
  float persistence = clamp(u_amp_ratio * u_roughness, 0.02, 0.98);
  float lacunarity = 1.0 / clamp(u_scale_ratio, 0.05, 0.95);
  float freq = 1.0 / max(u_wavelength, 1e-5);
  float amp = 1.0;
  float sum = 0.0, norm = 0.0, rough = 0.0, rough_norm = 0.0;
  float first = 0.0, prev = 0.0;
  float acc_max = -1e9, acc_min = 1e9, acc_mul = 1.0;
  int oct = clamp(u_octaves, 1, 16);
  for (int i = 0; i < oct; ++i){
    float damp = 1.0 / (1.0 + float(i) * u_stretch_damping * 2.0);
    float sx = pow(max(u_stretch_x, 1e-3), damp);
    float sy = pow(max(u_stretch_y, 1e-3), damp);
    float pxx = x / sx, pyy = y / sy;
    if (u_rotate == 1 && i > 0){
      float a = float(i) * 2.39996323;
      float c = cos(a), s = sin(a);
      float rx = pxx * c - pyy * s, ry = pxx * s + pyy * c;
      pxx = rx; pyy = ry;
    }
    uint hs = u_seed + uint(i) * 1013u;
    float n = base_noise(u_base, pxx * freq + float(i) * 7.31,
                         pyy * freq - float(i) * 3.17, hs);
    if (u_double_noise == 1)
      n *= 0.5 + 0.5 * base_noise(u_base, pxx * freq * 1.31 + 91.0,
                                  pyy * freq * 0.77 + 17.0,
                                  hs ^ 0x9e3779b9u) + 0.25;
    if (u_filter_steepness != 1.0)
      n = copysign_pow(n, 1.0 / max(u_filter_steepness, 0.05));
    n = shape(n);
    if (u_variation_strength > 0.0){
      float g = perlin(pxx * freq * u_variation_roughness + 5.0,
                       pyy * freq * u_variation_roughness - 9.0,
                       hs ^ 0x7f4a7c15u);
      float var = 1.0 - u_variation_strength *
                  clamp(0.5 + 0.5 * g - u_smooth_altitude, 0.0, 1.0);
      n *= var;
    }
    float a_i = amp;
    if (i > 0 && u_influence > 0.0){
      float ref = first * (1.0 - u_local_influence) + prev * u_local_influence;
      float d = clamp(abs(ref - u_smooth_level), 0.0, 1.0);
      a_i *= 1.0 - u_influence * (1.0 - d);
    }
    float contrib = n * a_i;
    if (u_combine == 1){ sum += contrib; norm += a_i; }           // BLEND
    else if (u_combine == 2){                                      // VAR_ROUGH
      float w = i == 0 ? 1.0 : clamp(0.5 - first * 0.5, 0.0, 1.0);
      sum += contrib * w; norm += a_i * w;
    } else if (u_combine == 3){                                    // VAR_ROUGH_ABS
      float w = i == 0 ? 1.0 : clamp(1.0 - abs(first), 0.0, 1.0);
      sum += contrib * w; norm += a_i * w;
    }
    else if (u_combine == 4) acc_max = max(acc_max, contrib);      // MAX
    else if (u_combine == 5){                                      // MAX_ABS
      if (abs(contrib) > abs(acc_max) || i == 0) acc_max = contrib;
    }
    else if (u_combine == 6) acc_min = min(acc_min, contrib);      // MIN
    else if (u_combine == 7){                                      // MIN_ABS
      if (abs(contrib) < abs(acc_min) || i == 0) acc_min = contrib;
    }
    else if (u_combine == 8) acc_mul *= 0.5 + 0.5 * n;             // MULTIPLY
    else { sum += contrib; norm += a_i; }                          // ADD
    if (u_rough_ref <= 0.0 || 1.0 / freq < u_rough_ref){
      rough += abs(contrib);
      rough_norm += a_i;
    }
    if (i == 0) first = n;
    prev = n;
    amp *= persistence;
    freq *= lacunarity;
  }
  float v;
  if (u_combine == 1 || u_combine == 2 || u_combine == 3)
    v = norm > 0.0 ? sum / norm : 0.0;
  else if (u_combine == 4 || u_combine == 5) v = acc_max;
  else if (u_combine == 6 || u_combine == 7) v = acc_min;
  else if (u_combine == 8) v = acc_mul * 2.0 - 1.0;
  else v = norm > 0.0 ? sum / norm : 0.0;

  if (u_bump_surge != 0.0) v += u_bump_surge * v * abs(v);
  if (u_gain != 1.0)
    v = copysign_pow(clamp(v, -1.0, 1.0), 1.0 / max(u_gain, 0.05));
  if (u_profile != 0){
    float uu = clamp(v * 0.5 + 0.5, 0.0, 1.0);
    float lo = min(u_filter_min, u_filter_max);
    float hi = max(u_filter_max, lo + 1e-4);
    float t = clamp((uu - lo) / (hi - lo), 0.0, 1.0);
    float f = lo + profile_of(t) * (hi - lo);
    if (uu < lo || uu > hi) f = uu;
    uu = f * (1.0 - u_creep_in) + uu * u_creep_in;
    v = uu * 2.0 - 1.0;
  }
  uint idx = uint(px.y) * uint(u_size.x) + uint(px.x);
  out_h[idx] = v * u_amplitude + u_offset;
  out_r[idx] = rough_norm > 0.0 ? clamp(rough / rough_norm, 0.0, 1.0) : 0.0;
}
)GLSL";
  return src;
}

} // namespace studio
