// Geekatplay TerraForge - a mesh object's geometry and pictures onto the
// GPU. Split from renderer_scene.cpp for the 500-line module rule: this is
// the one place a mesh's VAO is laid out, so every reader of `verts` (the
// colour pass, the shadow pass, picking) sees the same six-float layout.
#include "renderer_internal.hpp"
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
  glBindVertexArray(0);
  // the parts' pictures, decoded on load, become textures here
  for (SceneObject::Part &part : o.parts) {
    if (part.tex || part.rgba.empty() || part.w <= 0) continue;
    glGenTextures(1, &part.tex);
    glBindTexture(GL_TEXTURE_2D, part.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, part.w, part.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 part.rgba.data());
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
