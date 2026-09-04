// Geekatplay TerraForge - the bridge between a scene object and the mesh
// module (engine/mesh_*.cpp).
//
// The renderer wants a triangle soup with a normal on every corner; every
// question about topology - is it closed, how many shells, where are the
// holes - needs the opposite, an indexed mesh where a shared corner is one
// vertex. So the two representations both exist, and this is the only place
// that converts between them.
#pragma once
#include "gpx/mesh_report.hpp"
#include <string>

namespace studio {

struct App;
struct SceneObject;

// Scene object -> indexed mesh. Welds the soup back into shared vertices,
// which is what makes an imported STL analysable at all.
gpx::TriMesh mesh_from_object(const SceneObject &o);

// Indexed mesh -> scene object geometry, with flat normals recomputed and the
// GPU buffer marked for upload. The transform is left alone.
void mesh_to_object(SceneObject &o, const gpx::TriMesh &m);

// Import any mesh file we read (OBJ, STL, PLY, OFF) as a new scene object,
// scaled to a sensible size in the world. Returns the object index, or -1.
int scene_import_mesh(const std::string &path, std::string &err);

// The selected object as a mesh, or nullptr with a reason. Used by every op
// and by the panel, so "no object selected" is worded once.
SceneObject *mesh_selected_object(App &a, std::string &err);

// One millimetre in world units for the given object, so the report can talk
// in millimetres: the world unit is the terrain tile.
float mesh_unit_mm(const SceneObject &o);

// The Mesh Tools panel's state, shared with the ops so a scripted repair and
// a clicked one leave the same thing on screen.
struct MeshToolsState {
  int object = -1;              // scene index the report belongs to
  bool has_report = false;
  gpx::MeshReport report;
  bool has_repair = false;
  gpx::MeshRepairResult repair;
  bool has_reduce = false;
  gpx::MeshReduceResult reduce;
  int issue = -1;               // which issue is highlighted, -1 for none
  // What one unit of the file means. STL carries no units and slicers read
  // it as millimetres, so that is the default.
  float unit_mm = 1.f;
  std::string note;             // last thing that happened, for the panel
};
MeshToolsState &mesh_tools();

// Analyse the selected object and store the result in mesh_tools().
bool mesh_tools_analyse(App &a, std::string &err);

} // namespace studio
