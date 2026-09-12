// Geekatplay TerraForge — GLSL sources for the viewport renderer.

//

// Kept out of renderer.cpp so neither file is unreadable: the shaders are

// long, they change for different reasons than the draw code, and a module

// nobody can scroll through is a module nobody checks.

#pragma once



namespace studio {



extern const char *const TERRAIN_VERT_COMMON;

extern const char *const VS_TERRAIN_TAIL;

extern const char *const TCS_TERRAIN;

extern const char *const TES_TERRAIN_TAIL;

extern const char *const VS_TERRAIN_PASS;

extern const char *const FRACTAL_FN;
// the relief as a smooth B-spline surface at a level of detail, shared by the
// terrain's vertices and its shadow (shaders_terrain.cpp)
extern const char *const HEIGHT_SMOOTH_FN;

extern const char *const SKY_FN;
// deep space behind the air (shaders_space.cpp): stars, the galaxy, the nebulas
// deep space, spliced in this order through SPACE_FN_PLACEHOLDER: what
// they share, the stars, the milky band and the discs, the nebulas, and
// last the entry the sky calls (shaders_space.cpp)
extern const char *const SPACE_COMMON_FN;
extern const char *const SPACE_STARS_FN;
extern const char *const SPACE_GAL_FN;
extern const char *const SPACE_NEB_FN;
extern const char *const SPACE_ENTRY_FN;
// the sea (shaders_water.cpp): the waves' GLSL twin, the shading every
// program that draws water shares, and the one water surface
extern const char *const WATER_WAVES_GLSL;
extern const char *const WATER_FN_GLSL;
extern const char *const FOG_FN;
// the cloud layers (shaders_clouds.cpp): the world the air lies on, the
// layer's shape at a point (shared with the shadows it casts) and the march;
// the pass that draws them over the ground keeps its shader beside it
// (renderer_clouds.cpp)
extern const char *const SKY_WORLD_GLSL;
extern const char *const CLOUD_SHAPE_GLSL;
extern const char *const CLOUD_FN_GLSL;
// the sky pass's panorama as everything that reflects the sky reads it
// (renderer_clouds.cpp), spliced through SKY_ENV_PLACEHOLDER
extern const char *const SKY_ENV_GLSL;

extern const char *const FS_TERRAIN_SRC;

extern const char *const VS_DEPTH_SRC;
extern const char *const VS_DEPTH_MESH;
extern const char *const FS_DEPTH_MESH;
extern const char *const DEFORM_FN_GLSL;
extern const char *const INSTANCE_FN; // one scattered copy's placement, shared by both mesh passes

extern const char *const FS_DEPTH;
extern const char *const FS_DEPTH_TERRAIN;

extern const char *const VS_WATER;

extern const char *const FS_WATER;

extern const char *const VS_SKY;

extern const char *const FS_SKY_SRC;

extern const char *const VS_MESH;

extern const char *const FS_MESH;

// the card a scattered copy becomes at distance (shaders_billboard.cpp)
extern const char *const VS_BILLBOARD;
extern const char *const FS_BILLBOARD;

extern const char *const VS_GIZMO;

extern const char *const FS_GIZMO;

extern const char *const VS_MATPREV;

extern const char *const FS_MATPREV;

extern const char *const VS_LINES;

extern const char *const FS_LINES;

extern const char *const VS_BG;

extern const char *const FS_BG;



} // namespace studio

