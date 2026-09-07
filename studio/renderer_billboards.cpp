// Geekatplay TerraForge - drawing the far scattered copies as cards.
//
// The runs the LOD pass produced carry a level each; the ones at
// SCATTER_LOD_BILLBOARD are drawn here, from the same instance stream the
// geometry uses, as four vertices apiece with no vertex buffer at all (the
// corners come from gl_VertexID). One draw call per run, and a run is
// usually a whole cell.
#include "renderer_billboards.hpp"
#include "renderer_instances.hpp"
#include "renderer_internal.hpp"
#include "renderer_shaders.hpp"
#include "scatter_lod.hpp"
#include "scene.hpp"
#include "uniform_cache.hpp"
#include <cmath>

namespace studio {

void instances_draw_billboards(SceneObject &o, const std::vector<InstanceRun> &runs,
                               const BillboardPass &bp) {
  if (!prog_billboard || !o.card_tex || !o.card_vao) return;
  bool any = false;
  for (const InstanceRun &r : runs) any |= r.level == SCATTER_LOD_BILLBOARD;
  if (!any) return;

  glUseProgram(prog_billboard);
  glUniformMatrix4fv(uniform_location(prog_billboard, "u_mvp"), 1, GL_FALSE, bp.mvp);
  glUniformMatrix4fv(uniform_location(prog_billboard, "u_model"), 1, GL_FALSE, bp.model);
  glUniform3f(uniform_location(prog_billboard, "u_inst_base"), bp.model[12], bp.model[13], bp.model[14]);
  // the camera's right, from the first row of projection*view: the
  // projection only scales it, so normalising gives the direction back
  float right[3] = {bp.mvp[0], bp.mvp[4], bp.mvp[8]};
  const float rl = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
  if (rl > 1e-9f) for (float &v : right) v /= rl;
  uni3(prog_billboard, "u_cam_right", right);
  uni3(prog_billboard, "u_card_centre", o.card_centre);
  // the model matrix's own scale, so a card is the size the mesh would be
  auto col_len = [&](int c) {
    const float *m = bp.model + c * 4;
    return std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  };
  glUniform2f(uniform_location(prog_billboard, "u_card_size"),
              o.card_size[0] * col_len(0), o.card_size[1] * col_len(1));
  uni1(prog_billboard, "u_inst_sway", o.scatter_sway);
  uni1(prog_billboard, "u_inst_time", bp.time_acc);
  uni3(prog_billboard, "u_cam", bp.view_eye);
  uni3(prog_billboard, "u_sun", bp.sun);
  uni3(prog_billboard, "u_sun_color", bp.sun_color);
  uni1(prog_billboard, "u_hscale", bp.hscale);
  uni1(prog_billboard, "u_exposure", bp.exposure);
  uni3(prog_billboard, "u_grade", bp.grade);
  uni1(prog_billboard, "u_sat", bp.saturation);
  unii(prog_billboard, "u_aov", bp.aov);
  unii(prog_billboard, "u_object_id", bp.object_id);
  if (bp.RS) upload_fog_uniforms(prog_billboard, *bp.RS, bp.atmosphere);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, o.card_tex);
  unii(prog_billboard, "u_card", 0);
  glBindVertexArray(o.card_vao);
  for (const InstanceRun &r : runs) {
    if (r.level != SCATTER_LOD_BILLBOARD) continue;
    uni1(prog_billboard, "u_inst_grow", r.grow);
    glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, r.count, (GLuint)r.base);
  }
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace studio
