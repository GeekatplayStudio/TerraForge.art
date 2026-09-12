#include "uniform_cache.hpp"
// Geekatplay TerraForge - the frame passes: shadow map, sky and
// volumetric clouds, the terrain tile, water, and selection outlines. Each is
// one verbatim block moved out of draw_scene (renderer_scene.cpp), sharing
// the per-frame FrameCtx; proven pixel-identical under GPX_FREEZE_TIME.
#include "perf.hpp"
#include "renderer_internal.hpp"
#include "app.hpp"
#include "cloud_noise.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "terrain_relief.hpp"
#include "terrain_xform.hpp"
#include "terrain_tiles.hpp"
#include "planet_place.hpp"
#include "world_shape.hpp"
#include "renderer_space.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace studio {

TerrainXform terrain_xform_current() {
  // the tile being drawn (terrain_tiles.hpp), tile 0 between draws
  const int cur = terrain_tile_current();
  const int obj = terrain_tile_object(cur < 0 ? 0 : cur);
  if (obj >= 0 && obj < (int)scene().objects.size())
    return terrain_xform_of(scene().objects[(size_t)obj], render_settings().height_scale);
  return TerrainXform();
}

// The tile's transform and outline into a terrain program (terrain_xform.hpp):
// the colour pass, the tessellated pass and the shadow pass all take it, so
// a moved tile shadows where it stands.
void upload_terrain_xform(unsigned prog) {
  const TerrainXform t = terrain_xform_current();
  const RenderSettings &RS = render_settings();
  unii(prog, "u_tx_on", t.on ? 1 : 0);
  if (t.on) {
    uni3(prog, "u_tx_pos", t.pos);
    uni3(prog, "u_tx_scl", t.scl);
    glUniformMatrix3fv(uniform_location(prog, "u_tx_rot"), 1, GL_TRUE, t.rot);
    unii(prog, "u_def_on", t.deform.identity() ? 0 : 1);
    uni3(prog, "u_def_twist", t.deform.twist);
    uni3(prog, "u_def_shear", t.deform.shear);
    uni1(prog, "u_def_bend", t.deform.bend);
    unii(prog, "u_def_bend_axis", t.deform.bend_axis);
    uni1(prog, "u_def_taper", t.deform.taper);
    uni3(prog, "u_bmin", t.bmin);
    uni3(prog, "u_bmax", t.bmax);
  }
  // the outline is a hard cut only when the placement is not doing it
  const bool placed = RS.place_on_planet && planet_place_last().placed;
  unii(prog, "u_tx_cut", (RS.terrain_shape != 0 && !placed) ? 1 : 0);
  unii(prog, "u_tx_shape", RS.terrain_shape);
  uni1(prog, "u_tx_aspect", RS.terrain_aspect);
}

void upload_world_shape(unsigned prog, const gpx::planet::Shape &S) {
  glUniform4f(uniform_location(prog, "u_world_shape"), S.flat_x ? 1.f : 0.f,
              S.flat_z ? 1.f : 0.f, S.inside ? 1.f : 0.f, S.flip ? 1.f : 0.f);
  glUniform1f(uniform_location(prog, "u_world_thick"), S.thick);
}

// The shape of the face the tile being drawn stands on (world_shape.hpp):
// the globe's, a ring's, an inside face's.
static gpx::planet::Shape tile_shape(const RenderSettings &RS) {
  const int cur = terrain_tile_current() < 0 ? 0 : terrain_tile_current();
  const int obj = terrain_tile_object(cur);
  const SceneObject *o = (obj >= 0 && obj < (int)scene().objects.size())
                             ? &scene().objects[(size_t)obj] : nullptr;
  return world_shape_of(RS, o);
}

