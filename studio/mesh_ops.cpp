// Geekatplay TerraForge - mesh diagnosis, repair, reduction, import and
// export as operations: the Objects menu, the assistant, the Python API and
// MCP all run these, so a script can do everything the panel can.
//
// The module this drives is a port of Meshwright (Geekatplay Studio, MIT):
// engine/mesh_analysis.cpp, mesh_repair.cpp, mesh_reduce.cpp, mesh_io.cpp.
#include "app.hpp"
#include "gpx/mesh_engines.hpp"
#include "gpx/mesh_io.hpp"
#include "mesh_object.hpp"
#include "scene.hpp"
#include "undo.hpp"
#include <algorithm>
#include <cstdio>
#include <json.hpp>
#include <string>

using nlohmann::json;

namespace studio {

MeshToolsState &mesh_tools() {
  static MeshToolsState s;
  return s;
}

namespace {

std::string one_line(const gpx::MeshReport &r) {
  char buf[256];
  std::snprintf(buf, sizeof buf, "%s - score %d, %zu faces, %s",
                r.verdict.c_str(), r.score, r.stats.faces,
                r.stats.watertight ? "watertight" : "not watertight");
  return buf;
}

} // namespace

bool mesh_tools_analyse(App &a, std::string &err) {
  SceneObject *o = mesh_selected_object(a, err);
  if (!o) return false;
  gpx::TriMesh m = mesh_from_object(*o);
  MeshToolsState &st = mesh_tools();
  gpx::mesh_analyse(m, st.report, mesh_unit_mm(*o));
  st.has_report = true;
  st.object = scene().selected;
  st.issue = -1;
  st.note = one_line(st.report);
  a.status = "mesh: " + st.note;
  return true;
}

// Returns 1 handled and changed, 0 handled and refused, -1 not ours.
int ai_mesh_op(App &a, const std::string &op, const json &act,
               std::string &err) {
  if (op == "import_mesh" || op == "import_object") {
    std::string path = act.value("path", std::string());
    if (path.empty()) {
      err = "import_mesh needs a 'path'";
      return 0;
    }
    int idx = scene_import_mesh(path, err);
    if (idx < 0) return 0;
    scene().selected = idx;
    a.status = "imported " + scene().objects[(size_t)idx].name;
    // A fresh import is worth a look immediately: an imported file is exactly
    // where the holes and the loose vertices come from.
    std::string ignored;
    mesh_tools_analyse(a, ignored);
    return 1;
  }

  if (op == "mesh_analyse" || op == "mesh_analyze")
    return mesh_tools_analyse(a, err) ? 1 : 0;

  if (op == "mesh_repair") {
    SceneObject *o = mesh_selected_object(a, err);
    if (!o) return 0;
    gpx::TriMesh m = mesh_from_object(*o);
    gpx::MeshRepairOptions opt;
    opt.fill_holes = act.value("fill_holes", opt.fill_holes);
    opt.drop_small_shells =
        act.value("drop_small_shells", opt.drop_small_shells);
    opt.max_passes = std::clamp(act.value("passes", opt.max_passes), 1, 8);
    if (act.contains("max_hole_edges"))
      opt.max_hole_edges = (size_t)std::max(3, act.value("max_hole_edges", 5000));
    MeshToolsState &st = mesh_tools();
    gpx::mesh_repair(m, opt, st.repair, mesh_unit_mm(*o));
    if (!st.repair.changed) {
      st.has_repair = true;
      st.report = st.repair.after;
      st.has_report = true;
      st.object = scene().selected;
      st.note = "nothing to repair - " + one_line(st.report);
      a.status = "mesh: " + st.note;
      return 0;
    }
    undo_push(a, "repair mesh");
    mesh_to_object(*o, m);
    st.has_repair = true;
    st.has_report = true;
    st.report = st.repair.after;
    st.object = scene().selected;
    st.issue = -1;
    st.note = std::to_string(st.repair.fixes.size()) + " fix(es) - " +
              one_line(st.report);
    a.status = "mesh: " + st.note;
    return 1;
  }

  if (op == "mesh_reduce") {
    SceneObject *o = mesh_selected_object(a, err);
    if (!o) return 0;
    gpx::TriMesh m = mesh_from_object(*o);
    size_t target = 0;
    if (act.contains("faces"))
      target = (size_t)std::max(4, act.value("faces", 0));
    else if (act.contains("fraction"))
      target = (size_t)(m.face_count() *
                        std::clamp(act.value("fraction", 0.5f), 0.01f, 1.f));
    if (!target) {
      err = "mesh_reduce needs 'faces' (a target count) or 'fraction'";
      return 0;
    }
    MeshToolsState &st = mesh_tools();
    if (!gpx::mesh_reduce(m, target, st.reduce)) {
      err = "the mesh already has " + std::to_string(m.face_count()) +
            " faces or fewer";
      return 0;
    }
    undo_push(a, "reduce mesh");
    mesh_to_object(*o, m);
    st.has_reduce = true;
    gpx::mesh_analyse(m, st.report, mesh_unit_mm(*o));
    st.has_report = true;
    st.object = scene().selected;
    st.issue = -1;
    char buf[220];
    std::snprintf(buf, sizeof buf,
                  "%zu -> %zu faces, largest deviation %.3f%% of the model",
                  st.reduce.faces_before, st.reduce.faces_after,
                  st.reduce.deviation_frac * 100.0);
    st.note = buf;
    a.status = "mesh: " + st.note;
    return 1;
  }

  if (op == "mesh_solidify") {
    SceneObject *o = mesh_selected_object(a, err);
    if (!o) return 0;
    gpx::TriMesh m = mesh_from_object(*o);
    if (!gpx::mesh_solidify(m, err)) return 0;
    undo_push(a, "solidify mesh");
    mesh_to_object(*o, m);
    MeshToolsState &st = mesh_tools();
    gpx::mesh_analyse(m, st.report, mesh_unit_mm(*o));
    st.has_report = true;
    st.object = scene().selected;
    st.issue = -1;
    st.note = "rebuilt as a solid - " + one_line(st.report);
    a.status = "mesh: " + st.note;
    return 1;
  }

  if (op == "mesh_split") {
    SceneObject *o = mesh_selected_object(a, err);
    if (!o) return 0;
    gpx::TriMesh m = mesh_from_object(*o);
    std::vector<int> label;
    int n = gpx::mesh_components(m, label);
    if (n <= 1) {
      err = "'" + o->name + "' is a single connected piece";
      return 0;
    }
    // Each shell becomes its own object, keeping the original's transform, so
    // a multi-part model can be examined, repaired and printed piece by piece.
    undo_push(a, "split mesh");
    SceneObject base = *o;
    const std::string stem = base.name;
    std::vector<gpx::TriMesh> parts((size_t)n);
    for (size_t i = 0; i < m.face_count(); ++i) {
      gpx::TriMesh &p = parts[(size_t)label[i]];
      const uint32_t *fc = m.face(i);
      for (int k = 0; k < 3; ++k) {
        p.f.push_back((uint32_t)p.vert_count());
        p.v.insert(p.v.end(), m.vert(fc[k]), m.vert(fc[k]) + 3);
      }
    }
    int first = -1;
    for (int c = 0; c < n; ++c) {
      gpx::TriMesh &p = parts[(size_t)c];
      gpx::mesh_weld(p, 1e-6f);
      SceneObject piece = base;
      piece.name = stem + " part " + std::to_string(c + 1);
      piece.vao = piece.vbo = 0; // its own GPU buffer, not the original's
      piece.inst.clear();
      mesh_to_object(piece, p);
      scene().objects.push_back(std::move(piece));
      if (first < 0) first = (int)scene().objects.size() - 1;
    }
    // The original goes: its pieces are the model now.
    int idx = scene().selected;
    scene().objects.erase(scene().objects.begin() + idx);
    scene().selected = first - 1;
    a.scene_selection_serial++;
    mesh_tools().has_report = false;
    a.status = "split '" + stem + "' into " + std::to_string(n) + " pieces";
    mesh_tools().note = a.status;
    return 1;
  }

  if (op == "mesh_export") {
    SceneObject *o = mesh_selected_object(a, err);
    if (!o) return 0;
    std::string path = act.value("path", std::string());
    if (path.empty()) {
      err = "mesh_export needs a 'path' (.stl, .obj, .ply or .off)";
      return 0;
    }
    gpx::TriMesh m = mesh_from_object(*o);
    if (act.value("apply_transform", false)) {
      // Written where it sits in the world rather than in its own space:
      // what a print farm wants when several parts share a plate.
      float m16[16], n9[9];
      scene_object_matrix(*o, 1.f, m16, n9);
      for (size_t i = 0; i < m.vert_count(); ++i) {
        float x = m.v[i * 3], y = m.v[i * 3 + 1], z = m.v[i * 3 + 2];
        m.v[i * 3] = m16[0] * x + m16[4] * y + m16[8] * z + m16[12];
        m.v[i * 3 + 1] = m16[1] * x + m16[5] * y + m16[9] * z + m16[13];
        m.v[i * 3 + 2] = m16[2] * x + m16[6] * y + m16[10] * z + m16[14];
      }
    }
    if (!gpx::mesh_save(path, m, err, act.value("ascii", false))) return 0;
    gpx::MeshSolidity sol = gpx::mesh_solidity(m, mesh_unit_mm(*o));
    a.status = "wrote " + path;
    // Saying it now is the whole point: an open surface slices as a shell
    // with no infill, and nobody discovers that until the print is done.
    if (!sol.warnings.empty()) a.status += " - " + sol.warnings[0];
    mesh_tools().note = a.status;
    return 1;
  }

  return -1;
}

} // namespace studio
