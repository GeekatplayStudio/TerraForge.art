// Geekatplay TerraForge - the crash-recovery dialog. Split from autosave.cpp
// so the autosave logic stays ImGui-free and links into the headless tests.
#include "autosave.hpp"
#include "app.hpp"
#include <imgui.h>

namespace studio {

void autosave_recovery_dialog(App &a) {
  std::string path;
  if (!autosave_crash_recovery_available(path)) return;
  if (!ImGui::IsPopupOpen("Restore last session?"))
    ImGui::OpenPopup("Restore last session?");
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Restore last session?", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted(
      "The last session did not close properly, but the work\n"
      "was autosaved. Restore it?");
  ImGui::TextDisabled("%s", path.c_str());
  ImGui::Spacing();
  if (ImGui::Button("Restore", ImVec2(150, 0))) {
    if (project_load(a, path)) {
      // it came from an autosave slot; the user's own file name is unknown,
      // so Save must not silently overwrite the slot
      a.project_path.clear();
      a.status = "restored the autosaved session";
    }
    autosave_mark_recovery_answered();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Start fresh", ImVec2(150, 0))) {
    autosave_mark_recovery_answered();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace studio