void upload_terrain_xform_inverse(unsigned prog, int side) {
  const RenderSettings &rsw = render_settings();
  // the further tiles, for the surround's holes (planet_shaders.cpp);
  // asked for one face of the world, only the tiles standing on it
  {
    const std::vector<TerrainTileGpu> &extra = terrain_tiles_extra();
    int n = 0;
    int on[8] = {};
    float pos[16] = {}, inv[32] = {};
    for (size_t k = 0; k < extra.size() && n < 8; ++k) {
      if (!terrain_tile_visible((int)k + 1)) continue;
      const SceneObject &o = scene().objects[(size_t)extra[k].object];
      if (side != 0 && object_side(rsw, o) != side) continue;
      const TerrainXform tk = terrain_xform_of(o, render_settings().height_scale);
      const float rad = tk.yaw * 3.14159265f / 180.f;
      on[n] = 1;
      pos[n * 2] = tk.pos[0];
      pos[n * 2 + 1] = tk.pos[2];
      inv[n * 4] = std::cos(rad);
      inv[n * 4 + 1] = std::sin(rad);
      inv[n * 4 + 2] = 1.f / std::max(std::fabs(tk.scl[0]), 1e-4f);
      inv[n * 4 + 3] = 1.f / std::max(std::fabs(tk.scl[2]), 1e-4f);
      ++n;
    }
    unii(prog, "u_tile_n", n);
    if (n) {
      glUniform1iv(uniform_location(prog, "u_txn_on"), n, on);
      glUniform2fv(uniform_location(prog, "u_txn_pos"), n, pos);
      glUniform4fv(uniform_location(prog, "u_txn"), n, inv);
    }
  }
  const TerrainXform t = terrain_xform_current();
  if (side != 0) {
    // tile 0 on the other face: no hole for it here - its footprint is
    // pushed out of reach, so the surround reads the planet everywhere
    const int obj0 = terrain_tile_object(0);
    if (obj0 < 0 || object_side(rsw, scene().objects[(size_t)obj0]) != side) {
      unii(prog, "u_tx_on", 1);
      glUniform2f(uniform_location(prog, "u_txi_pos"), 1e5f, 1e5f);
      glUniform4f(uniform_location(prog, "u_txi"), 1.f, 0.f, 1.f, 1.f);
      glUniform2f(uniform_location(prog, "u_txi_y"), 1.f, 0.f);
      return;
    }
  }
  unii(prog, "u_tx_on", t.on ? 1 : 0);
  // The identity is uploaded too: the surround multiplies the tile's edge
  // height by u_txi_y.x whether or not the transform is on, and a uniform
  // never set reads as zero - which put the tile's rim at height nought,
  // flooded it, and dragged the surround down to meet it.
  if (!t.on) {
    glUniform2f(uniform_location(prog, "u_txi_pos"), 0.f, 0.f);
    glUniform4f(uniform_location(prog, "u_txi"), 1.f, 0.f, 1.f, 1.f);
    glUniform2f(uniform_location(prog, "u_txi_y"), 1.f, 0.f);
    return;
  }
  const float rad = t.yaw * 3.14159265f / 180.f;
  glUniform2f(uniform_location(prog, "u_txi_pos"), t.pos[0], t.pos[2]);
  glUniform4f(uniform_location(prog, "u_txi"), std::cos(rad), std::sin(rad),
              1.f / std::max(std::fabs(t.scl[0]), 1e-4f), 1.f / std::max(std::fabs(t.scl[2]), 1e-4f));
  glUniform2f(uniform_location(prog, "u_txi_y"), t.scl[1], t.pos[1]);
}

float g_view_tri_k[SLOT_COUNT] = {}; // renderer_internal.hpp

