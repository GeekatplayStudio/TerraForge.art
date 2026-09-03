// Geekatplay TerraForge - the default docking layout: which panel
// goes where on first run and on View > Reset layout, and the 1-6
// viewport cell arrangements. Split from app.cpp for the 500-line
// module rule.
#include "app.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace studio {

void build_default_layout(ImGuiID dockspace_id, int view_count) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

  ImGuiID main_id = dockspace_id;
  ImGuiID right = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.24f,
                                              nullptr, &main_id);
  ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.68f,
                                                     nullptr, &right);
  ImGuiID left = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Left, 0.16f,
                                             nullptr, &main_id);
  ImGuiID top = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Up, 0.55f, nullptr,
                                            &main_id);
  ImGui::DockBuilderDockWindow("Library", left);
  // The Node List is tabbed with the Library on purpose: it is the other way
  // to reach the same graph, and Terragen's point is that you should be able
  // to work entirely in it without ever opening the network.
  ImGui::DockBuilderDockWindow("Node List", left);
  ImGui::DockBuilderDockWindow("AI", left); // tabbed with Library
  // The console sits under the graph: it is where messages about the graph
  // appear, and a log nobody can see is a log nobody reads.
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.28f,
                                               nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Graph", main_id);
  ImGui::DockBuilderDockWindow("###console", bottom);
  // Blender layout: Outliner on top, the Preview under it, one Properties
  // editor below - the picture stays in view while parameters are edited
  ImGui::DockBuilderDockWindow("Outliner", right);
  ImGuiID preview = ImGui::DockBuilderSplitNode(right_bottom, ImGuiDir_Up, 0.36f,
                                                nullptr, &right_bottom);
  ImGui::DockBuilderDockWindow("Preview", preview);
  ImGui::DockBuilderDockWindow("Properties", right_bottom);

  // viewport region split into 1..6 independent view windows; each remains a
  // normal window the user can resize, re-dock or float
  view_count = std::clamp(view_count, 1, 6);
  ImGuiID cells[6];
  for (int i = 0; i < 6; ++i) cells[i] = top;
  switch (view_count) {
    case 1:
      break;
    case 2: {
      ImGuiID l = top;
      cells[1] = ImGui::DockBuilderSplitNode(l, ImGuiDir_Right, 0.5f, nullptr, &l);
      cells[0] = l;
    } break;
    case 3: {
      ImGuiID l = top;
      ImGuiID r = ImGui::DockBuilderSplitNode(l, ImGuiDir_Right, 0.5f, nullptr, &l);
      ImGuiID rb = ImGui::DockBuilderSplitNode(r, ImGuiDir_Down, 0.5f, nullptr, &r);
      cells[0] = l; cells[1] = r; cells[2] = rb;
    } break;
    case 4: {
      ImGuiID tl = top;
      ImGuiID tr = ImGui::DockBuilderSplitNode(tl, ImGuiDir_Right, 0.5f, nullptr, &tl);
      ImGuiID bl = ImGui::DockBuilderSplitNode(tl, ImGuiDir_Down, 0.5f, nullptr, &tl);
      ImGuiID br = ImGui::DockBuilderSplitNode(tr, ImGuiDir_Down, 0.5f, nullptr, &tr);
      cells[0] = tl; cells[1] = tr; cells[2] = bl; cells[3] = br;
    } break;
    case 5: {
      ImGuiID t = top;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.5f, nullptr, &t);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.66f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.5f, nullptr, &b2);
      cells[0] = t; cells[1] = t2; cells[2] = b; cells[3] = b2; cells[4] = b3;
    } break;
    default: { // 6 = 3 columns x 2 rows
      ImGuiID t = top;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.66f, nullptr, &t);
      ImGuiID t3 = ImGui::DockBuilderSplitNode(t2, ImGuiDir_Right, 0.5f, nullptr, &t2);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.66f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.5f, nullptr, &b2);
      cells[0] = t; cells[1] = t2; cells[2] = t3;
      cells[3] = b; cells[4] = b2; cells[5] = b3;
    } break;
  }
  for (int i = 0; i < view_count; ++i)
    ImGui::DockBuilderDockWindow(view_window_name(i), cells[i]);
  ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace studio
