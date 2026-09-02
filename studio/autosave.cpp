// Geekatplay TerraForge — autosave and crash recovery. See autosave.hpp.
#include "autosave.hpp"
#include "app.hpp"
#include "console.hpp"
#include "undo.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace studio {

namespace {

std::string g_dir_override;
bool g_prev_session_crashed = false;
bool g_recovery_answered = false;
int g_last_slot = -1;
double g_last_save_time = -1e18;
// what the undo history looked like when we last saved; the history moves on
// every push, undo and redo, so "has it moved" is "is there anything new"
int g_saved_history_count = -1;
int g_saved_history_pos = -1;

std::string lock_path() { return autosave_dir() + "/session.lock"; }

std::string slot_path(int slot) {
  return autosave_dir() + "/autosave_" + std::to_string(slot + 1) + ".gpxt";
}

// history fingerprint: enough to know whether an edit happened
void history_state(int &count, int &pos) {
  const std::string *labels = nullptr;
  count = undo_history(&labels);
  pos = undo_history_position();
}

} // namespace

std::string autosave_dir() {
  if (!g_dir_override.empty()) return g_dir_override;
  const char *base = std::getenv("LOCALAPPDATA");
  fs::path d = base ? fs::path(base) : fs::temp_directory_path();
  d = d / "GeekatplayTerraForge" / "autosave";
  std::error_code ec;
  fs::create_directories(d, ec);
  return d.string();
}

void autosave_set_dir(const std::string &dir) {
  g_dir_override = dir;
  std::error_code ec;
  fs::create_directories(dir, ec);
  // a fresh directory is a fresh session for the tests
  g_prev_session_crashed = false;
  g_recovery_answered = false;
  g_last_slot = -1;
  g_last_save_time = -1e18;
  g_saved_history_count = g_saved_history_pos = -1;
}

void autosave_mark_recovery_answered() { g_recovery_answered = true; }

int autosave_next_slot(int last_slot) { return (last_slot + 1) % 3; }

void autosave_session_begin() {
  std::error_code ec;
  g_prev_session_crashed = fs::exists(lock_path(), ec);
  std::ofstream f(lock_path());
  f << "running\n";
}

void autosave_session_end() {
  std::error_code ec;
  fs::remove(lock_path(), ec);
}

bool autosave_crash_recovery_available(std::string &path_out) {
  if (!g_prev_session_crashed || g_recovery_answered) return false;
  // If this process wrote a slot itself, that one is newest by construction.
  // Filesystem timestamps only decide for a fresh process after a crash, and
  // there they are the only signal there is.
  if (g_last_slot >= 0) {
    std::error_code lec;
    std::string p = slot_path(g_last_slot);
    if (fs::exists(p, lec)) {
      path_out = p;
      return true;
    }
  }
  // the newest autosave wins
  fs::file_time_type best_time{};
  std::string best;
  std::error_code ec;
  for (int i = 0; i < 3; ++i) {
    std::string p = slot_path(i);
    if (!fs::exists(p, ec)) continue;
    auto t = fs::last_write_time(p, ec);
    if (best.empty() || t > best_time) {
      best_time = t;
      best = p;
    }
  }
  if (best.empty()) return false;
  path_out = best;
  return true;
}

void autosave_tick(App &a, double now_seconds, double interval_s) {
  // While "Restore last session?" is still on screen, write NOTHING. The
  // first tick of a fresh session fires immediately (the interval and the
  // history baseline both start unset), and writing the empty new session
  // into the rotation at that moment overwrites exactly the file being
  // offered back - the user who clicks Restore would get their crash served
  // as a blank scene. Found by review, not by a user, which is the cheap way.
  if (g_prev_session_crashed && !g_recovery_answered) return;
  if (now_seconds - g_last_save_time < interval_s) return;
  int count, pos;
  history_state(count, pos);
  if (count == g_saved_history_count && pos == g_saved_history_pos)
    return; // nothing has happened since the last autosave
  g_last_slot = autosave_next_slot(g_last_slot);
  const std::string path = slot_path(g_last_slot);
  // project_save updates a.status and a.project_path, which an automatic
  // background save must not do - restore both
  std::string status = a.status, project = a.project_path;
  bool ok = project_save(a, path);
  a.status = status;
  a.project_path = project;
  if (ok) {
    g_last_save_time = now_seconds;
    g_saved_history_count = count;
    g_saved_history_pos = pos;
    log_trace("autosave", "saved " + path);
  } else {
    // do not hammer the disk with retries; wait a full interval and say so
    g_last_save_time = now_seconds;
    log_warn("autosave", "could not write " + path);
  }
}

} // namespace studio
