// Geekatplay TerraForge - the docking layout: which panel goes where on
// first run and on View > Reset layout, and the live arrangement of the
// viewport windows.
//
// Two rules run through this file:
//
//   1. Adding, closing or splitting a viewport never rebuilds the layout.
//      A rebuild is a full DockBuilderRemoveNode, and it throws away every
//      window the user placed by hand. Opening a fifth view should cost the
//      user nothing, so these helpers touch one dock node and finish.
//   2. The viewports are a set, not a count (Prefs::view_mask). Closing
//      View 2 of four leaves 1, 3 and 4 exactly where they are rather than
//      renumbering them under the user's hands.
#include "app.hpp"
#include "prefs.hpp"
#include "render_settings.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace studio {

namespace {

// The dock node a viewport window currently lives in, 0 if it has never been
// shown or is floating free.
ImGuiID view_node(int slot) {
  ImGuiWindow *w = ImGui::FindWindowByName(view_window_name(slot));
  return w ? w->DockId : 0;
}

// DockBuilder edits must be finished on the root of the tree they touched,
// which is not always the main dockspace: a window floated onto a second
// monitor carries a dock tree of its own.
ImGuiID root_of(ImGuiID node, ImGuiID fallback) {
  ImGuiDockNode *n = ImGui::DockBuilderGetNode(node);
  if (!n) return fallback;
  ImGuiDockNode *root = ImGui::DockNodeGetRootNode(n);
  return root ? root->ID : fallback;
}

// Where a new viewport should appear: beside the one last worked in, else the
// lowest open one, else the main dockspace.
ImGuiID reference_node(const App &a) {
  unsigned mask = prefs().view_mask;
  if (a.view_focus >= 0 && (mask & (1u << a.view_focus))) {
    ImGuiID n = view_node(a.view_focus);
    if (n) return n;
  }
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
    if (mask & (1u << i)) {
      ImGuiID n = view_node(i);
      if (n) return n;
    }
  return a.dockspace_id;
}

// Split `region` into `n` cells and dock the first n open viewports into them.
// The shapes are the ones a landscape artist actually asks for: side by side,
// a big view with two stacked beside it, 2x2, and 3x2.
void arrange_into(ImGuiID region, int n, const int *slots) {
  ImGuiID cells[RenderSettings::MAX_VIEWS];
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i) cells[i] = region;
  switch (n) {
    case 1:
      break;
    case 2: {
      ImGuiID l = region;
      cells[1] = ImGui::DockBuilderSplitNode(l, ImGuiDir_Right, 0.5f, nullptr, &l);
      cells[0] = l;
    } break;
    case 3: {
      ImGuiID l = region;
      ImGuiID r = ImGui::DockBuilderSplitNode(l, ImGuiDir_Right, 0.5f, nullptr, &l);
      ImGuiID rb = ImGui::DockBuilderSplitNode(r, ImGuiDir_Down, 0.5f, nullptr, &r);
      cells[0] = l; cells[1] = r; cells[2] = rb;
    } break;
    case 4: {
      ImGuiID tl = region;
      ImGuiID tr = ImGui::DockBuilderSplitNode(tl, ImGuiDir_Right, 0.5f, nullptr, &tl);
      ImGuiID bl = ImGui::DockBuilderSplitNode(tl, ImGuiDir_Down, 0.5f, nullptr, &tl);
      ImGuiID br = ImGui::DockBuilderSplitNode(tr, ImGuiDir_Down, 0.5f, nullptr, &tr);
      cells[0] = tl; cells[1] = tr; cells[2] = bl; cells[3] = br;
    } break;
    case 5: {
      ImGuiID t = region;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.5f, nullptr, &t);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.66f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.5f, nullptr, &b2);
      cells[0] = t; cells[1] = t2; cells[2] = b; cells[3] = b2; cells[4] = b3;
    } break;
    case 6: {
      ImGuiID t = region;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.66f, nullptr, &t);
      ImGuiID t3 = ImGui::DockBuilderSplitNode(t2, ImGuiDir_Right, 0.5f, nullptr, &t2);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.66f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.5f, nullptr, &b2);
      cells[0] = t; cells[1] = t2; cells[2] = t3;
      cells[3] = b; cells[4] = b2; cells[5] = b3;
    } break;
    default: { // 7 and 8: four columns over two rows
      ImGuiID t = region;
      ImGuiID b = ImGui::DockBuilderSplitNode(t, ImGuiDir_Down, 0.5f, nullptr, &t);
      ImGuiID t2 = ImGui::DockBuilderSplitNode(t, ImGuiDir_Right, 0.75f, nullptr, &t);
      ImGuiID t3 = ImGui::DockBuilderSplitNode(t2, ImGuiDir_Right, 0.66f, nullptr, &t2);
      ImGuiID t4 = ImGui::DockBuilderSplitNode(t3, ImGuiDir_Right, 0.5f, nullptr, &t3);
      ImGuiID b2 = ImGui::DockBuilderSplitNode(b, ImGuiDir_Right, 0.75f, nullptr, &b);
      ImGuiID b3 = ImGui::DockBuilderSplitNode(b2, ImGuiDir_Right, 0.66f, nullptr, &b2);
      ImGuiID b4 = ImGui::DockBuilderSplitNode(b3, ImGuiDir_Right, 0.5f, nullptr, &b3);
      cells[0] = t; cells[1] = t2; cells[2] = t3; cells[3] = t4;
      cells[4] = b; cells[5] = b2; cells[6] = b3; cells[7] = b4;
    } break;
  }
  for (int i = 0; i < n; ++i)
    ImGui::DockBuilderDockWindow(view_window_name(slots[i]), cells[i]);
}

} // namespace

