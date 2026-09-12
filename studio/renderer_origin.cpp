// Geekatplay TerraForge - the world's centre, drawn (render_settings.hpp:
// ViewConfig::show_origin).
//
// Everything in the scene is positioned against one point, and until now that
// point was the only thing in the world with no way to see it. On a tile it is
// easy enough to guess - a terrain runs 0..1 and the middle is the middle -
// but a scene can be a planet, a ring, a shell with ground on both faces, or
// a surround thirty tiles deep, and in any of those "where is nought" is a
// real question with no landmark to answer it.
//
// So: three axes through it, red for X, green for Y, blue for Z, drawn at the
// size of the world they are in. Negative halves are dimmer than positive
// ones, because an axis that looks the same in both directions tells you
// where the origin is but not which way is forward.
#include "render_settings.hpp"
#include "renderer_internal.hpp"
#include <glad/gl.h>
#include <cmath>
#include <vector>

namespace studio {

namespace {

unsigned vao_origin = 0, vbo_origin = 0;
int origin_verts = 0;

// The axes, as line segments in world space. Each arm is split into a bright
// positive half and a dim negative one; the marks along them are the scale,
// so the origin says how big the world is as well as where its middle is.
void build(float reach) {
  std::vector<float> v;
  auto seg = [&v](float ax, float ay, float az, float bx, float by, float bz) {
    v.insert(v.end(), {ax, ay, az, bx, by, bz});
  };
  for (int axis = 0; axis < 3; ++axis)
    for (int side = 0; side < 2; ++side) {
      const float s = side ? -reach : reach;
      float e[3] = {0, 0, 0};
      e[axis] = s;
      seg(0.f, 0.f, 0.f, e[0], e[1], e[2]);
      // ticks at a tenth of the reach, across the other two axes
      for (int k = 1; k <= 10; ++k) {
        const float t = s * (float)k / 10.f;
        const float m = reach * (k % 5 == 0 ? 0.03f : 0.012f);
        float p[3] = {0, 0, 0};
        p[axis] = t;
        for (int other = 0; other < 3; ++other) {
          if (other == axis) continue;
          float a[3] = {p[0], p[1], p[2]}, b[3] = {p[0], p[1], p[2]};
          a[other] -= m;
          b[other] += m;
          seg(a[0], a[1], a[2], b[0], b[1], b[2]);
        }
      }
    }
  if (!vao_origin) {
    glGenVertexArrays(1, &vao_origin);
    glGenBuffers(1, &vbo_origin);
  }
  glBindVertexArray(vao_origin);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_origin);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
  glBindVertexArray(0);
  origin_verts = (int)(v.size() / 3);
}

// How far the arms reach: a tile's width, which is the scale a person is
// working at whatever shape the world happens to be.
float reach_for(const RenderSettings &RS) {
  (void)RS;
  return 0.75f;
}

} // namespace

void draw_world_origin(const float *mvp, const RenderSettings &RS) {
  const float reach = reach_for(RS);
  static float built_for = -1.f;
  if (!vao_origin || std::fabs(built_for - reach) > 1e-6f) {
    build(reach);
    built_for = reach;
  }
  if (origin_verts <= 0) return;

  glUseProgram(prog_lines);
  glUniformMatrix4fv(uniform_location(prog_lines, "u_mvp"), 1, GL_FALSE, mvp);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(vao_origin);
  // Drawn over whatever is in front of it. The origin is a reference, not a
  // thing in the world: one buried under the terrain would be no use at all,
  // and it is the question "where is nought" that is being answered, not
  // "what can I see from here".
  const bool depth = glIsEnabled(GL_DEPTH_TEST);
  glDisable(GL_DEPTH_TEST);
  const int per_arm = origin_verts / 6; // three axes, two halves each
  static const float COL[3][3] = {{0.95f, 0.32f, 0.32f}, {0.42f, 0.9f, 0.42f}, {0.38f, 0.55f, 1.f}};
  for (int axis = 0; axis < 3; ++axis)
    for (int side = 0; side < 2; ++side) {
      // the negative half dimmer, so the arms say which way is forward
      const float a = side ? 0.28f : 0.9f;
      glUniform4f(uniform_location(prog_lines, "u_color"), COL[axis][0], COL[axis][1],
                  COL[axis][2], a);
      glDrawArrays(GL_LINES, (axis * 2 + side) * per_arm, per_arm);
    }
  if (depth) glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
}

} // namespace studio
