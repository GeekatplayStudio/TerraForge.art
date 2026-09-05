// Geekatplay TerraForge - the Mesh Tools panel: analyse, repair, reduce and
// export the selected mesh object.
//
// This is Meshwright's window (Geekatplay Studio, MIT) rebuilt in our own UI.
// Its central idea is kept exactly: never a spinner and a green tick. Every
// issue carries a count, a severity and somewhere to look; every repair is
// measured against a fresh analysis of the result; and the export says when
// the file is not a closed solid, before the print rather than after it.
#include "app.hpp"
#include "gpx/mesh_engines.hpp"
#include "gpx/mesh_io.hpp"
#include "icons.hpp"
#include "mesh_object.hpp"
#include "panel_float.hpp"
#include "render_settings.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <imgui.h>
#include <json.hpp>
#include <string>

namespace studio {

std::string dialog_open_file(const char *filter, const char *def_ext);
std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested);
int ai_mesh_op(App &a, const std::string &op, const nlohmann::json &act,
               std::string &err);

namespace {

const char *MESH_FILTER =
    "Meshes (*.obj *.stl *.ply *.off)\0*.obj;*.stl;*.ply;*.off\0"
    "OBJ (*.obj)\0*.obj\0STL (*.stl)\0*.stl\0PLY (*.ply)\0*.ply\0"
    "OFF (*.off)\0*.off\0All files\0*.*\0";

ImVec4 severity_color(int sev) {
  if (sev == gpx::MESH_CRITICAL) return ImVec4(0.90f, 0.35f, 0.30f, 1.f);
  if (sev == gpx::MESH_WARNING) return ImVec4(0.87f, 0.62f, 0.24f, 1.f);
  return ImVec4(0.62f, 0.66f, 0.72f, 1.f);
}

const char *severity_word(int sev) {
  return sev == gpx::MESH_CRITICAL ? "critical"
         : sev == gpx::MESH_WARNING ? "warning"
                                    : "note";
}

// A length in the units the user reads elsewhere in the application.
std::string mm_text(double mm) {
  char buf[64];
  if (mm >= 1000.0)
    std::snprintf(buf, sizeof buf, "%.2f m", mm / 1000.0);
  else if (mm >= 10.0)
    std::snprintf(buf, sizeof buf, "%.1f mm", mm);
  else
    std::snprintf(buf, sizeof buf, "%.2f mm", mm);
  return buf;
}

void run(App &a, const char *op, const nlohmann::json &act) {
  std::string err;
  if (ai_mesh_op(a, op, act, err) <= 0 && !err.empty()) {
    a.status = "mesh: " + err;
    mesh_tools().note = err;
  }
}

// Push the selected issue's points into the viewport overlay, in world space.
// Re-pushed every frame it is selected so nothing else can quietly take the
// overlay back - a location you clicked must stay on screen.
void highlight_issue(App &a, const SceneObject &o) {
  MeshToolsState &st = mesh_tools();
  if (!st.has_report || st.issue < 0 ||
      st.issue >= (int)st.report.issues.size())
    return;
  const gpx::MeshIssue &is = st.report.issues[(size_t)st.issue];
  if (is.points.empty()) return;
  float m16[16], n9[9];
  scene_object_matrix(o, render_settings().height_scale, m16, n9);
  std::vector<float> world;
  world.reserve(is.points.size());
  for (size_t i = 0; i + 2 < is.points.size(); i += 3) {
    float x = is.points[i], y = is.points[i + 1], z = is.points[i + 2];
    world.push_back(m16[0] * x + m16[4] * y + m16[8] * z + m16[12]);
    world.push_back(m16[1] * x + m16[5] * y + m16[9] * z + m16[13]);
    world.push_back(m16[2] * x + m16[6] * y + m16[10] * z + m16[14]);
  }
  renderer_set_points_overlay(world);
  (void)a;
}

void draw_stats(const gpx::MeshStats &s, float unit_mm) {
  if (!ImGui::BeginTable("##meshstats", 2,
                         ImGuiTableFlags_SizingStretchProp))
    return;
  auto row = [&](const char *k, const std::string &v) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", k);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(v.c_str());
  };
  row("Triangles", std::to_string(s.faces));
  row("Vertices", std::to_string(s.vertices));
  row("Edges", std::to_string(s.edges));
  row("Shells", std::to_string(s.shells));
  row("Closed solid", s.watertight ? "yes" : "no");
  row("Winding", s.winding_consistent ? "consistent" : "mixed");
  if (s.has_genus) row("Genus", std::to_string(s.genus));
  char dim[96];
  std::snprintf(dim, sizeof dim, "%s x %s x %s",
                mm_text(s.dims[0] * unit_mm).c_str(),
                mm_text(s.dims[1] * unit_mm).c_str(),
                mm_text(s.dims[2] * unit_mm).c_str());
  row("Size", dim);
  if (s.watertight) {
    char v[64];
    std::snprintf(v, sizeof v, "%.3f cm3",
                  s.volume * unit_mm * unit_mm * unit_mm / 1000.0);
    row("Volume", v);
  }
  char ar[64];
  std::snprintf(ar, sizeof ar, "%.2f cm2", s.area * unit_mm * unit_mm / 100.0);
  row("Surface", ar);
  ImGui::EndTable();
}