int view_first_free() {
  unsigned mask = prefs().view_mask;
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
    if (!(mask & (1u << i))) return i;
  return -1;
}

void view_open(App &a, int slot) {
  if (slot < 0 || slot >= RenderSettings::MAX_VIEWS) return;
  if (prefs().view_mask & (1u << slot)) return; // already there
  ImGuiID ref = reference_node(a);
  prefs().view_mask |= 1u << slot;
  prefs_save();
  // As a tab beside the reference view: it appears where the eye already is,
  // and one drag turns it into a split. Nothing else in the layout moves.
  ImGui::DockBuilderDockWindow(view_window_name(slot), ref);
  ImGui::DockBuilderFinish(root_of(ref, a.dockspace_id));
  a.view_focus = slot;
  a.status = std::string("opened ") + view_window_name(slot);
}

void view_close(App &a, int slot) {
  unsigned &mask = prefs().view_mask;
  if (slot < 0 || slot >= RenderSettings::MAX_VIEWS) return;
  unsigned without = mask & ~(1u << slot);
  if (!without) return; // the last viewport stays: an app with no view is a bug
  mask = without;
  prefs_save();
  if (a.view_focus == slot)
    for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
      if (mask & (1u << i)) { a.view_focus = i; break; }
  a.status = std::string("closed ") + view_window_name(slot);
}

void view_split(App &a, int slot, bool vertical) {
  int fresh = view_first_free();
  if (fresh < 0) {
    a.status = "all viewports are already open";
    return;
  }
  ImGuiID node = view_node(slot);
  if (!node) node = a.dockspace_id;
  ImGuiID root = root_of(node, a.dockspace_id);
  // The existing view keeps the half it had; the new one takes the other.
  ImGuiID keep = node;
  ImGuiID other = ImGui::DockBuilderSplitNode(
      node, vertical ? ImGuiDir_Down : ImGuiDir_Right, 0.5f, nullptr, &keep);
  ImGui::DockBuilderDockWindow(view_window_name(slot), keep);
  ImGui::DockBuilderDockWindow(view_window_name(fresh), other);
  ImGui::DockBuilderFinish(root);
  prefs().view_mask |= 1u << fresh;
  prefs_save();
  a.view_focus = fresh;
  a.status = std::string("split into ") + view_window_name(fresh);
}

