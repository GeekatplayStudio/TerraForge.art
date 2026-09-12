// Geekatplay TerraForge — the bars above and beside the workspace.
//
// The layout every serious 3D application converges on, and the one Cinema 4D
// uses: menus on top, then what you are working on, then the commands for
// that work, with the modes down the left and the properties of whatever is
// selected down the right.
//
//   row 1   File Edit Terrain View AI Help       — classic text menus only
//   row 2   Terrain | Materials | Objects | ...  — which workflow
//   row 3   undo redo | refresh render | the settings for that workflow
//   left    the modes and tools for that workflow   (toolbar_left.cpp)
//   right   properties of the selection             (the Properties editor)
//
// Nothing about the selected object lives up here: Move, Rotate and Scale
// are modes, so they sit in the left column and in the object's own
// Properties, where every other application keeps them. The per-workspace
// settings rows are in toolbar_tools.cpp; this file is the frame and the
// palette vocabulary.
#include "app.hpp"
#include "i18n.hpp"
#include "scene.hpp"
#include "theme_colors.hpp"
#include "toolbar_internal.hpp"
#include "undo.hpp"
#include <algorithm>
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
    case WS_PLANTS: return tr("workspace.plants");
    default: return tr("workspace.terrain");
  }
}

// ---------------------------------------------------- the palette vocabulary
namespace {
bool g_vertical = false;
const float GAP = 3.f;   // between buttons of one group
const float AIR = 6.f;   // either side of a group rule
} // namespace

float tool_size() {
  return icon_toolbar_size() + ImGui::GetStyle().FramePadding.y * 2.f;
}

bool tool_vertical() { return g_vertical; }
void tool_column_begin() { g_vertical = true; }
void tool_column_end() { g_vertical = false; }

void tool_pad(float px) {
  if (g_vertical) ImGui::Dummy(ImVec2(1.f, px - GAP));
  else ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px);
}

void tool_gap(float px) {
  if (!g_vertical) ImGui::SameLine(0, px);
}

bool tool_icon(Icon ic, const char *id, const char *tip, bool active,
               bool expandable) {
  bool hit = IconButton(ic, id, tip, active, tool_size(), expandable);
  tool_gap(GAP);
  return hit;
}

bool tool_text(const char *label, const char *tip, bool active) {
  const float h = tool_size();
  const float w = std::max(h, ImGui::CalcTextSize(label).x + 14.f);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool hit = ImGui::Button(label, ImVec2(w, h));
  ImGui::PopStyleColor(4);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p1(p.x + w, p.y + h);
  ImU32 fill, frame;
  if (active) {
    fill = theme::fade(theme::accent(), held ? 0.70f : hovered ? 0.55f : 0.42f);
    frame = theme::accent();
  } else {
    fill = theme::shade(theme::LEAD_SURFACE, held ? 1.25f : hovered ? 2.3f : 1.8f);
    frame = theme::shade(theme::LEAD_SURFACE, 0.60f);
  }
  dl->AddRectFilled(p, p1, fill, 4.f);
  dl->AddRect(p, p1, frame, 4.f);
  ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f),
              active || hovered ? theme::text() : theme::shade(theme::text(), 0.85f), label);
  if (tip && hovered) ImGui::SetTooltip("%s", tip);
  tool_gap(GAP);
  return hit;
}

void tool_sep() {
  const float h = tool_size();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImU32 col = theme::fade(theme::text_dim(), 0.6f);
  if (g_vertical) {
    ImGui::Dummy(ImVec2(h, AIR - GAP));
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(p.x + 3.f, p.y), ImVec2(p.x + h - 3.f, p.y), col, 1.f);
    ImGui::Dummy(ImVec2(h, 1.f));
    ImGui::Dummy(ImVec2(h, AIR - GAP));
    return;
  }
  ImGui::SameLine(0, AIR);
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddLine(ImVec2(p.x, p.y + 3.f), ImVec2(p.x, p.y + h - 3.f), col, 1.f);
  ImGui::Dummy(ImVec2(1.f, h));
  ImGui::SameLine(0, AIR);
}

