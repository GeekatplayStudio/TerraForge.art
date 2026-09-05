// Geekatplay TerraForge - the motion path: the selected object's animated
// position drawn in the viewport as a line through the play range, a tick
// per key, so a move can be judged in space and not only on the ruler
// (Cinema 4D, Blender and After Effects all draw one). Only objects at the
// root of the hierarchy are exact - a child's path is in its parent's
// space, and the parent may move too.
#include "anim_targets.hpp"
#include "render_settings.hpp"
#include "renderer_internal.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "uniform_cache.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace studio {

void draw_motion_path(const float *mvp, float hscale) {
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) return;
  SceneObject &o = sc.objects[(size_t)sc.selected];
  if (o.anim.empty() || o.parent >= 0) return;
  const AnimProp *pp = anim_find_prop(o, "pos");
  if (!pp || !anim_prop_animated(o.anim, *pp)) return;
  const gpx::Timeline &tl = sc.timeline;
  gpx::Track *tx = anim_find(o.anim, *pp, 0), *ty = anim_find(o.anim, *pp, 1), *tz = anim_find(o.anim, *pp, 2);
  // the range the path covers: the play range extended to the outermost key
  float t0 = tl.play_start(), t1 = tl.play_end();
  for (gpx::Track *t : {tx, ty, tz})
    if (t && !t->keys.empty()) { t0 = std::min(t0, t->first_time()); t1 = std::max(t1, t->last_time()); }
  if (t1 <= t0) return;
  // one sample per frame, thinned to fit the dynamic buffer (256 vertices)
  int frames = (int)std::ceil((t1 - t0) * tl.fps);
  int step = std::max(1, (frames + 110) / 110);
  gpx::ExprContext ctx = anim_expr_context(t0);
  auto at = [&](float t, float *out) {
    ctx.t = t;
    out[0] = tx && tx->animated() ? tx->sample(t, ctx) : o.pos[0];
    out[1] = (ty && ty->animated() ? ty->sample(t, ctx) : o.pos[1]) * hscale;
    out[2] = tz && tz->animated() ? tz->sample(t, ctx) : o.pos[2];
  };
  std::vector<float> seg;
  float prev[3];
  at(t0, prev);
  for (int f = step; f <= frames + step; f += step) {
    float t = std::min(t0 + (float)f / tl.fps, t1);
    float cur[3];
    at(t, cur);
    seg.insert(seg.end(), {prev[0], prev[1], prev[2], cur[0], cur[1], cur[2]});
    prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
    if (seg.size() >= 200 * 3) break;
  }
  glBindVertexArray(vao_dyn);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_dyn);
  glUseProgram(prog_lines);
  glUniformMatrix4fv(uniform_location(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
  glUniform4f(uniform_location(prog_lines, "u_color"), 0.85f, 0.55f, 0.2f, 0.9f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBufferSubData(GL_ARRAY_BUFFER, 0, seg.size() * 4, seg.data());
  glDrawArrays(GL_LINES, 0, (int)(seg.size() / 3));
  // a small cross at every key time (on any component)
  std::vector<float> times;
  for (gpx::Track *t : {tx, ty, tz}) if (t) for (const gpx::Key &k : t->keys) times.push_back(k.time);
  std::sort(times.begin(), times.end());
  times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::fabs(a - b) < 1e-4f; }), times.end());
  seg.clear();
  const float r = 0.006f;
  for (float t : times) {
    float p[3];
    at(t, p);
    seg.insert(seg.end(), {p[0] - r, p[1], p[2], p[0] + r, p[1], p[2], p[0], p[1] - r, p[2], p[0], p[1] + r, p[2], p[0], p[1], p[2] - r, p[0], p[1], p[2] + r});
    if (seg.size() >= 240 * 3) break;
  }
  if (!seg.empty()) {
    glUniform4f(uniform_location(prog_lines, "u_color"), 1.f, 1.f, 1.f, 0.9f);
    glBufferSubData(GL_ARRAY_BUFFER, 0, seg.size() * 4, seg.data());
    glDrawArrays(GL_LINES, 0, (int)(seg.size() / 3));
  }
  glDisable(GL_BLEND);
}

} // namespace studio
