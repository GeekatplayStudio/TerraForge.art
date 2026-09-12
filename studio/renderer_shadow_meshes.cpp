// Geekatplay TerraForge - mesh objects in the sun's shadow map. The shadow
// pass drew only the terrain, so a rock cast no shadow and a scattered
// forest lit the ground under it as if it were not there. Every visible
// mesh is now drawn into the depth map with the same deformers and the
// same instance stream the colour pass uses, so a twisted tree shadows as
// a twisted tree. The map is redrawn only when a mesh actually moved:
// mesh_shadow_key() hashes the fields that place a mesh (never the struct
// bytes - AGENTS.md, Performance 1).
#include "renderer_instances.hpp"
#include "renderer_internal.hpp"
#include "scene.hpp"
#include "uniform_cache.hpp"
#include <algorithm>
#include <cstring>

namespace studio {

bool a_plant_wind_preview(); // studio/scene_plants_species.cpp: the wind preview toggle


unsigned long long mesh_shadow_key() {
  unsigned long long h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  };
  SceneState &sc = scene();
  for (const SceneObject &o : sc.objects) {
    if (o.type != SceneObject::Mesh) continue;
    unsigned char vis = sc.object_visible(o) ? 1 : 0;
    mix(&vis, 1);
    if (!vis) continue;
    mix(o.pos, sizeof o.pos);
    mix(&o.scale, sizeof o.scale);
    mix(o.scl, sizeof o.scl);
    mix(&o.yaw, sizeof o.yaw);
    mix(&o.pitch, sizeof o.pitch);
    mix(&o.roll, sizeof o.roll);
    mix(o.deform.twist, sizeof o.deform.twist);
    mix(&o.deform.bend, sizeof o.deform.bend);
    mix(&o.deform.bend_axis, sizeof o.deform.bend_axis);
    mix(o.deform.shear, sizeof o.deform.shear);
    mix(&o.deform.taper, sizeof o.deform.taper);
    mix(&o.vert_count, sizeof o.vert_count);
    mix(&o.inst_revision, sizeof o.inst_revision);
    mix(&o.vao, sizeof o.vao);
    mix(&o.parent, sizeof o.parent);
  }
  return h;
}

void pass_shadow_meshes(const FrameCtx &F) {
  if (!prog_depth_mesh) return;
  SceneState &sc = scene();
  glUseProgram(prog_depth_mesh);
  glUniformMatrix4fv(uniform_location(prog_depth_mesh, "u_light_mvp"), 1, GL_FALSE, F.light_mvp);
  for (SceneObject &o : sc.objects) {
    if (o.type != SceneObject::Mesh || !o.vao || o.vert_count <= 0 || !sc.object_visible(o)) continue;
    float model[16], nrm[9];
    scene_object_matrix(o, F.RS.height_scale, model, nrm);
    glUniformMatrix4fv(uniform_location(prog_depth_mesh, "u_model"), 1, GL_FALSE, model);
    unii(prog_depth_mesh, "u_def_on", o.deform.identity() ? 0 : 1);
    uni3(prog_depth_mesh, "u_def_twist", o.deform.twist);
    uni1(prog_depth_mesh, "u_def_bend", o.deform.bend);
    unii(prog_depth_mesh, "u_def_bend_axis", o.deform.bend_axis);
    uni3(prog_depth_mesh, "u_def_shear", o.deform.shear);
    uni1(prog_depth_mesh, "u_def_taper", o.deform.taper);
    // a plant sways by its wind weights, in the wind its species root set;
    // the preview toggle stills it without touching the species
    unii(prog_depth_mesh, "u_plant_on", o.plant ? 1 : 0);
    if (o.plant) {
      const gpx::PlantWind &w = o.plant_wind;
      const float on = a_plant_wind_preview() ? 1.f : 0.f;
      uni1(prog_depth_mesh, "u_plant_time", F.time_acc);
      glUniform4f(uniform_location(prog_depth_mesh, "u_pw_a"), w.strength * on, w.dir[0], w.dir[1], w.breeze);
      glUniform4f(uniform_location(prog_depth_mesh, "u_pw_b"), w.breeze_speed, w.flutter, w.flutter_speed, w.gust);
      glUniform4f(uniform_location(prog_depth_mesh, "u_pw_c"), w.gust_frequency, w.breeze_randomness, 0.f, 0.f);
    }
    uni3(prog_depth_mesh, "u_bmin", o.bmin);
    uni3(prog_depth_mesh, "u_bmax", o.bmax);
    glBindVertexArray(o.vao);
    const bool instanced = !o.inst.empty();
    std::vector<InstanceRun> runs;
    if (instanced) {
      // the same stream and the same per-cell thinning the colour pass
      // uses, measured from the view camera, so a copy shadows exactly
      // where it stands; the sun sees every cell, so no frustum here
      instances_upload(o);
      instance_runs(o, F.view_eye, nullptr, F.RS.height_scale, F.RS.terrain_size_m,
                    instance_lod_params(), runs);
      unii(prog_depth_mesh, "u_inst_on", 1);
      uni1(prog_depth_mesh, "u_inst_sway", o.scatter_sway);
      uni1(prog_depth_mesh, "u_inst_time", F.time_acc);
      glUniform3f(uniform_location(prog_depth_mesh, "u_inst_base"), model[12], model[13], model[14]);
    } else {
      unii(prog_depth_mesh, "u_inst_on", 0);
    }
    auto draw = [&](int first, int count) {
      if (instanced) instances_draw_range(prog_depth_mesh, runs, first, count);
      else glDrawArrays(GL_TRIANGLES, first, count);
    };
    // part by part where a part has a picture, so its alpha cuts the shadow
    const bool have_uv = o.uvs.size() == (size_t)o.vert_count * 2;
    if (o.parts.empty() || !have_uv) {
      unii(prog_depth_mesh, "u_has_tex", 0);
      draw(0, o.vert_count);
      if (instanced) {
        instances_draw_lods(prog_depth_mesh, o, runs);
        uni1(prog_depth_mesh, "u_inst_grow", 1.f);
      }
      continue;
    }
    glActiveTexture(GL_TEXTURE3);
    int covered = 0;
    for (const SceneObject::Part &part : o.parts) {
      if (part.first < 0 || part.count <= 0 || part.first + part.count > o.vert_count) continue;
      if (part.tex) {
        glBindTexture(GL_TEXTURE_2D, part.tex);
        unii(prog_depth_mesh, "u_albedo_tex", 3);
        unii(prog_depth_mesh, "u_has_tex", 1);
      } else {
        unii(prog_depth_mesh, "u_has_tex", 0);
      }
      draw(part.first, part.count);
      covered = std::max(covered, part.first + part.count);
    }
    if (covered < o.vert_count) {
      unii(prog_depth_mesh, "u_has_tex", 0);
      draw(covered, o.vert_count - covered);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    if (instanced) {
      unii(prog_depth_mesh, "u_has_tex", 0);
      instances_draw_lods(prog_depth_mesh, o, runs);
      uni1(prog_depth_mesh, "u_inst_grow", 1.f);
    }
  }
  glBindVertexArray(0);
}

} // namespace studio
