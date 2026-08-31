// Geekatplay TerraForge — the field domain.
//
// Our original graph is a *raster* graph: nodes pass Heightmap/TextureRGBA
// buffers at one fixed resolution. Vue and Terragen are both built on a *field*
// graph instead — a function evaluated per point, with no resolution anywhere
// in it:
//
//     f(position, normal, altitude, slope, orientation, time) -> value
//
// That single difference is why their displacement is stronger, their terrain
// is infinite, and their nodes reach into lighting and clouds. This header adds
// that second domain beside the raster one; neither replaces the other.
//
//   field  — resolution-independent, 3D, compiles to GLSL for the GPU.
//            noise, fractals, math, colour, gradients, warps, displacement.
//   raster — neighbour-aware and iterative, stays on the CPU.
//            erosion, blur, flow accumulation, sculpt layers, image IO.
//
// The two meet at Rasterize (field -> buffer) and Sample (buffer -> field), so
// an authored field can be eroded and an eroded heightfield can drive a
// displacement shader.
//
// Terragen p4-5 makes the same argument for keeping both: heightfields are
// where erosion is tractable, procedurals are where infinite detail lives, and
// the advice is to mix them in one scene.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace gpx {

// The four data types a field graph moves around. Deliberately the same set
// Vue settled on (manual p772), because they are what surface shading needs.
enum class FieldType : uint8_t {
  Number,   // scalar; the usual output of a terrain or mask function
  Color,    // rgba
  Vector,   // a position or direction in space
  TexCoord, // 2D texture coordinates
};

inline const char *field_type_name(FieldType t) {
  switch (t) {
    case FieldType::Number: return "number";
    case FieldType::Color: return "color";
    case FieldType::Vector: return "vector";
    case FieldType::TexCoord: return "texcoord";
  }
  return "?";
}

// Link colours follow Vue's convention (p772-773) so the graph reads the same
// way to anyone who has used one of these tools: blue number, green colour,
// red vector, purple texture coordinates.
inline uint32_t field_type_color(FieldType t) {
  switch (t) {
    case FieldType::Number: return 0xFF8CA8C8u;   // blue
    case FieldType::Color: return 0xFF7FB07Fu;    // green
    case FieldType::Vector: return 0xFF8C6DDCu;   // red/violet
    case FieldType::TexCoord: return 0xFFB07FC8u; // purple
  }
  return 0xFF999999u;
}

// One value flowing along a link. A tagged 4-float bundle rather than a variant:
// every type fits in it, conversion is explicit and cheap, and the whole struct
// maps one-to-one onto a GLSL vec4 when the graph is transpiled.
struct FieldValue {
  FieldType type = FieldType::Number;
  float v[4] = {0, 0, 0, 1};

  FieldValue() = default;
  explicit FieldValue(float n) : type(FieldType::Number) { v[0] = n; }
  FieldValue(FieldType t, float a, float b = 0, float c = 0, float d = 1)
      : type(t) {
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
  }
  static FieldValue number(float n) { return FieldValue(n); }
  static FieldValue color(float r, float g, float b, float a = 1.f) {
    return FieldValue(FieldType::Color, r, g, b, a);
  }
  static FieldValue vector(float x, float y, float z) {
    return FieldValue(FieldType::Vector, x, y, z, 0.f);
  }
  static FieldValue texcoord(float u, float t) {
    return FieldValue(FieldType::TexCoord, u, t, 0.f, 0.f);
  }

  float number() const {
    // A colour used as a number reads as luminance, a vector as its length.
    // Being permissive here is what lets a user wire a noise into a colour slot
    // (or the reverse) and get something sensible rather than an error.
    switch (type) {
      case FieldType::Number: return v[0];
      case FieldType::Color: return 0.299f * v[0] + 0.587f * v[1] + 0.114f * v[2];
      case FieldType::Vector: return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
      case FieldType::TexCoord: return v[0];
    }
    return v[0];
  }
  void as_color(float *out) const {
    if (type == FieldType::Color) {
      for (int i = 0; i < 4; ++i) out[i] = v[i];
    } else {
      float n = number();
      out[0] = out[1] = out[2] = n;
      out[3] = 1.f;
    }
  }
  void as_vector(float *out) const {
    if (type == FieldType::Vector || type == FieldType::Color) {
      for (int i = 0; i < 3; ++i) out[i] = v[i];
    } else {
      out[0] = out[1] = out[2] = number();
    }
  }
  bool finite() const {
    for (float f : v)
      if (!std::isfinite(f)) return false;
    return true;
  }
  bool operator==(const FieldValue &o) const {
    if (type != o.type) return false;
    for (int i = 0; i < 4; ++i)
      if (v[i] != o.v[i]) return false;
    return true;
  }
};

// Everything a field node may know about the point it is being asked about.
// This is the full input surface of the domain: Vue's standard inputs
// (Position, Normal, Altitude, Slope, Orientation — p769-770) plus time for
// animation and an LOD hint so a node can cheapen itself when the caller is far
// away. Nothing here is resolution-dependent, by design.
struct FieldContext {
  float pos[3] = {0, 0, 0};    // evaluation position, in the graph's space
  float normal[3] = {0, 1, 0}; // surface direction at that point
  float altitude = 0.f;        // height above the reference plane
  float slope = 1.f;           // 1 flat, 0 vertical, -1 flat facing down
  float orientation = 0.f;     // -1..1 by azimuth of the normal
  float time = 0.f;            // seconds, for animated graphs
  float lod = 12.f;            // octave budget; lower when far from the camera

  // Derive normal-dependent inputs so callers only have to supply a normal.
  void derive_from_normal() {
    slope = normal[1];
    orientation = std::atan2(normal[0], normal[2]) * 0.31830989f; // /pi -> -1..1
  }
  // Convenience for the common case of sampling a direction on a unit sphere.
  static FieldContext at(float x, float y, float z) {
    FieldContext c;
    c.pos[0] = x; c.pos[1] = y; c.pos[2] = z;
    c.altitude = y;
    return c;
  }
};

} // namespace gpx
