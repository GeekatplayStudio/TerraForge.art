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
extern const char *const WATER_FN_GLSL;
extern const char *const FOG_FN;

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

