// Geekatplay TerraForge - a panel's hold on the graph for one frame.
#include "graph_lease.hpp"
#include "app.hpp"
#include <chrono>
#include <imgui.h>

namespace studio {

GraphLease::GraphLease(App &a) : a_(a) {
  // Any popup anywhere counts. It is not worth asking which window owns the
  // menu: the answer is almost always this one, and the cost of being wrong
  // is a few milliseconds on a frame that already has a menu open.
  const bool menu_open = ImGui::IsPopupOpen(
      "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  // A drag in progress is the other case. Every frame of a slider drag asks
  // for an evaluation, the evaluation holds the lock for its whole run (a
  // material's is ~30 ms at interactive resolution), so with a plain try_lock
  // a panel missed most frames of the drag and blanked itself on each one:
  // "the sliders flicker and disappear while I change them". Waiting a
  // bounded frame-or-so keeps the panel drawn; the cost is a UI that runs at
  // the evaluation's rate while the value is being dragged, which it is
  // driving anyway.
  const bool dragging = ImGui::IsAnyItemActive() &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (menu_open)
    owns_ = a_.graph_mtx.try_lock_for(std::chrono::milliseconds(GRAPH_LEASE_WAIT_MS));
  else if (dragging)
    owns_ = a_.graph_mtx.try_lock_for(std::chrono::milliseconds(GRAPH_LEASE_DRAG_MS));
  else
    owns_ = a_.graph_mtx.try_lock();
}

void GraphLease::unlock() {
  if (!owns_) return;
  owns_ = false;
  a_.graph_mtx.unlock();
}

GraphLease::~GraphLease() { unlock(); }

} // namespace studio