// The relief's level of detail for a view (docs/LOD.md): the mip level whose
// texel spans as many pixels as the tessellation puts between two vertices,
// times the dial - at the default 0.5, the level whose texel is one edge long.
// Per unit of distance, so the shader multiplies by its own. It follows the
// lens and the screen: the old fixed `32 x dial` read the whole tile at level
// 4 from the default camera whatever it was, a telephoto shot included. An
// orthographic view has no distance to scale by and reads every texel. Per
// tile (hm_w is the tile's own), so call it after a tile's swap.
static float relief_lod_k(const FrameCtx &F) {
  if (F.vc.camera != 0 || F.RS.terrain_lod <= 0.f) return 0.f;
  const float pixel_k = 2.f * std::tan(g_last_fovy * 0.5f) / float(std::max(F.h, 1));
  const float edge_px = F.RS.tessellation ? std::max(F.RS.tess_pixels, 1.f) : 8.f;
  return F.RS.terrain_lod * 2.f * edge_px * float(std::max(hm_w, 1)) * pixel_k;
}
static void upload_relief_lod(GLuint prog, const FrameCtx &F) {
  uni3(prog, "u_lod_cam", F.view_eye);
  uni1(prog, "u_height_lod_k", relief_lod_k(F));
  extern bool g_terrain_base_set;
  uni1(prog, "u_lod_ground",
       (g_terrain_base_set ? g_terrain_base : g_terrain_mean) * F.RS.height_scale);
  int levels = 1;
  for (int s = std::max(hm_w, 1); s > 1; s >>= 1) ++levels;
  uni1(prog, "u_height_top", float(levels - 1));
  // how long a triangle's edge is per unit of distance, for the micro-relief
  // (none finer than the triangles carrying it)
  const float tri_k = F.vc.camera == 0
                          ? relief_tri_k(g_last_fovy, F.h, F.RS.tessellation, F.RS.tess_pixels)
                          : 0.f;
  // kept per view: the pickers and the orbit pivot read the relief with the
  // octaves this view drew it with (renderer_camera.cpp)
  g_view_tri_k[std::clamp(F.slot, 0, SLOT_COUNT - 1)] = tri_k;
  uni1(prog, "u_tri_k", tri_k);
}

void pass_shadow(const FrameCtx &F) {
  RenderSettings &RS = F.RS;
  const float *light_mvp = F.light_mvp;
  bool shadows_ok = F.shadows_ok;
  if (shadows_ok) {
    static unsigned long long revision = ~0ull;
    static GLuint program = 0;
    static float matrix[16] = {}, height = 0.f, strength = 0.f;
    static unsigned long long meshes = 0;
    float wanted_strength = g_field_glsl.empty() ? 0.f : RS.field_displacement;
    const unsigned long long mesh_key = mesh_shadow_key();
    if (revision == g_shadow_revision && program == prog_depth &&
        height == RS.height_scale && strength == wanted_strength &&
        meshes == mesh_key && std::equal(matrix, matrix + 16, light_mvp)) return;
    revision = g_shadow_revision;
    meshes = mesh_key;
    program = prog_depth;
    height = RS.height_scale;
    strength = wanted_strength;
    std::copy(light_mvp, light_mvp + 16, matrix);
    GpuTimer::Scope timer(gpu_timer("shadow"));
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(prog_depth);
    glUniformMatrix4fv(uniform_location(prog_depth, "u_light_mvp"), 1, GL_FALSE,
                       light_mvp);
    uni1(prog_depth, "u_hscale", RS.height_scale);
    upload_terrain_xform(prog_depth);
    // the same relief level the view reads, so the shadow lies on the
    // surface the viewer sees
    upload_relief_lod(prog_depth, F);
    uni1(prog_depth, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    bind_field_textures(prog_depth);
    glBindVertexArray(vao_grid);
    for (int k = 0; k < terrain_tile_count(); ++k) {
      if (k > 0 && !terrain_tile_visible(k)) continue;
      if (k == 0 && terrain_tile_object(0) < 0) continue;
      TileSwap swap(k);
      upload_terrain_xform(prog_depth);
      upload_relief_lod(prog_depth, F);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex_height);
      unii(prog_depth, "u_height", 0);
      glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    }
    pass_shadow_meshes(F); // rocks, trees and their copies cast too
  }
}

