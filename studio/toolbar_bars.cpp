// Geekatplay TerraForge — the bars above and beside the workspace.
//
// The layout every serious 3D application converges on, and the one Cinema 4D
// uses: menus on top, then what you are working on, then the tools for that
// work, with global tools down the left and the properties of whatever is
// selected down the right.
//
//   row 1   File Edit Terrain View Help          — classic text menus only
//   row 2   Terrain | Materials | Atmosphere | Render  — which workflow
//   row 3   the tools for that workflow          — changes with row 2
//   left    global tools                         — the same in every workflow
//   right   properties of the selection          — the Properties editor
//
// It was all one row before, which is why it read as a wall: the menus, the
// workflow tabs, the camera, the resolution and the statistics all had equal
// weight and none of them were grouped. The per-workspace rows are in
// toolbar_tools.cpp; this file is the frame, the palette vocabulary and the
// global tools.
#include "app.hpp"
#include "i18n.hpp"
#include "gizmo.hpp"
#include "sculpt.hpp"
#include "theme_colors.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include <cstdarg>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace studio {

const char *workspace_name(int ws) {
  switch (ws) {
    case WS_MATERIALS: return tr("workspace.materials");
    case WS_ATMOSPHERE: return tr("workspace.atmosphere");
    case WS_RENDER: return tr("workspace.render");
    case WS_ALL: return tr("workspace.all");
    case WS_OBJECTS: return tr("workspace.objects");
    case WS_LIGHTING: return tr("workspace.lighting");
    case WS_CAMERAS: return tr("workspace.cameras");
    case WS_ANIMATION: return tr("workspace.animation");
    default: return tr("workspace.terrain");
  }
}

// ---------------------------------------------------- the palette vocabulary
float tool_size() {
  return icon_toolbar_size() + ImGui::GetStyle().FramePadding.y * 2.f;
}

bool tool_icon(Icon ic, const char *id, const char *tip, bool active) {
  bool hit = IconButton(ic, id, tip, active, tool_size());
  ImGui::SameLine(0, 2);
  return hit;
}

void tool_sep() {
  ImGui::SameLine(0, 4);
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = tool_size();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 3.f), ImVec2(p.x, p.y + h - 3.f),
                                      theme::fade(theme::text_dim(), 0.6f), 1.f);
  ImGui::Dummy(ImVec2(1.f, h));
  ImGui::SameLine(0, 4);
}

void tool_label(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  // centred on the button height, whatever the font size
  const float h = tool_size();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (h - ImGui::GetTextLineHeight()) * 0.5f);
  ImGui::TextDisabled("%s", buf);
  ImGui::SameLine(0, 4);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (h - ImGui::GetTextLineHeight()) * 0.5f);
}

// What the graph is currently holding, for the readout at the end of the tool
// row. Buffers dominate; everything else is noise beside them.
static size_t graph_memory_bytes(App &a) {
  size_t total = 0;
  for (auto &n : a.graph.nodes)
    for (auto &p : n->ports) {
      if (p.hmap) total += p.hmap->v.size() * sizeof(float);
      if (p.tex) total += p.tex->v.size() * sizeof(float);
    }
  return total;
}

// ------------------------------------------------------------ row 2: tabs
// Wide, evenly weighted, and clearly the most important control on the screen,
// because choosing the workflow changes everything below it.
void draw_workspace_bar(App &a) {
  ImGuiStyle &st = ImGui::GetStyle();
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, st.ItemSpacing.y));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 5));

  for (int oi = 0; oi < 8; ++oi) {
    const int w = WORKSPACE_ORDER[oi];
    const bool active = a.workspace == w;
    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::ColorConvertU32ToFloat4(theme::accent()));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImGui::ColorConvertU32ToFloat4(theme::accent()));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.09f, 0.08f, 1.f));
    } else {
      ImGui::PushStyleColor(
          ImGuiCol_Button,
          ImGui::ColorConvertU32ToFloat4(theme::shade(theme::panel_bg(), 0.85f)));
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::ColorConvertU32ToFloat4(theme::text_dim()));
    }
    if (ImGui::Button(workspace_name(w)) && a.workspace != w) {
      a.workspace = w;
      // a node from another domain must not linger in the inspector
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        gpx::Node *n = a.graph.find_node(a.selected_node);
        if (!n || domain_of_category(n->category) != w) {
          a.selected_node = 0;
          a.prop_tab = TAB_OBJECT;
        }
      }
      if (w == WS_ATMOSPHERE) ImGui::SetWindowFocus("Environment");
      else if (w == WS_RENDER) ImGui::SetWindowFocus("Render");
      else if (w == WS_OBJECTS) ImGui::SetWindowFocus("Objects");
      else if (w == WS_ANIMATION) ImGui::SetWindowFocus("Timeline");
    }
    ImGui::PopStyleColor(active ? 3 : 2);
    ImGui::SameLine();
  }
  ImGui::NewLine();
  ImGui::PopStyleVar(3);
}

