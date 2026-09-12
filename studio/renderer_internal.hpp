#include "uniform_cache.hpp"
// Geekatplay TerraForge — the renderer's shared state and small helpers.
//
// renderer.cpp was 2121 lines because everything that touched a GL handle had
// to live in the one file that could see the statics. This header is the deal
// that breaks that: the state is declared here (defined once, in
// renderer.cpp), and the renderer's halves — programs, scene drawing,
// cameras and picking, previews and exports — each live in a file of their
// own and include this.
//
// Private to the renderer_*.cpp family. Nothing else includes it: panels talk
// to the renderer through render_settings.hpp's function surface, and that
// boundary is what keeps the GL out of everything the tests link.
#pragma once
#include "render_settings.hpp"
#include "gpx/heightmap.hpp"
#include <glad/gl.h>
#include <string>
#include <vector>

namespace studio {

// ------------------------------------------------------------------ camera
struct Camera {
  // cinematic default: low angle so terrain and sky both read
  float yaw = 0.7f, pitch = 0.26f, dist = 1.9f;
  float target[3] = {0.5f, 0.08f, 0.5f};
};
extern Camera CAM;
extern float g_last_fovy; // camera_matrices writes; the planet pass reads

// ------------------------------------------------------------ GL resources
extern GLuint prog_terrain, prog_water, prog_sky, prog_depth;
extern GLuint prog_depth_mesh;
// renderer_shadow_meshes.cpp: meshes in the shadow map, and the key that
// says whether any of them moved
unsigned long long mesh_shadow_key();
extern GLuint prog_lines, prog_bg, prog_mesh, prog_gizmo;
extern GLuint prog_billboard; // far scattered copies, as cards
extern GLuint prog_matprev;
extern GLuint matprev_fbo, matprev_tex, matprev_depth;
extern int matprev_size;
extern GLuint prev_vao[3], prev_vbo[3];
extern int prev_verts[3];
extern GLuint vao_grid, vbo_grid, ebo_grid, vao_quad;
extern GLuint vao_lines, vbo_lines;
extern GLuint vao_dyn, vbo_dyn;
extern GLuint vao_sphere, vbo_sphere;
extern int sphere_verts;
extern int line_vert_count;
extern GLuint tex_height, tex_albedo;
extern GLuint tex_place_w;
extern bool has_place_w;
extern GLuint tex_normal, tex_rough, tex_disp;
extern GLuint tex_cloud_shape, tex_cloud_detail;
extern bool has_normal_map, has_rough_map, has_disp_map;
extern int grid_n, index_count;
// The offscreen targets themselves; the slot numbering is in
// render_settings.hpp because the panels pick slots too.
extern GLuint fbo[SLOT_COUNT], fbo_color[SLOT_COUNT], fbo_depth[SLOT_COUNT];
extern int fbo_w[SLOT_COUNT], fbo_h[SLOT_COUNT];
extern GLuint shadow_fbo, shadow_tex;
extern const int SHADOW_RES;

// ------------------------------------------------------------ terrain data
extern float g_terrain_mean;   // mean tile height, for the infinite surround
// The level the tile was placed at (studio/planet_place.cpp), heightmap
// units: the surround's relief is built on it, so the two meet. Set with the
// heightmap; NaN means "use the mean", the pre-placement behaviour.
extern float g_terrain_base;
extern int hm_w;
inline unsigned long long g_shadow_revision = 0;
extern bool has_albedo;
extern gpx::Heightmap cpu_height; // normalized copy, for picking
extern float g_brush[4];
extern float cloud_time;
// the sea's animation clock, as the views last drew it (renderer.cpp)
float renderer_anim_time();

// ------------------------------------------------------------ tessellation
extern GLuint vao_patch, vbo_patch, ibo_patch;
extern int patch_index_count;
extern GLuint prog_terrain_tess;
extern bool tess_ok;
extern GLuint tex_patch_bounds;
extern std::vector<float> cpu_patch_bounds;
// x,y,z triplets in world space: the selected node's point cloud, drawn as
// ticks so a scatter is visible before anything stamps it
extern std::vector<float> g_points_overlay;
extern int g_patches_visible;
extern int g_view_w, g_view_h;
extern const int patch_n;

// --------------------------------------------------- generated field GLSL
extern const char *GPX_FIELD_STUB;
extern const char *GPX_SURFACE_STUB;
extern const char *GPX_ROUGH_STUB;
extern const char *GPX_BUMP_STUB;
extern std::string g_surface_want, g_surface_glsl;
extern std::string g_rough_want, g_rough_glsl;
extern std::string g_bump_want, g_bump_glsl;
extern float g_surf_bump_strength, g_surf_bump_scale;
extern std::string g_field_want, g_field_glsl, g_field_error;
extern bool g_field_dirty;
struct FieldTex {
  std::string name;
  GLuint tex = 0;
};
extern std::vector<FieldTex> g_field_tex;

// ------------------------------------------------------------------- film
extern float g_grade[3];
extern float g_saturation;
extern float g_exposure_mult;

// -------------------------------------------------------- per-view matrix
extern float g_last_mvp[SLOT_COUNT][16];
extern bool g_last_mvp_valid[SLOT_COUNT];
// The u_tri_k each view last drew its terrain with (renderer_passes.cpp): a
// triangle's edge per unit of distance, 0 for an orthographic view or one not
// yet drawn. What the CPU reads to meet the micro-relief that view shows.
extern float g_view_tri_k[SLOT_COUNT];

// ---------------------------------------------------------------- helpers
inline void uni3(GLuint prog, const char *name, const float *v) {
  glUniform3fv(uniform_location(prog, name), 1, v);
}
inline void uni1(GLuint prog, const char *name, float v) {
  glUniform1f(uniform_location(prog, name), v);
}
inline void unii(GLuint prog, const char *name, int v) {
  glUniform1i(uniform_location(prog, name), v);
}
inline void mat_mul(float *o, const float *a, const float *b) {
  float r[16];
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      r[i * 4 + j] = 0;
      for (int k = 0; k < 4; ++k) r[i * 4 + j] += a[k * 4 + j] * b[i * 4 + k];
    }
  for (int i = 0; i < 16; ++i) o[i] = r[i];
}
bool mat_inverse(float *out, const float *m);

