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
  owns_ = menu_open
              ? a_.graph_mtx.try_lock_for(
                    std::chrono::milliseconds(GRAPH_LEASE_WAIT_MS))
              : a_.graph_mtx.try_lock();
}

void GraphLease::unlock() {
  if (!owns_) return;
  owns_ = false;
  a_.graph_mtx.unlock();
}

GraphLease::~GraphLease() { unlock(); }

} // namespace studio
