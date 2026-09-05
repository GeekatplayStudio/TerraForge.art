// Geekatplay TerraForge - mesh objects in the sun's shadow map. The shadow
// pass drew only the terrain, so a rock cast no shadow and a scattered
// forest lit the ground under it as if it were not there. Every visible
// mesh is now drawn into the depth map with the same deformers and the
// same instance stream the colour pass uses, so a twisted tree shadows as
// a twisted tree. The map is redrawn only when a mesh actually moved:
// mesh_shadow_key() hashes the fields that place a mesh (never the struct
// bytes - AGENTS.md, Performance 1).
#include "renderer_internal.hpp"
#include "scene.hpp"
#include "uniform_cache.hpp"
#include <cstring>

namespace studio {

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
  for (const SceneObject &o : sc.objects) {
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
    uni3(prog_depth_mesh, "u_bmin", o.bmin);
    uni3(prog_depth_mesh, "u_bmax", o.bmax);
    glBindVertexArray(o.vao);
    if (!o.inst.empty()) {
      // the instance stream is attached to the mesh's VAO by the colour
      // pass; the first frame after a scatter rebuild draws the copies at
      // the model's origin until that pass has uploaded it
      unii(prog_depth_mesh, "u_inst_on", 1);
      uni1(prog_depth_mesh, "u_inst_sway", o.scatter_sway);
      uni1(prog_depth_mesh, "u_inst_time", F.time_acc);
      glUniform3f(uniform_location(prog_depth_mesh, "u_inst_base"), model[12], model[13], model[14]);
      glDrawArraysInstanced(GL_TRIANGLES, 0, o.vert_count, (int)(o.inst.size() / 8));
    } else {
      unii(prog_depth_mesh, "u_inst_on", 0);
      glDrawArrays(GL_TRIANGLES, 0, o.vert_count);
    }
  }
  glBindVertexArray(0);
}

} // namespace studio
