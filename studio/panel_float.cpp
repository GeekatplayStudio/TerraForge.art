// Geekatplay TerraForge — float/dock controls for every panel. See the header.
#include "panel_float.hpp"
#include "app.hpp"
#include "icons.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <map>
#include <string>

namespace studio {

namespace {
enum { REQ_NONE = 0, REQ_DETACH = 1, REQ_DOCK = 2 };
std::map<std::string, int> g_requests;

// Somewhere to put a detached window. A second monitor if there is one —
// that is what "move the node editor to the other screen" means — else a
// floating window inside the main one, offset so its title bar is visible.
void place_detached(ImVec2 &pos, ImVec2 &size) {
  ImGuiViewport *mv = ImGui::GetMainViewport();
  pos = ImVec2(mv->Pos.x + 80.f, mv->Pos.y + 80.f);
  size = ImVec2(std::max(640.f, mv->Size.x * 0.55f),
                std::max(480.f, mv->Size.y * 0.6f));
  const ImVec2 mc(mv->Pos.x + mv->Size.x * 0.5f, mv->Pos.y + mv->Size.y * 0.5f);
  const ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
  for (const ImGuiPlatformMonitor &m : pio.Monitors) {
    bool holds_main = mc.x >= m.MainPos.x && mc.x < m.MainPos.x + m.MainSize.x &&
                      mc.y >= m.MainPos.y && mc.y < m.MainPos.y + m.MainSize.y;
    if (holds_main) continue;
    pos = ImVec2(m.WorkPos.x + 40.f, m.WorkPos.y + 40.f);
    size = ImVec2(m.WorkSize.x - 80.f, m.WorkSize.y - 80.f);
    return;
  }
}
} // namespace

void panel_float_prepare(App &a, const char *name) {
  auto it = g_requests.find(name);
  if (it == g_requests.end()) return;
  int req = it->second;
  g_requests.erase(it);
  if (req == REQ_DETACH) {
    ImVec2 pos, size;
    place_detached(pos, size);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  } else if (req == REQ_DOCK && a.dockspace_id) {
    ImGui::SetNextWindowDockID((ImGuiID)a.dockspace_id, ImGuiCond_Always);
  }
}

void panel_float_controls(App &a, const char *name) {
  (void)a;
  const bool docked = ImGui::IsWindowDocked();
  const float bw = ImGui::GetFontSize() + 6.f;
  const ImVec2 saved = ImGui::GetCursorPos();
  const float right = ImGui::GetWindowContentRegionMax().x;
  ImGui::SetCursorPos(ImVec2(right - bw, saved.y - 1.f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  char id[96];
  snprintf(id, sizeof id, "##float_%s", name);
  if (IconButton(docked ? Icon::Detach : Icon::Dock, id,
                 docked ? "Float this window out of the main window\n"
                          "(onto the second monitor when there is one)"
                        : "Dock this window back into the main layout",
                 false, bw))
    g_requests[name] = docked ? REQ_DETACH : REQ_DOCK;
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  ImGui::SetCursorPos(saved);
}

} // namespace studio
