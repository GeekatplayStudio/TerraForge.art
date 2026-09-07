// Geekatplay TerraForge - see renderer_instances.hpp.
#include "renderer_instances.hpp"
#include "billboard.hpp"
#include "mesh_lod.hpp"
#include "perf.hpp"
#include "renderer_internal.hpp"
#include "scatter_lod.hpp"
#include "scene.hpp"
#include "uniform_cache.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <map>
#include <set>

namespace studio {

namespace {

struct InstanceBuffer { GLuint vbo = 0; unsigned long long revision = ~0ull; };
std::map<GLuint, InstanceBuffer> g_buffers;

void attach(GLuint vao, GLuint vbo) {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  const GLsizei stride = SceneObject::INST_FLOATS * sizeof(float);
  for (unsigned k = 0; k < 4; ++k) {
    glEnableVertexAttribArray(2 + k);
    glVertexAttribPointer(2 + k, 4, GL_FLOAT, GL_FALSE, stride,
                          (const void *)(uintptr_t)(k * 4 * sizeof(float)));
    glVertexAttribDivisor(2 + k, 1);
  }
  glBindVertexArray(0);
}

int g_drawn = 0, g_total = 0, g_cards = 0, g_frame = -1;

} // namespace

void instances_count(int drawn, int total, int cards) {
  const int f = ImGui::GetFrameCount();
  if (f != g_frame) { g_frame = f; g_drawn = g_total = g_cards = 0; }
  g_drawn += drawn;
  g_total += total;
  g_cards += cards;
}

void renderer_instance_stats(int &drawn, int &total, int *cards) {
  drawn = g_drawn;
  total = g_total;
  if (cards) *cards = g_cards;
}

LodParams instance_lod_params() {
  const RenderSettings &RS = render_settings();
  LodParams p;
  p.full_m = RS.scatter_lod_full_m;
  p.far_m = RS.scatter_lod_far_m;
  p.cull_m = RS.scatter_lod_cull_m;
  p.billboard_m = RS.scatter_lod_billboard_m;
  p.min_keep = RS.scatter_lod_min_keep;
  p.scale = perf_quality().lod_scale;
  return p;
}

void instances_upload(SceneObject &o) {
  // Retire buffers whose owning mesh VAO is no longer in the scene.
  static int cleanup_frame = -1;
  if (cleanup_frame != ImGui::GetFrameCount()) {
    cleanup_frame = ImGui::GetFrameCount();
    std::set<GLuint> live;
    for (const auto &mesh : scene().objects) if (mesh.vao) live.insert(mesh.vao);
    for (auto it = g_buffers.begin(); it != g_buffers.end();) {
      if (!live.count(it->first)) {
        glDeleteBuffers(1, &it->second.vbo);
        it = g_buffers.erase(it);
      } else ++it;
    }
  }
  // the reduced meshes, once per mesh
  if (!o.lod_tried) {
    mesh_build_lods(o);
    for (int k = 0; k < 2; ++k) {
      if (o.lod_count[k] <= 0) continue;
      if (!o.lod_vao[k]) { glGenVertexArrays(1, &o.lod_vao[k]); glGenBuffers(1, &o.lod_vbo[k]); }
      glBindVertexArray(o.lod_vao[k]);
      glBindBuffer(GL_ARRAY_BUFFER, o.lod_vbo[k]);
      glBufferData(GL_ARRAY_BUFFER, o.lod_verts[k].size() * 4, o.lod_verts[k].data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
      glBindVertexArray(0);
    }
    // a new mesh means the stream must be re-attached to the new VAOs
    g_buffers[o.vao].revision = ~0ull;
  }
  // the card the copies become past the billboard distance: baked on the
  // CPU once, uploaded here, drawn from a VAO that carries only the
  // instance stream (its four corners come from gl_VertexID)
  if (!o.card_tried) {
    billboard_build(o);
    if (o.card_px > 0 && !o.card_rgba.empty()) {
      if (!o.card_tex) glGenTextures(1, &o.card_tex);
      glBindTexture(GL_TEXTURE_2D, o.card_tex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, o.card_px, o.card_px, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, o.card_rgba.data());
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
      if (!o.card_vao) glGenVertexArrays(1, &o.card_vao);
      o.card_rgba.clear(); // it lives on the GPU now
      o.card_rgba.shrink_to_fit();
      g_buffers[o.vao].revision = ~0ull;
    }
  }
  auto &buffer = g_buffers[o.vao];
  if (!buffer.vbo) glGenBuffers(1, &buffer.vbo);
  if (buffer.revision != o.inst_revision) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo);
    glBufferData(GL_ARRAY_BUFFER, o.inst.size() * sizeof(float), o.inst.data(), GL_STATIC_DRAW);
    attach(o.vao, buffer.vbo);
    for (int k = 0; k < 2; ++k) if (o.lod_vao[k]) attach(o.lod_vao[k], buffer.vbo);
    if (o.card_vao) attach(o.card_vao, buffer.vbo);
    glBindVertexArray(o.vao);
    buffer.revision = o.inst_revision;
  }
}

