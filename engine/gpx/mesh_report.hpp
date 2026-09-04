// Geekatplay TerraForge - what a mesh measures, what is wrong with it, and
// what a repair changed.
//
// Ported from Meshwright (Geekatplay Studio, MIT). The shape of the report is
// deliberately the same: a slicer needs a closed solid, and the difference
// between "mesh has errors" and a usable tool is that every issue carries a
// count, a severity and somewhere to look.
#pragma once
#include "mesh.hpp"
#include <string>
#include <vector>

namespace gpx {

enum MeshSeverity { MESH_INFO = 0, MESH_WARNING = 1, MESH_CRITICAL = 2 };

struct MeshIssue {
  std::string id;     // "holes", "nonmanifold", ... stable, for scripts
  std::string title;  // one line, already counted: "3 open holes"
  std::string detail; // why it matters, in the user's language
  int severity = MESH_INFO;
  size_t count = 0;
  // Where it is, so the viewport can point at it. Sampled down to at most
  // MESH_MAX_LOCATIONS points; `total` is how many there really were.
  std::vector<float> points; // xyz triples
  size_t total = 0;
  float center[3] = {0, 0, 0};
  float extent = 0.f;
};

static const size_t MESH_MAX_LOCATIONS = 1500;

struct MeshStats {
  size_t vertices = 0, faces = 0, edges = 0, shells = 0;
  bool watertight = false, winding_consistent = false, manifold = false;
  size_t boundary_edges = 0, holes = 0, nonmanifold_edges = 0;
  size_t degenerate_faces = 0, duplicate_faces = 0;
  size_t duplicate_vertices = 0, unreferenced_vertices = 0, sliver_faces = 0;
  bool inverted = false;
  bool has_euler = false;
  int euler = 0;
  bool has_genus = false;
  int genus = 0;
  float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0}, dims[3] = {0, 0, 0};
  float max_dim = 0.f;
  double volume = 0.0; // model units cubed, 0 unless watertight
  double area = 0.0;
};

struct MeshReport {
  MeshStats stats;
  std::vector<MeshIssue> issues;
  int score = 0; // 0..100 readiness
  std::string verdict;
};

// The whole diagnosis. `unit_mm` is how many millimetres one model unit is,
// used only for the "unusual scale" advice.
void mesh_analyse(const TriMesh &m, MeshReport &out, float unit_mm = 1.f);

// One line per measurement that moved, for the before/after table.
struct MeshChange {
  std::string label;
  double before = 0, after = 0;
  bool improved = false;
  bool boolean = false; // render as yes/no rather than a number
};
std::vector<MeshChange> mesh_compare(const MeshReport &before,
                                     const MeshReport &after);

// ------------------------------------------------------------------- repair
struct MeshRepairOptions {
  // Absolute distance under which two vertices are the same point. 0 asks for
  // a tolerance derived from the model's own size, which is what an imported
  // file in unknown units needs.
  float weld_tol = 0.f;
  bool fill_holes = true;
  size_t max_hole_edges = 5000;
  bool drop_small_shells = false; // off by default: a small piece may be wanted
  size_t min_shell_faces = 8;
  float min_shell_extent = 0.005f; // fraction of the model's own size
  int max_passes = 3;
  // Last resort when our own stages cannot close the surface: rebuild it
  // as a solid with the Manifold engine, if this build has it. Skipped
  // silently when the mesh is already closed; reported when unavailable.
  bool solidify = true;
};

struct MeshRepairResult {
  std::vector<std::string> fixes; // "Cleanup: merged 412 duplicate vertices"
  // Stages that could not run, in the user's language. A repair that
  // skipped something must say so rather than let the report imply
  // that everything possible was tried.
  std::vector<std::string> notes;
  MeshReport before, after;
  std::vector<MeshChange> changes;
  int passes = 0;
  bool changed = false;
};

// Staged repair: cleanup, orientation, holes, shells - repeated until the
// mesh stops changing, then re-analysed from scratch so the report is
// measured on the result rather than predicted from the operations.
void mesh_repair(TriMesh &m, const MeshRepairOptions &opt,
                 MeshRepairResult &out, float unit_mm = 1.f);

// ------------------------------------------------------------------ reduce
struct MeshReduceResult {
  size_t faces_before = 0, faces_after = 0;
  double max_deviation = 0.0; // model units, measured against the original
  double deviation_frac = 0.0; // as a fraction of the model's own size
};

// Quadric error decimation to a face count. Returns false if the target is
// already met. `deviation` is measured, not estimated.
bool mesh_reduce(TriMesh &m, size_t target_faces, MeshReduceResult &out);

// Is this a printable solid, and what would a slicer make of it?
struct MeshSolidity {
  bool is_solid = false;
  bool watertight = false;
  size_t boundary_edges = 0;
  double volume = 0.0;
  double avg_wall = 0.0; // 2V/A, the average wall of a hollow shell
  bool has_wall = false;
  std::vector<std::string> warnings;
};
MeshSolidity mesh_solidity(const TriMesh &m, float unit_mm = 1.f);

} // namespace gpx
