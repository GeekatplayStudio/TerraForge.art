#include "uniform_cache.hpp"
// Geekatplay TerraForge — the water pass: one sea for the whole world.
//
// The surface is a clipmap: a stack of square rings about the eye, each
// with cells twice the size of the one inside it, reaching as far as the
// ground does. Every level is centred on a point snapped to twice its own
// cell, so its vertices never slide across the waves as the camera moves;
// toward its rim a level morphs onto the next one's lattice and filters its
// waves to the next one's cell (VS_WATER), so where two levels meet they
// are the same surface. Each level draws only the part no finer level owns
// - the fragment stage discards by the point's resting position - so no
// point of the sea is shaded twice (water_surface.hpp lays the levels out;
// tests/cpp/test_water.cpp proves the partition).
//
// The scene is copied before the sea is drawn - its depth, and for the
// picture its colour: the fragment stage reads how much water each ray
// crosses and how deep the ground is under the surface, and composites the
// light that comes through with the light the surface sends, in light
// rather than in the display's encoding (shaders_water.cpp).
#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"
#include "gpu_timer.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "terrain_tiles.hpp"
#include "terrain_xform.hpp"
#include "water_surface.hpp"
#include "world_shape.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace studio {

namespace {

constexpr int MAX_LEVELS = 28;

GLuint g_vbo = 0, g_ibo[2] = {0, 0}, g_vao[2] = {0, 0};
int g_count[2] = {0, 0};
GLuint g_depth_fbo[SLOT_COUNT] = {}, g_depth_tex[SLOT_COUNT] = {};
GLuint g_color_tex[SLOT_COUNT] = {};
int g_depth_w[SLOT_COUNT] = {}, g_depth_h[SLOT_COUNT] = {};
int g_color_w[SLOT_COUNT] = {}, g_color_h[SLOT_COUNT] = {};
bool g_color_hdr[SLOT_COUNT] = {};
int g_cell_exp[SLOT_COUNT] = {};
bool g_cell_set[SLOT_COUNT] = {};

} // namespace

unsigned scene_depth_copy(int slot) { return g_depth_tex[slot]; }
unsigned scene_color_copy(int slot) { return g_color_tex[slot]; }

// The scene's depth as it stands before the water, into a texture the
// water can read while it draws into the scene's own target.
void copy_scene_depth(int slot, int w, int h) {
  glActiveTexture(GL_TEXTURE4); // the water's own unit: no other pass's binding is disturbed
  if (!g_depth_tex[slot] || g_depth_w[slot] != w || g_depth_h[slot] != h) {
    if (!g_depth_fbo[slot]) glGenFramebuffers(1, &g_depth_fbo[slot]);
    if (!g_depth_tex[slot]) glGenTextures(1, &g_depth_tex[slot]);
    glBindTexture(GL_TEXTURE_2D, g_depth_tex[slot]);
    // the format of the scene's own depth buffer (ensure_fbo): a blit of
    // depth needs the two to match
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, g_depth_fbo[slot]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           g_depth_tex[slot], 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    g_depth_w[slot] = w;
    g_depth_h[slot] = h;
  }
  GLint scene_fbo = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &scene_fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)scene_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_depth_fbo[slot]);
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)scene_fbo);
}

// The scene's colour as it stands before the water, in the target's own
// kind of number (a pass target holds light in floats, a view holds the
// finished picture in bytes).
void copy_scene_color(int slot, int w, int h) {
  GLint type = GL_UNSIGNED_NORMALIZED;
  glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &type);
  const bool hdr = type == GL_FLOAT;
  glActiveTexture(GL_TEXTURE5);
  if (!g_color_tex[slot] || g_color_w[slot] != w || g_color_h[slot] != h ||
      g_color_hdr[slot] != hdr) {
    if (!g_color_tex[slot]) glGenTextures(1, &g_color_tex[slot]);
    glBindTexture(GL_TEXTURE_2D, g_color_tex[slot]);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA,
                 hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_color_w[slot] = w;
    g_color_h[slot] = h;
    g_color_hdr[slot] = hdr;
  }
  glBindTexture(GL_TEXTURE_2D, g_color_tex[slot]);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
}