void draw_issues(App &a, const SceneObject &o) {
  MeshToolsState &st = mesh_tools();
  if (st.report.issues.empty()) {
    ImGui::TextColored(ImVec4(0.40f, 0.78f, 0.45f, 1.f),
                       "No problems found. This mesh is ready to slice.");
    return;
  }
  ImGui::TextDisabled("Click an issue to mark it in the viewport.");
  for (int i = 0; i < (int)st.report.issues.size(); ++i) {
    const gpx::MeshIssue &is = st.report.issues[(size_t)i];
    ImGui::PushID(i);
    bool sel = st.issue == i;
    ImGui::PushStyleColor(ImGuiCol_Text, severity_color(is.severity));
    std::string label = "[" + std::string(severity_word(is.severity)) + "]  " +
                        is.title;
    if (ImGui::Selectable(label.c_str(), sel)) st.issue = sel ? -1 : i;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && !is.detail.empty())
      ImGui::SetTooltip("%s", is.detail.c_str());
    if (sel) {
      ImGui::Indent(14.f);
      ImGui::TextWrapped("%s", is.detail.c_str());
      if (is.total)
        ImGui::TextDisabled("%zu location(s), spread over %s", is.total,
                            mm_text(is.extent * mesh_unit_mm(o)).c_str());
      ImGui::Unindent(14.f);
    }
    ImGui::PopID();
  }
  (void)a;
}

