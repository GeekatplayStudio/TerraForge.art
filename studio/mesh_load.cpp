// Geekatplay TerraForge - a model file into a scene object: geometry from
// the engine's readers (gpx/mesh_io.hpp), texture coordinates and the
// materials' pictures decoded to RGBA. Split from mesh_object.cpp so the
// scene can load meshes without the Mesh Tools panel behind it (the tests
// link the scene alone).
#include "mesh_object.hpp"
#include "gpx/mesh_io.hpp"
#include "scene.hpp"
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace studio {

void mesh_to_object(SceneObject &o, const gpx::TriMesh &m) {
  o.verts.clear();
  o.verts.reserve(m.face_count() * 18);
  for (size_t i = 0; i < m.face_count(); ++i) {
    const uint32_t *fc = m.face(i);
    const float *p0 = m.vert(fc[0]), *p1 = m.vert(fc[1]), *p2 = m.vert(fc[2]);
    float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    float nx = u[1] * v[2] - u[2] * v[1];
    float ny = u[2] * v[0] - u[0] * v[2];
    float nz = u[0] * v[1] - u[1] * v[0];
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.f) {
      nx /= len;
      ny /= len;
      nz /= len;
    } else {
      ny = 1.f;
    }
    for (const float *p : {p0, p1, p2}) {
      o.verts.insert(o.verts.end(), p, p + 3);
      o.verts.push_back(nx);
      o.verts.push_back(ny);
      o.verts.push_back(nz);
    }
  }
  o.vert_count = (int)(o.verts.size() / 6);
  o.gpu_dirty = true;

  // texture coordinates, per triangle corner like the vertices
  o.uvs.clear();
  const bool have_uv = m.uv.size() == m.vert_count() * 2;
  if (have_uv) {
    o.uvs.reserve(m.f.size() * 2);
    for (size_t i = 0; i < m.f.size(); ++i) {
      uint32_t vi = m.f[i];
      o.uvs.push_back(vi < m.vert_count() ? m.uv[(size_t)vi * 2] : 0.f);
      o.uvs.push_back(vi < m.vert_count() ? m.uv[(size_t)vi * 2 + 1] : 0.f);
    }
  }
  // the materials, as vertex runs; each picture decoded once and shared
  for (SceneObject::Part &p : o.parts) p.tex = 0; // the GL textures die with the old vao
  o.parts.clear();
  std::vector<std::pair<std::vector<uint8_t>, std::pair<int, int>>> decoded(m.images.size());
  std::vector<bool> tried(m.images.size(), false);
  auto image = [&](int i) -> const std::pair<std::vector<uint8_t>, std::pair<int, int>> * {
    if (i < 0 || i >= (int)m.images.size()) return nullptr;
    if (!tried[(size_t)i]) {
      tried[(size_t)i] = true;
      const gpx::MeshImage &mi = m.images[(size_t)i];
      int w = 0, h = 0, comp = 0;
      unsigned char *px = nullptr;
      if (!mi.bytes.empty()) px = stbi_load_from_memory(mi.bytes.data(), (int)mi.bytes.size(), &w, &h, &comp, 4);
      else if (!mi.path.empty()) px = stbi_load(mi.path.c_str(), &w, &h, &comp, 4);
      if (px) {
        // big pictures are halved until they fit 2048: the viewport does
        // not need more, and the memory would be paid by every copy
        std::vector<uint8_t> rgba(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        while (w > 2048 || h > 2048) {
          int nw = w / 2, nh = h / 2;
          std::vector<uint8_t> half((size_t)nw * nh * 4);
          for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
              for (int k = 0; k < 4; ++k) {
                int s = rgba[((size_t)(y * 2) * w + x * 2) * 4 + k] + rgba[((size_t)(y * 2) * w + x * 2 + 1) * 4 + k] +
                        rgba[((size_t)(y * 2 + 1) * w + x * 2) * 4 + k] + rgba[((size_t)(y * 2 + 1) * w + x * 2 + 1) * 4 + k];
                half[((size_t)y * nw + x) * 4 + k] = (uint8_t)(s / 4);
              }
          rgba.swap(half);
          w = nw;
          h = nh;
        }
        decoded[(size_t)i] = {std::move(rgba), {w, h}};
      }
    }
    return decoded[(size_t)i].first.empty() ? nullptr : &decoded[(size_t)i];
  };
  for (const gpx::MeshPart &mp : m.parts) {
    SceneObject::Part p;
    p.first = (int)mp.first_face * 3;
    p.count = (int)mp.face_count * 3;
    p.name = mp.name;
    for (int k = 0; k < 3; ++k) p.color[k] = mp.color[k];
    if (const auto *img = image(mp.image)) {
      p.rgba = img->first;
      p.w = img->second.first;
      p.h = img->second.second;
      // a separate opacity picture (a leaf card's cut-out) becomes the colour
      // picture's alpha, resampled to its size; the shaders cut on alpha
      if (mp.alpha_image >= 0)
        if (const auto *am = image(mp.alpha_image)) {
          const int aw = am->second.first, ah = am->second.second;
          for (int y = 0; y < p.h; ++y) {
            const int ay = std::min((int)((y + 0.5f) * ah / p.h), ah - 1);
            for (int x = 0; x < p.w; ++x) {
              const int ax = std::min((int)((x + 0.5f) * aw / p.w), aw - 1);
              const uint8_t *src = &am->first[((size_t)ay * aw + ax) * 4];
              // white = opaque: the picture's own alpha when it has one, else its brightness
              p.rgba[((size_t)y * p.w + x) * 4 + 3] = src[3] < 255 ? src[3] : (uint8_t)((src[0] + src[1] + src[2]) / 3);
            }
          }
        }
    }
    if (p.count > 0) o.parts.push_back(std::move(p));
  }
}

bool scene_load_mesh(const std::string &path, SceneObject &o, std::string &err) {
  gpx::TriMesh m;
  if (!gpx::mesh_load(path, m, err)) return false;
  o.path = path;
  mesh_to_object(o, m);
  return true;
}

int scene_import_mesh(const std::string &path, std::string &err) {
  gpx::TriMesh m;
  if (!gpx::mesh_load(path, m, err)) return -1;
  SceneObject o;
  o.type = SceneObject::Mesh;
  o.path = path;
  size_t slash = path.find_last_of("/\\");
  o.name = slash == std::string::npos ? path : path.substr(slash + 1);

  // The geometry is left exactly as the file has it. A model's coordinates
  // are the one thing a repair tool must not quietly rewrite - a millimetre
  // in the file has to still be a millimetre in the report - so the object is
  // placed and sized by its transform instead, which changes nothing about
  // the mesh itself.
  float lo[3], hi[3];
  if (gpx::mesh_bounds(m, lo, hi)) {
    float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    if (span > 0.f) o.scale = 0.08f / span; // about a tenth of the tile
  }
  mesh_to_object(o, m);
  scene().objects.push_back(std::move(o));
  return (int)scene().objects.size() - 1;
}

} // namespace studio
