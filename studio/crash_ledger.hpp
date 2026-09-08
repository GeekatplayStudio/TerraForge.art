// Geekatplay TerraForge — the ledger of crashes, hangs and sessions that
// never said goodbye.
//
// Three kinds of evidence sit in logs/: crash_<stamp>.txt (a handler ran),
// hang_<stamp>.txt (the watchdog caught the main thread standing still),
// and a terraforge_<stamp>.log with no "=== clean exit" line - a session
// that was killed, which after a hang is what a person does. This module
// lists them, with a one-line summary each, and remembers which ones have
// been looked at and fixed (logs/crash_ledger.json), so the startup line
// and the `crash_reports` op say only what is still open.
//
// Pure file work, no window, no GL: tested on a scratch directory.
#pragma once
#include <string>
#include <vector>

namespace studio {

struct CrashReport {
  std::string file;    // "crash_20260902_220023.txt", relative to the dir
  std::string kind;    // "crash", "hang", "unclean"
  std::string stamp;   // "20260902_220023"
  std::string summary; // the reason line, or the log's last line
  bool fixed = false;
  std::string note;    // what fixed it, from crash_mark_fixed
};

// Every report in `dir`, newest first. `current_stamp` names this run's log,
// which is never "unclean" while it is being written.
std::vector<CrashReport> crash_reports(const std::string &dir,
                                       const std::string &current_stamp);

// Record that `file` (a name from crash_reports) has been dealt with.
bool crash_mark_fixed(const std::string &dir, const std::string &file,
                      const std::string &note, std::string &err);

// The reports as a JSON array (the op's reply).
std::string crash_reports_json(const std::vector<CrashReport> &reports);

// One line for the log at startup: "3 open reports: hang 20260908_1150
// (...), ..." or empty when nothing is open.
std::string crash_reports_summary(const std::vector<CrashReport> &reports);

} // namespace studio
