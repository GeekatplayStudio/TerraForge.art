// Geekatplay TerraForge — planets and infinite terrains (GPU-procedural).
//
// Every planet and every infinite terrain layer is generated on the GPU from
// its parameter block each frame: no textures, no per-object meshes, no
// caches. Three shared sphere meshes (LOD) plus one shared surround grid are
// the entire memory footprint, whatever the number of planets — this is the
// "ultra smart memory" design: distant worlds cost arithmetic, not RAM.
#pragma once
#include <string>
#include <vector>

namespace studio {

// lighting/grading context shared with the main renderer for one frame
struct PlanetFrame {
  const float *mvp;       // 16
  const float *eye;       // 3
  const float *sun;       // 3
  float sun_intensity;
  float exposure;         // includes camera exposure multiplier
  const float *grade;     // 3
  float saturation;
  int view_h;             // viewport height in pixels, for LOD
  float fovy_rad;
};

bool planet_renderer_init();

// A field graph, transpiled to GLSL, that shapes every procedural surface -
// planets and the infinite ground plane alike. `glsl` is the whole generated
// program (prelude included); empty means "back to the built-in layers only".
// `strength` weights it against those layers. Both shaders are relinked on the
// next frame that draws, and a program that fails to compile falls back to the
// stub rather than leaving the sky empty.
// One program pair per SurfaceDisplacement node, keyed by its id: a planet
// (PlanetData::surface_node) or the home surround names the graph that
// shapes it. Call for every such node each time the graph changes, then
// planet_field_programs_keep with the ids still alive.
void planet_set_field_program(unsigned long long node, const std::string &glsl,
                              float strength);
void planet_field_programs_keep(const std::vector<unsigned long long> &live);
// why the last relink failed, empty if it did not
const std::string &planet_field_error();

// all visible planets, painter-sorted, as a sky layer (depth write off)
void planet_draw_all(const PlanetFrame &f);

// the home ground plane extended to the horizon by the root-level
// InfiniteSurface layers; blends seamlessly into the terrain tile's edges
struct InfiniteFrame {
  const float *mvp;
  const float *eye;
  const float *sun;
  const float *sun_color;
  float sun_intensity;
  float exposure;
  const float *grade;
  float saturation;
  float ambient;
  const float *sky_zenith;
  const float *sky_horizon;
  unsigned tex_height;    // the tile heightmap, for edge blending
  unsigned tex_albedo;    // 0 if none
  float height_scale;
  // The tile's mean height, in world units. The surround settles to this away
  // from the tile so the two meet without a step, whatever the terrain's
  // absolute altitude happens to be.
  float base_height;
  float planet_radius;    // curvature (0 = flat)
  int fog_type;
  float fog_density;
  const float *fog_color;
  bool textured = true; // the view's shading mode, as for the terrain tile
};
void infinite_draw(const InfiniteFrame &f);
bool infinite_layers_present(); // any visible root-level InfiniteSurface?

// ray test against the planets; returns scene object index or -1
int planet_pick(const float ro[3], const float rd[3], float &t_out);

} // namespace studio
