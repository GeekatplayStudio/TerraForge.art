// Geekatplay TerraForge - drawing one frame of the scene: terrain (fixed
// grid or tessellated), water, sky, clouds, meshes, planets, the surround,
// shadows, outlines and the grid. Split from renderer.cpp for the 500-line
// module rule; state lives in renderer_internal.hpp.
#include "renderer_internal.hpp"
#include "app.hpp"
#include "console.hpp"
#include "cloud_noise.hpp"
#include "planet_renderer.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stb_image_write.h"
#include "renderer_shaders.hpp"

namespace studio {


void build_light_mvp(const float *sun, float hscale, float *out) {
  float cx = 0.5f, cy = hscale * 0.5f, cz = 0.5f;
  float eye[3] = {cx + sun[0] * 2.f, cy + sun[1] * 2.f, cz + sun[2] * 2.f};
  float fz[3] = {cx - eye[0], cy - eye[1], cz - eye[2]};
  float fl = std::sqrt(fz[0] * fz[0] + fz[1] * fz[1] + fz[2] * fz[2]);
  for (float &v : fz) v /= fl;
  float upw[3] = {0, 1, 0};
  if (std::fabs(fz[1]) > 0.99f) { upw[0] = 1; upw[1] = 0; }
  float sx[3] = {fz[1] * upw[2] - fz[2] * upw[1], fz[2] * upw[0] - fz[0] * upw[2],
                 fz[0] * upw[1] - fz[1] * upw[0]};
  float sl = std::sqrt(sx[0] * sx[0] + sx[1] * sx[1] + sx[2] * sx[2]);
  for (float &v : sx) v /= sl;
  float uy[3] = {sx[1] * fz[2] - sx[2] * fz[1], sx[2] * fz[0] - sx[0] * fz[2],
                 sx[0] * fz[1] - sx[1] * fz[0]};
  float view[16] = {sx[0], uy[0], -fz[0], 0, sx[1], uy[1], -fz[1], 0,
                    sx[2], uy[2], -fz[2], 0,
                    -(sx[0] * eye[0] + sx[1] * eye[1] + sx[2] * eye[2]),
                    -(uy[0] * eye[0] + uy[1] * eye[1] + uy[2] * eye[2]),
                    fz[0] * eye[0] + fz[1] * eye[1] + fz[2] * eye[2], 1};
  float r = 0.95f, znear = 0.1f, zfar = 4.5f;
  float proj[16] = {1.f / r, 0, 0, 0, 0, 1.f / r, 0, 0,
                    0, 0, -2.f / (zfar - znear), 0,
                    0, 0, -(zfar + znear) / (zfar - znear), 1};
  mat_mul(out, proj, view);
}


void draw_box_outline(const float *mvp, float x0, float y0, float z0,
                             float x1, float y1, float z1, const float *rgba) {
  float c[8][3] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},
                   {x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}};
  static const int E[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                               {0,4},{1,5},{2,6},{3,7}};
  std::vector<float> v;
  for (auto &e : E) {
    for (int k = 0; k < 3; ++k) v.push_back(c[e[0]][k]);
    for (int k = 0; k < 3; ++k) v.push_back(c[e[1]][k]);
  }
  glBindVertexArray(vao_dyn);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
  glBufferSubData(GL_ARRAY_BUFFER, 0, v.size() * 4, v.data());
  glUseProgram(prog_lines);
  glUniformMatrix4fv(glGetUniformLocation(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
  glUniform4fv(glGetUniformLocation(prog_lines, "u_color"), 1, rgba);
  glLineWidth(2.f);
  glDrawArrays(GL_LINES, 0, (int)(v.size() / 3));
  glLineWidth(1.f);
}


void draw_scene(int slot, const RenderSettings::ViewConfig &vc, int w,
                       int h, float time_acc, const float *view_eye,
                       const float *mvp, const float *inv_vp) {
  RenderSettings &RS = render_settings();
  float sun[3];
  compute_sun_dir(RS, sun);
  bool atmosphere = vc.atmosphere;
  bool textured = vc.display == 2;
  bool wireframe = vc.display == 0 || RS.wireframe;
  bool cinematic = RS.viewport_engine == 1 && vc.camera == 0;

  SceneState &sc = scene();
  bool show_terrain_obj = true, show_water_obj = true, sun_on = true;
  int sel_type = -1;
  for (size_t i = 0; i < sc.objects.size(); ++i) {
    const SceneObject &o = sc.objects[i];
    bool vis = sc.object_visible(o);
    if (o.type == SceneObject::Terrain) show_terrain_obj = vis;
    else if (o.type == SceneObject::Water) show_water_obj = vis;
    else if (o.type == SceneObject::Sun) sun_on = vis;
    else if (o.type == SceneObject::Atmosphere) atmosphere = atmosphere && vis;
    if ((int)i == sc.selected) sel_type = o.type;
  }
  float sun_intensity = sun_on ? RS.sun_intensity : 0.05f;

  // ---- progressive quality: features that only matter near the ground are
  // shut off as the camera pulls away, with hysteresis so nothing flickers
  // at the threshold. This is where the frame budget for planets comes from.
  float tile_dx = view_eye[0] - 0.5f, tile_dz = view_eye[2] - 0.5f;
  float tile_dist = std::sqrt(tile_dx * tile_dx + view_eye[1] * view_eye[1] +
                              tile_dz * tile_dz);
  static bool far_tier[6] = {false};
  if (!far_tier[slot] && tile_dist > 9.f) far_tier[slot] = true;
  else if (far_tier[slot] && tile_dist < 7.f) far_tier[slot] = false;
  bool near_ground = !far_tier[slot];
  // volumetric clouds are a ground-view effect; from high above they cost a
  // full raymarch for a few pixels
  bool clouds_ok = RS.clouds_on && view_eye[1] < 3.f && near_ground;
  bool shadows_ok = RS.shadows && near_ground; // shadow texels vanish out there
  bool heavy_maps = near_ground; // 4K material maps are wasted on a far tile
  // how far out of the atmosphere the camera is (0 ground .. 1 open space);
  // smooth, so the sky thins continuously as you pull back
  float space_t = std::clamp((tile_dist - 6.f) / 22.f, 0.f, 1.f);
  space_t = space_t * space_t * (3.f - 2.f * space_t);

  // shadow pass
  float light_mvp[16];
  build_light_mvp(sun, RS.height_scale, light_mvp);
  if (shadows_ok) {
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
    glViewport(0, 0, SHADOW_RES, SHADOW_RES);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(prog_depth);
    glUniformMatrix4fv(glGetUniformLocation(prog_depth, "u_light_mvp"), 1, GL_FALSE,
                       light_mvp);
    uni1(prog_depth, "u_hscale", RS.height_scale);
    uni1(prog_depth, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    bind_field_textures(prog_depth);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_height);
    unii(prog_depth, "u_height", 0);
    glBindVertexArray(vao_grid);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, fbo[slot]);
  glViewport(0, 0, w, h);
  glClearColor(RS.bg_color[0], RS.bg_color[1], RS.bg_color[2], 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  float wind_rad = RS.cloud_wind_dir * 0.017453293f;
  float wind[2] = {std::cos(wind_rad) * RS.cloud_wind_speed,
                   std::sin(wind_rad) * RS.cloud_wind_speed};

  // background
  if (atmosphere && RS.background_mode == 0) {
    glUseProgram(prog_sky);
    glDepthMask(GL_FALSE);
    glUniformMatrix4fv(glGetUniformLocation(prog_sky, "u_inv_vp"), 1, GL_FALSE,
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
    int steps = RS.cloud_quality == 0 ? 24 : (RS.cloud_quality == 2 ? 72 : 44);
    if (cinematic) steps = (int)(steps * 1.5f);
    unii(prog_sky, "u_cl_steps", steps);
    unii(prog_sky, "u_cl_type", RS.cloud_type);
    uni1(prog_sky, "u_sun_intensity", sun_intensity);
    unii(prog_sky, "u_panorama", 0);
    unii(prog_sky, "u_hdr", 0);
    unii(prog_sky, "u_no_sun", 0);
    uni1(prog_sky, "u_space", vc.camera == 0 ? space_t : 0.f);
    uni1(prog_sky, "u_cl_cov", RS.cloud_coverage);
    uni1(prog_sky, "u_cl_den", RS.cloud_density);
    uni1(prog_sky, "u_cl_alt", RS.cloud_altitude);
    uni1(prog_sky, "u_cl_thick", RS.cloud_thickness);
    uni1(prog_sky, "u_cl_detail_amt", RS.cloud_detail);
    uni1(prog_sky, "u_cl_time", cloud_time);
    uni1(prog_sky, "u_cl_ambient", RS.cloud_ambient);
    uni1(prog_sky, "u_cl_anvil", RS.cloud_anvil);
    glUniform2fv(glGetUniformLocation(prog_sky, "u_cl_wind"), 1, wind);
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

  // planets: drawn between the sky and the ground, so terrain in front of
  // them occludes correctly and they are never clipped however far you zoom
  if (vc.camera == 0) {
    PlanetFrame pf;
    pf.mvp = mvp;
    pf.eye = view_eye;
    pf.sun = sun;
    pf.sun_intensity = sun_intensity;
    pf.exposure = RS.exposure * g_exposure_mult;
    pf.grade = g_grade;
    pf.saturation = g_saturation;
    pf.view_h = h;
    pf.fovy_rad = g_last_fovy;
    planet_draw_all(pf);
  }

  // terrain
  if (show_terrain_obj) {
    // Adaptive subdivision when the driver took the tessellated program and
    // the user has not turned it off; the fixed grid is always there as the
    // fallback, and both run the same placement code.
    const bool use_tess = tess_ok && RS.tessellation && prog_terrain_tess;
    const GLuint PT = use_tess ? prog_terrain_tess : prog_terrain;
    glUseProgram(PT);
    glUniformMatrix4fv(glGetUniformLocation(PT, "u_mvp"), 1, GL_FALSE, mvp);
    glUniformMatrix4fv(glGetUniformLocation(PT, "u_light_mvp"), 1, GL_FALSE,
                       light_mvp);
    uni1(PT, "u_hscale", RS.height_scale);
    uni3(PT, "u_sun", sun);
    uni3(PT, "u_sun_color", RS.sun_color);
    uni1(PT, "u_sun_intensity", sun_intensity);
    uni3(PT, "u_sky_zenith", RS.sky_zenith);
    uni3(PT, "u_sky_horizon", RS.sky_horizon);
    uni1(PT, "u_ambient", RS.ambient_intensity);
    uni1(PT, "u_atmo", RS.atmosphere_density);
    uni3(PT, "u_cam", view_eye);
    uni1(PT, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(PT, "u_grade", g_grade);
    uni1(PT, "u_sat", g_saturation);
    uni1(PT, "u_texel", hm_w > 0 ? 1.f / hm_w : 1.f / 512.f);
    unii(PT, "u_has_albedo", (has_albedo && RS.use_albedo && textured) ? 1 : 0);
    unii(PT, "u_has_normal", (has_normal_map && textured && heavy_maps) ? 1 : 0);
    unii(PT, "u_has_rough", (has_rough_map && textured && heavy_maps) ? 1 : 0);
    unii(PT, "u_has_disp", (has_disp_map && RS.mat_displacement > 0) ? 1 : 0);
    uni1(PT, "u_disp_strength", RS.mat_displacement);
    uni1(PT, "u_frac_amount", RS.fractal_detail);
    uni1(PT, "u_frac_scale", RS.fractal_scale);
    // zero when no graph is driving displacement, which also short-circuits
    // the stub call in both stages
    uni1(PT, "u_field_strength",
         g_field_glsl.empty() ? 0.f : RS.field_displacement);
    unii(PT, "u_surface_on", g_surface_glsl.empty() ? 0 : 1);
    unii(PT, "u_surf_rough_on", g_rough_glsl.empty() ? 0 : 1);
    unii(PT, "u_surf_bump_on", g_bump_glsl.empty() ? 0 : 1);
    uni1(PT, "u_surf_bump_strength", g_surf_bump_strength);
    uni1(PT, "u_surf_bump_scale", g_surf_bump_scale);
    glUniform4f(glGetUniformLocation(PT, "u_brush"), g_brush[0],
                g_brush[1], g_brush[2], g_brush[3]);
    uni1(PT, "u_planet_radius", RS.planet_radius);
    unii(PT, "u_shadows", (shadows_ok && vc.display != 0) ? 1 : 0);
    unii(PT, "u_quality", cinematic ? 1 : 0);
    uni1(PT, "u_shadow_soft", RS.shadow_softness);
    uni1(PT, "u_roughness", RS.mat_roughness);
    uni1(PT, "u_metallic", RS.mat_metallic);
    uni1(PT, "u_specular", RS.mat_specular);
    uni1(PT, "u_reflection", RS.mat_reflection);
    uni1(PT, "u_translucency", RS.mat_translucency);
    uni1(PT, "u_transparency", RS.mat_transparency);
    uni1(PT, "u_normal_strength", RS.mat_normal_strength);
    unii(PT, "u_fog_type", atmosphere ? RS.fog_type : 0);
    uni1(PT, "u_fog_density", RS.fog_density);
    uni1(PT, "u_fog_level", RS.fog_level);
    uni1(PT, "u_fog_falloff", RS.fog_falloff);
    uni3(PT, "u_fog_color", RS.fog_color);
    uni3(PT, "u_absorb", RS.absorption_color);
    uni1(PT, "u_fog_scatter", RS.fog_sun_scatter);
    unii(PT, "u_cloud_shadows",
         (clouds_ok && atmosphere && cinematic) ? 1 : 0);
    uni1(PT, "u_cl_cov", RS.cloud_coverage);
    uni1(PT, "u_cl_alt", RS.cloud_altitude);
    uni1(PT, "u_cl_thick", RS.cloud_thickness);
    uni1(PT, "u_cl_time", cloud_time);
    glUniform2fv(glGetUniformLocation(PT, "u_cl_wind"), 1, wind);
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
      glUniform2fv(glGetUniformLocation(PT, "u_viewport"), 1, vp);
      uni1(PT, "u_tess_px", RS.tess_pixels);
      uni1(PT, "u_tess_min", RS.tess_min);
      uni1(PT, "u_tess_max", RS.tess_max);
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
        glUniform4fv(glGetUniformLocation(PT, "u_frustum"), 6, &fr.p[0][0]);
        float pad = cull_pad(RS.mat_displacement,
                             has_disp_map && RS.mat_displacement > 0,
                             RS.fractal_detail,
                             g_field_glsl.empty() ? 0.f : RS.field_displacement);
        uni1(PT, "u_cull_pad", pad);
        uni3(PT, "u_cull_cam", view_eye);
        uni1(PT, "u_cull_radius", RS.planet_radius);
        // The same test on the CPU, for the status readout: no readback, so
        // the number shown is the one the shader arrived at rather than an
        // estimate. Every tenth frame, because a readout does not need to be
        // recomputed 60 times a second and the shader is the thing that has
        // to be fast.
        static int stat_tick = 0;
        if (slot == 0 && (stat_tick++ % 10) == 0)
          g_patches_visible = patches_visible(fr, cpu_patch_bounds, patch_n - 1,
                                              RS.height_scale, pad, view_eye,
                                              RS.planet_radius);
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
        GpuTimer::Scope s(gpu_timer("terrain"));
        glDrawElements(GL_PATCHES, patch_index_count, GL_UNSIGNED_INT, nullptr);
      } else {
        glDrawElements(GL_PATCHES, patch_index_count, GL_UNSIGNED_INT, nullptr);
      }
    } else {
      glBindVertexArray(vao_grid);
      if (slot == 0) {
        GpuTimer::Scope s(gpu_timer("terrain"));
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
      } else {
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
      }
    }
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (RS.mat_transparency > 0.001f) glDisable(GL_BLEND);
  }

  // infinite ground: the tile continues procedurally to the horizon when
  // root-level InfiniteSurface layers exist (drawn after the tile so its
  // depth wins in the overlap band; the surround discards inside the tile)
  if (vc.camera == 0 && show_terrain_obj && infinite_layers_present()) {
    InfiniteFrame inf;
    inf.mvp = mvp;
    inf.eye = view_eye;
    inf.sun = sun;
    inf.sun_color = RS.sun_color;
    inf.sun_intensity = sun_intensity;
    inf.exposure = RS.exposure * g_exposure_mult;
    inf.grade = g_grade;
    inf.saturation = g_saturation;
    inf.ambient = RS.ambient_intensity;
    inf.sky_zenith = RS.sky_zenith;
    inf.sky_horizon = RS.sky_horizon;
    inf.tex_height = tex_height;
    inf.tex_albedo = (has_albedo && RS.use_albedo && textured) ? tex_albedo : 0;
    inf.height_scale = RS.height_scale;
    inf.base_height = g_terrain_mean * RS.height_scale;
    inf.planet_radius = RS.planet_radius;
    inf.fog_type = atmosphere ? RS.fog_type : 0;
    inf.fog_density = RS.fog_density;
    inf.fog_color = RS.fog_color;
    infinite_draw(inf);
  }

  // scene meshes
  for (SceneObject &o : sc.objects) {
    if (o.type != SceneObject::Mesh || !sc.object_visible(o)) continue;
    if (o.gpu_dirty) {
      if (!o.vao) {
        glGenVertexArrays(1, &o.vao);
        glGenBuffers(1, &o.vbo);
      }
      glBindVertexArray(o.vao);
      glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
      glBufferData(GL_ARRAY_BUFFER, o.verts.size() * 4, o.verts.data(),
                   GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
      glBindVertexArray(0);
      o.gpu_dirty = false;
    }
    bool is_sel = (&o - sc.objects.data()) == sc.selected;
    glUseProgram(prog_mesh);
    glUniformMatrix4fv(glGetUniformLocation(prog_mesh, "u_mvp"), 1, GL_FALSE, mvp);
    float model[16], nrm[9];
    scene_object_matrix(o, RS.height_scale, model, nrm);
    glUniformMatrix4fv(glGetUniformLocation(prog_mesh, "u_model"), 1, GL_FALSE,
                       model);
    glUniformMatrix3fv(glGetUniformLocation(prog_mesh, "u_nrm"), 1, GL_FALSE,
                       nrm);
    uni3(prog_mesh, "u_color", o.color);
    uni3(prog_mesh, "u_sun", sun);
    uni3(prog_mesh, "u_sun_color", RS.sun_color);
    uni1(prog_mesh, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_mesh, "u_grade", g_grade);
    uni1(prog_mesh, "u_sat", g_saturation);
    unii(prog_mesh, "u_selected", is_sel ? 1 : 0);
    glBindVertexArray(o.vao);
    glDrawArrays(GL_TRIANGLES, 0, o.vert_count);
  }

  // sun gizmo (a real, selectable scene object)
  if (sun_on) {
    float gd = 1.9f;
    float gpos[3] = {0.5f + sun[0] * gd, RS.height_scale + sun[1] * gd,
                     0.5f + sun[2] * gd};
    float radius = 0.055f;
    glUseProgram(prog_gizmo);
    glUniformMatrix4fv(glGetUniformLocation(prog_gizmo, "u_mvp"), 1, GL_FALSE, mvp);
    glUniform4f(glGetUniformLocation(prog_gizmo, "u_xform"), gpos[0], gpos[1],
                gpos[2], radius);
    float sun_col[3] = {RS.sun_color[0] * 1.4f, RS.sun_color[1] * 1.3f,
                        RS.sun_color[2] * 0.9f};
    uni3(prog_gizmo, "u_color", sun_col);
    unii(prog_gizmo, "u_selected", sel_type == SceneObject::Sun ? 1 : 0);
    glBindVertexArray(vao_sphere);
    glDrawArrays(GL_TRIANGLES, 0, sphere_verts);
  }

  // reference grid
  if (vc.grid) {
    glUseProgram(prog_lines);
    glUniformMatrix4fv(glGetUniformLocation(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
    glUniform4f(glGetUniformLocation(prog_lines, "u_color"), 0.7f, 0.7f, 0.72f, 0.35f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vao_lines);
    glDrawArrays(GL_LINES, 0, line_vert_count);
    glDisable(GL_BLEND);
  }

  // water
  if (RS.show_water && vc.show_water_view && show_water_obj) {
    glUseProgram(prog_water);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniformMatrix4fv(glGetUniformLocation(prog_water, "u_mvp"), 1, GL_FALSE, mvp);
    uni1(prog_water, "u_hscale", RS.height_scale);
    uni1(prog_water, "u_level", RS.water_level * RS.height_scale);
    uni3(prog_water, "u_sun", sun);
    uni3(prog_water, "u_sun_color", RS.sun_color);
    uni3(prog_water, "u_cam", view_eye);
    uni1(prog_water, "u_time", time_acc);
    uni1(prog_water, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_water, "u_grade", g_grade);
    uni1(prog_water, "u_sat", g_saturation);
    uni3(prog_water, "u_deep", RS.water_deep_color);
    uni3(prog_water, "u_shallow", RS.water_shallow_color);
    uni1(prog_water, "u_wave_amp", RS.water_wave_amp);
    uni1(prog_water, "u_wave_scale", RS.water_wave_scale);
    uni1(prog_water, "u_wave_speed", RS.water_wave_speed);
    uni1(prog_water, "u_clarity", RS.water_clarity);
    uni1(prog_water, "u_opacity", RS.water_opacity);
    uni3(prog_water, "u_sky_zenith", RS.sky_zenith);
    uni3(prog_water, "u_sky_horizon", RS.sky_horizon);
    uni1(prog_water, "u_atmo", RS.atmosphere_density);
    unii(prog_water, "u_foam_on", RS.water_foam ? 1 : 0);
    uni3(prog_water, "u_foam_color", RS.foam_color);
    uni1(prog_water, "u_foam_amount", RS.foam_amount);
    uni1(prog_water, "u_foam_scale", RS.foam_scale);
    uni1(prog_water, "u_foam_crests", RS.foam_crests);
    uni1(prog_water, "u_roughness", RS.mat_roughness * 0.2f);
    uni1(prog_water, "u_reflection", RS.mat_reflection + 0.4f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_height);
    unii(prog_water, "u_height", 0);
    glBindVertexArray(vao_grid);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_BLEND);
  }

  // selection outlines
  if (sel_type >= 0 && vc.outlines) {
    float orange[4] = {1.f, 0.55f, 0.15f, 0.95f};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (sel_type == SceneObject::Terrain) {
      draw_box_outline(mvp, 0.f, 0.f, 0.f, 1.f, RS.height_scale, 1.f, orange);
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
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace studio