void transform_point(const float *m, float x, float y, float z, float out[3]) {
  const float v[4] = {x, y, z, 1.f};
  float r[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r[i] += m[j * 4 + i] * v[j];
  const float iw = std::fabs(r[3]) > 1e-30f ? 1.f / r[3] : 1.f;
  out[0] = r[0] * iw; out[1] = r[1] * iw; out[2] = r[2] * iw;
}

float dist3(const float *a, const float *b) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

namespace {

// Does any ground stand on this face of the world: a surface layer, or a
// visible tile? The sea lies on the faces that have land to lie against.
bool face_has_ground(const RenderSettings &RS, int side, int &layer_count) {
  layer_count = (int)planet_home_layers(side).size();
  if (layer_count > 0) return true;
  const SceneState &sc = scene();
  for (int k = 0; k < terrain_tile_count(); ++k) {
    if (k > 0 && !terrain_tile_visible(k)) continue;
    const int obj = terrain_tile_object(k);
    if (obj < 0 || obj >= (int)sc.objects.size()) continue;
    if (object_side(RS, sc.objects[(size_t)obj]) == side) return true;
  }
  return false;
}

// The sea on every face of the world that has ground, level by level.
void draw_water_faces(const FrameCtx &F, float R, float size_m, bool ortho) {
  const RenderSettings &RS = F.RS;
  const RenderSettings::ViewConfig &vc = F.vc;
  // the waves, in the order the shader and the CPU twin read them
  const WaterWaves &ww = water_waves(RS);
  float a[gpx::water::MAX_WAVES * 4] = {}, b[gpx::water::MAX_WAVES * 4] = {};
  for (int i = 0; i < ww.n; ++i) {
    a[i * 4] = ww.w[i].dx;
    a[i * 4 + 1] = ww.w[i].dz;
    a[i * 4 + 2] = ww.w[i].k;
    a[i * 4 + 3] = ww.w[i].amp;
  }
  glUniform4fv(uniform_location(prog_water, "u_wv_a"), gpx::water::MAX_WAVES, a);
  unii(prog_water, "u_wv_n", ww.n);
  const bool infinite = vc.camera == 0 && F.show_terrain_obj && infinite_layers_present() &&
                        !(R > 0.f && R < 1.f / 6.2831853f);
  const float level = RS.water_level * RS.height_scale;

  for (int side : {SIDE_OUTSIDE, SIDE_INSIDE}) {
    int layers = 0;
    if (!face_has_ground(RS, side, layers)) continue;
    const gpx::planet::Shape S = world_shape(RS, side);
    upload_world_shape(prog_water, S);
    // where the eye stands over this face's flat world, and how high above
    // the sea: the finest cell follows the height, so the rings spend their
    // vertices where they show
    double eu, ev;
    water_eye_param(F.view_eye, R, S, eu, ev);
    float lim_x, lim_z;
    water_param_limits(R, S, lim_x, lim_z);
    const double cx = std::min(lim_x, 64.f), cz = std::min(lim_z, 64.f);
    eu = std::clamp(eu, 0.5 - cx, 0.5 + cx);
    ev = std::clamp(ev, 0.5 - cz, 0.5 + cz);
    glUniform2f(uniform_location(prog_water, "u_param_lim"), lim_x, lim_z);
    glUniform2f(uniform_location(prog_water, "u_eye_param"), (float)eu, (float)ev);
    float alt = F.view_eye[1];
    if (R > 0.f && !gpx::planet::shape_is_flat(S)) alt = gpx::planet::world_alt(F.view_eye, R, S);
    if (S.flip) alt = -alt - S.thick;
    const double above_m = std::fabs(double(alt) - double(level)) * size_m;
    const double want_cell = ortho ? double(vc.ortho_zoom) / 256.0
                                   : std::max(above_m * 0.004, 0.03) / size_m;
    const double want_exp = std::log2(want_cell);
    int &ce = g_cell_exp[F.slot];
    // hysteresis: a camera hovering at a power of two keeps its rings
    if (!g_cell_set[F.slot] || want_exp < ce - 0.25 || want_exp > ce + 1.25) {
      ce = (int)std::floor(want_exp);
      g_cell_set[F.slot] = true;
    }
    const double c0 = std::ldexp(1.0, ce);
    // The ground's reach decides the sea's. Where a far shell follows the
    // surround it carries its own sea from the square 29 tiles out, so the
    // water stops on that square; where none does, the sea runs to the
    // surround's rim and fades into the sky with it.
    const bool shell = infinite && surround_far_shell(RS, F.view_eye, R, side, layers);
    const bool flat = world_is_flat(RS);
    unii(prog_water, "u_horizon", (shell || flat) ? 0 : 1);
    unii(prog_water, "u_reach_square", shell ? 1 : 0);
    const float reach = shell ? 29.f : 30.5f;
    uni1(prog_water, "u_reach", reach);
    // the wave origin: a quarter-tile lattice point near the eye
    const double wx = water_origin_snap(eu), wz = water_origin_snap(ev);
    glUniform2f(uniform_location(prog_water, "u_wave_origin"), (float)wx, (float)wz);
    float ph[gpx::water::MAX_WAVES] = {};
    gpx::water::rebase(ww.w, ww.n, wx * size_m, wz * size_m, F.time_acc, ph);
    for (int i = 0; i < ww.n; ++i) {
      b[i * 4] = ph[i];
      b[i * 4 + 1] = ww.w[i].q;
      b[i * 4 + 2] = ww.w[i].lambda;
      b[i * 4 + 3] = 0.f;
    }
    glUniform4fv(uniform_location(prog_water, "u_wv_b"), gpx::water::MAX_WAVES, b);
    const double cell_m = water_foam_cell_m(RS.foam_scale);
    glUniform2f(uniform_location(prog_water, "u_w_foam_off"),
                (float)std::fmod(wx * size_m / cell_m, 65536.0),
                (float)std::fmod(wz * size_m / cell_m, 65536.0));
    // as many levels as it takes to cover the reach from wherever the eye is
    const double need = reach + std::max(std::fabs(eu - 0.5), std::fabs(ev - 0.5)) + 1.0;
    WaterLevel lv[MAX_LEVELS];
    const int levels = water_levels(eu, ev, c0, need, lv, MAX_LEVELS);
    for (int l = 0; l < levels; ++l) {
      const WaterLevel &L = lv[l];
      glUniform2f(uniform_location(prog_water, "u_lvl_origin"), (float)L.ox, (float)L.oz);
      uni1(prog_water, "u_lvl_cell", (float)L.cell);
      glUniform2f(uniform_location(prog_water, "u_lvl_rel_m"), (float)((L.ox - wx) * size_m),
                  (float)((L.oz - wz) * size_m));
      unii(prog_water, "u_lvl_inner", l > 0 ? 1 : 0);
      if (l > 0) {
        const WaterLevel &P = lv[l - 1];
        glUniform2f(uniform_location(prog_water, "u_inner_rel_m"),
                    (float)((P.ox - wx) * size_m), (float)((P.oz - wz) * size_m));
        uni1(prog_water, "u_inner_r_m", (float)(WATER_OWN * P.cell * size_m));
      }
      unii(prog_water, "u_lvl_outer", l + 1 < levels ? 1 : 0);
      glUniform2f(uniform_location(prog_water, "u_outer_rel_m"), (float)((L.ox - wx) * size_m),
                  (float)((L.oz - wz) * size_m));
      uni1(prog_water, "u_outer_r_m", (float)(WATER_OWN * L.cell * size_m));
      const int k = l == 0 ? 0 : 1;
      glBindVertexArray(g_vao[k]);
      glDrawElements(GL_TRIANGLES, g_count[k], GL_UNSIGNED_INT, nullptr);
    }
  }
  glBindVertexArray(0);
}

} // namespace

void water_init() {
  const int WN = WATER_GRID, HOLE = WATER_HOLE;
  const int S = WN + 1, H = WN / 2;
  std::vector<float> verts;
  verts.reserve((size_t)S * S * 2);
  for (int j = 0; j < S; ++j)
    for (int i = 0; i < S; ++i) {
      verts.push_back(float(i - H));
      verts.push_back(float(j - H));
    }
  std::vector<unsigned> full, ring;
  for (int j = 0; j < WN; ++j)
    for (int i = 0; i < WN; ++i) {
      const unsigned a = unsigned(j * S + i), b = a + unsigned(S), c = a + 1, d = b + 1;
      // the diagonal the terrain grid uses; the morph relies on every
      // level splitting its cells the same way
      full.insert(full.end(), {a, b, c, c, b, d});
      const int x0 = i - H, y0 = j - H;
      const bool open = x0 >= -HOLE && x0 + 1 <= HOLE && y0 >= -HOLE && y0 + 1 <= HOLE;
      if (!open) ring.insert(ring.end(), {a, b, c, c, b, d});
    }
  glGenBuffers(1, &g_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(),
               GL_STATIC_DRAW);
  const std::vector<unsigned> *lists[2] = {&full, &ring};
  for (int k = 0; k < 2; ++k) {
    glGenVertexArrays(1, &g_vao[k]);
    glBindVertexArray(g_vao[k]);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glGenBuffers(1, &g_ibo[k]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo[k]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(lists[k]->size() * sizeof(unsigned)),
                 lists[k]->data(), GL_STATIC_DRAW);
    g_count[k] = (int)lists[k]->size();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
  }
  glBindVertexArray(0);
}

void water_shutdown() {
  for (int k = 0; k < 2; ++k) {
    if (g_vao[k]) glDeleteVertexArrays(1, &g_vao[k]);
    if (g_ibo[k]) glDeleteBuffers(1, &g_ibo[k]);
    g_vao[k] = g_ibo[k] = 0;
  }
  if (g_vbo) glDeleteBuffers(1, &g_vbo);
  g_vbo = 0;
  for (int s = 0; s < SLOT_COUNT; ++s) {
    if (g_depth_tex[s]) glDeleteTextures(1, &g_depth_tex[s]);
    if (g_depth_fbo[s]) glDeleteFramebuffers(1, &g_depth_fbo[s]);
    if (g_color_tex[s]) glDeleteTextures(1, &g_color_tex[s]);
    g_depth_tex[s] = g_depth_fbo[s] = g_color_tex[s] = 0;
    g_depth_w[s] = g_depth_h[s] = g_color_w[s] = g_color_h[s] = 0;
  }
}

// The water's look, into any program carrying WATER_FN_GLSL: the water
// pass and the planet's far shell, so the near sea and the far one agree.
void upload_water_uniforms(unsigned prog, const RenderSettings &RS, float time) {
  const WaterWaves &ww = water_waves(RS);
  uni1(prog, "u_w_time", time);
  // Opacity leans on the clarity: its default leaves the clarity as set,
  // 1 is water no light gets into, and lower is clearer than the clarity.
  const float see = std::clamp((1.f - RS.water_opacity) / 0.08f, 0.f, 10.f);
  uni1(prog, "u_w_clarity", RS.water_clarity * see);
  uni1(prog, "u_w_atmo", RS.atmosphere_density);
  uni1(prog, "u_w_sun_half", RS.sun_angle_deg * 0.5f * 0.017453293f);
  uni1(prog, "u_w_slope_var", ww.slope_var);
  uni3(prog, "u_w_deep", RS.water_deep_color);
  uni3(prog, "u_w_shallow", RS.water_shallow_color);
  unii(prog, "u_w_foam_on", RS.water_foam ? 1 : 0);
  uni3(prog, "u_w_foam_color", RS.foam_color);
  uni1(prog, "u_w_foam_amount", RS.foam_amount);
  uni1(prog, "u_w_foam_scale", RS.foam_scale);
  uni1(prog, "u_w_foam_crests", RS.foam_crests);
  uni1(prog, "u_w_foam_depth", RS.foam_depth_m);
  uni1(prog, "u_w_foam_coverage", RS.foam_coverage);
  uni1(prog, "u_w_whitecaps", gpx::water::whitecap_share(RS.water_wind_speed));
  const float wd = RS.water_wind_dir * 0.017453293f;
  glUniform2f(uniform_location(prog, "u_w_wind"), std::cos(wd), std::sin(wd));
  // the sky it reflects, clouds and all (renderer_clouds.cpp)
  bind_sky_env(prog);
}

void pass_water(const FrameCtx &F) {
  RenderSettings &RS = F.RS;
  const RenderSettings::ViewConfig &vc = F.vc;
  if (!RS.show_water || !vc.show_water_view || !F.show_water_obj || !prog_water) return;
  const float size_m = std::max(RS.terrain_size_m, 1e-3f);
  const float R = view_planet_radius(RS, vc);
  const bool ortho = vc.camera != 0;
  // A geometry pass wants the water surface itself; only the picture and
  // the linear beauty see through it, and those need the colour behind.
  const bool see_through = g_aov == 0 || g_aov == AOV_BEAUTY_LINEAR;
  copy_scene_depth(F.slot, F.w, F.h);
  if (see_through) copy_scene_color(F.slot, F.w, F.h);

  glUseProgram(prog_water);
  glDisable(GL_BLEND); // the water composites what is behind it itself
  upload_fog_uniforms(prog_water, RS, F.atmosphere);
  backdrop_bind(prog_water);
  upload_water_uniforms(prog_water, RS, F.time_acc);
  unii(prog_water, "u_aov", g_aov);
  unii(prog_water, "u_object_id", 2);
  glUniformMatrix4fv(uniform_location(prog_water, "u_mvp"), 1, GL_FALSE, F.mvp);
  uni1(prog_water, "u_hscale", RS.height_scale);
  uni1(prog_water, "u_level", RS.water_level * RS.height_scale);
  uni1(prog_water, "u_planet_radius", R);
  uni1(prog_water, "u_size_m", size_m);
  uni3(prog_water, "u_sun", F.sun);
  uni3(prog_water, "u_sun_color", RS.sun_color);
  uni1(prog_water, "u_sun_intensity", F.sun_intensity);
  uni1(prog_water, "u_ambient", RS.ambient_intensity);
  uni3(prog_water, "u_sky_light", sky_light_rgb());
  uni3(prog_water, "u_cam", F.view_eye);
  uni1(prog_water, "u_exposure", RS.exposure * g_exposure_mult);
  uni3(prog_water, "u_grade", g_grade);
  uni1(prog_water, "u_sat", g_saturation);
  uni3(prog_water, "u_sky_zenith", RS.sky_zenith);
  uni3(prog_water, "u_sky_horizon", RS.sky_horizon);
  unii(prog_water, "u_displace", RS.water_displaced ? 1 : 0);
  unii(prog_water, "u_selected", F.sel_type == SceneObject::Water ? 1 : 0);
  unii(prog_water, "u_world_outline", RS.world_outline);
  uni1(prog_water, "u_shell_w", RS.world_width);
  uni1(prog_water, "u_morph_lo", WATER_MORPH_LO);
  uni1(prog_water, "u_morph_hi", WATER_MORPH_HI);
  // the scene behind the water, and how its depth buffer maps to distance
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, g_depth_tex[F.slot]);
  unii(prog_water, "u_scene_depth", 4);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, g_color_tex[F.slot]);
  unii(prog_water, "u_scene_color", 5);
  unii(prog_water, "u_linear_target", g_aov == AOV_BEAUTY_LINEAR ? 1 : 0);
  glUniform2f(uniform_location(prog_water, "u_viewport"), (float)F.w, (float)F.h);
  unii(prog_water, "u_ortho", ortho ? 1 : 0);
  {
    float pn[3], pf[3];
    transform_point(F.inv_vp, 0.f, 0.f, -1.f, pn);
    transform_point(F.inv_vp, 0.f, 0.f, 1.f, pf);
    // and one pixel's size, measured a pixel above the middle of the far
    // plane - at the near plane the two points are too close for a float
    float pu[3];
    transform_point(F.inv_vp, 0.f, 2.f / float(std::max(F.h, 1)), 1.f, pu);
    if (ortho) {
      uni1(prog_water, "u_depth_near", 0.f);
      uni1(prog_water, "u_depth_far", dist3(pn, pf));
      uni1(prog_water, "u_pixel_k", dist3(pu, pf));
    } else {
      const float far = dist3(pf, F.view_eye);
      uni1(prog_water, "u_depth_near", dist3(pn, F.view_eye));
      uni1(prog_water, "u_depth_far", far);
      uni1(prog_water, "u_pixel_k", dist3(pu, pf) / std::max(far, 1e-12f));
    }
  }
  // the terrain's shadows fall on the sea too
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, shadow_tex);
  unii(prog_water, "u_shadowmap", 2);
  glUniformMatrix4fv(uniform_location(prog_water, "u_light_mvp"), 1, GL_FALSE, F.light_mvp);
  unii(prog_water, "u_shadows", (F.shadows_ok && vc.display != 0) ? 1 : 0);
  uni1(prog_water, "u_shadow_soft", RS.shadow_softness);
  // Timed on the GPU for the main view only, like the terrain: with vsync
  // on, the clock cannot say what the sea costs.
  if (pass_timed(F.slot)) {
    GpuTimer::Scope s(gpu_timer("water"));
    draw_water_faces(F, R, size_m, ortho);
  } else {
    draw_water_faces(F, R, size_m, ortho);
  }
  glActiveTexture(GL_TEXTURE0);
}

} // namespace studio