void draw_repair_report() {
  MeshToolsState &st = mesh_tools();
  if (!st.has_repair) return;
  ImGui::SeparatorText("What the repair did");
  if (st.repair.fixes.empty()) {
    ImGui::TextDisabled("Nothing needed changing.");
  } else {
    for (const std::string &f : st.repair.fixes)
      ImGui::BulletText("%s", f.c_str());
  }
  // A stage that could not run is said out loud, or the table above would
  // imply that everything possible was tried.
  for (const std::string &n : st.repair.notes)
    ImGui::TextWrapped("Not done: %s", n.c_str());
  if (st.repair.changes.empty()) return;
  if (ImGui::BeginTable("##meshdiff", 3,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Measure");
    ImGui::TableSetupColumn("Before");
    ImGui::TableSetupColumn("After");
    ImGui::TableHeadersRow();
    for (const gpx::MeshChange &c : st.repair.changes) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(c.label.c_str());
      ImGui::TableNextColumn();
      if (c.boolean)
        ImGui::TextUnformatted(c.before != 0 ? "yes" : "no");
      else
        ImGui::Text("%g", c.before);
      ImGui::TableNextColumn();
      ImGui::PushStyleColor(ImGuiCol_Text,
                            c.improved ? ImVec4(0.40f, 0.78f, 0.45f, 1.f)
                                       : ImVec4(0.87f, 0.62f, 0.24f, 1.f));
      if (c.boolean)
        ImGui::TextUnformatted(c.after != 0 ? "yes" : "no");
      else
        ImGui::Text("%g", c.after);
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("Measured on the repaired mesh, not predicted.");
}

} // namespace

void draw_panel_mesh(App &a) {
  if (!a.show_mesh_tools) return;
  // A first appearance needs a size: with nothing but buttons and tables
  // inside, ImGui's auto-fit made a 32 px sliver the first time it opened.
  ImGui::SetNextWindowSize(ImVec2(430, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(320, 240), ImVec2(FLT_MAX, FLT_MAX));
  panel_float_prepare(a, "Mesh Tools");
  if (!ImGui::Begin("Mesh Tools", &a.show_mesh_tools)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "Mesh Tools");
  MeshToolsState &st = mesh_tools();

  if (ImGui::Button("Import mesh...")) {
    std::string p = dialog_open_file(MESH_FILTER, "obj");
    if (!p.empty()) run(a, "import_mesh", {{"path", p}});
  }
  ImGui::SameLine();
  ImGui::TextDisabled("OBJ, STL, PLY, OFF");

  std::string err;
  SceneObject *o = mesh_selected_object(a, err);
  if (!o) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", err.c_str());
    ImGui::TextDisabled(
        "Import a model, or pick one in the Objects tree, and this panel "
        "reports what is wrong with it and repairs it.");
    ImGui::End();
    return;
  }
  // A different object means the report on screen is about something else.
  if (st.object != scene().selected) {
    st.has_report = st.has_repair = st.has_reduce = false;
    st.issue = -1;
  }

  ImGui::Separator();
  ImGui::Text("%s", o->name.c_str());
  ImGui::TextDisabled("%d triangles", o->vert_count / 3);

  // What the file's numbers mean. STL says nothing about units and every
  // slicer reads it as millimetres; a file that meant centimetres, metres or
  // inches is told so here, and every measurement follows.
  static const char *UNIT_LABEL[] = {"millimetres", "centimetres", "metres",
                                     "inches"};
  static const float UNIT_MM[] = {1.f, 10.f, 1000.f, 25.4f};
  int unit = 0;
  for (int i = 0; i < 4; ++i)
    if (st.unit_mm == UNIT_MM[i]) unit = i;
  ImGui::SetNextItemWidth(160);
  if (ImGui::BeginCombo("File units", UNIT_LABEL[unit])) {
    for (int i = 0; i < 4; ++i)
      if (ImGui::Selectable(UNIT_LABEL[i], unit == i)) {
        st.unit_mm = UNIT_MM[i];
        if (st.has_report) {
          gpx::TriMesh m = mesh_from_object(*o);
          gpx::mesh_analyse(m, st.report, st.unit_mm);
        }
      }
    ImGui::EndCombo();
  }

  if (ImGui::Button("Analyse", ImVec2(110, 0)))
    run(a, "mesh_analyse", nlohmann::json::object());
  ImGui::SameLine();
  if (ImGui::Button("Repair", ImVec2(110, 0)))
    run(a, "mesh_repair", nlohmann::json::object());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Cleanup, orientation and hole filling, repeated until the mesh\n"
        "stops changing - then measured against a fresh analysis. Undoable.");
  ImGui::SameLine();
  if (ImGui::Button("Export...", ImVec2(110, 0))) {
    std::string p = dialog_save_file(
        "STL (*.stl)\0*.stl\0OBJ (*.obj)\0*.obj\0PLY (*.ply)\0*.ply\0"
        "OFF (*.off)\0*.off\0",
        "stl", (o->name + ".stl").c_str());
    if (!p.empty()) run(a, "mesh_export", {{"path", p}});
  }

  if (st.has_report) {
    ImGui::Separator();
    // The score, in the colour of its verdict: it is the one number that says
    // whether this file can be sliced at all.
    ImVec4 col = st.report.score >= 80   ? ImVec4(0.40f, 0.78f, 0.45f, 1.f)
                 : st.report.score >= 50 ? ImVec4(0.87f, 0.62f, 0.24f, 1.f)
                                         : ImVec4(0.90f, 0.35f, 0.30f, 1.f);
    ImGui::TextColored(col, "%d / 100", st.report.score);
    ImGui::SameLine();
    ImGui::TextUnformatted(st.report.verdict.c_str());
    ImGui::Spacing();
    draw_stats(st.report.stats, mesh_unit_mm(*o));
    ImGui::SeparatorText("Issues");
    draw_issues(a, *o);
    highlight_issue(a, *o);
  } else {
    ImGui::Separator();
    ImGui::TextDisabled("Press Analyse to measure this mesh.");
  }

  draw_repair_report();

  ImGui::SeparatorText("Rebuild as a solid");
  ImGui::BeginDisabled(!gpx::mesh_engines().solidify);
  if (ImGui::Button("Solidify", ImVec2(110, 0)))
    run(a, "mesh_solidify", nlohmann::json::object());
  ImGui::EndDisabled();
  ImGui::TextWrapped("Engines: %s", gpx::mesh_engines_text().c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Rebuilds the surface as a solid that is manifold by construction,\n"
        "and unions overlapping shells into one piece.\n"
        "Repair runs this by itself when its own stages cannot close it.");

  ImGui::SeparatorText("Reduce");
  static int target = 5000;
  // The buttons go under the field rather than beside it: at the panel's
  // default width the second one was off the edge and could not be pressed.
  ImGui::SetNextItemWidth(140);
  ImGui::InputInt("Target triangles", &target);
  target = std::clamp(target, 4, 20000000);
  if (ImGui::Button("Reduce", ImVec2(110, 0)))
    run(a, "mesh_reduce", {{"faces", target}});
  ImGui::SameLine();
  ImGui::BeginDisabled(!gpx::mesh_engines().retopo);
  if (ImGui::Button("Rebuild as quads", ImVec2(150, 0)))
    run(a, "mesh_retopo", {{"faces", target / 2}});
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(
        "Reduce thins the triangles it was given.\n"
        "Quads rebuilds the surface as evenly sized, curvature-aligned\n"
        "quads - better for a sculpt or a scan, and the only one of the\n"
        "two that changes the topology.");
  if (st.has_reduce)
    ImGui::TextDisabled("%zu -> %zu triangles, worst deviation %s (%.2f%%)",
                        st.reduce.faces_before, st.reduce.faces_after,
                        mm_text(st.reduce.max_deviation * mesh_unit_mm(*o)).c_str(),
                        st.reduce.deviation_frac * 100.0);

  if (!st.note.empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", st.note.c_str());
  }
  ImGui::End();
}

// The same three actions on the Objects tool row, beside the primitives:
// creating a cube and importing a model to fix are the same kind of act, and
// they belong in the same place.
void mesh_tool_buttons(App &a) {
  if (ImGui::SmallButton("import mesh")) {
    std::string p = dialog_open_file(MESH_FILTER, "obj");
    if (!p.empty()) run(a, "import_mesh", {{"path", p}});
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Import OBJ, STL, PLY or OFF and analyse it");
  ImGui::SameLine();
  std::string err;
  bool have = mesh_selected_object(a, err) != nullptr;
  ImGui::BeginDisabled(!have);
  if (ImGui::SmallButton("analyse"))
    run(a, "mesh_analyse", nlohmann::json::object());
  ImGui::SameLine();
  if (ImGui::SmallButton("repair"))
    run(a, "mesh_repair", nlohmann::json::object());
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("mesh tools")) a.show_mesh_tools = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Open the Mesh Tools panel: issues with locations,\n"
                      "a measured repair report, reduction and export");
}

} // namespace studio
