// Geekatplay TerraForge - drawing one frame of the scene: terrain (fixed
// grid or tessellated), water, sky, clouds, meshes, planets, the surround,
// shadows, outlines and the grid. Split from renderer.cpp for the 500-line
// module rule; state lives in renderer_internal.hpp.
#include "perf.hpp"
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


void upload_scene_lights(unsigned prog, float hscale) {
  float pos4[8 * 4], col3[8 * 3], dir4[8 * 4];
  int count = 0;
  SceneState &sc = scene();
  for (const SceneObject &o : sc.objects) {
    if (o.type != SceneObject::Light || !sc.object_visible(o)) continue;
    if (count == 8) break;
    pos4[count * 4 + 0] = o.pos[0];
    pos4[count * 4 + 1] = o.pos[1] * hscale;
    pos4[count * 4 + 2] = o.pos[2];
    pos4[count * 4 + 3] = o.light_radius;
    for (int k = 0; k < 3; ++k)
      col3[count * 3 + k] = o.color[k] * o.light_intensity;
    if (o.light_type == 1) {
      // the spot aims along the object's heading and pitch; pitch -90 points
      // straight down, the streetlamp default
      float yaw = o.yaw * 0.017453293f, pit = o.pitch * 0.017453293f;
      dir4[count * 4 + 0] = std::cos(pit) * std::sin(yaw);
      dir4[count * 4 + 1] = std::sin(pit);
      dir4[count * 4 + 2] = std::cos(pit) * std::cos(yaw);
      dir4[count * 4 + 3] =
          std::cos(std::clamp(o.light_cone, 1.f, 170.f) * 0.5f * 0.017453293f);
    } else {
      dir4[count * 4 + 0] = 0;
      dir4[count * 4 + 1] = -1;
      dir4[count * 4 + 2] = 0;
      dir4[count * 4 + 3] = -2.f; // no cone cut
    }
    ++count;
  }
  unii(prog, "u_light_count", count);
  if (count) {
    glUniform4fv(glGetUniformLocation(prog, "u_lights"), count, pos4);
    glUniform3fv(glGetUniformLocation(prog, "u_light_col"), count, col3);
    glUniform4fv(glGetUniformLocation(prog, "u_light_dir"), count, dir4);
  }
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
  static bool far_tier[8] = {false};
  if (!far_tier[slot] && tile_dist > 9.f) far_tier[slot] = true;
  else if (far_tier[slot] && tile_dist < 7.f) far_tier[slot] = false;
  bool near_ground = !far_tier[slot];
  // volumetric clouds are a ground-view effect; from high above they cost a
  // full raymarch for a few pixels
  bool clouds_ok = RS.clouds_on && view_eye[1] < 3.f && near_ground;
  bool shadows_ok = RS.shadows && near_ground && perf_shadows_for(slot); // shadow texels vanish out there; the governor may drop them
  bool heavy_maps = near_ground; // 4K material maps are wasted on a far tile
  // how far out of the atmosphere the camera is (0 ground .. 1 open space);
  // smooth, so the sky thins continuously as you pull back
  float space_t = std::clamp((tile_dist - 6.f) / 22.f, 0.f, 1.f);
  space_t = space_t * space_t * (3.f - 2.f * space_t);

  float wind_rad = RS.cloud_wind_dir * 0.017453293f;
  float wind[2] = {std::cos(wind_rad) * RS.cloud_wind_speed,
                   std::sin(wind_rad) * RS.cloud_wind_speed};

  FrameCtx F{slot, w, h, time_acc, vc, view_eye, mvp, inv_vp,
             RS, {sun[0], sun[1], sun[2]}, sun_intensity,
             atmosphere, textured, wireframe, cinematic,
             show_terrain_obj, show_water_obj, sun_on, sel_type,
             clouds_ok, shadows_ok, heavy_maps, space_t,
             {}, {wind[0], wind[1]}};

  // shadow pass
  build_light_mvp(sun, RS.height_scale, F.light_mvp);
  pass_shadow(F);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo[slot]);
  glViewport(0, 0, w, h);
  if (g_aov != 0) glClearColor(0.f, 0.f, 0.f, 1.f); // passes start from nothing
  else glClearColor(RS.bg_color[0], RS.bg_color[1], RS.bg_color[2], 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);


  pass_sky(F);

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

  pass_terrain(F);

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
    extern bool g_terrain_base_set;
    inf.base_height =
        (g_terrain_base_set ? g_terrain_base : g_terrain_mean) * RS.height_scale;
    inf.planet_radius = RS.planet_radius;
    inf.water_level = (RS.show_water && show_water_obj)
                          ? RS.water_level * RS.height_scale
                          : -1e9f;
    inf.water_deep = RS.water_deep_color;
    inf.water_shallow = RS.water_shallow_color;
    inf.water_clarity = RS.water_clarity;
    inf.latitude = std::fabs(RS.latitude) / 90.f;
    inf.atmosphere = atmosphere;
    inf.textured = textured;
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
      scene_object_bounds(o);
    }
    bool is_sel = (&o - sc.objects.data()) == sc.selected;
    glUseProgram(prog_mesh);
    upload_scene_lights(prog_mesh, RS.height_scale);
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
    uni3(prog_mesh, "u_cam", view_eye);
    uni1(prog_mesh, "u_hscale", RS.height_scale);
    upload_fog_uniforms(prog_mesh, RS, atmosphere);
    unii(prog_mesh, "u_aov", g_aov);
    unii(prog_mesh, "u_object_id", 3 + (int)(&o - sc.objects.data()));
    uni1(prog_mesh, "u_exposure", (RS.exposure) * g_exposure_mult);
    uni3(prog_mesh, "u_grade", g_grade);
    uni1(prog_mesh, "u_sat", g_saturation);
    unii(prog_mesh, "u_selected", is_sel ? 1 : 0);
    // deformers, in the object's own space (gpx/deform.hpp is the CPU twin)
    unii(prog_mesh, "u_def_on", o.deform.identity() ? 0 : 1);
    uni3(prog_mesh, "u_def_twist", o.deform.twist);
    uni1(prog_mesh, "u_def_bend", o.deform.bend);
    unii(prog_mesh, "u_def_bend_axis", o.deform.bend_axis);
    uni3(prog_mesh, "u_def_shear", o.deform.shear);
    uni1(prog_mesh, "u_def_taper", o.deform.taper);
    uni3(prog_mesh, "u_bmin", o.bmin);
    uni3(prog_mesh, "u_bmax", o.bmax);
    glBindVertexArray(o.vao);
    if (!o.inst.empty()) {
      // scattered copies: batches of 256 through the uniform arrays; the
      // shader swaps each copy's translation in for the model's own
      unii(prog_mesh, "u_inst_on", 1);
      uni1(prog_mesh, "u_inst_sway", o.scatter_sway);
      uni1(prog_mesh, "u_inst_time", time_acc);
      glUniform3f(glGetUniformLocation(prog_mesh, "u_inst_base"), model[12],
                  model[13], model[14]);
      const size_t per = 8, batch = 256;
      const size_t total = o.inst.size() / per;
      std::vector<float> pos4(batch * 4), rot4(batch * 4);
      for (size_t off = 0; off < total; off += batch) {
        size_t nb = std::min(batch, total - off);
        for (size_t i = 0; i < nb; ++i) {
          const float *s = o.inst.data() + (off + i) * per;
          pos4[i * 4 + 0] = s[0];
          pos4[i * 4 + 1] = s[1];
          pos4[i * 4 + 2] = s[2];
          pos4[i * 4 + 3] = s[3];
          rot4[i * 4 + 0] = s[4];
          rot4[i * 4 + 1] = s[5];
          rot4[i * 4 + 2] = s[6]; // per-copy brightness
          rot4[i * 4 + 3] = 0.f;
        }
        glUniform4fv(glGetUniformLocation(prog_mesh, "u_inst"), (int)nb,
                     pos4.data());
        glUniform4fv(glGetUniformLocation(prog_mesh, "u_inst_rot"), (int)nb,
                     rot4.data());
        glDrawArraysInstanced(GL_TRIANGLES, 0, o.vert_count, (int)nb);
      }
      unii(prog_mesh, "u_inst_on", 0);
    } else {
      unii(prog_mesh, "u_inst_on", 0);
      glDrawArrays(GL_TRIANGLES, 0, o.vert_count);
    }
  }

  // sun gizmo (a real, selectable scene object)
  if (sun_on && g_aov == 0) {
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

  // points overlay: the selected node's point cloud as vertical ticks, so a
  // scatter or a routed path is visible before anything stamps it
  if (!g_points_overlay.empty() && g_aov == 0) {
    glUseProgram(prog_lines);
    glUniformMatrix4fv(glGetUniformLocation(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
    glUniform4f(glGetUniformLocation(prog_lines, "u_color"), 1.f, 0.62f, 0.25f,
                0.9f);
    glBindVertexArray(vao_dyn);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
    const float tick = 0.035f;
    // vbo_dyn holds 256 vertices; two per tick = 128 points a batch
    std::vector<float> seg;
    seg.reserve(128 * 6);
    for (size_t i = 0; i + 2 < g_points_overlay.size(); i += 3) {
      seg.push_back(g_points_overlay[i]);
      seg.push_back(g_points_overlay[i + 1]);
      seg.push_back(g_points_overlay[i + 2]);
      seg.push_back(g_points_overlay[i]);
      seg.push_back(g_points_overlay[i + 1] + tick);
      seg.push_back(g_points_overlay[i + 2]);
      if (seg.size() >= 128 * 6) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, seg.size() * 4, seg.data());
        glDrawArrays(GL_LINES, 0, (int)(seg.size() / 3));
        seg.clear();
      }
    }
    if (!seg.empty()) {
      glBufferSubData(GL_ARRAY_BUFFER, 0, seg.size() * 4, seg.data());
      glDrawArrays(GL_LINES, 0, (int)(seg.size() / 3));
    }
  }

  // light gizmos: a small glowing ball where each point light sits
  for (size_t li = 0; li < sc.objects.size(); ++li) {
    const SceneObject &o = sc.objects[li];
    if (o.type != SceneObject::Light || !sc.object_visible(o)) continue;
    glUseProgram(prog_gizmo);
    glUniformMatrix4fv(glGetUniformLocation(prog_gizmo, "u_mvp"), 1, GL_FALSE,
                       mvp);
    glUniform4f(glGetUniformLocation(prog_gizmo, "u_xform"), o.pos[0],
                o.pos[1] * RS.height_scale, o.pos[2], 0.012f);
    uni3(prog_gizmo, "u_color", o.color);
    unii(prog_gizmo, "u_selected", (int)li == sc.selected ? 1 : 0);
    glBindVertexArray(vao_sphere);
    glDrawArrays(GL_TRIANGLES, 0, sphere_verts);
    // a spot shows its aim: four cone edge lines to the reach distance
    if (o.light_type == 1) {
      float yaw2 = o.yaw * 0.017453293f, pit2 = o.pitch * 0.017453293f;
      float dx = std::cos(pit2) * std::sin(yaw2);
      float dy = std::sin(pit2);
      float dz = std::cos(pit2) * std::cos(yaw2);
      float half = std::clamp(o.light_cone, 1.f, 170.f) * 0.5f * 0.017453293f;
      // an orthonormal frame around the axis
      float ux = -dz, uy = 0.f, uz = dx;
      float ul = std::sqrt(ux * ux + uz * uz);
      if (ul < 1e-4f) { ux = 1; uz = 0; ul = 1; }
      ux /= ul; uz /= ul;
      float vx = dy * uz - dz * uy, vy = dz * ux - dx * uz,
            vz = dx * uy - dy * ux;
      float ox = o.pos[0], oy = o.pos[1] * RS.height_scale, oz = o.pos[2];
      float L = o.light_radius;
      float s = std::sin(half), c2 = std::cos(half);
      std::vector<float> seg;
      for (int k = 0; k < 4; ++k) {
        float a2 = k * 1.5707963f;
        float rx = ux * std::cos(a2) + vx * std::sin(a2);
        float ry = uy * std::cos(a2) + vy * std::sin(a2);
        float rz = uz * std::cos(a2) + vz * std::sin(a2);
        seg.insert(seg.end(), {ox, oy, oz, ox + (dx * c2 + rx * s) * L,
                               oy + (dy * c2 + ry * s) * L,
                               oz + (dz * c2 + rz * s) * L});
      }
      glBindVertexArray(vao_dyn);
      glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
      glBufferSubData(GL_ARRAY_BUFFER, 0, seg.size() * 4, seg.data());
      glUseProgram(prog_lines);
      glUniformMatrix4fv(glGetUniformLocation(prog_lines, "u_mvp"), 1,
                         GL_FALSE, mvp);
      glUniform4f(glGetUniformLocation(prog_lines, "u_color"), o.color[0],
                  o.color[1], o.color[2], 0.55f);
      glDrawArrays(GL_LINES, 0, (int)(seg.size() / 3));
    }
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

  pass_water(F);

  pass_outlines(F);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace studio