// ------------------------------------------------- row 3: workflow tools
void draw_workspace_tools(App &a); // toolbar_tools.cpp

void draw_tool_bar(App &a) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  draw_workspace_tools(a);

  // Status and statistics sit at the right-hand end of the tool row, where
  // they are readable but never in the way of a control.
  const char *right = nullptr;
  static char buf[256];
  if (a.eval.running.load()) {
    int done = a.eval.progress_done.load(), total = a.eval.progress_total.load();
    std::string cur;
    {
      std::lock_guard<std::mutex> lk(a.eval.mtx);
      cur = a.eval.current_node;
    }
    snprintf(buf, sizeof buf, tr("computing %s (%d/%d)"), cur.c_str(), done, total);
    right = buf;
  } else {
    static double total_ms = 0;
    static size_t mem = 0, count = 0;
    {
      std::unique_lock<std::mutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        mem = graph_memory_bytes(a);
        total_ms = 0;
        for (auto &n : a.graph.nodes) total_ms += n->last_compute_ms;
        count = a.graph.nodes.size();
      }
    }
    snprintf(buf, sizeof buf, tr("%zu nodes \xC2\xB7 %.0f MB \xC2\xB7 %.0f ms"),
             count, mem / (1024.0 * 1024.0), total_ms);
    right = buf;
  }
  // Right-aligned, but only when it fits. Forcing the cursor past the content
  // region makes the row scrollable, and a scrolled toolbar clips its first
  // control — which is how "res" lost its r.
  const float w = ImGui::CalcTextSize(right).x;
  const float avail = ImGui::GetWindowContentRegionMax().x;
  ImGui::SameLine();
  const float want = avail - w - 8.f;
  if (want > ImGui::GetCursorPosX() + 12.f) ImGui::SetCursorPosX(want);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                       (tool_size() - ImGui::GetTextLineHeight()) * 0.5f);
  if (a.eval.running.load()) {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::ColorConvertU32ToFloat4(theme::accent()));
    ImGui::TextUnformatted(right);
    ImGui::PopStyleColor();
  } else {
    ImGui::TextDisabled("%s", right);
  }
  ImGui::PopStyleVar();
}

// --------------------------------------------------- the global tools
// The same in every workflow, because these are things you do *to* the
// project rather than to one part of it. Nothing goes here that does not
// work — a palette of dead buttons is worse than no palette. They sit on
// the menu row as icons: vertical text buttons down the left edge cost a
// whole column of window and put undo where nobody looks for it.
//
//   [Undo Redo] [Refresh] [Brush] [Move Rotate Scale | Twist Bend Skew Taper]
//   [Gizmos] [Console]
void draw_global_tools(App &a) {
  if (tool_icon(Icon::Undo, "##undo", tr("Undo the last change  (Ctrl+Z)"))) {
    if (undo_perform(a)) a.status = "undo";
  }
  if (tool_icon(Icon::Redo, "##redo", tr("Redo  (Ctrl+Y)"))) {
    if (redo_perform(a)) a.status = "redo";
  }
  tool_sep();
  if (tool_icon(Icon::Refresh, "##eval", tr("Recompute the whole graph  (F5)"))) {
    std::lock_guard<std::mutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  tool_sep();
  SculptState &s = sculpt_state();
  if (tool_icon(Icon::Brush, "##brush", tr("Sculpt: brush directly on the terrain"), s.active))
    s.active = !s.active;

  // Transform tools. These used to be duplicates of the per-view wireframe,
  // grid and sky toggles, which now live in each viewport's own header where
  // they belong - a global bar should carry what is global.
  tool_sep();
  GizmoMode &gm = gizmo_mode();
  auto tool = [&](Icon ic, const char *id, GizmoMode m, const char *tip) {
    if (tool_icon(ic, id, tip, gm == m)) gm = gm == m ? GizmoMode::None : m;
  };
  tool(Icon::Move, "##gmove", GizmoMode::Move,
       tr("Move tool  (W)\n\nDrag an axis in any viewport to move the\n"
          "selected object. The same numbers are in Properties,\n"
          "in metres."));
  tool(Icon::Rotate, "##grot", GizmoMode::Rotate,
       tr("Rotate tool  (E)\n\nDrag a ring to turn the selected object.\n"
          "Heading, pitch and bank, in degrees."));
  tool(Icon::Scale, "##gscl", GizmoMode::Scale,
       tr("Scale tool  (R)\n\nDrag an axis box to squeeze one axis, or\n"
          "the centre box to resize the whole object."));
  tool_sep();
  gizmo_deform_tools(a); // twist, bend, skew, taper, and the Gizmos switch
  tool_sep();
  if (tool_icon(Icon::Node, "##console", tr("Show the console"), a.show_console))
    a.show_console = !a.show_console;
}

} // namespace studio