void instance_runs(const SceneObject &o, const float eye[3], const Frustum *fr,
                   float height_scale, float size_m, const LodParams &p,
                   std::vector<InstanceRun> &runs) {
  runs.clear();
  if (o.inst_cells.empty()) {
    // a stream that was never bucketed: everything, one run
    runs.push_back({0, o.inst_count(), 0, 1.f});
    return;
  }
  // how tall a copy can be, for the cells' visibility bound
  const float tall = std::max((o.bmax[1] - o.bmin[1]) * o.scale * 2.5f, 0.02f);
  const float wide = std::max(std::max(o.bmax[0] - o.bmin[0], o.bmax[2] - o.bmin[2]) * o.scale * 1.5f, 0.01f);
  (void)height_scale;
  for (const InstanceCell &c : o.inst_cells) {
    float lo[3] = {c.lo[0] - wide, c.lo[1] - tall * 0.2f, c.lo[2] - wide};
    float hi[3] = {c.hi[0] + wide, c.hi[1] + tall, c.hi[2] + wide};
    if (fr && !aabb_visible(*fr, lo, hi)) continue;
    const float dist_m = aabb_distance(eye, c.lo, c.hi) * size_m;
    const float keep = scatter_keep(dist_m, p);
    if (keep <= 0.f) continue;
    const int n = std::min(c.count, (int)std::ceil(c.count * keep));
    if (n <= 0) continue;
    InstanceRun r;
    r.base = c.first;
    r.count = n;
    r.level = scatter_lod_level(dist_m, p);
    // fall back down the ladder to whatever this mesh actually has
    if (r.level == SCATTER_LOD_BILLBOARD && !o.card_tex) r.level = 2;
    if (r.level == 2 && o.lod_count[1] <= 0) r.level = 1;
    if (r.level >= 1 && r.level <= 2 && o.lod_count[0] <= 0) r.level = 0;
    r.grow = scatter_grow(keep);
    // adjacent full cells at the same level become one draw
    if (!runs.empty()) {
      InstanceRun &last = runs.back();
      if (last.level == r.level && last.grow == 1.f && r.grow == 1.f &&
          last.base + last.count == r.base && n == c.count) {
        last.count += n;
        continue;
      }
    }
    runs.push_back(r);
  }
}

void instances_draw_range(unsigned prog, const std::vector<InstanceRun> &runs, int first, int count) {
  for (const InstanceRun &r : runs) {
    if (r.level != 0) continue;
    uni1(prog, "u_inst_grow", r.grow);
    glDrawArraysInstancedBaseInstance(GL_TRIANGLES, first, count, r.count, (GLuint)r.base);
  }
}

void instances_draw_lods(unsigned prog, SceneObject &o, const std::vector<InstanceRun> &runs) {
  for (int level = 1; level <= 2; ++level) {
    if (o.lod_count[level - 1] <= 0 || !o.lod_vao[level - 1]) continue;
    bool any = false;
    for (const InstanceRun &r : runs) any |= r.level == level;
    if (!any) continue;
    glBindVertexArray(o.lod_vao[level - 1]);
    for (const InstanceRun &r : runs) {
      if (r.level != level) continue;
      uni1(prog, "u_inst_grow", r.grow);
      glDrawArraysInstancedBaseInstance(GL_TRIANGLES, 0, o.lod_count[level - 1], r.count, (GLuint)r.base);
    }
  }
  glBindVertexArray(o.vao);
}

} // namespace studio
