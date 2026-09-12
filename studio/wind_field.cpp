// Geekatplay TerraForge - the wind field as GLSL, twin of wind_field.hpp.
//
// Spliced into the mesh, billboard and shadow vertex shaders, so a scattered
// wood is read per copy: the gust a tree is standing in, not a random number
// that happens to differ from its neighbour's. See wind_field.hpp for what
// the three bands are and why the pattern is carried downwind.
//
// The arithmetic is the header's, operation for operation - including the
// hash, which is integer here as it is there, so a plant placed on the CPU
// and drawn on the GPU is in the same gust rather than nearly the same one.
#include "wind_field.hpp"
#include "render_settings.hpp"
#include "uniform_cache.hpp"
#include <glad/gl.h>
#include <algorithm>

namespace studio {

extern const char *const WIND_GUST_GLSL;
const char *const WIND_GUST_GLSL = R"GLSL(
uniform vec4 u_wind_a;   // drift.x, drift.z, 1/cell, time * gust frequency
uniform vec4 u_wind_b;   // gust strength, direction wander (rad), mean direction x/z
uint wf_mix(uint x){
  x ^= x >> 16; x *= 0x7feb352du;
  x ^= x >> 15; x *= 0x846ca68bu;
  x ^= x >> 16; return x;
}
float wf_hash(vec2 p){
  ivec2 i = ivec2(floor(p));
  uint h = wf_mix(uint(i.x) * 0x9E3779B9u ^ wf_mix(uint(i.y) * 0x85EBCA77u));
  return float(h) * (1.0 / 4294967296.0);
}
float wf_vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(wf_hash(i), wf_hash(i + vec2(1,0)), f.x),
             mix(wf_hash(i + vec2(0,1)), wf_hash(i + vec2(1,1)), f.x), f.y);
}
vec2 wf_turn(vec2 p, vec2 cs){ return vec2(p.x * cs.x - p.y * cs.y, p.x * cs.y + p.y * cs.x); }
// The gust where this stands, 0..1 about a mean of 0.5. `xz` is the place in
// tile units.
float wind_gust_field(vec2 xz){
  vec2 b = (xz - u_wind_a.xy) * u_wind_a.z;
  float t = u_wind_a.w;
  float s = 0.0;
  s += wf_vnoise(wf_turn(vec2(b.x * 0.25 + t * 0.11, b.y * 0.25 - t * 0.066),
                         vec2( 0.62160997,  0.78332691))) * 0.5;
  s += wf_vnoise(wf_turn(vec2(b.x * 1.0  + t * 0.29, b.y * 1.0  - t * 0.174),
                         vec2(-0.44323442,  0.89640574)) + vec2(19.7, 7.3)) * 0.35;
  s += wf_vnoise(wf_turn(vec2(b.x * 2.7  + t * 0.73, b.y * 2.7  - t * 0.438),
                         vec2(-0.84275935, -0.53829051)) + vec2(39.4, 14.6)) * 0.15;
  return s;
}
// What a thing standing at `xz` is blown by: how hard, and how far the
// direction has backed or veered there. A gust that only strengthened along a
// fixed line reads as a pulse; real air turns as it arrives.
vec2 wind_gust_lean(vec2 xz){
  float g = wind_gust_field(xz) * 2.0 - 1.0;
  float turn = (wind_gust_field(xz + vec2(11.3, 4.7)) - 0.5) * u_wind_b.y;
  float amt = max(1.0 + u_wind_b.x * g, 0.0);
  vec2 d = u_wind_b.zw;
  float c = cos(turn), sn = sin(turn);
  return vec2(d.x * c - d.y * sn, d.x * sn + d.y * c) * amt;
}
)GLSL";

// What the three shaders are told about the weather. One place, so a tree,
// its card at distance and its shadow are all in the same gust - they are
// drawn by different programs and nothing else would keep them together.
void wind_field_upload(GLuint prog, const RenderSettings &RS, float t) {
  const float cell = std::max(RS.wind.gust_size_m, 1.f) /
                     std::max(RS.terrain_size_m, 1.f); // metres into tiles
  const float a[4] = {RS.wind_drift[0], RS.wind_drift[1], 1.f / std::max(cell, 1e-6f),
                      t * std::max(RS.wind.gust_frequency, 0.f)};
  const float r = RS.wind.direction_deg * 0.01745329251994f;
  const float b[4] = {std::max(RS.wind.gust_strength, 0.f),
                      RS.wind.turbulence_deg * 0.01745329251994f,
                      std::cos(r), std::sin(r)};
  glUniform4fv(uniform_location(prog, "u_wind_a"), 1, a);
  glUniform4fv(uniform_location(prog, "u_wind_b"), 1, b);
}

} // namespace studio
