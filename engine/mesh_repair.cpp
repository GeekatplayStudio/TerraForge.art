// Geekatplay TerraForge - staged mesh repair.
// Ported from Meshwright's engine (MIT, Geekatplay Studio).
//
// The rule Meshwright is built on, kept here: a stage counts as a fix only if
// the diagnosis actually changed. The pipeline repeats until the mesh stops
// changing, then re-analyses the result from scratch - so the report is
// measured on the repaired mesh rather than predicted from the operations
// that ran. Meshwright's later stages (MeshFix, MeshLab, Manifold3D, voxel
// remesh) are GPL libraries and cannot ship here; the stages below are the
// ones that fix the overwhelming majority of real files, written from
// scratch, and the report says plainly when something is left over.
#include "gpx/mesh_report.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

// The tolerance under which two vertices are the same point. Derived from the
// model's own size when the caller does not care: a file in metres and the
// same file in millimetres should weld the same way.
float weld_tolerance(const TriMesh &m, float requested) {
  if (requested > 0.f) return requested;
  float lo[3], hi[3];
  if (!mesh_bounds(m, lo, hi)) return 1e-6f;
  float span = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]});
  return span > 0.f ? span * 1e-6f : 1e-6f;
}

std::string count_phrase(size_t n, const char *one, const char *many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

void mesh_repair(TriMesh &m, const MeshRepairOptions &opt,
                 MeshRepairResult &out, float unit_mm) {
  out = MeshRepairResult();
  mesh_analyse(m, out.before, unit_mm);
  if (m.empty()) {
    out.after = out.before;
    return;
  }

  const float tol = weld_tolerance(m, opt.weld_tol);
  const int max_passes = std::max(1, opt.max_passes);

  for (int pass = 0; pass < max_passes; ++pass) {
    bool moved = false;
    ++out.passes;

    // Stage 1 - junk geometry. Nothing later can be trusted while a triangle
    // has zero area or two vertices sit on the same point unjoined.
    {
      size_t degen = mesh_drop_degenerate(m);
      size_t dup = mesh_drop_duplicate_faces(m);
      size_t merged = mesh_weld(m, tol);
      // Welding can collapse a triangle onto itself, so sweep again.
      degen += mesh_drop_degenerate(m);
      dup += mesh_drop_duplicate_faces(m);
      size_t unref = mesh_drop_unreferenced(m);
      std::vector<std::string> parts;
      if (degen) parts.push_back("removed " + count_phrase(degen, "degenerate triangle", "degenerate triangles"));
      if (dup) parts.push_back("removed " + count_phrase(dup, "duplicate triangle", "duplicate triangles"));
      if (merged) parts.push_back("merged " + count_phrase(merged, "duplicate vertex", "duplicate vertices"));
      if (unref) parts.push_back("dropped " + count_phrase(unref, "unused vertex", "unused vertices"));
      if (!parts.empty()) {
        std::string line = "Cleanup: " + parts[0];
        for (size_t i = 1; i < parts.size(); ++i) line += ", " + parts[i];
        out.fixes.push_back(line);
        moved = true;
      }
    }

    // Stage 2 - orientation. Winding first (neighbours must agree before
    // "outward" means anything), then the whole-solid flip.
    {
      size_t flipped = mesh_fix_winding(m);
      if (flipped) {
        out.fixes.push_back(
            "Orientation: unified face winding, turning " +
            count_phrase(flipped, "triangle", "triangles") + " the right way");
        moved = true;
      }
      if (mesh_fix_inversion(m)) {
        out.fixes.push_back(
            "Orientation: flipped an inside-out mesh so normals face outward");
        moved = true;
      }
    }

    // Stage 3 - holes. Only closed rings are filled, and only ones small
    // enough to be a hole rather than a missing part.
    if (opt.fill_holes) {
      size_t closed = mesh_fill_holes(m, opt.max_hole_edges);
      if (closed) {
        out.fixes.push_back("Holes: closed " +
                            count_phrase(closed, "hole", "holes") +
                            " with new triangles");
        moved = true;
        // A fan can duplicate an existing triangle on a three-edge hole.
        mesh_drop_duplicate_faces(m);
      }
    }

    // Stage 4 - stray shells. Off by default: a small piece is often wanted.
    if (opt.drop_small_shells) {
      size_t gone = mesh_drop_small_shells(m, opt.min_shell_faces,
                                           opt.min_shell_extent);
      if (gone) {
        out.fixes.push_back("Shells: removed " +
                            count_phrase(gone, "stray piece", "stray pieces") +
                            " too small to print");
        moved = true;
      }
    }

    if (!moved) break;
  }

  mesh_analyse(m, out.after, unit_mm);
  out.changes = mesh_compare(out.before, out.after);
  out.changed = !out.fixes.empty();
}

} // namespace gpx
