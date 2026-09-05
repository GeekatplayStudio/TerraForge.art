// Geekatplay TerraForge - object deformers: twist, bend, skew (shear) and
// taper, applied to a point in the object's own space before its transform.
//
// Vue twists objects (manual p280, Numerics tab); Cinema 4D and Maya have
// bend, shear and taper deformers as gadgets. These are the same idea as
// one small struct: every deformer is a function of where the point sits
// along an axis of the object's bounding box (t in 0..1), so a twist of 90
// degrees turns the top a quarter turn and the base not at all, a bend
// curves the object progressively, a skew slides the top sideways, a taper
// narrows it. The mesh vertex shader (studio/shaders_scene.cpp) applies
// exactly this in GLSL; this header is the CPU twin the exports, the picking
// and the tests use, so the two cannot disagree.
#pragma once
#include <cmath>

namespace gpx {

struct Deform {
  float twist[3] = {0.f, 0.f, 0.f}; // degrees about each local axis, at the far end
  float bend = 0.f;                 // degrees, progressive rotation about bend_axis
  int bend_axis = 0;                // 0 X, 1 Y, 2 Z: the axis the object curls around
  float shear[3] = {0.f, 0.f, 0.f}; // skew: X and Z slide with height, Y with X
  float taper = 0.f;                // -1 (point) .. 3: the top's width against the base
  bool identity() const {
    return twist[0] == 0.f && twist[1] == 0.f && twist[2] == 0.f && bend == 0.f &&
           shear[0] == 0.f && shear[1] == 0.f && shear[2] == 0.f && taper == 0.f;
  }
};

namespace deform_detail {
inline void rotate_about(float *p, int axis, float radians, const float *pivot) {
  int u = (axis + 1) % 3, v = (axis + 2) % 3;
  float c = std::cos(radians), s = std::sin(radians);
  float a = p[u] - pivot[u], b = p[v] - pivot[v];
  p[u] = pivot[u] + a * c - b * s;
  p[v] = pivot[v] + a * s + b * c;
}
inline float frac(float x, float lo, float hi) {
  float d = hi - lo;
  return d > 1e-9f ? (x - lo) / d : 0.f;
}
} // namespace deform_detail

// Deform one point in place. `bmin`/`bmax` are the object's local bounds.
inline void deform_point(const Deform &d, const float *bmin, const float *bmax, float *p) {
  using namespace deform_detail;
  const float D2R = 0.017453292519943295f;
  float centre[3] = {(bmin[0] + bmax[0]) * 0.5f, (bmin[1] + bmax[1]) * 0.5f,
                     (bmin[2] + bmax[2]) * 0.5f};
  float ty = frac(p[1], bmin[1], bmax[1]);
  float tx = frac(p[0], bmin[0], bmax[0]);
  // taper: the cross-section shrinks or grows with height, about the centre
  if (d.taper != 0.f) {
    float k = 1.f + d.taper * ty;
    if (k < 0.f) k = 0.f;
    p[0] = centre[0] + (p[0] - centre[0]) * k;
    p[2] = centre[2] + (p[2] - centre[2]) * k;
  }
  // skew: slide with height (X, Z) and with X (Y), in object units of the
  // object's own extent, so 1 means "the top moved by one width"
  if (d.shear[0] != 0.f) p[0] += d.shear[0] * ty * (bmax[0] - bmin[0]);
  if (d.shear[2] != 0.f) p[2] += d.shear[2] * ty * (bmax[2] - bmin[2]);
  if (d.shear[1] != 0.f) p[1] += d.shear[1] * tx * (bmax[1] - bmin[1]);
  // twist: rotation about each axis growing along that axis
  for (int a = 0; a < 3; ++a)
    if (d.twist[a] != 0.f) {
      float t = frac(p[a], bmin[a], bmax[a]);
      rotate_about(p, a, d.twist[a] * D2R * t, centre);
    }
  // bend: a progressive rotation about the bend axis, pivoting at the base,
  // so the object curls like a finger
  if (d.bend != 0.f) {
    int up = d.bend_axis == 1 ? 0 : 1; // height is measured along an axis other than the bend axis
    float t = frac(p[up], bmin[up], bmax[up]);
    float pivot[3] = {centre[0], centre[1], centre[2]};
    pivot[up] = bmin[up];
    rotate_about(p, d.bend_axis, d.bend * D2R * t, pivot);
  }
}

// A deformed normal: the deformation's local rotation applied to the
// direction, read from two nearby points. Good enough for shading.
inline void deform_normal(const Deform &d, const float *bmin, const float *bmax,
                          const float *p, float *n) {
  if (d.identity()) return;
  float ext = 0.f;
  for (int i = 0; i < 3; ++i) ext = std::fmax(ext, bmax[i] - bmin[i]);
  const float eps = ext * 1e-3f + 1e-6f;
  // two tangents, deformed, give the deformed normal
  float t1[3], t2[3];
  // pick tangents perpendicular to n
  float ax[3] = {std::fabs(n[0]) < 0.9f ? 1.f : 0.f, std::fabs(n[0]) < 0.9f ? 0.f : 1.f, 0.f};
  t1[0] = ax[1] * n[2] - ax[2] * n[1];
  t1[1] = ax[2] * n[0] - ax[0] * n[2];
  t1[2] = ax[0] * n[1] - ax[1] * n[0];
  t2[0] = n[1] * t1[2] - n[2] * t1[1];
  t2[1] = n[2] * t1[0] - n[0] * t1[2];
  t2[2] = n[0] * t1[1] - n[1] * t1[0];
  float p0[3] = {p[0], p[1], p[2]}, p1[3], p2[3];
  for (int i = 0; i < 3; ++i) {
    p1[i] = p[i] + t1[i] * eps;
    p2[i] = p[i] + t2[i] * eps;
  }
  deform_point(d, bmin, bmax, p0);
  deform_point(d, bmin, bmax, p1);
  deform_point(d, bmin, bmax, p2);
  float a[3], b[3];
  for (int i = 0; i < 3; ++i) {
    a[i] = p1[i] - p0[i];
    b[i] = p2[i] - p0[i];
  }
  float c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  float l = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
  if (l > 1e-12f)
    for (int i = 0; i < 3; ++i) n[i] = c[i] / l;
}

} // namespace gpx
