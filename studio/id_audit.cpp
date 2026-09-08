// Geekatplay TerraForge - finding ImGui id conflicts on purpose.
//
// TEMPORARY DEBUG AID. Compiled only when GPX_ID_AUDIT is defined, which no
// normal build does; the hook it fills in lives behind the same macro in
// external/imgui/imgui.cpp.
//
// ImGui reports "2 visible items with conflicting ID" only for the item under
// the pointer, so finding one on a screen with a few thousand widgets means
// hovering the right pixel. This records every id submitted in a frame,
// per window, and names any that appear twice - which turns the hunt into
// running the application and reading the log.
#include "console.hpp"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
std::unordered_map<std::string, std::unordered_set<unsigned>> g_seen;
std::unordered_set<std::string> g_reported; // once each, or the log is useless
} // namespace

extern "C" void gpx_id_audit_frame_begin() { g_seen.clear(); }

void gpx_id_audit(unsigned id, const char *window) {
  const std::string w = window ? window : "?";
  if (!g_seen[w].insert(id).second) {
    char key[128];
    std::snprintf(key, sizeof key, "%s|%08X", w.c_str(), id);
    if (g_reported.insert(key).second) {
      char msg[256];
      std::snprintf(msg, sizeof msg, "duplicate id 0x%08X in window '%s'", id,
                    w.c_str());
      studio::log_warn("idaudit", msg);
    }
  }
}