std::string terrain_vs_source(); // renderer.cpp

// shader compilation (renderer_programs.cpp)
GLuint compile(GLenum type, const char *src);
GLuint link_prog(const char *vs, const char *fs);
GLuint link_checked(const std::string &vs, const std::string &fs,
                    std::string &err);
std::string inject_sky(const char *src);
bool rebuild_terrain_program(std::string &err);
void bind_field_textures(GLuint prog);

// geometry construction (renderer_init.cpp)
void make_sphere();
void make_preview_shapes();

// the frame (renderer_scene.cpp)
void draw_scene(int slot, const RenderSettings::ViewConfig &vc, int w, int h,
                float time_acc, const float *view_eye, const float *mvp,
                const float *inv_vp);
void build_light_mvp(const float *sun, float hscale, float *out);
// The sun's shadow box: half its width and its depth range, in tile units,
// shared with the shaders that size a depth bias from one map texel.
constexpr float SHADOW_HALF = 0.95f, SHADOW_NEAR = 0.1f, SHADOW_FAR = 4.5f;
void draw_box_corners(const float *mvp, const float c[8][3], const float *rgba);
void draw_box_outline(const float *mvp, float x0, float y0, float z0, float x1,
                      float y1, float z1, const float *color);

// cameras and rays (renderer_camera.cpp)
// Where the perspective view is looking from and at, and its up axis.
// `up_out` may be null; when given it is the camera's own up - analytic for
// the orbit camera, reconstructed for a scene camera - and is what stops the
// view rolling over as it passes vertical.
float perspective_eye_target(float *eye, float *target, float *up_out = nullptr);
void camera_matrices(int w, int h, float *eye, float *mvp, float *inv_vp);
void ortho_matrices(const RenderSettings::ViewConfig &vc, int w, int h,
                    float hscale, float *eye, float *mvp, float *inv_vp);
// `eye`, when given, receives where the view looks from - the point the
// shaders measure the relief's distance from (u_cam)
bool view_ray(const RenderSettings::ViewConfig &vc, float u, float v, int w,
              int h, float *pn, float *rd, float *eye = nullptr);
bool ray_sphere(const float *ro, const float *rd, const float *c, float r,
                float &t);

// framebuffers (renderer.cpp); hdr = RGBA32F colour for the render passes
void ensure_fbo(int slot, int w, int h, bool hdr = false);
// FXAA over a slot's finished picture, in place (renderer_fxaa.cpp); a float
// target is left alone
void renderer_fxaa(int slot, int w, int h);
void renderer_fxaa_shutdown();

// the optical pass (renderer_post.cpp): distortion, chromatic aberration,
// motion blur, flare and vignette, applied to a finished view. Returns the
// texture to show - the scene target itself when there is nothing to do.
unsigned renderer_post_process(int slot, int w, int h, const LensOptics &o);
bool optics_active(const LensOptics &o);

// ---------------------------------------------------------- render passes
// Which pass the scene is being drawn for: 0 the picture, 1..RENDER_PASS_COUNT
// a RenderPass bit + 1, AOV_BEAUTY_LINEAR the shaded colour before tone
// mapping. Every shader that draws a surface reads it (u_aov). renderer_aov.cpp.
extern int g_aov;
static const int AOV_BEAUTY_LINEAR = 13;
// the HDR backdrop dome and the fog: bound into every program that carries
// SKY_FN / FOG_FN (renderer_backdrop.cpp)
void backdrop_bind(GLuint prog);
void upload_fog_uniforms(GLuint prog, const RenderSettings &RS, bool atmosphere);
// The picture a cloud layer is shaped by, bound on `unit` into any program
// carrying CLOUD_SHAPE_GLSL - the sky march and the terrain's cloud shadows,
// so the shadow is cast by the cloud that is actually there
// (cloud_shape_map.cpp). Unit 11 everywhere; nothing else uses it.
void cloud_shape_map_bind(GLuint prog, const RenderSettings &RS, int unit);
static const int CLOUD_SHAPE_MAP_UNIT = 11;


