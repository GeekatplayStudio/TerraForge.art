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
#include "terrain_xform.hpp"
#include "terrain_tiles.hpp"
#include "planet_place.hpp"
#include "world_shape.hpp"
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

// The water's look, into any program carrying WATER_FN_GLSL: the tile's
// water pass and the planet surround, so a lake crossing the tile's border
// is one lake.
void upload_water_uniforms(unsigned prog, const RenderSettings &RS, float time) {
  uni1(prog, "u_w_time", time);
  uni1(prog, "u_w_wave_amp", RS.water_wave_amp);
  uni1(prog, "u_w_wave_scale", RS.water_wave_scale);
  uni1(prog, "u_w_wave_speed", RS.water_wave_speed);
  uni1(prog, "u_w_clarity", RS.water_clarity);
  uni1(prog, "u_w_opacity", RS.water_opacity);
  uni1(prog, "u_w_roughness", RS.mat_roughness * 0.2f);
  uni1(prog, "u_w_reflection", RS.mat_reflection + 0.4f);
  uni1(prog, "u_w_atmo", RS.atmosphere_density);
  uni3(prog, "u_w_deep", RS.water_deep_color);
  uni3(prog, "u_w_shallow", RS.water_shallow_color);
  unii(prog, "u_w_foam_on", RS.water_foam ? 1 : 0);
  uni3(prog, "u_w_foam_color", RS.foam_color);
  uni1(prog, "u_w_foam_amount", RS.foam_amount);
  uni1(prog, "u_w_foam_scale", RS.foam_scale);
  uni1(prog, "u_w_foam_crests", RS.foam_crests);
}

