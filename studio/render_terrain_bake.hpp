#pragma once
// Geekatplay TerraForge - the world the viewport draws, baked for the
// offline renderers.
//
// A path-traced frame has no shaders of ours to run: it gets meshes and
// textures. What it used to get was the graph's raw heightmap over a single
// square tile - not placed on the planet, not surrounded by anything, not
// bent by the world's curvature and with no colour at all. So the render
// through a camera and the viewport through the same camera were two
// different pictures: a grey slab in a void against a landscape running to
// the horizon.
//
// This builds what the viewport builds, from the same CPU functions the
// placement already uses (planet_place.hpp, gpx/planet_math.hpp) and the
// palette's CPU twin (gpx/planet_palette.hpp, checked against the shader by
// planet_gpu_check.cpp): the placed tile, the surround out to the horizon,
// both on the world's shape, each with an albedo map painted by the same
// palette the viewport paints with.
#include <string>

namespace studio {

struct App;

struct BakedTerrain {
  bool ok = false;
  std::string err;
  std::string tile_obj, tile_albedo;         // the terrain tile, placed
  std::string surround_obj, surround_albedo; // the ground beyond it
  std::string water_obj;                     // the sea, on the same curve
  float extent = 0.f;  // how far the surround reaches, tile units
};

// `dir` is the render working directory; `tile_side` and `surround_side` are
// grid resolutions (vertices per edge). Pure apart from reading the scene and
// writing the four files.
BakedTerrain render_bake_terrain(App &a, const std::string &dir,
                                 int tile_side, int surround_side,
                                 int albedo_side);

} // namespace studio
