// Geekatplay TerraForge — more than one terrain tile.
//
// The renderer grew up with one tile: its heightmap, albedo, placement
// weight, patch bounds and picking copy are globals (renderer_internal.hpp),
// and every pass draws "the terrain". A scene can now hold any number of
// Terrain objects, each driven by its own TerrainOutput chain and standing
// where its transform puts it. Rather than thread a tile index through
// every pass, the first Terrain object keeps the globals and every further
// tile keeps the same set here; a pass draws a tile by swapping that set
// into the globals for the duration of the draw (TileSwap) and calling the
// code it always called. The swap is a handful of handles and two vector
// swaps - nothing is copied.
//
// Tile 0 is the first Terrain object in the scene, whatever its name; its
// chain is the TerrainOutput the view follows. Every further Terrain
// object names its own TerrainOutput in driver_node (component_add.cpp).
// Grounding, imprint and height probes still read tile 0 only.
#pragma once
#include "planet_place.hpp"
#include "gpx/heightmap.hpp"
#include "gpx/material_params.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace studio {
struct App;
struct TerrainUpload;

struct TerrainTileGpu {
  int object = -1;           // scene index of the Terrain object
  uint64_t output_node = 0;  // its TerrainOutput
  unsigned tex_height = 0, tex_albedo = 0, tex_place_w = 0, tex_patch_bounds = 0;
  int hm_w = 0;
  bool has_albedo = false, has_place_w = false;
  std::vector<float> cpu_patch_bounds;
  gpx::Heightmap cpu_height;
  float mean = 0.f;
  PlaceResult placement;
  gpx::MaterialParams matp;
  // the eight material scalars the pass uploads beside matp
  float mat[8] = {0.6f, 0.f, 0.5f, 0.f, 0.f, 0.f, 1.f, 0.f};
  bool ready = false;        // something has been uploaded
};

// Tiles 1..N (tile 0 is the globals). Sized by terrain_tiles_bind.
std::vector<TerrainTileGpu> &terrain_tiles_extra();
int terrain_tile_count(); // 1 + extra

// The scene's Terrain objects in order, with the TerrainOutput each one
// draws: tile 0 takes its driver or else the first TerrainOutput; every
// later tile must name its own. Refreshes the extra tiles' object/output
// fields (creating or dropping entries) and returns the count. Caller
// holds the graph lock.
int terrain_tiles_bind(App &a);
// The tile index for a scene object, or -1.
int terrain_tile_for_object(int object);
// The scene object of a tile (0 = the first Terrain object), or -1.
int terrain_tile_object(int tile);

// Upload a prepared terrain into an extra tile's textures (main thread).
void terrain_tile_set_prepared(int tile, TerrainUpload &up);

// Which tile's transform terrain_xform_current() reports: set by TileSwap
// while a tile is being drawn, -1 = tile 0.
int terrain_tile_current();

// Swap an extra tile's set into the renderer's globals for a draw.
struct TileSwap {
  explicit TileSwap(int tile);
  ~TileSwap();
  TileSwap(const TileSwap &) = delete;
  TileSwap &operator=(const TileSwap &) = delete;
private:
  int tile_ = 0;
  int prev_current_ = -1;
  bool swapped_ = false;
};

// Whether a tile's object is visible in the scene.
bool terrain_tile_visible(int tile);

} // namespace studio
