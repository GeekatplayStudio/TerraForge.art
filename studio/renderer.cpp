// Geekatplay TerraForge — OpenGL scene renderer.
// Terrain (PBR: roughness/metallic/reflection/translucency/displacement),
// volumetric raymarched clouds, height fog with absorption, water with foam,
// shadow mapping, scene meshes, sun gizmo, selection outlines, object picking.
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <glad/gl.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "stb_image_write.h" // implementation lives in the engine lib

#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"

namespace studio {


Camera CAM;

GLuint prog_terrain = 0, prog_water = 0, prog_sky = 0, prog_depth = 0;
// mean height of the uploaded tile, so the infinite surround can meet it
float g_terrain_mean = 0.f;
GLuint prog_lines = 0, prog_bg = 0, prog_mesh = 0, prog_gizmo = 0;
GLuint prog_matprev = 0;
GLuint matprev_fbo = 0, matprev_tex = 0, matprev_depth = 0;
int matprev_size = 0;
// preview shapes: 0 sphere, 1 cube, 2 flat — pos(3)+nrm(3)+uv(2)
GLuint prev_vao[3] = {0, 0, 0}, prev_vbo[3] = {0, 0, 0};
int prev_verts[3] = {0, 0, 0};
GLuint vao_grid = 0, vbo_grid = 0, ebo_grid = 0, vao_quad = 0;
GLuint vao_lines = 0, vbo_lines = 0;
GLuint vao_dyn = 0, vbo_dyn = 0;      // dynamic outline lines
GLuint vao_sphere = 0, vbo_sphere = 0; // sun gizmo
int sphere_verts = 0;
int line_vert_count = 0;
GLuint tex_height = 0, tex_albedo = 0;
GLuint tex_normal = 0, tex_rough = 0, tex_disp = 0;
GLuint tex_cloud_shape = 0, tex_cloud_detail = 0;
bool has_normal_map = false, has_rough_map = false, has_disp_map = false;
int grid_n = 512, index_count = 0;
// A coarse quad grid the tessellator subdivides. 64x64 patches at up to 64
// subdivisions an edge reaches an effective 4096 across where the camera is
// close, while a patch at the horizon costs two triangles.
extern const int patch_n;
const int patch_n = 65; // vertices per side, so 64 patches
GLuint vao_patch = 0, vbo_patch = 0, ibo_patch = 0;
int patch_index_count = 0;
GLuint prog_terrain_tess = 0;
bool tess_ok = false;
// Per-patch height bounds, so a patch can be tested against the frustum before
// the tessellator is asked to subdivide it. Rebuilt only when the terrain
// changes; a 64x64 RG32F texture is 32 KB, and the CPU copy answers "how many
// patches survived" without a readback.
GLuint tex_patch_bounds = 0;
std::vector<float> cpu_patch_bounds;
int g_patches_visible = -1; // -1 = not measured this frame
int hm_w = 0;
bool has_albedo = false;
gpx::Heightmap cpu_height; // normalized copy, for picking
// sculpt brush cursor: uv, radius (<=0 hidden), erase flag. Reset every frame
// by the viewport, so a hidden panel never leaves a stale ring behind.
float g_brush[4] = {0, 0, -1.f, 0};
GLuint fbo[6] = {0}, fbo_color[6] = {0}, fbo_depth[6] = {0};
int fbo_w[6] = {0}, fbo_h[6] = {0};
GLuint shadow_fbo = 0, shadow_tex = 0;
extern const int SHADOW_RES;
const int SHADOW_RES = 2048;
float cloud_time = 0.f;
float g_last_fovy = 0.9f; // for the planet pass's pixel-size LOD

// ----------------------------------------------------------------- shaders

// ------------------------------------------------------------------ helpers
// The generated displacement function, or a stub. Always substituting
// something keeps every shader well-formed whether or not the user has
// authored a displacement graph, so the placeholder never needs a conditional
// and there is no second code path to get wrong.
const char *GPX_FIELD_STUB =
    "vec4 gpx_terrain_field(vec3 P, vec3 N, float alt, float slope,\n"
    "                       float orient, float t, float lod){\n"
    "  return vec4(0.0);\n}\n";
const char *GPX_SURFACE_STUB =
    "vec4 gpx_terrain_surface(vec3 P, vec3 N, float alt, float slope,\n"
    "                         float orient, float t, float lod){\n"
    "  return vec4(0.5, 0.5, 0.5, 1.0);\n}\n";
const char *GPX_ROUGH_STUB =
    "vec4 gpx_terrain_rough(vec3 P, vec3 N, float alt, float slope,\n"
    "                       float orient, float t, float lod){\n"
    "  return vec4(0.5, 0.0, 0.0, 1.0);\n}\n";
const char *GPX_BUMP_STUB =
    "vec4 gpx_terrain_bump(vec3 P, vec3 N, float alt, float slope,\n"
    "                      float orient, float t, float lod){\n"
    "  return vec4(0.0);\n}\n";
std::string g_surface_want, g_surface_glsl;
std::string g_rough_want, g_rough_glsl;
std::string g_bump_want, g_bump_glsl;
// bump shaping, taken from the TerrainSurface node when the graph evaluates
float g_surf_bump_strength = 1.f, g_surf_bump_scale = 0.004f;
// The source the graph asked for, and the source actually spliced into the
// live program. They differ only while a relink is owed, or after one failed:
// on failure the program falls back to the stub but the request is remembered,
// so the same broken source is never retried. Comparing against the *request*
// rather than the live source is what stops a failing graph from relinking a
// shader every single frame.
std::string g_field_want;      // what the graph asked for
std::string g_field_glsl;      // what is spliced in now, empty = stub
bool g_field_dirty = false;    // a relink is owed
std::string g_field_error;     // why the last relink failed, if it did
// Buffers a generated program samples, uploaded as textures and bound to the
// uniform names the transpiler declared.
std::vector<FieldTex> g_field_tex;



// active photographic grading (set per-frame from the active camera)
float g_grade[3] = {1.f, 1.f, 1.f};
float g_saturation = 1.f;
float g_exposure_mult = 1.f;

void renderer_set_film(const float tint[3], float saturation, float exposure_mult) {
  g_grade[0] = tint[0];
  g_grade[1] = tint[1];
  g_grade[2] = tint[2];
  g_saturation = saturation;
  g_exposure_mult = exposure_mult;
}



// Rebuild the terrain program around the current generated displacement.
// Called at init and again whenever the graph's displacement changes — the
// only shader in the codebase that is not built once and left alone.
//
// The old program is kept until the new one links. A graph that produces
// invalid GLSL therefore leaves the viewport exactly as it was rather than
// turning it black, which is the difference between a visible mistake and an
// apparently broken application.
// The plain vertex shader and the tessellation evaluation shader are the same
// body with a different way of arriving at a uv, so they are assembled from
// one string rather than kept in step by hand.
std::string terrain_vs_source() {
  return std::string("#version 430 core\n") + TERRAIN_VERT_COMMON +
         VS_TERRAIN_TAIL;
}

// Whether the terrain is actually being subdivided adaptively, and at what
// settings. Worth being able to ask: the fixed grid is a silent fallback, and
// a silent fallback that nobody can see is indistinguishable from a feature
// that does not work.
std::string renderer_tess_status() {
  const RenderSettings &RS = render_settings();
  if (!prog_terrain_tess)
    return "adaptive subdivision: unavailable (tessellation program did not "
           "link; using the fixed grid)";
  if (!RS.tessellation)
    return "adaptive subdivision: available but switched off";
  char buf[224];
  std::snprintf(buf, sizeof buf,
                "adaptive subdivision: ON, %dx%d patches, %.0fx to %.0fx an "
                "edge (effective %d to %d across), target %.0f px/edge",
                patch_n - 1, patch_n - 1, RS.tess_min, RS.tess_max,
                (int)((patch_n - 1) * RS.tess_min),
                (int)((patch_n - 1) * RS.tess_max), RS.tess_pixels);
  return buf;
}

// What per-patch culling is actually doing right now. A culling feature that
// cannot be measured is a culling feature nobody can tell is broken.
std::string renderer_cull_status() {
  const RenderSettings &RS = render_settings();
  const int total = (patch_n - 1) * (patch_n - 1);
  if (!RS.frustum_cull) {
    char off[96];
    std::snprintf(off, sizeof off, "patch culling: off, all %d submitted",
                  total);
    return off;
  }
  if (g_patches_visible < 0)
    return "patch culling: on, waiting for terrain bounds";
  char buf[160];
  std::snprintf(buf, sizeof buf,
                "patch culling: %d/%d drawn, %d culled (%.0f%%)",
                g_patches_visible, total, total - g_patches_visible,
                100.0 * (total - g_patches_visible) / total);
  return buf;
}

void ensure_fbo(int slot, int w, int h) {
  if (w == fbo_w[slot] && h == fbo_h[slot] && fbo[slot]) return;
  if (fbo[slot]) {
    glDeleteFramebuffers(1, &fbo[slot]);
    glDeleteTextures(1, &fbo_color[slot]);
    glDeleteRenderbuffers(1, &fbo_depth[slot]);
  }
  glGenFramebuffers(1, &fbo[slot]);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo[slot]);
  glGenTextures(1, &fbo_color[slot]);
  glBindTexture(GL_TEXTURE_2D, fbo_color[slot]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         fbo_color[slot], 0);
  glGenRenderbuffers(1, &fbo_depth[slot]);
  glBindRenderbuffer(GL_RENDERBUFFER, fbo_depth[slot]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            fbo_depth[slot]);
  fbo_w[slot] = w;
  fbo_h[slot] = h;
}

