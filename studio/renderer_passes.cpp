// Geekatplay TerraForge - the frame passes: shadow map, sky and
// volumetric clouds, the terrain tile, water, and selection outlines. Each is
// one verbatim block moved out of draw_scene (renderer_scene.cpp), sharing
// the per-frame FrameCtx; proven pixel-identical under GPX_FREEZE_TIME.
#include "renderer_internal.hpp"
#include "app.hpp"
#include "cloud_noise.hpp"
#include "scene.hpp"
#include "gpu_timer.hpp"
#include "terrain_cull.hpp"
#include "gpx/camera_math.hpp"
#include "gpx/field_glsl.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace studio {

void pass_shadow(const FrameCtx &F) {
  RenderSettings &RS = F.RS;
  const float *light_mvp = F.light_mvp;
  bool shadows_ok = F.shadows_ok;
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

void pass_terrain(const FrameCtx &F) {
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
  bool show_terrain_obj = F.show_terrain_obj;
  // terrain
  if (show_terrain_obj) {
    // Adaptive subdivision when the driver took the tessellated program and
    // the user has not turned it off; the fixed grid is always there as the
    // fallback, and both run the same placement code.
    const bool use_tess = tess_ok && RS.tessellation && prog_terrain_tess;
    const GLuint PT = use_tess ? prog_terrain_tess : prog_terrain;
    glUseProgram(PT);
    upload_scene_lights(PT, RS.height_scale);
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
    // A geometry pass wants the water surface, not a blend of it with the
    // bed underneath; only the picture and the linear beauty are translucent.
    const bool blend = g_aov == 0 || g_aov == AOV_BEAUTY_LINEAR;
    if (blend) glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    upload_fog_uniforms(prog_water, RS, F.atmosphere);
    backdrop_bind(prog_water);
    unii(prog_water, "u_aov", g_aov);
    unii(prog_water, "u_object_id", 2);
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
}

} // namespace studio
