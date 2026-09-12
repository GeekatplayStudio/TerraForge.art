// Geekatplay TerraForge — the sea over the world, without GL.
//
// The viewport (renderer_water.cpp) and the offline render's mesh
// (render_water_bake.cpp) both lay the one water surface out from here:
// the waves the settings make, where the eye stands over the flat world the
// placement unrolls onto the planet, and how far that flat world reaches on
// a small globe. Kept free of GL so the tests link it.
#pragma once
#include "gpx/planet_math.hpp"
#include "gpx/water_waves.hpp"

namespace studio {

struct RenderSettings;

// The settings as the wave builder reads them (render_settings.hpp water_*).
gpx::water::Params water_params(const RenderSettings &rs);

struct WaterWaves {
  gpx::water::Wave w[gpx::water::MAX_WAVES];
  int n = 0;
  float slope_var = 0.f; // every wave's mean-square slope: the far sea's roughness
};
// The waves the settings make, rebuilt only when a setting that shapes them
// changes. Main thread.
const WaterWaves &water_waves(const RenderSettings &rs);

// The point of the flat world (tile units) under an eye, through the
// inverse of gpx::planet::sphere_place: the angles of the eye about the
// world's centre or axis. A flat world, or no curvature, is the eye's x, z.
void water_eye_param(const float eye[3], float R, const gpx::planet::Shape &S,
                     double &u, double &v);

// How far the flat world's angles reach from the tile centre before they
// wrap a small world (tile units); a huge number along a flat axis.
void water_param_limits(float R, const gpx::planet::Shape &S, float &lim_x, float &lim_z);

// ---- the clipmap (renderer_water.cpp, VS_WATER / FS_WATER) ----
// A level is a grid WATER_GRID cells a side about a centre snapped to twice
// its cell. It owns the cells within WATER_OWN of its centre that no finer
// level owns; a ring (every level but the first) leaves WATER_HOLE cells
// about its centre open, which the finer level always covers.
inline constexpr int WATER_GRID = 64;
inline constexpr int WATER_OWN = WATER_GRID / 2 - 2;
inline constexpr int WATER_HOLE = WATER_GRID / 4 - 3;
// The morph runs between the finer level's rim - at most OWN/2 + 1 of this
// level's cells from the eye - and this level's own - at least OWN - 1 -
// with two cells to spare at each end, so every triangle that straddles
// either seam is wholly unmorphed or wholly morphed.
inline constexpr float WATER_MORPH_LO = float(WATER_OWN / 2 + 3);
inline constexpr float WATER_MORPH_HI = float(WATER_OWN - 3);

struct WaterLevel {
  double ox = 0.0, oz = 0.0; // centre, tile units
  double cell = 1.0;         // tile units
};
// The levels for an eye over the flat world at (eu, ev), finest cell c0,
// covering `need` tile units from the eye; returns how many (<= max).
int water_levels(double eu, double ev, double c0, double need, WaterLevel out[], int max);
// The level whose fragments draw the point (x, z) - FS_WATER's discards -
// or -1 when none does.
int water_level_owner(const WaterLevel *levels, int n, double x, double z);
// How far a vertex at (x, z) of a level has morphed onto the next level's
// lattice (VS_WATER): 0 near the eye, 1 at the level's rim.
inline double water_morph(double eu, double ev, const WaterLevel &L, double x, double z) {
  const double d = std::max(std::fabs(x - eu), std::fabs(z - ev)) / L.cell;
  return std::clamp((d - WATER_MORPH_LO) / (WATER_MORPH_HI - WATER_MORPH_LO), 0.0, 1.0);
}

// The wave origin for an eye: its point on a lattice a quarter tile apart,
// so the small numbers the shader evaluates at stay small and change only
// when the camera travels.
inline double water_origin_snap(double p) { return std::floor(p / 0.25 + 0.5) * 0.25; }

// The bubble cell of the foam pattern, metres (WATER_FN_GLSL foam_pattern).
inline double water_foam_cell_m(float foam_scale) {
  return std::max(double(foam_scale) * 0.4, 0.05);
}

} // namespace studio
