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
extern GLuint prog_lines, prog_bg, prog_mesh, prog_gizmo;
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
extern GLuint tex_normal, tex_rough, tex_disp;
extern GLuint tex_cloud_shape, tex_cloud_detail;
extern bool has_normal_map, has_rough_map, has_disp_map;
extern int grid_n, index_count;
extern GLuint fbo[6], fbo_color[6], fbo_depth[6];
extern int fbo_w[6], fbo_h[6];
extern GLuint shadow_fbo, shadow_tex;
extern const int SHADOW_RES;

// ------------------------------------------------------------ terrain data
extern float g_terrain_mean;   // mean tile height, for the infinite surround
extern int hm_w;
extern bool has_albedo;
extern gpx::Heightmap cpu_height; // normalized copy, for picking
extern float g_brush[4];
extern float cloud_time;

// ------------------------------------------------------------ tessellation
extern GLuint vao_patch, vbo_patch, ibo_patch;
extern int patch_index_count;
extern GLuint prog_terrain_tess;
extern bool tess_ok;
extern GLuint tex_patch_bounds;
extern std::vector<float> cpu_patch_bounds;
extern int g_patches_visible;
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
extern float g_last_mvp[6][16];
extern bool g_last_mvp_valid[6];

// ---------------------------------------------------------------- helpers
inline void uni3(GLuint prog, const char *name, const float *v) {
  glUniform3fv(glGetUniformLocation(prog, name), 1, v);
}
inline void uni1(GLuint prog, const char *name, float v) {
  glUniform1f(glGetUniformLocation(prog, name), v);
}
inline void unii(GLuint prog, const char *name, int v) {
  glUniform1i(glGetUniformLocation(prog, name), v);
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
void draw_box_outline(const float *mvp, float x0, float y0, float z0, float x1,
                      float y1, float z1, const float *color);

// cameras and rays (renderer_camera.cpp)
float perspective_eye_target(float *eye, float *target);
void camera_matrices(int w, int h, float *eye, float *mvp, float *inv_vp);
void ortho_matrices(const RenderSettings::ViewConfig &vc, int w, int h,
                    float hscale, float *eye, float *mvp, float *inv_vp);
bool view_ray(const RenderSettings::ViewConfig &vc, float u, float v, int w,
              int h, float *pn, float *rd);
bool ray_sphere(const float *ro, const float *rd, const float *c, float r,
                float &t);

// framebuffers (renderer.cpp)
void ensure_fbo(int slot, int w, int h);


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
void pass_shadow(const FrameCtx &F);
void pass_sky(const FrameCtx &F);
void pass_terrain(const FrameCtx &F);
void pass_water(const FrameCtx &F);
void pass_outlines(const FrameCtx &F);

} // namespace studio