void tool_label(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  // Painted by hand, centred on the button height, and the cursor advanced
  // by a full-height dummy: moving the cursor down and back up left the
  // next tile a few pixels high, which is why "256" floated above its row.
  const float h = tool_size();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImVec2 ts = ImGui::CalcTextSize(buf);
  ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + (h - ts.y) * 0.5f), theme::text_dim(), buf);
  ImGui::Dummy(ImVec2(ts.x, h));
  ImGui::SameLine(0, 5);
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
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, st.ItemSpacing.y));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 5));
  tool_pad(8.f);

  for (int oi = 0; oi < WORKSPACE_ORDER_COUNT; ++oi) {
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
      std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
      if (lk.owns_lock()) {
        gpx::Node *n = a.graph.find_node(a.selected_node);
        if (!n || domain_of_category(n->category) != w) {
          a.selected_node = 0;
          a.prop_tab = TAB_OBJECT;
        }
      }
      if (w == WS_ATMOSPHERE) ImGui::SetWindowFocus("Environment");
      else if (w == WS_RENDER) ImGui::SetWindowFocus("Render");
      else if (w == WS_OBJECTS) ImGui::SetWindowFocus("Scene###Outliner");
      else if (w == WS_ANIMATION) ImGui::SetWindowFocus("Timeline");
      else if (w == WS_PLANTS) ImGui::SetWindowFocus("Plants");
    }
    ImGui::PopStyleColor(active ? 3 : 2);
    ImGui::SameLine();
  }
  ImGui::NewLine();
  ImGui::PopStyleVar(3);
}

// ------------------------------------------------- row 3: commands
void draw_workspace_tools(App &a); // toolbar_tools.cpp

// The commands that act on the project as a whole, the same in every
// workflow, at the head of the row where the hand already is:
//
//   [Undo Redo] | [Refresh] | [Render]
void draw_global_tools(App &a) {
  if (tool_icon(Icon::Undo, "##undo", tr("Undo the last change  (Ctrl+Z)"))) {
    if (undo_perform(a)) a.status = "undo";
  }
  if (tool_icon(Icon::Redo, "##redo", tr("Redo  (Ctrl+Y)"))) {
    if (redo_perform(a)) a.status = "redo";
  }
  tool_sep();
  if (tool_icon(Icon::Refresh, "##eval", tr("Recompute the whole graph  (F5)"))) {
    std::lock_guard<App::GraphMutex> lk(a.graph_mtx);
    a.graph.mark_all_dirty();
    a.request_eval();
  }
  if (tool_icon(Icon::Render, "##rendercam",
                tr("Render the active camera\n\nRender through the active camera with its own\n"
                   "engine, resolution and sample settings.")))
    a.request_camera_render = scene_active_camera();
  tool_sep();
  // Add: every component the scene can take, in every workspace, one tile.
  if (tool_icon(Icon::Plus, "##addcomp",
                tr("Add a component\n\nTerrains (as many as you like), infinite terrains and\n"
                   "planets; atmosphere, cloud layers, sun and water; lights,\n"
                   "cameras, objects, populations, materials. Each arrives\n"
                   "whole: the object, its node and its material."),
                false, true))
    ImGui::OpenPopup("##addcomp_menu");
  if (ImGui::BeginPopup("##addcomp_menu")) {
    component_menu_items(a);
    ImGui::EndPopup();
  }
}

void draw_tool_bar(App &a) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
  tool_pad(8.f);
  draw_global_tools(a);
  tool_sep();
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
      std::unique_lock<App::GraphMutex> lk(a.graph_mtx, std::try_to_lock);
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
  const float want = avail - w - 10.f;
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

} // namespace studio
