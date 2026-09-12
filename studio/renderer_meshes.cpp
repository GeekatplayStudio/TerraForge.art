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

bool a_plant_wind_preview(); // studio/scene_plants_species.cpp: the wind preview toggle


void draw_scene_meshes(const FrameCtx &F, const float *sun, bool atmosphere, bool media) {
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
      unii(prog_mesh, "u_leaf", 0);
      unii(prog_mesh, "u_has_part_normal", 0);
      draw(0, o.vert_count);
      return;
    }
    glActiveTexture(GL_TEXTURE3);
    int covered = 0;
    for (const SceneObject::Part &part : o.parts) {
      if (part.first < 0 || part.count <= 0 || part.first + part.count > o.vert_count) continue;
      float col[3] = {o.color[0] * part.color[0], o.color[1] * part.color[1], o.color[2] * part.color[2]};
      uni3(prog_mesh, "u_color", col);
      // Foliage: thin, two-sided, cut out - lit and masked differently. How
      // much light it lets through and how sharply it reflects come from the
      // material now, not from a constant: a laurel is glossy and nearly
      // opaque, a beech in spring is matte and glows through.
      unii(prog_mesh, "u_leaf", part.double_sided ? 1 : 0);
      uni1(prog_mesh, "u_leaf_through", part.translucency > 0.f ? part.translucency
                                                                : (part.double_sided ? 0.4f : 0.f));
      uni1(prog_mesh, "u_part_rough", part.roughness);
      if (part.normal_tex) {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, part.normal_tex);
        unii(prog_mesh, "u_part_normal", 6);
        unii(prog_mesh, "u_has_part_normal", 1);
        glActiveTexture(GL_TEXTURE3);
      } else {
        unii(prog_mesh, "u_has_part_normal", 0);
      }
      if (part.rough_tex) {
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, part.rough_tex);
        unii(prog_mesh, "u_part_rough_tex", 7);
        unii(prog_mesh, "u_has_part_rough", 1);
        glActiveTexture(GL_TEXTURE3);
      } else {
        unii(prog_mesh, "u_has_part_rough", 0);
      }
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
      uni3(prog_mesh, "u_color", scene_display_color(o).data());
      unii(prog_mesh, "u_has_tex", 0);
      unii(prog_mesh, "u_leaf", 0);
      unii(prog_mesh, "u_has_part_normal", 0);
      draw(covered, o.vert_count - covered);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    unii(prog_mesh, "u_leaf", 0);
    unii(prog_mesh, "u_has_part_normal", 0);
    unii(prog_mesh, "u_has_part_rough", 0);
  };

  // Lighting is shared by every mesh in this view.
  glUseProgram(prog_mesh);
  upload_scene_lights(prog_mesh, RS.height_scale);
  uni3(prog_mesh, "u_sky_zenith", RS.sky_zenith);
  uni3(prog_mesh, "u_sky_horizon", RS.sky_horizon);
  uni1(prog_mesh, "u_ambient", RS.ambient_intensity);
  uni3(prog_mesh, "u_sky_light", sky_light_rgb());
  uni1(prog_mesh, "u_sun_intensity", RS.sun_intensity);
  bind_sky_env(prog_mesh); // the sky a glossy mesh reflects (renderer_clouds.cpp)
  // Scene meshes, in two passes: surfaces first, with the depth they write,
  // then the volumes - meshes whose material is a medium - blended over
  // them, back to front, without writing depth. A medium has no surface to
  // write; what it adds is light scattered toward the eye and what it takes
  // is in its alpha, and that only composes correctly over everything solid
  // and the sky. The two are separate calls (`media`), because the sky is
  // drawn between them (renderer_scene.cpp).
  std::vector<SceneObject *> volumes;
  for (int pass = 0; pass < 2; ++pass) {
  if (pass == 1) {
    if (!media || volumes.empty()) break;
    std::sort(volumes.begin(), volumes.end(), [&](SceneObject *p, SceneObject *q) {
      auto d2 = [&](SceneObject *o) {
        float dx = o->pos[0] - view_eye[0], dy = o->pos[1] * RS.height_scale - view_eye[1],
              dz = o->pos[2] - view_eye[2];
        return dx * dx + dy * dy + dz * dz;
      };
      return d2(p) > d2(q); // farthest first
    });
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
  }
  // The first pass walks the scene; the second the media in the order just
  // sorted. It used to walk the scene again and skip what was not a medium,
  // so the sort decided nothing and two smoke volumes blended in whatever
  // order they had been added.
  std::vector<SceneObject *> order;
  if (pass == 0)
    for (SceneObject &o : sc.objects) order.push_back(&o);
  else
    order = volumes;
  for (SceneObject *op : order) {
    SceneObject &o = *op;
    if (o.type != SceneObject::Mesh || !sc.object_visible(o)) continue;
    // a MaterialOutput assigned to this object drives its shading (Vue
    // Advanced Material Editor tabs); unassigned meshes get the defaults,
    // which is the same look this shader always had.
    gpx::MaterialParams mp;
    if (o.material_node) {
      if (gpx::Node *mn = app().graph.find_node(o.material_node))
        mp = gpx::material_params_from(mn->attrs);
    }
    const bool is_volume = mp.vol_density > 0.f;
    if (pass == 0 && is_volume) {
      volumes.push_back(&o);
      continue;
    }
    if (pass == 0 && media) continue; // the surfaces were drawn by the first call
    mesh_upload(o); // renderer_mesh_upload.cpp, when the geometry changed
    bool is_sel = (&o - sc.objects.data()) == sc.selected;
    glUseProgram(prog_mesh);
    glUniformMatrix4fv(uniform_location(prog_mesh, "u_mvp"), 1, GL_FALSE, mvp);
    float model[16], nrm[9], model_inv[16];
    scene_object_matrix(o, RS.height_scale, model, nrm);
    glUniformMatrix4fv(uniform_location(prog_mesh, "u_model"), 1, GL_FALSE,
                       model);
    // The eye and the sun in the object's own space, for the volume march:
    // done here rather than in the shader, where an inverse per fragment is
    // waste and a matrix that failed to arrive is a silent wrong answer.
    if (is_volume) {
      float cam_obj[3] = {0, 0, 0}, sun_obj[3] = {0, 1, 0};
      if (mat_inverse(model_inv, model)) {
        for (int r = 0; r < 3; ++r) {
          cam_obj[r] = model_inv[r] * view_eye[0] + model_inv[4 + r] * view_eye[1] +
                       model_inv[8 + r] * view_eye[2] + model_inv[12 + r];
          sun_obj[r] = model_inv[r] * sun[0] + model_inv[4 + r] * sun[1] +
                       model_inv[8 + r] * sun[2];
        }
      }
      uni3(prog_mesh, "u_cam_obj", cam_obj);
      uni3(prog_mesh, "u_sun_obj", sun_obj);
    }
    glUniformMatrix3fv(uniform_location(prog_mesh, "u_nrm"), 1, GL_FALSE,
                       nrm);
    uni3(prog_mesh, "u_color", scene_display_color(o).data());
    renderer_material_uniforms(prog_mesh, mp);
    unii(prog_mesh, "u_id_mode", vc.display == 3 ? RS.id_mode + 1 : 0);
    uni1(prog_mesh, "u_id_key", (float)(o.material_node % 1024));
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
    // a plant sways by its wind weights, in the wind its species root set;
    // the preview toggle stills it without touching the species
    unii(prog_mesh, "u_plant_on", o.plant ? 1 : 0);
    if (o.plant) {
      const gpx::PlantWind &w = o.plant_wind;
      const float on = a_plant_wind_preview() ? 1.f : 0.f;
      uni1(prog_mesh, "u_plant_time", time_acc);
      glUniform4f(uniform_location(prog_mesh, "u_pw_a"), w.strength * on, w.dir[0], w.dir[1], w.breeze);
      glUniform4f(uniform_location(prog_mesh, "u_pw_b"), w.breeze_speed, w.flutter, w.flutter_speed, w.gust);
      glUniform4f(uniform_location(prog_mesh, "u_pw_c"), w.gust_frequency, w.breeze_randomness, 0.f, 0.f);
    }
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
      // The reduced copies set their own colour and picture per part
      // (renderer_instances.cpp); a mesh without parts is drawn in the
      // object's own colour, as it always was.
      uni3(prog_mesh, "u_color", scene_display_color(o).data());
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
  } // passes
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  // Uniforms outlive the object that set them. The planet surface and the
  // horizon surround draw with this program after us without uploading a
  // material of their own, and a density left at 2.5 turned the whole world
  // beyond the tile into the last smoke cube drawn.
  uni1(prog_mesh, "u_v_density", 0.f);
}

} // namespace studio
