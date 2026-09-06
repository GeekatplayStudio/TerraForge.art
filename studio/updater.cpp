// Geekatplay TerraForge - checking GitHub for a newer version. See the header.
#include "updater.hpp"
#include "app.hpp"
#include "config.hpp"
#include "console.hpp"
#include "http_client.hpp"
#include <GLFW/glfw3.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <json.hpp>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef TF_GIT_SHA
#define TF_GIT_SHA "unknown"
#endif
#ifndef TF_SOURCE_DIR
#define TF_SOURCE_DIR ""
#endif

namespace studio {

namespace {
std::mutex g_mtx;
UpdateStatus g_status;
std::atomic<bool> g_running{false};
bool g_dismissed = false;   // "Later" for this session
bool g_popup_pending = false;

std::string short_sha(const std::string &s) { return s.size() > 10 ? s.substr(0, 10) : s; }

void run_check() {
  const UpdateConfig cfg = config().updates;
  UpdateStatus st;
  st.local_sha = TF_GIT_SHA;
  const std::string url = "https://api.github.com/repos/" + cfg.repo + "/commits/" + cfg.branch;
  HttpResponse r = http_get(url, {{"User-Agent", "GeekatplayTerraForge/2.0"},
                                  {"Accept", "application/vnd.github+json"}});
  if (!r.ok()) {
    st.error = r.error.empty() ? "GitHub answered " + std::to_string(r.status) : r.error;
  } else {
    nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("sha")) {
      st.error = "unexpected answer from GitHub";
    } else {
      st.remote_sha = j.value("sha", "");
      if (j.contains("commit") && j["commit"].is_object()) {
        const nlohmann::json &c = j["commit"];
        st.remote_message = c.value("message", "");
        if (c.contains("committer") && c["committer"].is_object())
          st.remote_date = c["committer"].value("date", "");
      }
      // only the first line of the message, for the dialog
      size_t nl = st.remote_message.find('\n');
      if (nl != std::string::npos) st.remote_message.resize(nl);
      st.available = st.local_sha != "unknown" && !st.remote_sha.empty() &&
                     st.remote_sha.rfind(st.local_sha, 0) != 0 &&
                     st.local_sha.rfind(st.remote_sha, 0) != 0 &&
                     st.remote_sha != cfg.skipped_sha;
    }
  }
  st.checked = true;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status = st;
    g_status.checking = false;
  }
  if (st.available) g_popup_pending = true;
  log_info("update", st.error.empty()
                         ? (st.available ? "a newer version is on GitHub: " + short_sha(st.remote_sha)
                                         : "up to date (" + short_sha(st.local_sha) + ")")
                         : "check failed: " + st.error);
  g_running = false;
}
} // namespace

const UpdateStatus &update_status() {
  static UpdateStatus copy;
  std::lock_guard<std::mutex> lk(g_mtx);
  copy = g_status;
  return copy;
}

std::string build_commit() { return short_sha(TF_GIT_SHA); }

void update_check_async() {
  bool expected = false;
  if (!g_running.compare_exchange_strong(expected, true)) return;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_status = UpdateStatus{};
    g_status.checking = true;
    g_status.local_sha = TF_GIT_SHA;
  }
  std::thread(run_check).detach();
}

bool update_and_rebuild(App &a) {
  namespace fs = std::filesystem;
  const UpdateConfig &cfg = config().updates;
  fs::path src = cfg.source_dir.empty() ? fs::path(TF_SOURCE_DIR) : fs::path(cfg.source_dir);
  std::error_code ec;
  if (src.empty() || !fs::exists(src / "CMakeLists.txt", ec)) {
    a.status = "update: the source tree is not at " + src.string() +
               " - set it under Settings > Updates";
    return false;
  }
#ifdef _WIN32
  fs::path script = src / "scripts" / "update.ps1";
  if (!fs::exists(script, ec)) {
    a.status = "update: " + script.string() + " is missing";
    return false;
  }
  char exe[MAX_PATH] = {0};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + script.string() +
                    "\" -Source \"" + src.string() + "\" -Exe \"" + exe + "\" -Pid " +
                    std::to_string((unsigned long)GetCurrentProcessId()) + " -Branch " + cfg.branch;
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  std::string mutable_cmd = cmd;
  if (!CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                      nullptr, src.string().c_str(), &si, &pi)) {
    a.status = "update: could not start PowerShell";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
#else
  fs::path script = src / "scripts" / "update.sh";
  if (!fs::exists(script, ec)) {
    a.status = "update: " + script.string() + " is missing";
    return false;
  }
  std::string cmd = "nohup sh \"" + script.string() + "\" \"" + src.string() + "\" " +
                    std::to_string((long)getpid()) + " " + cfg.branch + " >/dev/null 2>&1 &";
  if (std::system(cmd.c_str()) != 0) {
    a.status = "update: could not start the update script";
    return false;
  }
#endif
  log_info("update", "update script started; closing to let it rebuild");
  a.status = "updating: the application closes, rebuilds, and starts again";
  glfwSetWindowShouldClose(a.window, GLFW_TRUE);
  return true;
}

void update_ui(App &a) {
  if (g_popup_pending && !g_dismissed) {
    ImGui::OpenPopup("New version available");
    g_popup_pending = false;
  }
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("New version available", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  const UpdateStatus st = update_status();
  ImGui::Text("A newer TerraForge is on GitHub.");
  ImGui::Spacing();
  ImGui::TextDisabled("this build:  %s", short_sha(st.local_sha).c_str());
  ImGui::TextDisabled("on GitHub:   %s  %s", short_sha(st.remote_sha).c_str(), st.remote_date.c_str());
  if (!st.remote_message.empty()) ImGui::TextWrapped("%s", st.remote_message.c_str());
  ImGui::Spacing();
  ImGui::TextWrapped("Update and rebuild pulls the repository, builds it for this machine with "
                     "the same build script the installer used, replaces this executable and "
                     "starts TerraForge again. Save your work first: the application closes.");
  ImGui::Spacing();
  if (ImGui::Button("Update and rebuild", ImVec2(180, 0))) {
    if (update_and_rebuild(a)) ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Later", ImVec2(110, 0))) {
    g_dismissed = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Skip this version", ImVec2(150, 0))) {
    config().updates.skipped_sha = st.remote_sha;
    config_save();
    g_dismissed = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

} // namespace studio
