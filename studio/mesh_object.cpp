// Geekatplay TerraForge - scene object <-> mesh module. See mesh_object.hpp.
#include "mesh_object.hpp"
#include "app.hpp"
#include "gpx/mesh_io.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