void upload_world_shape(unsigned prog, const gpx::planet::Shape &S) {
  glUniform4f(uniform_location(prog, "u_world_shape"), S.flat_x ? 1.f : 0.f,
              S.flat_z ? 1.f : 0.f, S.inside ? 1.f : 0.f, S.flip ? 1.f : 0.f);
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
    uni3(prog_depth, "u_lod_cam", F.view_eye);
    uni1(prog_depth, "u_height_lod_k", RS.terrain_lod * 32.f);
    uni1(prog_depth, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    bind_field_textures(prog_depth);
    glBindVertexArray(vao_grid);
    for (int k = 0; k < terrain_tile_count(); ++k) {
      if (k > 0 && !terrain_tile_visible(k)) continue;
      if (k == 0 && terrain_tile_object(0) < 0) continue;
      TileSwap swap(k);
      upload_terrain_xform(prog_depth);
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
  const RenderSettings::ViewConfig &vc = F.vc;
  int slot = F.slot;
  const float *inv_vp = F.inv_vp;
  const float *view_eye = F.view_eye;
  const float *sun = F.sun;
  const float *wind = F.wind;
  float sun_intensity = F.sun_intensity;
  float space_t = F.space_t;
  bool atmosphere = F.atmosphere, cinematic = F.cinematic;
  bool clouds_ok = F.clouds_ok;
  // background
  if (atmosphere && RS.background_mode == 0) {
    glUseProgram(prog_sky);
    glDepthMask(GL_FALSE);
    glUniformMatrix4fv(uniform_location(prog_sky, "u_inv_vp"), 1, GL_FALSE,
                       inv_vp);
    uni3(prog_sky, "u_cam", view_eye);
    uni3(prog_sky, "u_sun", sun);
    uni3(prog_sky, "u_sun_color", RS.sun_color);
    uni1(prog_sky, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_sky, "u_grade", g_grade);
    uni1(prog_sky, "u_sat", g_saturation);
    uni3(prog_sky, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_sky, "u_sky_horizon", RS.sky_horizon);
    uni1(prog_sky, "u_atmo", RS.atmosphere_density);
    unii(prog_sky, "u_fog_type", RS.fog_type);
    uni3(prog_sky, "u_fog_color", RS.fog_color);
    uni1(prog_sky, "u_fog_density", RS.fog_density);
    // clouds
    unii(prog_sky, "u_clouds", clouds_ok ? 1 : 0);
    const int cq = std::min(RS.cloud_quality, perf_quality().cloud_quality_cap);
    int steps = cq == 0 ? 24 : (cq == 2 ? 72 : 44);
    if (cinematic) steps = (int)(steps * 1.5f);
    unii(prog_sky, "u_cl_steps", steps);
    unii(prog_sky, "u_cl_type", RS.cloud_type);
    uni1(prog_sky, "u_sun_intensity", sun_intensity);
    unii(prog_sky, "u_panorama", 0);
    unii(prog_sky, "u_hdr", 0);
    unii(prog_sky, "u_no_sun", 0);
    {
      float space = vc.camera == 0 ? space_t : 0.f;
      // an inside world's air is a layer on its ground: the sky over it
      // thins to space, and beyond a ring's rim there is nothing but stars
      // (world_shape.hpp); the far shell covers the rest
      if (RS.world_inside) space = std::max(space, 0.75f);
      uni1(prog_sky, "u_space", space);
    }
    uni1(prog_sky, "u_cl_cov", RS.cloud_coverage);
    uni1(prog_sky, "u_cl_den", RS.cloud_density);
    uni1(prog_sky, "u_cl_alt", RS.cloud_altitude);
    uni1(prog_sky, "u_cl_thick", RS.cloud_thickness);
    uni1(prog_sky, "u_cl_detail_amt", RS.cloud_detail);
    uni1(prog_sky, "u_cl_time", cloud_time);
    uni1(prog_sky, "u_cl_ambient", RS.cloud_ambient);
    uni1(prog_sky, "u_cl_anvil", RS.cloud_anvil);
    unii(prog_sky, "u_cl2", RS.cloud2_on ? 1 : 0);
    unii(prog_sky, "u_cl2_type", RS.cloud2_type);
    uni1(prog_sky, "u_cl2_cov", RS.cloud2_coverage);
    uni1(prog_sky, "u_cl2_den", RS.cloud2_density);
    uni1(prog_sky, "u_cl2_alt", RS.cloud2_altitude);
    uni1(prog_sky, "u_cl2_thick", RS.cloud2_thickness);
    {
      // the extra layers from CloudLayer nodes, as arrays
      const int n = std::min((int)RS.cloud_layers.size(), RenderSettings::MAX_CLOUD_LAYERS);
      int types[RenderSettings::MAX_CLOUD_LAYERS] = {};
      float cov[RenderSettings::MAX_CLOUD_LAYERS] = {}, den[RenderSettings::MAX_CLOUD_LAYERS] = {},
            alt[RenderSettings::MAX_CLOUD_LAYERS] = {}, thick[RenderSettings::MAX_CLOUD_LAYERS] = {};
      for (int i = 0; i < n; ++i) {
        const auto &L = RS.cloud_layers[(size_t)i];
        types[i] = L.type; cov[i] = L.coverage; den[i] = L.density; alt[i] = L.altitude; thick[i] = L.thickness;
      }
      unii(prog_sky, "u_clx_n", n);
      glUniform1iv(uniform_location(prog_sky, "u_clx_type"), RenderSettings::MAX_CLOUD_LAYERS, types);
      glUniform1fv(uniform_location(prog_sky, "u_clx_cov"), RenderSettings::MAX_CLOUD_LAYERS, cov);
      glUniform1fv(uniform_location(prog_sky, "u_clx_den"), RenderSettings::MAX_CLOUD_LAYERS, den);
      glUniform1fv(uniform_location(prog_sky, "u_clx_alt"), RenderSettings::MAX_CLOUD_LAYERS, alt);
      glUniform1fv(uniform_location(prog_sky, "u_clx_thick"), RenderSettings::MAX_CLOUD_LAYERS, thick);
    }
    glUniform2fv(uniform_location(prog_sky, "u_cl_wind"), 1, wind);
    uni3(prog_sky, "u_cl_color", RS.cloud_color);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_shape);
    unii(prog_sky, "u_cl_shape", 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_3D, tex_cloud_detail);
    unii(prog_sky, "u_cl_detail", 4);
    // The ray-march dither. An unbound sampler reads black, which would give
    // every ray the same zero offset and put the banding straight back.
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, blue_noise_texture());
    unii(prog_sky, "u_blue_noise", 9);
    unii(prog_sky, "u_cl_octaves", std::clamp(RS.cloud_scatter_octaves, 1, 4));
    uni1(prog_sky, "u_cl_ms_depth", std::clamp(RS.cloud_scatter_depth, 0.05f, 0.99f));
    backdrop_bind(prog_sky);
    unii(prog_sky, "u_aov", g_aov);
    glBindVertexArray(vao_quad);
    if (slot == 0) {
      // Timed for the same reason the terrain pass is: the volumetric march
      // is the most expensive thing on screen and the frame clock cannot see
      // it move.
      GpuTimer::Scope s(gpu_timer("sky+clouds"));
      glDrawArrays(GL_TRIANGLES, 0, 3);
    } else {
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
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
    uni1(PT, "u_atmo", RS.atmosphere_density);
    uni3(PT, "u_cam", view_eye);
    uni3(PT, "u_lod_cam", view_eye);
    uni1(PT, "u_height_lod_k", RS.terrain_lod * 32.f);
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
    renderer_material_uniforms(PT, RS.matp);
    upload_fog_uniforms(PT, RS, atmosphere);
    backdrop_bind(PT);
    unii(PT, "u_aov", g_aov);
    unii(PT, "u_object_id", 1);
    unii(PT, "u_cloud_shadows",
         (clouds_ok && atmosphere && cinematic) ? 1 : 0);
    uni1(PT, "u_cl_cov", RS.cloud_coverage);
    uni1(PT, "u_cl_alt", RS.cloud_altitude);
    uni1(PT, "u_cl_thick", RS.cloud_thickness);
    uni1(PT, "u_cl_time", cloud_time);
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
      if (slot == 0) {
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
      if (slot == 0) {
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
      if (slot == 0) {
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

void pass_water(const FrameCtx &F) {
  RenderSettings &RS = F.RS;
  const RenderSettings::ViewConfig &vc = F.vc;
  const float *mvp = F.mvp;
  const float *sun = F.sun;
  const float *view_eye = F.view_eye;
  float time_acc = F.time_acc;
  bool show_water_obj = F.show_water_obj;
  // water
  if (RS.show_water && vc.show_water_view && show_water_obj) {
    glUseProgram(prog_water);
    upload_terrain_xform(prog_water); // the plane is the tile's: it goes where the tile goes
    // A geometry pass wants the water surface, not a blend of it with the
    // bed underneath; only the picture and the linear beauty are translucent.
    const bool blend = g_aov == 0 || g_aov == AOV_BEAUTY_LINEAR;
    if (blend) glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    upload_fog_uniforms(prog_water, RS, F.atmosphere);
    backdrop_bind(prog_water);
    unii(prog_water, "u_aov", g_aov);
    unii(prog_water, "u_object_id", 2);
    glUniformMatrix4fv(uniform_location(prog_water, "u_mvp"), 1, GL_FALSE, mvp);
    uni1(prog_water, "u_hscale", RS.height_scale);
    uni1(prog_water, "u_level", RS.water_level * RS.height_scale);
    uni1(prog_water, "u_planet_radius", view_planet_radius(RS, vc));
    uni3(prog_water, "u_sun", sun);
    uni3(prog_water, "u_sun_color", RS.sun_color);
    uni3(prog_water, "u_cam", view_eye);
    uni1(prog_water, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_water, "u_grade", g_grade);
    uni1(prog_water, "u_sat", g_saturation);
    uni3(prog_water, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_water, "u_sky_horizon", RS.sky_horizon);
    upload_water_uniforms(prog_water, RS, time_acc);
    glBindVertexArray(vao_grid);
    for (int k = 0; k < terrain_tile_count(); ++k) {
      if (k > 0 && !terrain_tile_visible(k)) continue;
      if (k == 0 && terrain_tile_object(0) < 0) continue;
      TileSwap swap(k);
      upload_terrain_xform(prog_water);
      upload_world_shape(prog_water, tile_shape(RS)); // the water lies on the tile's face
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, tex_height);
      unii(prog_water, "u_height", 0);
      glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    }
    if (blend) glDisable(GL_BLEND);
  }
}

void pass_outlines(const FrameCtx &F) {
  if (g_aov != 0) return; // decoration, never part of a pass
  RenderSettings &RS = F.RS;
  const RenderSettings::ViewConfig &vc = F.vc;
  const float *mvp = F.mvp;
  const float *sun = F.sun;
  bool sun_on = F.sun_on;
  int sel_type = F.sel_type;
  SceneState &sc = scene();
  // selection outlines
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
    } else if (sel_type == SceneObject::Water) {
      float lv = RS.water_level * RS.height_scale;
      draw_box_outline(mvp, 0.f, lv - 0.001f, 0.f, 1.f, lv + 0.001f, 1.f, orange);
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
