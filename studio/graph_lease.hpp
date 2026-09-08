// Geekatplay TerraForge - a panel's hold on the graph for one frame.
//
// Evaluation owns graph_mtx for its whole run, so a panel has two bad
// choices: insist on the lock and stall with it, or give up instantly and
// draw nothing that frame. Every material panel took the second, and it is
// worse than it sounds.
//
// ImGui keeps a dropdown open by the window that opened it submitting the
// combo again next frame. A panel that skips its body submits nothing, so the
// popup dies. One missed frame in six hundred is enough - which is exactly
// what "the menus close as soon as I open them, in every tab" was. Measured:
// the popup closed on the first frame the panel failed to take the lock, and
// stayed open indefinitely once the panel stopped skipping.
//
// So the rule is: wait, but only while waiting is the cheaper mistake.
//
//   nothing open   the panel is only redrawing itself; a stall buys nothing
//                  and costs frame rate, so take the lock or skip as before.
//   popup open     the user is reading a menu. Losing it is far worse than a
//                  few milliseconds of frame time, so wait for the lock -
//                  bounded, so a long bake degrades the frame rate instead of
//                  hanging the application.
//
// An interactive evaluation is a millisecond or two and a full terrain pass
// about twenty, so the bound below covers every case somebody could be
// holding a menu open through, without ever letting the UI freeze.
#pragma once
#include <mutex>

namespace studio {
struct App;

// How long a panel will wait for the graph while a menu is open. Long enough
// for any evaluation a person could trigger while pointing at a dropdown,
// short enough that a runaway bake costs frame rate rather than the session.
constexpr int GRAPH_LEASE_WAIT_MS = 250;

class GraphLease {
public:
  explicit GraphLease(App &a);
  ~GraphLease();
  GraphLease(const GraphLease &) = delete;
  GraphLease &operator=(const GraphLease &) = delete;

  // False when the graph could not be had: the panel must draw *something*
  // rather than return, or it takes the user's menu down with it.
  bool owns_lock() const { return owns_; }

  // Give the graph back early, for a panel that is done with it and then
  // calls something that takes the lock itself. Safe to call twice.
  void unlock();

private:
  App &a_;
  bool owns_ = false;
};

} // namespace studio