// the view-projection each slot last drew with; see renderer_draw_view
float g_last_mvp[6][16];
bool g_last_mvp_valid[6] = {false, false, false, false, false, false};

const float *renderer_last_mvp(int slot) {
  slot = std::clamp(slot, 0, 5);
  return g_last_mvp_valid[slot] ? g_last_mvp[slot] : nullptr;
}

unsigned renderer_draw_view(int slot, RenderSettings::ViewConfig &vc, int w, int h,
                            float dt) {
  slot = std::clamp(slot, 0, 5);
  // Relink here rather than where the graph changed: this is the main thread
  // with the context current, and doing it once before the first view means
  // all six views draw the same program in the same frame.
  if (g_field_dirty) {
    g_field_dirty = false;
    g_field_glsl = g_field_want;
    g_surface_glsl = g_surface_want;
    g_rough_glsl = g_rough_want;
    g_bump_glsl = g_bump_want;
    std::string err;
    if (rebuild_terrain_program(err)) {
      g_field_error.clear();
    } else {
      g_field_error = err.empty() ? "generated terrain shader failed to link" : err;
      log_error("shader", "terrain program: " + g_field_error);
      // fall back to the stubs, which are known good, so the viewport keeps
      // drawing. The *requests* still hold the broken sources, so this is not
      // retried until the graph actually changes.
      g_field_glsl.clear();
      g_surface_glsl.clear();
      g_rough_glsl.clear();
      g_bump_glsl.clear();
      rebuild_terrain_program(err);
    }
  }
  if (slot == 0) cloud_time += dt;
  static float time_acc = 0;
  if (slot == 0) time_acc += dt;
  if (w < 8 || h < 8) return fbo_color[slot];
  ensure_fbo(slot, w, h);
  float eye[3], mvp[16], inv_vp[16];
  if (vc.camera == 0)
    camera_matrices(w, h, eye, mvp, inv_vp);
  else
    ortho_matrices(vc, w, h, render_settings().height_scale, eye, mvp, inv_vp);
  // Kept for the transform gizmo, which has to project world points onto the
  // same pixels this frame drew them at. Deriving it a second time in the UI
  // would be a second definition of where things are.
  std::memcpy(g_last_mvp[slot], mvp, sizeof mvp);
  g_last_mvp_valid[slot] = true;
  draw_scene(slot, vc, w, h, time_acc, eye, mvp, inv_vp);
  return fbo_color[slot];
}

unsigned renderer_draw(int w, int h, float dt) {
  return renderer_draw_view(0, render_settings().views[0], w, h, dt);
}

bool mat_inverse(float *inv_out, const float *m) {
  float inv[16];
  inv[0] = m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
  float det = m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
  if (std::fabs(det) < 1e-20f) return false;
  det = 1.f / det;
  for (int i = 0; i < 16; ++i) inv_out[i] = inv[i] * det;
  return true;
}

} // namespace studio










