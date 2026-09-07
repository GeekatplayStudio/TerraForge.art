// Geekatplay TerraForge - the mesh pass: every visible mesh object, its
// material, its deformers, its scattered copies at whatever level of
// detail their distance asks for, and the cards the farthest become.
// Split from renderer_scene.cpp for the 500-line module rule.
#include "app.hpp"
#include "perf.hpp"
#include "renderer_billboards.hpp"
#include "renderer_instances.hpp"
#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"
#include "scatter_lod.hpp"
#include "scene.hpp"
#include "uniform_cache.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace studio {

void draw_scene_meshes(const FrameCtx &F, const float *sun, bool atmosphere) {
  RenderSettings &RS = F.RS;
  SceneState &sc = scene();
  const float *view_eye = F.view_eye;
  const float *mvp = F.mvp;
  const float time_acc = F.time_acc;
  const RenderSettings::ViewConfig &vc = F.vc;

  // A mesh is drawn part by part: each run of vertices with its own picture
  // (bound on unit 3) and colour, or in one go when it has none.
  std::vector<InstanceRun> runs; // this mesh's copies to draw, by LOD cell
  auto draw_mesh_parts = [&](SceneObject &o, bool instanced) {
    auto draw = [&](int first, int count) {
      if (instanced) instances_draw_range(prog_mesh, runs, first, count);
      else glDrawArrays(GL_TRIANGLES, first, count);
    };
    const bool have_uv = o.uvs.size() == (size_t)o.vert_count * 2;
    if (o.parts.empty() || !have_uv) {
      unii(prog_mesh, "u_has_tex", 0);
      draw(0, o.vert_count);
      return;
    }
    glActiveTexture(GL_TEXTURE3);
    int covered = 0;
    for (const SceneObject::Part &part : o.parts) {
      if (part.first < 0 || part.count <= 0 || part.first + part.count > o.vert_count) continue;
      float col[3] = {o.color[0] * part.color[0], o.color[1] * part.color[1], o.color[2] * part.color[2]};
      uni3(prog_mesh, "u_color", col);
      if (part.tex) {
        glBindTexture(GL_TEXTURE_2D, part.tex);
        unii(prog_mesh, "u_albedo_tex", 3);
        unii(prog_mesh, "u_has_tex", 1);
      } else {
        unii(prog_mesh, "u_has_tex", 0);
      }
      draw(part.first, part.count);
      covered = std::max(covered, part.first + part.count);
    }
    // vertices no part claims are drawn plain
    if (covered < o.vert_count) {
      uni3(prog_mesh, "u_color", o.color);
      unii(prog_mesh, "u_has_tex", 0);
      draw(covered, o.vert_count - covered);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  };

  // Lighting is shared by every mesh in this view.
  glUseProgram(prog_mesh);
  upload_scene_lights(prog_mesh, RS.height_scale);
  uni3(prog_mesh, "u_sky_zenith", RS.sky_zenith);
  uni3(prog_mesh, "u_sky_horizon", RS.sky_horizon);
  uni1(prog_mesh, "u_ambient", RS.ambient_intensity);
  uni1(prog_mesh, "u_sun_intensity", RS.sun_intensity);
  // scene meshes
  for (SceneObject &o : sc.objects) {
    if (o.type != SceneObject::Mesh || !sc.object_visible(o)) continue;
    mesh_upload(o); // renderer_mesh_upload.cpp, when the geometry changed
    bool is_sel = (&o - sc.objects.data()) == sc.selected;
    glUseProgram(prog_mesh);
    glUniformMatrix4fv(uniform_location(prog_mesh, "u_mvp"), 1, GL_FALSE, mvp);
    float model[16], nrm[9];
    scene_object_matrix(o, RS.height_scale, model, nrm);
    glUniformMatrix4fv(uniform_location(prog_mesh, "u_model"), 1, GL_FALSE,
                       model);
    glUniformMatrix3fv(uniform_location(prog_mesh, "u_nrm"), 1, GL_FALSE,
                       nrm);
    uni3(prog_mesh, "u_color", o.color);
    // a MaterialOutput assigned to this object drives its shading (Vue
    // Advanced Material Editor tabs); unassigned meshes get the defaults,
    // which is the same look this shader always had.
    {
      gpx::MaterialParams mp;
      if (o.material_node) {
        if (gpx::Node *mn = app().graph.find_node(o.material_node))
          mp = gpx::material_params_from(mn->attrs);
      }
      renderer_material_uniforms(prog_mesh, mp);
    }
    uni3(prog_mesh, "u_sun", sun);
    uni3(prog_mesh, "u_sun_color", RS.sun_color);
    uni3(prog_mesh, "u_cam", view_eye);
    uni1(prog_mesh, "u_hscale", RS.height_scale);
    glUniformMatrix4fv(uniform_location(prog_mesh, "u_light_mvp"), 1, GL_FALSE, F.light_mvp);
    unii(prog_mesh, "u_shadows", (F.shadows_ok && vc.display != 0) ? 1 : 0);
    uni1(prog_mesh, "u_shadow_soft", RS.shadow_softness);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    unii(prog_mesh, "u_shadowmap", 2);
    glActiveTexture(GL_TEXTURE0);
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
      // A resident instance stream is shared by every view. Only an actual
      // scatter rebuild uploads it; the shader retains the same transforms.
      // Each view decides its own level of detail per cell of the population
      // (renderer_instances.cpp): near cells whole, far cells thinned and
      // drawn from the reduced mesh, off-screen cells not at all.
      unii(prog_mesh, "u_inst_on", 1);
      uni1(prog_mesh, "u_inst_sway", o.scatter_sway);
      uni1(prog_mesh, "u_inst_time", time_acc);
      glUniform3f(uniform_location(prog_mesh, "u_inst_base"), model[12],
                  model[13], model[14]);
      instances_upload(o);
      const Frustum fr = frustum_from_mvp(mvp);
      instance_runs(o, view_eye, &fr, RS.height_scale, RS.terrain_size_m,
                    instance_lod_params(), runs);
      int drawn = 0, cards = 0;
      for (const InstanceRun &r : runs) {
        drawn += r.count;
        if (r.level == SCATTER_LOD_BILLBOARD) cards += r.count;
      }
      instances_count(drawn, o.inst_count(), cards);
      draw_mesh_parts(o, true);
      uni3(prog_mesh, "u_color", o.color);
      unii(prog_mesh, "u_has_tex", 0);
      instances_draw_lods(prog_mesh, o, runs);
      uni1(prog_mesh, "u_inst_grow", 1.f);
      unii(prog_mesh, "u_inst_on", 0);
      // and the farthest ring, as cards baked from the mesh
      {
        BillboardPass bp;
        bp.mvp = mvp;
        bp.model = model;
        bp.view_eye = view_eye;
        bp.sun = sun;
        bp.sun_color = RS.sun_color;
        bp.grade = g_grade;
        bp.hscale = RS.height_scale;
        bp.exposure = RS.exposure * g_exposure_mult;
        bp.saturation = g_saturation;
        bp.time_acc = time_acc;
        bp.aov = g_aov;
        bp.object_id = 3 + (int)(&o - sc.objects.data());
        bp.atmosphere = atmosphere;
        bp.RS = &RS;
        instances_draw_billboards(o, runs, bp);
        glUseProgram(prog_mesh); // the loop goes on with the mesh program
      }
    } else {
      unii(prog_mesh, "u_inst_on", 0);
      draw_mesh_parts(o, false);
    }
  }
}

} // namespace studio
