// Geekatplay TerraForge - a mesh object's geometry and pictures onto the
// GPU. Split from renderer_scene.cpp for the 500-line module rule: this is
// the one place a mesh's VAO is laid out, so every reader of `verts` (the
// colour pass, the shadow pass, picking) sees the same six-float layout.
#include "renderer_internal.hpp"
#include "alpha_mips.hpp"
#include "scene.hpp"

namespace studio {

void mesh_upload(SceneObject &o) {
  if (!o.gpu_dirty) return;
  if (!o.vao) {
    glGenVertexArrays(1, &o.vao);
    glGenBuffers(1, &o.vbo);
  }
  glBindVertexArray(o.vao);
  glBindBuffer(GL_ARRAY_BUFFER, o.vbo);
  glBufferData(GL_ARRAY_BUFFER, o.verts.size() * 4, o.verts.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
  // texture coordinates in their own buffer, so a mesh without any keeps
  // the six-float layout every other reader of `verts` expects
  if (o.uvs.size() == (size_t)o.vert_count * 2) {
    if (!o.uvbo) glGenBuffers(1, &o.uvbo);
    glBindBuffer(GL_ARRAY_BUFFER, o.uvbo);
    glBufferData(GL_ARRAY_BUFFER, o.uvs.size() * 4, o.uvs.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
  } else {
    glDisableVertexAttribArray(6);
  }
  // a plant's wind weights and tints, four floats each, in their own buffers
  if (o.plant && o.wind.size() == (size_t)o.vert_count * 4 && o.tint.size() == (size_t)o.vert_count * 4) {
    if (!o.windbo) glGenBuffers(1, &o.windbo);
    glBindBuffer(GL_ARRAY_BUFFER, o.windbo);
    glBufferData(GL_ARRAY_BUFFER, o.wind.size() * 4, o.wind.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 16, nullptr);
    if (!o.tintbo) glGenBuffers(1, &o.tintbo);
    glBindBuffer(GL_ARRAY_BUFFER, o.tintbo);
    glBufferData(GL_ARRAY_BUFFER, o.tint.size() * 4, o.tint.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, 16, nullptr);
  } else {
    glDisableVertexAttribArray(7);
    glDisableVertexAttribArray(8);
  }
  glBindVertexArray(0);
  // the parts' pictures, decoded on load, become textures here
  for (SceneObject::Part &part : o.parts) {
    if (part.tex || part.rgba.empty() || part.w <= 0) continue;
    glGenTextures(1, &part.tex);
    glBindTexture(GL_TEXTURE_2D, part.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, part.w, part.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 part.rgba.data());
    if (alpha_has_cut(part.rgba, part.w, part.h)) {
      // a leaf card: every level keeps the base level's share of leaf, or a
      // tree thins to bare twigs with distance (alpha_mips.hpp)
      std::vector<int> sizes;
      const auto levels = alpha_coverage_mips(part.rgba, part.w, part.h, sizes);
      for (size_t l = 0; l < levels.size(); ++l)
        glTexImage2D(GL_TEXTURE_2D, (GLint)l + 1, GL_RGBA8, sizes[l * 2], sizes[l * 2 + 1], 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, levels[l].data());
    } else {
      glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  // and the part's surface: its normal map, which is what makes a leaf's
  // midrib and veins catch the light instead of the whole card lighting as
  // one facet. Plain mipmaps: a normal map has no cut-out to preserve.
  for (SceneObject::Part &part : o.parts) {
    if (part.normal_tex || part.normal_rgba.empty() || part.w <= 0) continue;
    glGenTextures(1, &part.normal_tex);
    glBindTexture(GL_TEXTURE_2D, part.normal_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, part.w, part.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 part.normal_rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  // and how sharply each texel reflects: a leaf's blade is waxy and glossy,
  // its veins are dull and its rim is dry, and one number for the whole leaf
  // cannot say that.
  for (SceneObject::Part &part : o.parts) {
    if (part.rough_tex || part.rough_rgba.empty() || part.w <= 0) continue;
    glGenTextures(1, &part.rough_tex);
    glBindTexture(GL_TEXTURE_2D, part.rough_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, part.w, part.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 part.rough_rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  o.gpu_dirty = false;
  // new geometry: the reduced copies for far instances are rebuilt on demand
  o.lod_tried = false;
  scene_object_bounds(o);
}

} // namespace studio