void views_arrange(App &a, int n) {
  n = std::clamp(n, 1, (int)RenderSettings::MAX_VIEWS);
  // The region the viewports already occupy - not the whole window, so the
  // node editor, properties and console keep their places.
  ImGuiID region = reference_node(a);
  // Nothing is docked (every viewport was floated out, or none has been shown
  // yet), so there is no region to subdivide. Clearing the root's children
  // here would flatten the whole application into one tab bar; rebuild the
  // default layout around the new set instead.
  if (region == a.dockspace_id) {
    prefs().view_mask = (1u << n) - 1u;
    prefs_save();
    a.request_layout_reset = true;
    a.view_focus = 0;
    a.status = std::to_string(n) + (n == 1 ? " viewport" : " viewports");
    return;
  }
  ImGuiID root = root_of(region, a.dockspace_id);
  ImGui::DockBuilderRemoveNodeChildNodes(region);
  int slots[RenderSettings::MAX_VIEWS];
  for (int i = 0; i < n; ++i) slots[i] = i;
  arrange_into(region, n, slots);
  ImGui::DockBuilderFinish(root);
  prefs().view_mask = (n >= 32) ? ~0u : ((1u << n) - 1u);
  prefs_save();
  a.view_focus = 0;
  a.status = std::to_string(n) + (n == 1 ? " viewport" : " viewports");
}

void build_default_layout(unsigned dockspace_id, unsigned view_mask) {
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

  // The open viewports fill the top region; each stays a normal window the
  // user can resize, re-dock, tear off or float onto another monitor.
  int slots[RenderSettings::MAX_VIEWS];
  int n = 0;
  for (int i = 0; i < RenderSettings::MAX_VIEWS && n < RenderSettings::MAX_VIEWS;
       ++i)
    if (view_mask & (1u << i)) slots[n++] = i;
  if (!n) { slots[0] = 0; n = 1; }
  arrange_into(top, n, slots);
  ImGui::DockBuilderFinish(dockspace_id);
}

// The Materials workspace is arranged the way a material editor is, not the
// way a terrain workspace is: the material being edited at the top - its
// preview and every one of its properties - a thin browser of project and
// library materials beneath it, and under those the two things you look at
// while you work: the node graph that makes the material, and a scene view
// that shows it on the terrain. Properties and the Outliner stay on the
// right, so the rest of the application still reads the same.
void build_materials_layout(unsigned dockspace_id, unsigned view_mask) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

  ImGuiID main_id = dockspace_id;
  ImGuiID right = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.22f,
                                              nullptr, &main_id);
  ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.70f,
                                                     nullptr, &right);
  // Every window the application can show is placed, or it floats over the
  // arrangement at wherever it last was - which is how the Node List ended
  // up covering the studio's left edge the first time this ran.
  ImGui::DockBuilderDockWindow("Outliner", right);
  ImGui::DockBuilderDockWindow("Preview", right);
  ImGui::DockBuilderDockWindow("Properties", right_bottom);
  ImGui::DockBuilderDockWindow("Library", right_bottom);
  ImGui::DockBuilderDockWindow("Node List", right_bottom);
  ImGui::DockBuilderDockWindow("AI", right_bottom);
  ImGui::DockBuilderDockWindow("Mesh Tools", right_bottom);
  ImGui::DockBuilderDockWindow("Material Editor", right_bottom);

  // top: the studio; below it the thin browser; bottom: graph | scene view
  ImGuiID bottom = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.42f,
                                               nullptr, &main_id);
  // tall enough for one row of tiles with their labels under the tab row
  ImGuiID browser = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.34f,
                                                nullptr, &main_id);
  ImGui::DockBuilderDockWindow("Material Studio", main_id);
  ImGui::DockBuilderDockWindow("Material Browser", browser);
  ImGuiID scene = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.5f,
                                              nullptr, &bottom);
  ImGui::DockBuilderDockWindow("Graph", bottom);
  ImGui::DockBuilderDockWindow("###console", bottom); // tabbed under the graph
  ImGui::DockBuilderDockWindow("Timeline", bottom);
  // every open viewport goes into the scene cell, tabbed
  bool any = false;
  for (int i = 0; i < RenderSettings::MAX_VIEWS; ++i)
    if (view_mask & (1u << i)) {
      ImGui::DockBuilderDockWindow(view_window_name(i), scene);
      any = true;
    }
  if (!any) ImGui::DockBuilderDockWindow(view_window_name(0), scene);
  ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace studio