// One frame's shared inputs, built once in draw_scene and handed to each
// pass (renderer_passes.cpp).
struct FrameCtx {
  int slot, w, h;
  float time_acc;
  const RenderSettings::ViewConfig &vc;
  const float *view_eye, *mvp, *inv_vp;
  RenderSettings &RS;
  float sun[3];
  float sun_intensity;
  bool atmosphere, textured, wireframe, cinematic;
  bool show_terrain_obj, show_water_obj, sun_on;
  int sel_type;
  bool clouds_ok, shadows_ok, heavy_maps;
  float space_t;
  float light_mvp[16];
  float wind[2];
};
// gathers the scene's visible point lights into `prog`'s u_light uniforms
void upload_scene_lights(unsigned prog, float hscale);
void pass_shadow(const FrameCtx &F);
void pass_shadow_meshes(const FrameCtx &F); // renderer_shadow_meshes.cpp
// every visible mesh, its copies and their cards (renderer_meshes.cpp):
// the surfaces, or with `media` the meshes whose material is a volume,
// blended back to front over everything drawn before them
void draw_scene_meshes(const FrameCtx &F, const float *sun, bool atmosphere, bool media);
void pass_sky(const FrameCtx &F);
void pass_terrain(const FrameCtx &F);
// the sea: one clipmap surface over the whole world (renderer_water.cpp);
// its rings are built once at start-up and freed at shut-down
void pass_water(const FrameCtx &F);
void water_init();
void water_shutdown();
void pass_outlines(const FrameCtx &F);
// The scene as it stands, copied for a pass that reads what it draws over
// (renderer_water.cpp): the depth, bound on unit 4 while copying, and the
// colour, on unit 5, in the target's own kind of number.
void copy_scene_depth(int slot, int w, int h);
void copy_scene_color(int slot, int w, int h);
unsigned scene_depth_copy(int slot);
unsigned scene_color_copy(int slot);
// a point through a 4x4 column-major matrix, divided by w; a distance
void transform_point(const float *m, float x, float y, float z, float out[3]);
float dist3(const float *a, const float *b);

// The sky's uniforms, set alike for every program that marches it
// (renderer_clouds.cpp): the sky pass, the panoramas, the clouds over the
// ground and the sky the sea reflects.
struct SkyUpload {
  const float *eye = nullptr;  // where the rays start
  const float *sun = nullptr;  // the sun's direction
  float sun_intensity = 1.f;
  float world_r = 0.f;         // the curvature the ground is drawn with
  float space = 0.f;           // u_space: 0 in the air .. 1 open space
  bool clouds = true;
  int steps = 44;              // the march's steps along a ray
  float pixel_k = 0.f;         // radians a pixel spans (0: no level of detail)
  int panorama = 0, hdr = 0, no_sun = 0, aov = 0;
  bool finished = false;       // a capture or a render pass (renderer_space.hpp)
};
void upload_sky_uniforms(GLuint prog, const RenderSettings &RS, const SkyUpload &u);
// the march's steps for the cloud quality, the governor's cap and the engine
int sky_cloud_steps(const RenderSettings &RS, bool cinematic);
// the sky pass's own settings for a view (its eye, curvature and air)
SkyUpload sky_upload_for_view(const FrameCtx &F);
// whether this slot's passes feed the GPU timers (renderer.cpp): the main
// view, or the captures when GPX_TIME_CAPTURES is set
bool pass_timed(int slot);
void clouds_init();
void clouds_shutdown();
// the clouds over the ground, after the sea (renderer_clouds_over.cpp): a
// working view marches half size and is brought up by depth, a capture and a
// render pass march every pixel
void pass_clouds(const FrameCtx &F);
void clouds_over_init();
void clouds_over_shutdown();
// The nebulas at half size for a working view (renderer_space_half.cpp):
// marched into the view's own target with the sky program and bound for the
// sky pass to read. Returns the u_neb_mode the sky pass then draws with - 2
// when the picture was made, 0 to march them in place.
int space_nebulas_half(const FrameCtx &F, GLuint prog, const SkyUpload &u);
void space_half_shutdown();
// Bloom (renderer_post_bloom.cpp): the glow round what is bright in the
// finished picture `src`, at half size; 0 when it cannot be made. `levels_out`
// is how many blur levels were summed into it.
unsigned renderer_bloom(int slot, int w, int h, unsigned src, float threshold,
                        float size, int *levels_out);
void renderer_bloom_shutdown();
// The sky everything reflects: a small panorama of the sky pass, clouds and
// all, taken from the water under the eye before anything reflects it; 0
// from sky_env_texture while the view has no sky. bind_sky_env puts it on
// unit 14 as SKY_ENV_GLSL's u_sky_env, for the sea, the ground and meshes.
void sky_env_update(const FrameCtx &F);
unsigned sky_env_texture();
void bind_sky_env(GLuint prog);

// The world's centre: three axes through 0,0,0 at the scale of the world
// (studio/renderer_origin.cpp).
void draw_world_origin(const float *mvp, const RenderSettings &RS);

} // namespace studio
