// Geekatplay TerraForge — per-patch visibility for the terrain surface.
//
// The terrain is drawn as one glDrawElements over a 64x64 grid of patches, and
// every one of those 4096 patches reaches the tessellation control shader every
// frame whether or not any part of it is on screen. Off-screen patches still
// pay for the control shader, still generate primitives, and are only thrown
// away after clipping — the most expensive place to discard anything.
//
// The research note's phrasing for a cluster pipeline applies exactly as well
// one level up: what the GPU never sees costs nothing. A patch that fails the
// frustum test has its outer tessellation levels set to zero, which the spec
// says discards the patch before a single primitive is generated.
//
// Everything here is deliberately free of GL so the maths can be tested without
// a context. The renderer owns the texture; this module owns the numbers in it.
#pragma once
#include <cstddef>
#include <vector>

namespace gpx {
struct Heightmap;
}

namespace studio {

// Six frustum planes as (a,b,c,d), inward-facing: a point is inside when
// a*x + b*y + c*z + d >= 0 for all six. Order: left, right, bottom, top,
// near, far.
struct Frustum {
  float p[6][4] = {};
};

// Gribb-Hartmann extraction from a column-major GL projection*view matrix
// (the same one handed to glUniformMatrix4fv with transpose = GL_FALSE).
// Planes are normalised, so the dot product is a signed distance in world
// units and a caller can pad a bound by a real distance.
Frustum frustum_from_mvp(const float mvp[16]);

// True when the axis-aligned box is at least partly inside. Conservative: it
// may answer true for a box that is in fact outside (the standard
// positive-vertex test does not catch every diagonal case), and must never
// answer false for one that is inside — a wrongly culled patch is a hole in
// the terrain.
bool aabb_visible(const Frustum &f, const float lo[3], const float hi[3]);

// Per-patch height bounds over `patches` x `patches` cells covering the whole
// heightmap, as interleaved (min, max) pairs in the heightmap's own units.
// Row-major, y outer, matching a texture uploaded with the same layout.
//
// The bounds cover one texel beyond each patch edge, because a vertex placed
// anywhere inside the patch samples the height texture with bilinear filtering
// and so can read the neighbouring texel.
std::vector<float> patch_height_bounds(const gpx::Heightmap &h, int patches);

// How much taller or shorter than its heightmap bound a patch can become, in
// world units, once everything else that moves a vertex has had its say. This
// is the one place that policy lives; the shader is handed the number.
//
// `field` is the graph-authored displacement strength, whose output range is
// not knowable from here — a field graph can return anything. It is padded by
// a generous factor rather than guessed at, because under-padding shows up as
// terrain vanishing at the edge of the view and over-padding only costs a few
// patches that would have been culled.
float cull_pad(float disp_strength, bool has_disp, float fractal_amount,
               float field_strength);

// Counts how many of the patches survive the same test the shader applies.
// Used for the status readout and by the tests: this is the reference
// implementation, and the GLSL is its mirror.
int patches_visible(const Frustum &f, const std::vector<float> &bounds,
                    int patches, float hscale, float pad,
                    const float cam[3], float planet_radius);

} // namespace studio