void pass_sky(const FrameCtx &F) {
  RenderSettings &RS = F.RS;
  int slot = F.slot;
  // background
  if (F.atmosphere && RS.background_mode == 0) {
    const SkyUpload su = sky_upload_for_view(F);
    auto draw = [&]() {
      // the nebulas' half-size picture first, where a working view has
      // nebulas to march (renderer_space_half.cpp)
      const int neb_mode = space_nebulas_half(F, prog_sky, su);
      glUseProgram(prog_sky);
      glDepthMask(GL_FALSE);
      // on the far plane, where nothing was drawn (VS_SKY)
      glDepthFunc(GL_LEQUAL);
      glUniformMatrix4fv(uniform_location(prog_sky, "u_inv_vp"), 1, GL_FALSE, F.inv_vp);
      // the same uniforms, the same way, as every other program that marches
      // this sky (renderer_clouds.cpp)
      upload_sky_uniforms(prog_sky, RS, su);
      unii(prog_sky, "u_neb_mode", neb_mode);
      glBindVertexArray(vao_quad);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    if (pass_timed(slot)) {
      // Timed for the same reason the terrain pass is: the volumetric march
      // is the most expensive thing on screen and the frame clock cannot see
      // it move.
      GpuTimer::Scope s(gpu_timer("sky+clouds"));
      draw();
    } else {
      draw();
    }
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
  } else if (RS.background_mode == 1) {
    glUseProgram(prog_bg);
    glDepthMask(GL_FALSE);
    uni3(prog_bg, "u_top", RS.bg_color);
    uni3(prog_bg, "u_bottom", RS.bg_color2);
    glBindVertexArray(vao_quad);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
  }
}

static void draw_terrain_tile(const FrameCtx &F) {
  RenderSettings &RS = F.RS;
  const RenderSettings::ViewConfig &vc = F.vc;
  int slot = F.slot, w = F.w, h = F.h;
  const float *mvp = F.mvp;
  const float *view_eye = F.view_eye;
  const float *sun = F.sun;
  const float *light_mvp = F.light_mvp;
  const float *wind = F.wind;
  float sun_intensity = F.sun_intensity;
  bool atmosphere = F.atmosphere, textured = F.textured;
  bool wireframe = F.wireframe, cinematic = F.cinematic;
  bool clouds_ok = F.clouds_ok, shadows_ok = F.shadows_ok;
  bool heavy_maps = F.heavy_maps;
  // tile 0 follows its object's visibility; a further tile was checked by
  // the caller before its swap (terrain_tiles.hpp)
  bool show_terrain_obj = terrain_tile_current() > 0 ? true : F.show_terrain_obj;
  // terrain
  if (show_terrain_obj) {
    // Adaptive subdivision when the driver took the tessellated program and
    // the user has not turned it off; the fixed grid is always there as the
    // fallback, and both run the same placement code.
    const bool use_tess = tess_ok && RS.tessellation && prog_terrain_tess;
    const GLuint PT = use_tess ? prog_terrain_tess : prog_terrain;
    glUseProgram(PT);
    upload_scene_lights(PT, RS.height_scale);
    glUniformMatrix4fv(uniform_location(PT, "u_mvp"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(uniform_location(PT, "u_light_mvp"), 1, GL_FALSE,
                       light_mvp);
    uni1(PT, "u_hscale", RS.height_scale);
    upload_terrain_xform(PT);
    uni3(PT, "u_sun", sun);
    uni3(PT, "u_sun_color", RS.sun_color);
    uni1(PT, "u_sun_intensity", sun_intensity);
    uni3(PT, "u_sky_zenith", RS.sky_zenith);
    uni3(PT, "u_sky_horizon", RS.sky_horizon);
    uni1(PT, "u_ambient", RS.ambient_intensity);
    uni3(PT, "u_sky_light", sky_light_rgb());
    uni1(PT, "u_atmo", RS.atmosphere_density);
    uni3(PT, "u_cam", view_eye);
    upload_relief_lod(PT, F);
    uni1(PT, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(PT, "u_grade", g_grade);
    uni1(PT, "u_sat", g_saturation);
    uni1(PT, "u_texel", hm_w > 0 ? 1.f / hm_w : 1.f / 512.f);
    unii(PT, "u_has_albedo", (has_albedo && RS.use_albedo && textured) ? 1 : 0);
    unii(PT, "u_has_normal", (has_normal_map && textured && heavy_maps) ? 1 : 0);
    unii(PT, "u_has_rough", (has_rough_map && textured && heavy_maps) ? 1 : 0);
    unii(PT, "u_has_disp", (has_disp_map && RS.mat_displacement > 0) ? 1 : 0);
    uni1(PT, "u_disp_strength", RS.mat_displacement);
    // The micro-relief is a world-unit amount. On a small planet that would
    // dwarf the globe (12x the radius on a 1 m world), so it is capped at a
    // twentieth of the radius: a planet is rough, never spiky.
    uni1(PT, "u_frac_amount",
         RS.planet_radius > 0.f
             ? std::min(RS.fractal_detail, RS.planet_radius * 0.05f)
             : RS.fractal_detail);
    uni1(PT, "u_frac_scale", RS.fractal_scale);
    uni1(PT, "u_frac_gain", RS.fractal_gain);
    // zero when no graph is driving displacement, which also short-circuits
    // the stub call in both stages
    uni1(PT, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    // Every albedo source is gated on the view's shading mode. A surface
    // graph used not to be, so a scene carrying one ignored both the Textured
    // button and the "textured" checkbox entirely - the reported symptom was
    // that the texture could not be turned off at all.
    unii(PT, "u_textured", textured ? 1 : 0);
    // the placement's blend weight: where the tile gives way to the planet
    // its colour gives way to the planet's palette (planet_place.hpp)
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, tex_place_w);
    unii(PT, "u_place_w", 12);
    unii(PT, "u_place_on", has_place_w ? 1 : 0);
    {
      // the Objects tree's "colour by layer": this tile in its layer's colour
      float tint[3] = {1.f, 1.f, 1.f};
      const int obj = terrain_tile_object(terrain_tile_current() < 0 ? 0 : terrain_tile_current());
      if (scene().color_by_layer && obj >= 0 && obj < (int)scene().objects.size()) {
        const std::array<float, 3> c = scene_display_color(scene().objects[(size_t)obj]);
        tint[0] = c[0]; tint[1] = c[1]; tint[2] = c[2];
      }
      uni3(PT, "u_layer_tint", tint);
    }
    // ID colours: one flat bright colour per object or per material, so a
    // layer or an object is found by eye. The terrain's key is its material.
    {
      float key = 1.f;
      for (const SceneObject &o : scene().objects)
        if (o.type == SceneObject::Terrain && o.material_node)
          key = (float)(o.material_node % 1024);
      unii(PT, "u_id_mode", vc.display == 3 ? RS.id_mode + 1 : 0);
      uni1(PT, "u_id_key", key);
    }
    unii(PT, "u_surface_on",
         (!g_surface_glsl.empty() && textured && RS.use_albedo) ? 1 : 0);
    unii(PT, "u_surf_rough_on", (!g_rough_glsl.empty() && textured) ? 1 : 0);
    unii(PT, "u_surf_bump_on", (!g_bump_glsl.empty() && textured) ? 1 : 0);
    uni1(PT, "u_surf_bump_strength", g_surf_bump_strength);
    uni1(PT, "u_surf_bump_scale", g_surf_bump_scale);
    glUniform4f(uniform_location(PT, "u_brush"), g_brush[0],
                g_brush[1], g_brush[2], g_brush[3]);
    uni1(PT, "u_planet_radius", view_planet_radius(RS, vc));
    upload_world_shape(PT, tile_shape(RS)); // the face this tile stands on
    uni1(PT, "u_water_level", RS.show_water ? RS.water_level * RS.height_scale
                                            : 0.f);
    uni1(PT, "u_lat", std::fabs(RS.latitude) / 90.f);
    uni1(PT, "u_snow_line", 0.62f);
    unii(PT, "u_shadows", (shadows_ok && vc.display != 0) ? 1 : 0);
    unii(PT, "u_quality", cinematic ? 1 : 0);
    uni1(PT, "u_shadow_soft", RS.shadow_softness);
    {
      // the shadow map's texel width and depth range, for its slope-scaled
      // bias, and the relief only the view draws: the vertex micro-relief
      // (half its amplitude either way) and a material's displacement
      const float frac = RS.planet_radius > 0.f
                             ? std::min(RS.fractal_detail, RS.planet_radius * 0.05f)
                             : RS.fractal_detail;
      const float slack = 0.5f * std::fabs(frac) +
                          ((has_disp_map && RS.mat_displacement > 0) ? RS.mat_displacement : 0.f);
      glUniform3f(uniform_location(PT, "u_shadow_geom"), 2.f * SHADOW_HALF / 2048.f,
                  SHADOW_FAR - SHADOW_NEAR, slack);
    }
    renderer_material_uniforms(PT, RS.matp);
    upload_fog_uniforms(PT, RS, atmosphere);
    backdrop_bind(PT);
    bind_sky_env(PT); // the sky its reflection reads (renderer_clouds.cpp)
    unii(PT, "u_aov", g_aov);
    unii(PT, "u_object_id", 1);
    unii(PT, "u_cloud_shadows",
         (clouds_ok && atmosphere && cinematic) ? 1 : 0);
    uni1(PT, "u_cl_cov", RS.cloud_coverage);
    uni1(PT, "u_cl_alt", RS.cloud_altitude);
    uni1(PT, "u_cl_thick", RS.cloud_thickness);
    uni1(PT, "u_cl_time", cloud_time);
    uni1(PT, "u_cl_scale", RS.cloud_scale);
    uni1(PT, "u_cl_weather", RS.cloud_weather);
    uni1(PT, "u_cl_weather_scale", RS.cloud_weather_scale);
    glUniform2fv(uniform_location(PT, "u_cl_wind"), 1, wind);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_height);
    unii(PT, "u_height", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_albedo);
    unii(PT, "u_albedo", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    unii(PT, "u_shadowmap", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
    unii(PT, "u_cl_shape", 3);
    // the shadow is cast by the cloud that is there, painted shape and all
    cloud_shape_map_bind(PT, RS, CLOUD_SHAPE_MAP_UNIT);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, tex_normal);
    unii(PT, "u_normal_map", 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, tex_rough);
    unii(PT, "u_rough_map", 6);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, tex_disp);
    unii(PT, "u_disp", 7);
    if (RS.mat_transparency > 0.001f) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    bind_field_textures(PT);
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (use_tess) {
      float vp[2] = {(float)w, (float)h};
      glUniform2fv(uniform_location(PT, "u_viewport"), 1, vp);
      if (pass_timed(slot)) {
        g_view_w = w;
        g_view_h = h;
      }
      // Wireframe subdivides to one quad per patch. At the shading levels
      // (8 to 32 an edge over 64x64 patches) the wires land within a pixel or
      // two of each other and the mode is indistinguishable from solid -
      // measured at 1.6 % of pixels different, which is what "the wireframe
      // button does nothing" actually looked like.
      uni1(PT, "u_tess_px", wireframe ? 1e6f : RS.tess_pixels);
      uni1(PT, "u_tess_min", wireframe ? 1.f : RS.tess_min);
      uni1(PT, "u_tess_max", wireframe ? 1.f : std::max(1.f, RS.tess_max * perf_quality().tess_scale));
      // Per-patch frustum culling. Only trusted once the bounds have actually
      // been built from a heightmap: an unbound or empty bounds texture reads
      // as a zero-height patch everywhere, which would cull ground that is on
      // screen.
      const bool cull = RS.frustum_cull && !cpu_patch_bounds.empty();
      unii(PT, "u_cull_on", cull ? 1 : 0);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, tex_patch_bounds);
      unii(PT, "u_patch_bounds", 4);
      if (cull) {
        Frustum fr = frustum_from_mvp(mvp);
        glUniform4fv(uniform_location(PT, "u_frustum"), 6, &fr.p[0][0]);
        float pad = cull_pad(RS.mat_displacement,
                             has_disp_map && RS.mat_displacement > 0,
                             RS.fractal_detail,
                             g_field_glsl.empty() ? 0.f : RS.field_displacement);
        uni1(PT, "u_cull_pad", pad);
        uni3(PT, "u_cull_cam", view_eye);
        uni1(PT, "u_cull_radius", view_planet_radius(RS, vc));
        // The same test on the CPU, for the status readout: no readback, so
        // the number shown is the one the shader arrived at rather than an
        // estimate. Every tenth frame, because a readout does not need to be
        // recomputed 60 times a second and the shader is the thing that has
        // to be fast.
        static int stat_tick = 0;
        if (slot == 0 && (stat_tick++ % 10) == 0)
          g_patches_visible = patches_visible(fr, cpu_patch_bounds, patch_n - 1,
                                              RS.height_scale, pad, view_eye,
                                              view_planet_radius(RS, vc), tile_shape(RS));
      } else if (slot == 0) {
        g_patches_visible = -1;
      }
      glPatchParameteri(GL_PATCH_VERTICES, 4);
      glBindVertexArray(vao_patch);
      // Timed on the GPU, not on the clock: with vsync on, every frame is
      // 16.7 ms whatever the terrain costs, so wall time cannot tell whether
      // culling helped. Only the main view is timed — the other viewports
      // would interleave into the same measurement.
      if (pass_timed(slot)) {
        // Time and triangle count together. With a tessellation shader the
        // count is not knowable on the CPU - the tessellator decides it per
        // patch, per frame - and the time alone cannot say whether the pass
        // is geometry-bound or fragment-bound, which is the whole question
        // a finer geometry LOD has to answer before it is worth building.
        GpuTimer::Scope s(gpu_timer("terrain"));
        GpuCounter::Scope c(gpu_counter("terrain"));
        glDrawElements(GL_PATCHES, patch_index_count, GL_UNSIGNED_INT, nullptr);
      } else {
        glDrawElements(GL_PATCHES, patch_index_count, GL_UNSIGNED_INT, nullptr);
      }
    } else {
      glBindVertexArray(vao_grid);
      if (pass_timed(slot)) {
        GpuTimer::Scope s(gpu_timer("terrain"));
        GpuCounter::Scope c(gpu_counter("terrain"));
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
      } else {
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
      }
    }
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (RS.mat_transparency > 0.001f) glDisable(GL_BLEND);
  }
}

// Every tile: the first with the renderer's own set, each further one with
// its set swapped in for the draw.
void pass_terrain(const FrameCtx &F) {
  if (terrain_tile_object(0) >= 0) { // no Terrain object at all: nothing to draw as tile 0
    TileSwap swap(0);
    draw_terrain_tile(F);
  }
  for (int k = 1; k < terrain_tile_count(); ++k) {
    if (!terrain_tile_visible(k)) continue;
    TileSwap swap(k);
    draw_terrain_tile(F);
  }
}

// The water pass is renderer_water.cpp.

void pass_outlines(const FrameCtx &F) {
  if (g_aov != 0) return; // decoration, never part of a pass
  RenderSettings &RS = F.RS;
  const RenderSettings::ViewConfig &vc = F.vc;
  const float *mvp = F.mvp;
  const float *sun = F.sun;
  bool sun_on = F.sun_on;
  int sel_type = F.sel_type;
  SceneState &sc = scene();
  // selection outlines; the water has no box - it is the whole sea, and it
  // tints itself when selected (renderer_water.cpp)
  if (sel_type >= 0 && vc.outlines) {
    float orange[4] = {1.f, 0.55f, 0.15f, 0.95f};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (sel_type == SceneObject::Terrain) {
      // the tile's box through its own transform (terrain_xform.hpp), so
      // the outline is the tile the viewer sees
      const TerrainXform tx = terrain_xform_of(sc.objects[(size_t)sc.selected], RS.height_scale);
      const float hs = RS.height_scale;
      float c[8][3] = {{0,0,0},{1,0,0},{1,0,1},{0,0,1},{0,hs,0},{1,hs,0},{1,hs,1},{0,hs,1}};
      for (auto &k : c) terrain_xform_apply(tx, k);
      draw_box_corners(mvp, c, orange);
    } else if (sel_type == SceneObject::Mesh) {
      const SceneObject &o = sc.objects[sc.selected];
      float r = scene_object_radius(o) * 0.62f;
      float top = o.scale * std::fabs(o.scl[1]);
      draw_box_outline(mvp, o.pos[0] - r, o.pos[1] * RS.height_scale,
                       o.pos[2] - r, o.pos[0] + r,
                       o.pos[1] * RS.height_scale + top, o.pos[2] + r, orange);
    } else if (sel_type == SceneObject::Sun && sun_on) {
      float gd = 1.9f, rr = 0.085f;
      float gx = 0.5f + sun[0] * gd, gy = RS.height_scale + sun[1] * gd,
            gz = 0.5f + sun[2] * gd;
      draw_box_outline(mvp, gx - rr, gy - rr, gz - rr, gx + rr, gy + rr, gz + rr,
                       orange);
    }
    glDisable(GL_BLEND);
  }
}

} // namespace studio
