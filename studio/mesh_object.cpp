// Geekatplay TerraForge - scene object <-> mesh module. See mesh_object.hpp.
#include "mesh_object.hpp"
#include "app.hpp"
#include "gpx/mesh_io.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>

namespace studio {

gpx::TriMesh mesh_from_object(const SceneObject &o) {
  gpx::TriMesh m;
  const size_t n = (size_t)o.vert_count;
  m.v.reserve(n * 3);
  m.f.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    m.v.push_back(o.verts[i * 6 + 0]);
    m.v.push_back(o.verts[i * 6 + 1]);
    m.v.push_back(o.verts[i * 6 + 2]);
    m.f.push_back((uint32_t)i);
  }
  // Weld on the object's own scale: a file in millimetres and the same file
  // in metres must join the same corners.
  float lo[3], hi[3];
  if (gpx::mesh_bounds(m, lo, hi)) {
    float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
    gpx::mesh_weld(m, span > 0.f ? span * 1e-6f : 1e-6f);
  }
  return m;
}

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

SceneObject *mesh_selected_object(App &a, std::string &err) {
  SceneState &sc = scene();
  if (sc.selected < 0 || sc.selected >= (int)sc.objects.size()) {
    err = "select a mesh object first";
    return nullptr;
  }
  SceneObject &o = sc.objects[(size_t)sc.selected];
  if (o.type != SceneObject::Mesh || o.vert_count <= 0) {
    err = "'" + o.name + "' is not a mesh object";
    return nullptr;
  }
  (void)a;
  return &o;
}

float mesh_unit_mm(const SceneObject &o) {
  // What one unit of the FILE means, in millimetres. STL has no units and
  // every slicer reads it as millimetres, so that is the default; the panel
  // lets the user say otherwise for a file that meant centimetres, metres or
  // inches. The world transform is deliberately not part of this: scaling an
  // object in the scene does not change what the file measures.
  (void)o;
  return mesh_tools().unit_mm;
}

} // namespace studio
