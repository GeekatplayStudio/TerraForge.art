// Geekatplay TerraForge - the wind as a field, not a number.
//
// The wind was a function of time alone: one speed and one direction for the
// whole world at each instant. Every tree in a wood therefore leaned at the
// same moment by the same amount, and the scatter hid that only by giving
// each copy a RANDOM phase - which trades one wrong answer for another, since
// a tree then has nothing to do with the tree beside it either. Real wind has
// neither property. It arrives in cells: a patch of ground is under a gust
// while the patch behind it is not, and the boundary travels.
//
// So the gust is a field over the ground, read where a thing stands:
//
//   large  - the weather-scale pattern, four gust-cells across, which is what
//            makes one side of a valley stream while the other is calm
//   medium - the gust proper, `gust_size_m` across
//   small  - the eddies inside it, the flutter in the lull
//
// The field is carried downwind rather than regenerated, so a squall crosses
// the ground at wind speed and a wood ripples away from the viewer instead of
// twitching. That is Taylor's frozen-turbulence hypothesis, which is how
// anemometry converts a time series at one mast into a picture of the air:
// over the seconds a gust takes to pass, the pattern moves much faster than
// it changes. Each band still creeps at its own slow rate on top of that, so
// the pattern evolves as well as travels.
//
// This is the CPU side. wind_field.cpp carries the GLSL twin, spliced into
// the mesh, billboard and shadow vertex shaders so a scattered wood ripples
// per copy; the arithmetic is the same in both, so a plant placed on the CPU
// and drawn on the GPU is in the same gust.
#pragma once
#include "terrain_relief.hpp"
#include <cmath>

namespace studio {

// The three bands, as (frequency, turn) pairs. The turns are written out for
// the same reason the relief's are: a cos() at runtime need not give the
// shader the same bits. See FRACTAL_FN in shaders_terrain.cpp for why a band
// that is only scaled and never turned leaves a visible grid.
inline constexpr float WIND_BAND_F[3] = {0.25f, 1.f, 2.7f};
inline constexpr float WIND_BAND_W[3] = {0.5f, 0.35f, 0.15f};
inline constexpr ReliefTurn WIND_BAND_T[3] = {
    {0.62160997f, 0.78332691f},  // 0.90 rad
    {-0.44323442f, 0.89640574f}, // 2.03 rad
    {-0.84275935f, -0.53829051f} // 3.71 rad
};
// How fast each band creeps through itself, cells per second at gust
// frequency 1. The gust is mostly carried past; this is the part that is
// genuinely new air rather than the same air arriving.
inline constexpr float WIND_BAND_CREEP[3] = {0.11f, 0.29f, 0.73f};

// The gust at a place, 0..1 about a mean of 0.5. `px`/`pz` are the place in
// tile units, `drift` how far the air has already blown (RenderSettings::
// wind_drift, tile units), `cell` the gust size in tile units, and `t` the
// clock times the gust frequency.
inline float wind_gust_field(float px, float pz, const float drift[2], float cell, float t) {
  const float inv = 1.f / (cell > 1e-6f ? cell : 1e-6f);
  const float bx = (px - drift[0]) * inv, bz = (pz - drift[1]) * inv;
  float sum = 0.f;
  for (int i = 0; i < 3; ++i) {
    float qx, qz;
    relief_turn(bx * WIND_BAND_F[i] + t * WIND_BAND_CREEP[i],
                bz * WIND_BAND_F[i] - t * WIND_BAND_CREEP[i] * 0.6f,
                WIND_BAND_T[i], qx, qz);
    sum += relief_vnoise(qx + float(i) * 19.7f, qz + float(i) * 7.3f) * WIND_BAND_W[i];
  }
  return sum; // the weights sum to 1, so this is already 0..1
}

} // namespace studio
